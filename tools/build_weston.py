"""Build the pinned adapter into a private-prefix Weston with headless/nested backends."""

import argparse
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--drm", action="store_true", help="also build the native display backend")
    args = parser.parse_args()
    if sys.platform != "linux":
        raise RuntimeError("Weston requires a native Linux build environment")
    project = Path(__file__).resolve().parents[1]
    source = project / "build/weston-source"
    build = project / "build/weston"
    prefix = project / "build/weston-install"
    subprocess.run([sys.executable, str(project / "tools/prepare_weston.py")], check=True)
    options = {
        "backend-default": "headless",
        "backend-drm": "true" if args.drm else "false",
        "backend-drm-screencast-vaapi": "false",
        "backend-pipewire": "false",
        "backend-rdp": "false",
        "backend-vnc": "false",
        "backend-x11": "false",
        "backend-wayland": "true",
        "renderer-gl": "false",
        "xwayland": "false",
        "systemd": "false",
        "remoting": "false",
        "pipewire": "false",
        "screenshare": "false",
        "shell-desktop": "true",
        "shell-fullscreen": "false",
        "shell-ivi": "false",
        "shell-kiosk": "true",
        "color-management-lcms": "false",
        "image-jpeg": "false",
        "image-webp": "false",
        "tools": "",
        "demo-clients": "false",
        "simple-clients": "shm,damage",
        "wcap-decode": "false",
        "test-junit-xml": "false",
        "doc": "false",
    }
    command = ["meson", "setup", str(build), str(source), "--prefix", str(prefix)]
    if (build / "build.ninja").exists():
        command.append("--reconfigure")
    command.extend(f"-D{key}={value}" for key, value in options.items())
    subprocess.run(command, check=True)
    subprocess.run(["ninja", "-C", str(build), "-j", "4"], check=True)
    subprocess.run(["meson", "install", "-C", str(build)], check=True)


if __name__ == "__main__":
    main()
