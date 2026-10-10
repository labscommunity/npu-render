"""Fetch pinned WDK headers and optional kernel build libraries.

This extracts a build dependency only. It does not install or register drivers.
"""

import argparse
import hashlib
import json
import shutil
import urllib.request
import zipfile
from pathlib import Path

VERSION = "10.0.26100.6584"
SHA256 = "c393d03dfb640b5c92f546b32f6770ef68cd3aaf691956e7d66d8e2c28a1b55e"
URL = (
    "https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x64/"
    f"{VERSION}/microsoft.windows.wdk.x64.{VERSION}.nupkg"
)
PREFIX = "c/Include/10.0.26100.0/"
DEFAULT_ROOT = Path("build/dependencies") / f"wdk-{VERSION}"


def prepare(root: Path, *, kernel: bool = False, umdf: bool = False) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    archive = root / "wdk.nupkg"
    if not archive.exists():
        temporary = root / "wdk.nupkg.part"
        with urllib.request.urlopen(URL, timeout=90) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError("WDK package checksum mismatch")
    include = root / "include"
    records = {}
    libraries = {}
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            if umdf and not entry.is_dir():
                umdf_prefixes = {
                    "c/Include/wdf/umdf/2.25/": root / "umdf/include",
                    "c/Lib/wdf/umdf/x64/2.25/": root / "umdf/lib",
                    "c/Lib/10.0.26100.0/um/x64/iddcx/1.4/": root / "iddcx/lib",
                }
                for prefix, destination in umdf_prefixes.items():
                    if not entry.filename.startswith(prefix):
                        continue
                    relative = Path(entry.filename.removeprefix(prefix))
                    if relative.is_absolute() or ".." in relative.parts:
                        raise RuntimeError("unsafe UMDF dependency path")
                    target = destination / relative
                    data = package.read(entry)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                    libraries[target.relative_to(root).as_posix()] = hashlib.sha256(
                        data
                    ).hexdigest()
            library_prefix = "c/Lib/10.0.26100.0/km/x64/"
            if kernel and entry.filename.startswith(library_prefix) and not entry.is_dir():
                relative = Path(entry.filename.removeprefix(library_prefix))
                if relative.is_absolute() or ".." in relative.parts:
                    raise RuntimeError("unsafe WDK library path")
                target = root / "lib" / relative
                data = package.read(entry)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                libraries[relative.as_posix()] = hashlib.sha256(data).hexdigest()
            if not entry.filename.startswith(PREFIX) or entry.is_dir():
                continue
            relative = Path(entry.filename.removeprefix(PREFIX))
            if relative.is_absolute() or ".." in relative.parts:
                raise RuntimeError("unsafe WDK header path")
            target = include / relative
            data = package.read(entry)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            records[relative.as_posix()] = hashlib.sha256(data).hexdigest()
    if not (include / "um/d3d10umddi.h").is_file():
        raise RuntimeError("WDK driver interface header missing")
    if kernel:
        for name in ("ntoskrnl.lib", "hal.lib", "displib.lib", "BufferOverflowK.lib"):
            if name not in libraries:
                raise RuntimeError(f"WDK kernel library missing: {name}")
    if umdf:
        for name in (
            "umdf/include/wdf.h",
            "umdf/lib/WdfDriverStubUm.lib",
            "iddcx/lib/iddcxstub.lib",
        ):
            if not (root / name).is_file():
                raise RuntimeError(f"WDK UMDF dependency missing: {name}")
    (root / "manifest.json").write_text(
        json.dumps(
            {
                "url": URL,
                "sha256": SHA256,
                "headers_sha256": records,
                "libraries_sha256": libraries,
            },
            indent=2,
        )
        + "\n"
    )
    return include.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--kernel", action="store_true", help="also extract x64 kernel libraries")
    parser.add_argument(
        "--umdf", action="store_true", help="also extract pinned UMDF/IddCx dependencies"
    )
    args = parser.parse_args()
    print(prepare(args.output, kernel=args.kernel, umdf=args.umdf))


if __name__ == "__main__":
    main()
