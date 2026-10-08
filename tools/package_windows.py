"""Create a private portable native-window bundle with pinned runtime notices."""

import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
import os
import re
import shutil
import tempfile
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/native/Release"))
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", type=Path, default=Path("dist/npu-render-windows-x64.zip"))
    args = parser.parse_args()
    if os.name != "nt" or not re.fullmatch(r"[0-9a-f]{40}", args.source_revision):
        raise ValueError("requires Windows and a full source commit SHA")
    package = importlib.util.find_spec("openvino")
    runtime = Path(package.origin).parent / "libs"
    metadata = importlib.metadata.distribution("openvino")
    if metadata.version != "2026.1.0":
        raise ValueError("bundle must use the pinned OpenVINO 2026.1.0")
    with tempfile.TemporaryDirectory(prefix="npu-render-package-") as directory:
        root = Path(directory) / "npu-render"
        (root / "runtime").mkdir(parents=True)
        for name in (
            "nr_native_live.exe",
            "nr_desktop_delegate.exe",
            "nr_windows_route_probe.exe",
            "nr_d3d12_interop.exe",
            "npu_render_native.dll",
        ):
            shutil.copy2(args.build_dir / name, root / name)
        libraries = list(runtime.glob("*.dll"))
        if not libraries:
            raise RuntimeError("runtime DLLs missing")
        for path in libraries + list(runtime.glob("*.xml")):
            shutil.copy2(path, root / "runtime" / path.name)
        notice_count = 0
        for entry in metadata.files:
            if ".dist-info/licenses/" in str(entry).replace("\\", "/"):
                relative = str(entry).replace("\\", "/").split(".dist-info/licenses/", 1)[1]
                target = root / "licenses" / "openvino" / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(metadata.locate_file(entry), target)
                notice_count += 1
        if not notice_count:
            raise RuntimeError("OpenVINO redistribution notices missing")
        launcher = (
            "@echo off\r\nsetlocal\r\n"
            'set "PATH=%~dp0runtime;%PATH%"\r\n'
            'if not exist "%LOCALAPPDATA%\\npu-render" mkdir "%LOCALAPPDATA%\\npu-render"\r\n'
            '"%~dp0nr_native_live.exe" --seconds 3600 '
            '--report "%LOCALAPPDATA%\\npu-render\\native-live.json" %*\r\n'
            "if errorlevel 1 pause\r\n"
        )
        (root / "run.cmd").write_bytes(launcher.encode("ascii"))
        delegate_launcher = (
            "@echo off\r\nsetlocal\r\n"
            'set "PATH=%~dp0runtime;%PATH%"\r\n'
            'if not exist "%LOCALAPPDATA%\\npu-render" mkdir "%LOCALAPPDATA%\\npu-render"\r\n'
            '"%~dp0nr_desktop_delegate.exe" --fps 30 --seconds 1800 '
            '--report "%LOCALAPPDATA%\\npu-render\\desktop-delegate.json" %*\r\n'
            "if errorlevel 1 pause\r\n"
        )
        (root / "run-delegate.cmd").write_bytes(delegate_launcher.encode("ascii"))
        (root / "README.txt").write_text(
            "PRIVATE NPU-RENDER PROTOTYPE\n\n"
            "Run run.cmd on a Windows Intel Core Ultra NPU machine.\n"
            "Keys: 1 GPU, 2 NPU, 3 CPU, Escape exit.\n"
            "This changes this application's source-over compositor only.\n"
            "Windows DWM, other applications, games, codecs and scanout "
            "stay on their existing paths.\n"
            "The window reads the npu-render CLI's saved selection every 250 ms.\n"
            "All providers compile and pass full-shape pixel checks before startup.\n"
            "An unavailable NPU causes an explicit startup error. No hidden device fallback.\n"
            "The default session lasts one hour; pass --seconds N to change it.\n"
            "The report is %LOCALAPPDATA%\\npu-render\\native-live.json.\n"
            "Present HRESULT 0x087A0001 means occluded; it is not visible-display evidence.\n\n"
            "Requires current Intel GPU/NPU drivers and Microsoft Visual C++ x64 runtime.\n"
            "OpenVINO 2026.1.0 runtime and its notices are included. Python is not needed.\n"
            "Tested device: Panther Lake Core Ultra X7 358H, Arc B390, NPU 5010.\n"
            "Practical Tahoma acceleration remains unproven.\n\n"
            "COOPERATING WINDOWS EXPERIMENT\n"
            "Run run-delegate.cmd on an unlocked Windows 11 desktop.\n"
            "Keys: 1 Windows DWM, 2 NPU, 3 GPU, Escape exit. Starts in DWM mode.\n"
            "This creates three test windows with GDI content. NPU/GPU modes hide\n"
            "those originals and combine the same surfaces into one canvas.\n"
            "It does not move other applications, the taskbar or all of Windows\n"
            "to the NPU. The outer canvas still uses Windows DWM and GPU copies.\n"
            "The default is 1280x720 at 30 updates/s for 30 minutes.\n"
            "Keep the test canvas visible and unscaled; screen pixel checks reject occlusion.\n"
            "All three modes pass pixel checks before the experiment begins.\n"
            "The report is %LOCALAPPDATA%\\npu-render\\desktop-delegate.json.\n"
            "NPU mode remains an experiment, not a recommended performance setting.\n",
            encoding="utf-8",
        )
        files = {
            path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(root.rglob("*"))
            if path.is_file()
        }
        (root / "manifest.json").write_text(
            json.dumps(
                {
                    "source_revision": args.source_revision,
                    "openvino": metadata.version,
                    "sha256": files,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(
            args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6
        ) as archive:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(root.parent))
    print(
        json.dumps(
            {
                "bundle": str(args.output),
                "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
            }
        )
    )


if __name__ == "__main__":
    main()
