"""Isolate NPU blend passes with changing premultiplied inputs and an integer oracle."""

import argparse
import json
import time

import numpy as np
import openvino as ov
from openvino import opset13 as op

from npu_render.openvino_diagnostics import inspect_stack, require_npu_driver


def model(width, height, variant):
    shape = [1, height, width, 4]
    bg, fg = [op.parameter(shape, np.uint8, name=name) for name in ("background", "foreground")]

    def blocked(parameter):
        value = op.convert(parameter, np.float16)
        return op.transpose(
            op.reshape(value, np.array([1, height, width // 4, 16]), False), np.array([0, 3, 1, 2])
        )

    b, f = blocked(bg), blocked(fg)
    if variant.startswith("conv"):
        weights = np.zeros((16, 16, 1, 1), np.float16)
        for c in range(16):
            weights[c, c // 4 * 4 + 3, 0, 0] = -1 / 255
        inverse = op.add(op.convolution(f, weights, [1, 1], [0, 0], [0, 0], [1, 1]), np.float16(1))
    else:
        alpha = op.gather(f, np.repeat(np.arange(4) * 4 + 3, 4).astype(np.int64), np.array(1))
        inverse = op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255)))
    value = op.add(f, op.multiply(b, inverse))
    if variant.endswith("bias"):
        rounded = op.clamp(op.add(value, np.float16(0.5)), 0, 255)
    elif variant == "truncate":
        rounded = op.clamp(value, 0, 255)
    else:
        rounded = op.round(op.clamp(value, 0, 255), "half_to_even")
    packed = op.transpose(op.convert(rounded, np.uint8), np.array([0, 2, 3, 1]))
    return ov.Model([op.reshape(packed, np.array(shape), False)], [bg, fg])


def run(args):
    core = ov.Core()
    report = {
        "variant": args.variant,
        "shape": [args.width, args.height],
        "passed": False,
        "scope": "host-tensor graph cost and accuracy only; no desktop offload or Tahoma gain",
    }
    try:
        report["stack"] = inspect_stack(core)
        require_npu_driver(report["stack"], args.expected_npu_driver)
        graph = model(args.width, args.height, args.variant)
        started = time.perf_counter()
        compiled = core.compile_model(
            graph, "NPU", {"NPU_COMPILER_TYPE": "PLUGIN", "PERF_COUNT": True}
        )
        report["compile_ms"] = (time.perf_counter() - started) * 1000
        devices = compiled.get_property("EXECUTION_DEVICES")
        report["actual_devices"] = [devices] if isinstance(devices, str) else list(devices)
        if not report["actual_devices"] or any(
            d.split(".")[0] != "NPU" for d in report["actual_devices"]
        ):
            raise RuntimeError("NPU execution not confirmed")
        report["compiler"] = str(compiled.get_property("NPU_COMPILER_TYPE"))
        request = compiled.create_infer_request()
        cpu = core.compile_model(
            graph, "CPU", {"INFERENCE_PRECISION_HINT": "f32"}
        ).create_infer_request()
        rng = np.random.default_rng(5010)
        maximum, cpu_maximum, total_error, value_count = 0, 0, 0, 0
        times = []
        for frame in range(args.frames):
            values = [
                rng.integers(0, 256, (1, args.height, args.width, 4), np.uint8) for _ in range(2)
            ]
            for v in values:
                if frame < 2:
                    v[..., 3] = frame * 255
                v[..., :3] = np.minimum(v[..., :3], v[..., 3:4])
            bg, fg = [v.astype(np.uint32) for v in values]
            expected = np.minimum(255, fg + (bg * (255 - fg[..., 3:4]) + 127) // 255).astype(
                np.int16
            )
            control = cpu.infer(values)[0].copy().astype(np.int16)
            cpu_maximum = max(cpu_maximum, int(np.abs(control - expected).max()))
            started = time.perf_counter()
            observed = request.infer(values)[0].copy().astype(np.int16)
            times.append((time.perf_counter() - started) * 1000)
            difference = observed - expected
            maximum = max(maximum, int(np.abs(difference).max()))
            total_error += int(difference.sum())
            value_count += difference.size
        report.update(
            cpu_max_byte_error=cpu_maximum,
            max_byte_error=maximum,
            mean_signed_byte_error=total_error / value_count,
            infer_copy_ms=times,
            passed=maximum <= 1 and cpu_maximum <= 1,
        )
        report["profile"] = [
            {
                "name": p.node_name,
                "type": p.node_type,
                "exec_type": p.exec_type,
                "real_ms": p.real_time.total_seconds() * 1000,
                "cpu_ms": p.cpu_time.total_seconds() * 1000,
                "status": str(p.status),
            }
            for p in request.get_profiling_info()
        ]
    except Exception as error:
        report["error"] = str(error)
    print(json.dumps(report), flush=True)
    return int(not report["passed"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--variant", choices=["baseline", "bias", "truncate", "conv", "conv_bias"], required=True
    )
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--frames", type=int, default=8)
    parser.add_argument("--expected-npu-driver", default="1005540")
    args = parser.parse_args()
    if args.width <= 0 or args.width % 4 or args.height <= 0 or args.frames < 3:
        parser.error(
            "positive dimensions, width divisible by four, and at least three frames required"
        )
    raise SystemExit(run(args))


if __name__ == "__main__":
    main()
