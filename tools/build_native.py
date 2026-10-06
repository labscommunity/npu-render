"""Build and test the native C ABI against the installed pinned OpenVINO wheel."""

import argparse
import importlib.util
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/native"))
    parser.add_argument("--devices", nargs="+", default=["CPU"])
    parser.add_argument(
        "--pixman", action="store_true", help="also test the Linux compositor adapter"
    )
    args = parser.parse_args()
    package = importlib.util.find_spec("openvino")
    if package is None or package.origin is None:
        raise RuntimeError("install the locked Python environment first")
    root = Path(package.origin).parent
    environment = os.environ.copy()
    environment["PATH"] = str(root / "libs") + os.pathsep + environment["PATH"]
    subprocess.run(
        [
            "cmake",
            "-S",
            "native",
            "-B",
            str(args.build_dir),
            f"-DOpenVINO_DIR={root / 'cmake'}",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DNR_WITH_PIXMAN={'ON' if args.pixman else 'OFF'}",
        ],
        check=True,
        env=environment,
    )
    subprocess.run(
        ["cmake", "--build", str(args.build_dir), "--config", "Release", "--parallel", "4"],
        check=True,
        env=environment,
    )
    executable = (
        args.build_dir / "Release" / "nr_native_test.exe"
        if os.name == "nt"
        else args.build_dir / "nr_native_test"
    )
    for device in args.devices:
        subprocess.run([str(executable), device], check=True, env=environment)
        sampler = executable.with_name(
            "nr_sampling_test.exe" if os.name == "nt" else "nr_sampling_test"
        )
        subprocess.run([str(sampler), device], check=True, env=environment)
    subprocess.run(
        [
            "ctest",
            "--test-dir",
            str(args.build_dir),
            "--build-config",
            "Release",
            "--output-on-failure",
        ],
        check=True,
        env=environment,
    )


if __name__ == "__main__":
    main()
