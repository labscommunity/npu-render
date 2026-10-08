"""Fetch pinned WDK headers for the process-local Windows DDI laboratory.

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


def prepare(root: Path) -> Path:
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
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
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
    (root / "manifest.json").write_text(
        json.dumps({"url": URL, "sha256": SHA256, "headers_sha256": records}, indent=2) + "\n"
    )
    return include.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_ROOT)
    args = parser.parse_args()
    print(prepare(args.output))


if __name__ == "__main__":
    main()
