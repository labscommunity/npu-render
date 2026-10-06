"""Qualify actual backend switches; absent devices fail rather than falling back."""

import argparse
import json
import platform
from dataclasses import asdict
from datetime import UTC, datetime
from importlib.metadata import version
from pathlib import Path

import numpy as np
import openvino as ov

from npu_render import Composition, Renderer
from npu_render.openvino_backend import OpenVINOProvider
from npu_render.reference import qualification_inputs, source_over_reference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backends", nargs="+", required=True)
    parser.add_argument("--width", type=int, default=96)
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--layers", type=int, default=2)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    spec = Composition(args.width, args.height, args.layers)
    provider = OpenVINOProvider()
    report = {
        "created_utc": datetime.now(UTC).isoformat(),
        "platform": platform.platform(),
        "openvino": ov.__version__,
        "package_version": version("npu-render"),
        "devices": [asdict(device) for device in provider.devices()],
        "operation": asdict(spec),
        "scope": "backend switching and correctness; no display or Tahoma performance claim",
        "switches": [],
    }
    inputs = qualification_inputs(spec)
    expected = source_over_reference(inputs, spec)
    renderer = None
    for backend in args.backends:
        item = {"requested": backend}
        try:
            if renderer is None:
                renderer = Renderer(spec, backend, provider=provider)
            else:
                renderer.switch(backend)
            frame = renderer.compose(
                inputs["background"], [inputs[n] for n in spec.input_names[1:]]
            )
            error = float(np.max(np.abs(frame.pixels.astype(np.float64) - expected)))
            item.update(
                backend=frame.backend,
                execution_devices=list(frame.execution_devices),
                host_call_ms=frame.host_call_ms,
                max_abs_error=error if np.isfinite(error) else None,
                passed=bool(np.isfinite(frame.pixels).all() and error <= spec.tolerance),
            )
        except (ValueError, RuntimeError) as error:
            item.update(passed=False, error=str(error))
        report["switches"].append(item)
        print(backend, "pass" if item["passed"] else "fail")
    report["passed"] = all(item["passed"] for item in report["switches"])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
