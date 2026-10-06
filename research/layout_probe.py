"""Compare exact one-hot convolution and transpose for packed graphics buffers."""

import argparse
import json
import time

import numpy as np
import openvino as ov
from openvino import opset13 as op


def model(width, height, variant):
    shape = [1, height, width, 4]
    bg, fg = op.parameter(shape, np.uint8), op.parameter(shape, np.uint8)
    weights = np.eye(4, dtype=np.float16).reshape(4, 1, 1, 4)

    if variant.startswith("channels_"):
        group = int(variant.split("_")[1])
        if width % group:
            raise ValueError("width must be divisible by the pixel group")
        blocked_shape = np.array([1, height, width // group, 4 * group])

        def blocked(parameter):
            blocks = op.reshape(op.convert(parameter, np.float16), blocked_shape, False)
            return op.transpose(blocks, np.array([0, 3, 1, 2]))

        b, f = blocked(bg), blocked(fg)
        alpha_indices = np.repeat(np.arange(group) * 4 + 3, 4).astype(np.int64)
        alpha = op.gather(f, alpha_indices, np.array(1))
        value = op.add(
            f, op.multiply(b, op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255))))
        )
        result = op.convert(op.round(op.clamp(value, 0, 255), "half_to_even"), np.uint8)
        result = op.transpose(result, np.array([0, 2, 3, 1]))
        return ov.Model([op.reshape(result, np.array(shape), False)], [bg, fg])

    if variant in {"flat_nearest", "flat_broadcast", "flat_convolution"}:
        flat_shape = np.array([1, 1, height, width * 4])
        b = op.reshape(op.convert(bg, np.float16), flat_shape, False)
        f = op.reshape(op.convert(fg, np.float16), flat_shape, False)
        if variant == "flat_convolution":
            alpha = op.convolution(f, weights[3:4], [1, 4], [0, 0], [0, 0], [1, 1])
            alpha = op.convolution_backprop_data(
                alpha,
                np.ones((1, 1, 1, 4), np.float16),
                [1, 4],
                pads_begin=[0, 0],
                pads_end=[0, 0],
                dilations=[1, 1],
            )
        else:
            alpha = op.strided_slice(
                f,
                np.array([0, 0, 0, 3]),
                flat_shape,
                np.array([1, 1, 1, 4]),
                [0, 0, 0, 0],
                [0, 0, 0, 0],
            )
            if variant == "flat_nearest":
                alpha = op.interpolate(
                    alpha,
                    np.array([width * 4]),
                    "nearest",
                    "sizes",
                    axes=np.array([3]),
                    coordinate_transformation_mode="asymmetric",
                    nearest_mode="floor",
                )
            else:
                alpha = op.unsqueeze(alpha, np.array([4]))
                alpha = op.broadcast(alpha, np.array([1, 1, height, width, 4]))
                alpha = op.reshape(alpha, flat_shape, False)
        value = op.add(
            f, op.multiply(b, op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255))))
        )
        result = op.convert(op.round(op.clamp(value, 0, 255), "half_to_even"), np.uint8)
        return ov.Model([op.reshape(result, np.array(shape), False)], [bg, fg])

    def planar(parameter):
        converted = op.convert(parameter, np.float16)
        if variant in {"pack", "both"}:
            rows = op.reshape(converted, np.array([1, 1, height, width * 4]), False)
            return op.convolution(rows, weights, [1, 4], [0, 0], [0, 0], [1, 1])
        return op.transpose(converted, np.array([0, 3, 1, 2]))

    b, f = planar(bg), planar(fg)
    alpha = op.gather(f, np.array([3]), np.array(1))
    result = op.add(
        f, op.multiply(b, op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255))))
    )
    if variant in {"unpack", "both"}:
        result = op.convolution_backprop_data(
            result, weights, [1, 4], pads_begin=[0, 0], pads_end=[0, 0], dilations=[1, 1]
        )
        result = op.reshape(result, np.array(shape), False)
    else:
        result = op.transpose(result, np.array([0, 2, 3, 1]))
    result = op.convert(op.round(op.clamp(result, 0, 255), "half_to_even"), np.uint8)
    return ov.Model([result], [bg, fg])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--device", default="NPU")
    parser.add_argument("--frames", type=int, default=8)
    parser.add_argument(
        "--variants",
        nargs="+",
        default=["transpose", "pack", "unpack", "both"],
        choices=[
            "transpose",
            "pack",
            "unpack",
            "both",
            "flat_nearest",
            "flat_broadcast",
            "flat_convolution",
            "channels_4",
            "channels_16",
            "channels_32",
        ],
    )
    args = parser.parse_args()
    core = ov.Core()
    for variant in args.variants:
        report = {"variant": variant, "device": args.device, "extent": [args.width, args.height]}
        try:
            start = time.perf_counter()
            compiled = core.compile_model(model(args.width, args.height, variant), args.device)
            report["compile_ms"] = 1000 * (time.perf_counter() - start)
            report["actual"] = compiled.get_property("EXECUTION_DEVICES")
            request = compiled.create_infer_request()
            rng = np.random.default_rng(5010)
            times, maximum = [], 0
            for _ in range(args.frames):
                shape = (1, args.height, args.width, 4)
                background = rng.integers(0, 256, shape, dtype=np.uint8)
                background[..., 3] = 255
                foreground = rng.integers(0, 256, shape, dtype=np.uint8)
                foreground[..., :3] = np.minimum(foreground[..., :3], foreground[..., 3:4])
                expected = foreground.astype(np.float64) + background * (
                    1 - foreground[..., 3:4].astype(np.float64) / 255
                )
                expected = np.rint(np.clip(expected, 0, 255)).astype(np.uint8)
                start = time.perf_counter()
                observed = request.infer([background, foreground])[0].copy()
                times.append(1000 * (time.perf_counter() - start))
                maximum = max(
                    maximum,
                    int(np.abs(observed.astype(np.int16) - expected.astype(np.int16)).max()),
                )
            report.update(infer_copy_ms=times, max_byte_error=maximum, passed=maximum <= 1)
        except Exception as error:
            report.update(passed=False, error=str(error))
        print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
