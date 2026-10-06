"""Exercise the native window's shared selection file and rejected-switch recovery."""

import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

from npu_render.benchmark import write_report
from npu_render.config import save_backend


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--report", type=Path, default=Path("results/native-control.json"))
    parser.add_argument(
        "--allow-occluded",
        action="store_true",
        help="validate rendering/control only, not visible submission",
    )
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("Windows hardware is required")
    package = importlib.util.find_spec("openvino")
    environment = os.environ.copy()
    environment.pop("NPU_RENDER_BACKEND", None)
    environment["PATH"] = (
        str(Path(package.origin).parent / "libs") + os.pathsep + environment["PATH"]
    )
    with tempfile.TemporaryDirectory(prefix="npu-render-control-") as directory:
        root = Path(directory)
        config, ready, stop, output = (
            root / name for name in ("config.json", "ready", "stop", "frames.json")
        )
        save_backend("GPU", config)
        command = [
            str(args.executable.resolve()),
            "--config",
            str(config),
            "--ready-file",
            str(ready),
            "--stop-file",
            str(stop),
            "--report",
            str(output),
            "--seconds",
            "60",
        ]
        with (root / "child.log").open("w+", encoding="utf-8") as log:
            process = subprocess.Popen(command, env=environment, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 90
                while not ready.exists():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        log.seek(0)
                        raise RuntimeError("renderer did not become ready: " + log.read())
                    time.sleep(0.05)
                time.sleep(3)
                save_backend("NPU", config)
                time.sleep(5)
                write_report(config, {"version": 1, "backend": "AUTO"})
                time.sleep(3)
                save_backend("CPU", config)
                time.sleep(5)
                save_backend("GPU", config)
                time.sleep(3)
                stop.write_text("stop\n", encoding="utf-8")
                if process.wait(timeout=15):
                    log.seek(0)
                    raise RuntimeError("renderer failed: " + log.read())
                graphics = json.loads(output.read_text(encoding="utf-8"))
                write_report(args.report, {"passed": False, "graphics": graphics})
                accepted = [s["backend"] for s in graphics["selections"] if s["accepted"]]
                rejected = [s for s in graphics["selections"] if not s["accepted"]]
                assert accepted == ["GPU", "NPU", "CPU", "GPU"], accepted
                assert len(rejected) == 1, rejected
                cpu_switch = next(
                    s["elapsed_s"] for s in graphics["selections"] if s.get("backend") == "CPU"
                )
                retained = [
                    f
                    for f in graphics["frames"]
                    if rejected[0]["elapsed_s"] + 0.001 <= f["elapsed_s"] < cpu_switch - 0.001
                ]
                assert retained and all(f["backend"] == "NPU" for f in retained)
                assert {f["backend"] for f in graphics["frames"]} == {"GPU", "NPU", "CPU"}
                visible = graphics["initial_present_hresult"] == 0 and all(
                    f["present_hresult"] == 0 for f in graphics["frames"]
                )
                report = {
                    "passed": visible or args.allow_occluded,
                    "control_and_recovery_passed": True,
                    "all_present_calls_s_ok": visible,
                    "scanout_measured": False,
                    "renderer_sha256": hashlib.sha256(args.executable.read_bytes()).hexdigest(),
                    "graphics": graphics,
                }
                write_report(args.report, report)
                if not report["passed"]:
                    raise RuntimeError(
                        "control passed, but display was occluded; see preserved report"
                    )
                print(json.dumps({k: v for k, v in report.items() if k != "graphics"}), flush=True)
            finally:
                if process.poll() is None:
                    stop.write_text("stop\n", encoding="utf-8")
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        process.wait(timeout=15)


if __name__ == "__main__":
    main()
