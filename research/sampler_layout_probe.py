"""Compare indexed texel layouts using changing addresses and an independent oracle."""

import argparse
import json
import os
import signal
import subprocess
import sys
import time

import numpy as np
import openvino as ov
from openvino import opset13 as op


def isolated(args):
    """Bound experimental compiler/driver calls and stop after device failure."""
    failed = False
    for variant in args.variants:
        command = [
            sys.executable,
            os.path.abspath(__file__),
            "--worker",
            "--width",
            str(args.width),
            "--height",
            str(args.height),
            "--side",
            str(args.side),
            "--frames",
            str(args.frames),
            "--device",
            args.device,
            "--variants",
            variant,
        ]
        if args.profile:
            command.append("--profile")
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
                    check=True,
                    capture_output=True,
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
                    {"variant": variant, "passed": False, "timeout_seconds": args.timeout_seconds}
                ),
                flush=True,
            )
            return 1
        if "DEVICE_LOST" in stdout + stderr or "driver is not applicable" in stdout + stderr:
            return 1
    return int(failed)


def graph(source_pixels, side, variant):
    if variant == "gather_nd":
        image = op.parameter([4, source_pixels], np.float16)
        indices = op.parameter([4, 4, side, side, 2], np.int32)
        weights = op.parameter([1, 4, side, side], np.float16)
        background = op.parameter([4, side, side], np.float16)
        fetched = op.gather_nd(image, indices)
        sampled = op.reduce_sum(op.multiply(fetched, weights), np.array([1]), False)
        alpha = op.gather(sampled, np.array([3]), np.array(0))
        result = op.add(
            sampled,
            op.multiply(
                background, op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255)))
            ),
        )
        return ov.Model([result], [image, indices, weights, background])
    if variant == "width_neighbors":
        image = op.parameter([4, source_pixels], np.float16)
        indices = op.parameter([side, side * 4], np.int32)
        weights = op.parameter([1, 1, side, side * 4], np.float16)
        background = op.parameter([1, 4, side, side], np.float16)
        fetched = op.gather(image, indices, np.array(1))
        fetched = op.reshape(fetched, np.array([1, 4, side, side * 4]), False)
        weighted = op.multiply(fetched, weights)
        sampled = op.multiply(
            op.avg_pool(weighted, [1, 4], [0, 0], [0, 0], [1, 4], True), np.float16(4)
        )
        alpha = op.gather(sampled, np.array([3]), np.array(1))
        result = op.add(
            sampled,
            op.multiply(
                background, op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255)))
            ),
        )
        return ov.Model([result], [image, indices, weights, background])
    if variant == "rank5":
        image = op.parameter([1, 4, source_pixels], np.float16)
        indices = op.parameter([4, side, side], np.int32)
        weights = op.parameter([1, 1, 4, side, side], np.float16)
        background = op.parameter([1, 4, side, side], np.float16)
        fetched = op.gather(image, indices, np.array(2))
        sampled = op.reduce_sum(op.multiply(fetched, weights), np.array([2]), False)
        alpha = op.gather(sampled, np.array([3]), np.array(1))
        result = op.add(
            sampled,
            op.multiply(
                background, op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255)))
            ),
        )
        return ov.Model([result], [image, indices, weights, background])
    planar = variant == "planar_f16"
    dtype = np.uint8 if variant == "aos_u8" else np.float16
    image = op.parameter([4, source_pixels] if planar else [source_pixels, 4], dtype)
    indices = op.parameter([4, side, side], np.int32)
    weights = op.parameter([1, 4, side, side] if planar else [4, side, side, 1], np.float16)
    background = op.parameter([4, side, side] if planar else [side, side, 4], np.float16)
    fetched = op.gather(image, indices, np.array(1 if planar else 0, np.int64))
    if dtype == np.uint8:
        fetched = op.convert(fetched, np.float16)
    sampled = op.reduce_sum(op.multiply(fetched, weights), np.array([1 if planar else 0]), False)
    alpha = op.gather(sampled, np.array([3]), np.array(0 if planar else 2))
    result = op.add(
        sampled,
        op.multiply(
            background, op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255)))
        ),
    )
    return ov.Model([result], [image, indices, weights, background])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=360)
    parser.add_argument("--side", type=int, default=128)
    parser.add_argument("--frames", type=int, default=5)
    parser.add_argument("--device", default="NPU", choices=["NPU", "GPU", "CPU"])
    parser.add_argument("--profile", action="store_true")
    parser.add_argument("--timeout-seconds", type=float, default=45)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "--variants",
        nargs="+",
        default=["planar_f16"],
        choices=["planar_f16", "aos_f16", "aos_u8", "width_neighbors", "rank5", "gather_nd"],
        help="AoS variants caused device loss on the tested NPU stack; default is planar only",
    )
    args = parser.parse_args()
    if not all(1 <= size <= 4096 for size in [args.width, args.height, args.side]):
        parser.error("width, height and side must be between 1 and 4096")
    if args.frames < 1:
        parser.error("frames must be positive")
    if not 0 < args.timeout_seconds <= 300:
        parser.error("timeout must be positive and at most 300 seconds")
    if not args.worker:
        return isolated(args)
    core = ov.Core()
    source_pixels = args.width * args.height
    failed = False
    for variant in args.variants:
        report = {
            "variant": variant,
            "device": args.device,
            "source": [args.width, args.height],
            "side": args.side,
        }
        try:
            print(json.dumps({**report, "stage": "compiling"}), file=sys.stderr, flush=True)
            start = time.perf_counter()
            compiled = core.compile_model(
                graph(source_pixels, args.side, variant),
                args.device,
                {ov.properties.enable_profiling(): True} if args.profile else {},
            )
            report["compile_ms"] = 1000 * (time.perf_counter() - start)
            report["actual"] = compiled.get_property("EXECUTION_DEVICES")
            actual_devices = report["actual"]
            if isinstance(actual_devices, str):
                actual_devices = [actual_devices]
            if not actual_devices or any(
                actual.split(".")[0] != args.device.split(".")[0] for actual in actual_devices
            ):
                raise RuntimeError("compiled graph selected another device")
            print(json.dumps({**report, "stage": "compiled"}), file=sys.stderr, flush=True)
            request = compiled.create_infer_request()
            rng = np.random.default_rng(5010)
            times, maximum = [], 0
            for _ in range(args.frames):
                image = rng.integers(0, 256, (source_pixels, 4), dtype=np.uint8)
                image[:, :3] = np.minimum(image[:, :3], image[:, 3:4])
                indices = rng.integers(0, source_pixels, (4, args.side, args.side), dtype=np.int32)
                weights = rng.random((4, args.side, args.side, 1), dtype=np.float32)
                weights /= weights.sum(axis=0, keepdims=True)
                weights = weights.astype(np.float16)
                background = rng.integers(0, 256, (args.side, args.side, 4), dtype=np.uint8).astype(
                    np.float16
                )
                background[..., 3] = 255
                ref = (image[indices].astype(np.float64) * weights.astype(np.float64)).sum(axis=0)
                ref += background.astype(np.float64) * (1 - ref[..., 3:4] / 255)
                expected = np.rint(np.clip(ref, 0, 255)).astype(np.int16)
                if variant == "width_neighbors":
                    values = [
                        image.T.astype(np.float16).copy(),
                        indices.transpose(1, 2, 0).reshape(args.side, args.side * 4).copy(),
                        weights[..., 0]
                        .transpose(1, 2, 0)
                        .reshape(1, 1, args.side, args.side * 4)
                        .copy(),
                        background.transpose(2, 0, 1)[None].copy(),
                    ]
                elif variant == "rank5":
                    values = [
                        image.T.astype(np.float16)[None].copy(),
                        indices,
                        weights[..., 0][None, None].copy(),
                        background.transpose(2, 0, 1)[None].copy(),
                    ]
                elif variant in {"planar_f16", "gather_nd"}:
                    addresses = indices
                    if variant == "gather_nd":
                        addresses = np.empty((4, 4, args.side, args.side, 2), dtype=np.int32)
                        addresses[..., 0] = np.arange(4, dtype=np.int32)[:, None, None, None]
                        addresses[..., 1] = indices[None]
                    values = [
                        image.T.astype(np.float16).copy(),
                        addresses,
                        weights[..., 0][None].copy(),
                        background.transpose(2, 0, 1).copy(),
                    ]
                else:
                    values = [
                        image if variant == "aos_u8" else image.astype(np.float16),
                        indices,
                        weights,
                        background,
                    ]
                start = time.perf_counter()
                out = request.infer(values)[0].copy()
                times.append(1000 * (time.perf_counter() - start))
                print(
                    json.dumps({"variant": variant, "stage": "inferred", "frame": len(times)}),
                    file=sys.stderr,
                    flush=True,
                )
                if variant in {"width_neighbors", "rank5"}:
                    out = out[0].transpose(1, 2, 0)
                elif variant in {"planar_f16", "gather_nd"}:
                    out = out.transpose(1, 2, 0)
                if not np.isfinite(out).all():
                    raise RuntimeError("non-finite output")
                observed = np.rint(np.clip(out.astype(np.float64), 0, 255)).astype(np.int16)
                maximum = max(maximum, int(np.abs(observed - expected).max()))
            report.update(infer_copy_ms=times, max_byte_error=maximum, passed=maximum <= 2)
            if args.profile:
                report["profiling"] = [
                    {
                        "name": info.node_name,
                        "type": info.node_type,
                        "exec_type": info.exec_type,
                        "real_ms": info.real_time.total_seconds() * 1000,
                        "cpu_ms": info.cpu_time.total_seconds() * 1000,
                        "status": str(info.status),
                    }
                    for info in request.get_profiling_info()
                ]
        except Exception as error:
            report.update(passed=False, error=str(error))
        failed |= not report["passed"]
        print(json.dumps(report), flush=True)
        # Device loss invalidates subsequent measurements. Do not submit another
        # experimental graph into the same failed device context.
        if "DEVICE_LOST" in report.get("error", "") or "driver is not applicable" in report.get(
            "error", ""
        ):
            return 1
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
