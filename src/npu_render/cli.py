"""Inspect devices, select the backend, and render a deterministic image."""

import argparse
import json
import sys
from dataclasses import asdict
from pathlib import Path

import numpy as np

from . import desktop_switch
from .config import config_path, resolve_backend, save_backend
from .contracts import Composition
from .openvino_backend import OpenVINOProvider
from .openvino_diagnostics import inspect_stack, require_npu_driver
from .reference import from_nchw, to_nchw
from .renderer import Renderer
from .windows_session import inspect_session, require_interactive_desktop


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
    stack = commands.add_parser(
        "stack", help="inspect loaded runtime, compiler and driver versions"
    )
    stack.add_argument("--expected-npu-driver", help="require the exact runtime-reported version")
    session = commands.add_parser("session", help="inspect the Windows desktop without changing it")
    session.add_argument(
        "--require-desktop", action="store_true", help="require benchmark preconditions"
    )
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
    live = commands.add_parser("live", help="animate and switch a software window's composition")
    live.add_argument("--backend")
    live.add_argument("--config", type=Path)
    live.add_argument("--width", type=int, default=1280)
    live.add_argument("--height", type=int, default=720)
    live.add_argument("--layers", type=int, default=2)
    live.add_argument("--seconds", type=float, default=30)
    live.add_argument("--fps", type=float, default=60)
    live.add_argument("--sequence", default="", help="comma-separated switch destinations")
    live.add_argument("--switch-every", type=float, default=10)
    live.add_argument("--headless", action="store_true", help="explicitly skip window presentation")
    live.add_argument("--report", type=Path, default=Path("results/live.json"))
    desktop_switch.add_parser(commands)
    args = parser.parse_args(argv)
    try:
        if args.command == "devices":
            result = {"devices": [asdict(device) for device in OpenVINOProvider().devices()]}
        elif args.command == "desktop":
            result = desktop_switch.run(args)
            if args.desktop_command == "status" and not args.json:
                print(desktop_switch.summarize(result))
                return 0
            if result.get("passed") is False or result.get("ready") is False:
                print(json.dumps(result, indent=2))
                return 1
        elif args.command == "stack":
            result = inspect_stack()
            if args.expected_npu_driver:
                try:
                    require_npu_driver(result, args.expected_npu_driver)
                except RuntimeError as error:
                    print(json.dumps({**result, "passed": False, "error": str(error)}, indent=2))
                    return 1
                result["passed"] = True
        elif args.command == "session":
            result = inspect_session()
            if args.require_desktop:
                try:
                    require_interactive_desktop(result)
                except RuntimeError as error:
                    print(json.dumps({**result, "passed": False, "error": str(error)}, indent=2))
                    return 1
                result["passed"] = True
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
        elif args.command == "live":
            from .live import run_session
            from .presentation import NullPresenter, WindowPresenter

            spec = Composition(args.width, args.height, args.layers)
            renderer = Renderer(spec, args.backend, config=args.config)
            presenter = (
                NullPresenter() if args.headless else WindowPresenter(spec.width, spec.height)
            )
            try:
                result = run_session(
                    renderer,
                    presenter,
                    seconds=args.seconds,
                    fps=args.fps,
                    sequence=[part.strip() for part in args.sequence.split(",") if part.strip()],
                    switch_every=args.switch_every,
                )
            finally:
                presenter.close()
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            result = {key: value for key, value in result.items() if key != "frames"}
            result["report"] = str(args.report)
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
