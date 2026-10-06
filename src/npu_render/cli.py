"""Inspect devices, select the backend, and render a deterministic image."""

import argparse
import json
import sys
from dataclasses import asdict
from pathlib import Path

import numpy as np

from .config import config_path, resolve_backend, save_backend
from .contracts import Composition
from .openvino_backend import OpenVINOProvider
from .reference import from_nchw, to_nchw
from .renderer import Renderer


def demo_inputs(spec: Composition) -> tuple[np.ndarray, list[np.ndarray]]:
    y, x = np.mgrid[: spec.height, : spec.width]
    background = np.ones((1, 4, spec.height, spec.width), dtype=spec.dtype)
    background[0, 0] = 0.03 + 0.12 * x / spec.width
    background[0, 1] = 0.04 + 0.12 * y / spec.height
    background[0, 2] = 0.12
    layers = []
    for i in range(spec.layers):
        rgba = np.zeros_like(background)
        shift = i / (spec.layers + 1) * 0.25
        inside = (
            (x >= spec.width * (0.1 + shift))
            & (x < spec.width * (0.65 + shift))
            & (y >= spec.height * (0.1 + shift))
            & (y < spec.height * (0.7 + shift))
        )
        rgba[0, 3, inside] = 0.65
        color = np.array(([0.1, 0.65, 0.9], [0.85, 0.15, 0.2], [0.25, 0.8, 0.3])[i % 3])
        rgba[0, :3] = color[:, None, None] * rgba[0, 3]
        layers.append(from_nchw(rgba, spec))
    return from_nchw(background, spec), layers


def write_ppm(path: Path, pixels: np.ndarray, spec: Composition) -> None:
    rgb = np.clip(to_nchw(pixels, spec)[0, :3].transpose(1, 2, 0).astype(np.float64), 0, 1)
    # Demo buffers use linear sRGB. The opaque preview is encoded for display.
    srgb = np.where(rgb <= 0.0031308, 12.92 * rgb, 1.055 * rgb ** (1 / 2.4) - 0.055)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        f"P6\n{spec.width} {spec.height}\n255\n".encode()
        + np.rint(srgb * 255).astype(np.uint8).tobytes()
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("devices", help="list actual runtime devices")
    status = commands.add_parser("status", help="show the effective backend selection")
    status.add_argument("--config", type=Path)
    select = commands.add_parser("select", help="qualify a small graph and save the backend")
    select.add_argument("backend")
    select.add_argument("--config", type=Path)
    demo = commands.add_parser("demo", help="compose translucent rectangles into a PPM image")
    demo.add_argument("--backend")
    demo.add_argument("--config", type=Path)
    demo.add_argument("--width", type=int, default=640)
    demo.add_argument("--height", type=int, default=360)
    demo.add_argument("--layers", type=int, default=2)
    demo.add_argument("--precision", choices=["f16", "f32"], default="f16")
    demo.add_argument("--layout", choices=["NCHW", "NHWC"], default="NCHW")
    demo.add_argument("--output", type=Path, default=Path("results/frame.ppm"))
    args = parser.parse_args(argv)
    try:
        if args.command == "devices":
            result = {"devices": [asdict(device) for device in OpenVINOProvider().devices()]}
        elif args.command == "status":
            result = {
                "backend": resolve_backend(path=args.config),
                "config": str(args.config if args.config is not None else config_path()),
            }
        elif args.command == "select":
            renderer = Renderer(Composition(16, 16), backend=args.backend)
            path = save_backend(renderer.backend, args.config)
            result = {
                "saved_backend": renderer.backend,
                "effective_backend": resolve_backend(path=path),
                "config": str(path),
                "qualified_shape": [16, 16],
                "scope": "new renderers or explicit reload_selection(); integrations required",
            }
        else:
            spec = Composition(args.width, args.height, args.layers, args.precision, args.layout)
            renderer = Renderer(spec, args.backend, config=args.config)
            frame = renderer.compose(*demo_inputs(spec))
            write_ppm(args.output, frame.pixels, spec)
            result = {
                "backend": frame.backend,
                "execution_devices": frame.execution_devices,
                "provider": frame.provider,
                "host_call_ms": frame.host_call_ms,
                "output": str(args.output),
                "scope": "single graph call; excludes preparation, conversion and presentation",
            }
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, RuntimeError, OSError) as error:
        print(json.dumps({"error": str(error)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
