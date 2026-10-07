"""Bounded comparison of plugin/driver compilers for graphics graph operations."""

import argparse
import json
import os
import signal
import subprocess
import sys
import time

import numpy as np
import openvino as ov
from layout_probe import model as layout_model
from openvino import opset13 as op

from npu_render.openvino_diagnostics import inspect_stack, require_npu_driver


def fixture(case, width, height, side):
    rng = np.random.default_rng(5010)
    if case == "relu":
        value = np.linspace(-8, 7, 16, dtype=np.float16).reshape(1, 1, 1, 16)
        parameter = op.parameter(value.shape, value.dtype)
        return ov.Model([op.relu(parameter)], [parameter]), [value], np.maximum(value, 0), 0
    if case.startswith("grid_"):
        dtype = np.float16 if case == "grid_f16" else np.float32
        image = rng.random((1, 4, height, width), dtype=np.float32).astype(dtype)
        image[:, 3] = 1
        # Include fractional samples outside all four edges. The oracle uses the
        # actual supplied grid precision, including half-pixel rounding effects.
        grid = rng.uniform(-1.1, 1.1, (1, side, side, 2)).astype(dtype)
        sx = (grid[0, :, :, 0].astype(np.float64) + 1) * width / 2 - 0.5
        sy = (grid[0, :, :, 1].astype(np.float64) + 1) * height / 2 - 0.5
        x0, y0 = np.floor(sx).astype(int), np.floor(sy).astype(int)
        wx, wy = sx - x0, sy - y0
        expected = np.zeros((1, 4, side, side), np.float64)
        for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]:
            valid = (x0 + dx >= 0) & (x0 + dx < width) & (y0 + dy >= 0) & (y0 + dy < height)
            expected += image[
                :, :, np.clip(y0 + dy, 0, height - 1), np.clip(x0 + dx, 0, width - 1)
            ].astype(np.float64) * ((wx if dx else 1 - wx) * (wy if dy else 1 - wy) * valid)
        data, coordinates = op.parameter(image.shape, dtype), op.parameter(grid.shape, dtype)
        sampled = op.grid_sample(
            data, coordinates, {"mode": "bilinear", "padding_mode": "zeros", "align_corners": False}
        )
        return ov.Model([sampled], [data, coordinates]), [image, grid], expected, 2 / 255
    shape = (1, height, width, 4)
    background = rng.integers(0, 256, shape, dtype=np.uint8)
    background[..., 3] = 255
    foreground = rng.integers(0, 256, shape, dtype=np.uint8)
    foreground[..., :3] = np.minimum(foreground[..., :3], foreground[..., 3:4])
    expected = np.rint(
        foreground.astype(np.float64)
        + background.astype(np.float64) * (1 - foreground[..., 3:4].astype(np.float64) / 255)
    )
    return layout_model(width, height, case), [background, foreground], expected, 1


def worker(args):
    report = {
        "case": args.cases[0],
        "requested_compiler": args.compilers[0],
        "source": [args.width, args.height],
        "grid_output": [args.side, args.side],
        "passed": False,
    }
    try:
        core = ov.Core()
        report["stack"] = inspect_stack(core)
        require_npu_driver(report["stack"], args.expected_npu_driver)
        model, values, expected, tolerance = fixture(
            args.cases[0], args.width, args.height, args.side
        )
        report["tolerance"] = tolerance
        cpu = core.compile_model(model, "CPU", {"INFERENCE_PRECISION_HINT": "f32"})
        control = cpu.create_infer_request().infer(values)[0].copy()
        report["cpu_control_max_error"] = float(np.abs(control.astype(np.float64) - expected).max())
        if not np.isfinite(control).all() or report["cpu_control_max_error"] > tolerance:
            raise RuntimeError("CPU control disagrees with the independent oracle")
        print(json.dumps({"stage": "compiling", **report}), file=sys.stderr, flush=True)
        start = time.perf_counter()
        compiled = core.compile_model(model, "NPU", {"NPU_COMPILER_TYPE": args.compilers[0]})
        report["compile_ms"] = 1000 * (time.perf_counter() - start)
        report["actual_compiler"] = str(compiled.get_property("NPU_COMPILER_TYPE"))
        if report["actual_compiler"].split(".")[-1] != args.compilers[0]:
            raise RuntimeError("compiled model used another compiler")
        devices = compiled.get_property("EXECUTION_DEVICES")
        report["actual_devices"] = [devices] if isinstance(devices, str) else list(devices)
        if not report["actual_devices"] or any(
            device.split(".")[0] != "NPU" for device in report["actual_devices"]
        ):
            raise RuntimeError("compiled model selected another execution device")
        try:
            report["compiled_compiler_version"] = str(compiled.get_property("NPU_COMPILER_VERSION"))
        except RuntimeError as error:
            report["compiler_version_query_error"] = str(error)
        print(json.dumps({"stage": "compiled", **report}), file=sys.stderr, flush=True)
        request = compiled.create_infer_request()
        times, maximum = [], 0.0
        for _ in range(args.frames):
            start = time.perf_counter()
            observed = request.infer(values)[0].copy()
            times.append(1000 * (time.perf_counter() - start))
            if not np.isfinite(observed).all():
                raise RuntimeError("non-finite output")
            maximum = max(maximum, float(np.abs(observed.astype(np.float64) - expected).max()))
        report.update(infer_copy_ms=times, max_error=maximum, passed=maximum <= tolerance)
    except Exception as error:
        report["error"] = str(error)
    print(json.dumps(report), flush=True)
    return int(not report["passed"])


def isolated(args):
    failed = False
    for case in args.cases:
        for compiler in args.compilers:
            command = [
                sys.executable,
                os.path.abspath(__file__),
                "--worker",
                "--cases",
                case,
                "--compilers",
                compiler,
                "--expected-npu-driver",
                args.expected_npu_driver,
                "--width",
                str(args.width),
                "--height",
                str(args.height),
                "--side",
                str(args.side),
                "--frames",
                str(args.frames),
            ]
            process = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                start_new_session=os.name != "nt",
            )
            timed_out = False
            try:
                stdout, stderr = process.communicate(timeout=args.timeout_seconds)
            except subprocess.TimeoutExpired:
                timed_out = True
                if os.name == "nt":
                    subprocess.run(
                        ["taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
                        capture_output=True,
                        check=True,
                        timeout=10,
                    )
                else:
                    os.killpg(process.pid, signal.SIGKILL)
                stdout, stderr = process.communicate(timeout=10)
            print(stdout, end="", flush=True)
            print(stderr, end="", file=sys.stderr, flush=True)
            failed |= process.returncode != 0
            if timed_out:
                print(
                    json.dumps(
                        {
                            "case": case,
                            "requested_compiler": compiler,
                            "passed": False,
                            "timeout_seconds": args.timeout_seconds,
                        }
                    ),
                    flush=True,
                )
                return 1
            if "DEVICE_LOST" in stdout + stderr or "driver is not applicable" in stdout + stderr:
                return 1
    return int(failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cases",
        nargs="+",
        default=["relu"],
        choices=["relu", "grid_f16", "grid_f32", "pack", "unpack", "both", "channels_4"],
    )
    parser.add_argument(
        "--compilers", nargs="+", default=["PLUGIN", "DRIVER"], choices=["PLUGIN", "DRIVER"]
    )
    parser.add_argument("--expected-npu-driver", required=True)
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--side", type=int, default=32)
    parser.add_argument("--frames", type=int, default=5)
    parser.add_argument("--timeout-seconds", type=float, default=45)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not all(1 <= size <= 4096 for size in [args.width, args.height, args.side]):
        parser.error("extents must be between 1 and 4096")
    if args.frames < 1 or not 0 < args.timeout_seconds <= 300:
        parser.error("frames must be positive; timeout must be between 0 and 300 seconds")
    if args.worker and (len(args.cases) != 1 or len(args.compilers) != 1):
        parser.error("each worker must have exactly one case and compiler")
    return worker(args) if args.worker else isolated(args)


if __name__ == "__main__":
    sys.exit(main())
