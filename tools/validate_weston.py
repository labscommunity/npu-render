"""Exercise a real headless Wayland client and adapter; never claims physical display proof."""

import argparse
import json
import subprocess
import tempfile
import time
from pathlib import Path

from weston_environment import environment as weston_environment

from npu_render.config import save_backend


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", default="CPU", choices=["CPU", "GPU", "NPU"])
    parser.add_argument("--report", type=Path, default=Path("results/weston.json"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    prefix = root / "build/weston-install"
    compositor = client = None
    with tempfile.TemporaryDirectory(prefix="npu-render-weston-") as directory:
        runtime = Path(directory)
        config = runtime / "selection.json"
        save_backend(args.backend, config)
        environment = weston_environment(root)
        environment.pop("NPU_RENDER_BACKEND", None)
        environment.update(
            XDG_RUNTIME_DIR=str(runtime),
            WAYLAND_DISPLAY="npu-render-validation",
            NPU_RENDER_CONFIG=str(config),
            NPU_RENDER_NATIVE_LIBRARY=str(root / "build/native/libnpu_render_native.so"),
        )
        with (
            (runtime / "weston.log").open("w") as log,
            (runtime / "client.log").open("w") as client_log,
        ):
            try:
                compositor = subprocess.Popen(
                    [
                        str(prefix / "bin/weston"),
                        "--backend=headless-backend.so",
                        "--renderer=pixman",
                        "--shell=kiosk-shell.so",
                        "--socket=npu-render-validation",
                        "--width=320",
                        "--height=240",
                        "--idle-time=0",
                        "--no-config",
                    ],
                    env=environment,
                    stdout=log,
                    stderr=log,
                )
                deadline = time.monotonic() + 30
                while not (runtime / environment["WAYLAND_DISPLAY"]).exists():
                    if compositor.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Weston failed to create its socket")
                    time.sleep(0.05)
                client = subprocess.Popen(
                    [str(prefix / "bin/weston-simple-shm")],
                    env=environment,
                    stdout=client_log,
                    stderr=client_log,
                )
                time.sleep(3)
                if client.poll() is not None:
                    raise RuntimeError("Wayland client exited unexpectedly")
                # Invalid selection must leave the running renderer usable.
                config.write_text('{"version":1,"backend":"INVALID"}\n')
                time.sleep(1)
                save_backend(args.backend, config)
                time.sleep(2)
                if compositor.poll() is not None or client.poll() is not None:
                    raise RuntimeError("configuration change interrupted Wayland rendering")
            finally:
                stop(client)
                stop(compositor)
                log.flush()
                client_log.flush()
                print((runtime / "weston.log").read_text(), flush=True)
                print((runtime / "client.log").read_text(), flush=True)
        captured = (runtime / "weston.log").read_text()
        summaries = [
            json.loads(line.split("npu-render-summary: ", 1)[1])
            for line in captured.splitlines()
            if "npu-render-summary: " in line
        ]
        frames = [
            json.loads(line.split("npu-render-frame: ", 1)[1])
            for line in captured.splitlines()
            if "npu-render-frame: " in line
        ]
        if not summaries or summaries[-1]["native_calls"] < 1 or not frames:
            raise RuntimeError("no actual native compositor work was observed")
        if summaries[-1]["native_failures"]:
            raise RuntimeError("native compositor operations failed")
        if "rejected invalid selection" not in captured:
            raise RuntimeError("live configuration rejection was not exercised")
        report = {
            "backend_requested": args.backend,
            "scope": "headless Wayland integration; no scanout or native-display timing",
            "summary": summaries[-1],
            "sampled_native_frames": frames,
            "invalid_selection_preserved_rendering": True,
        }
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
