"""Emit the frame kernel blobs: per-blob generated headers, fixtures and inlined SHAVE sources.

The host build uses native/windows/ptl_kernels/frame_programs.h (every eligible
program). Each kernel blob K gets its own header with only its programs, the
fixtures that exercise them, and a single-file kernel source for Delta.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAB = ROOT / "native/windows/registered_lab"
KERNELS = ROOT / "native/windows/ptl_kernels"
sys.path.insert(0, str(ROOT / "tools"))
from build_ptl_kernel import inline_local_includes  # noqa: E402

BLOBS = 18  # frame::blob_count(): the ranges' splits summed
CXX = [
    "clang++",
    "-std=c++17",
    "-O1",
    "-ffp-contract=off",
    "-Wall",
    "-Wextra",
    "-Wshadow",
    "-Wno-unused-function",
    "-Wno-unused-parameter",
    "-I",
    str(LAB),
]


def build_test(output, header, identity=None):
    """Compile frame_kernel_test against a specific generated header (and kernel identity)."""
    subprocess.run(
        CXX
        + ([f"-DNR_KERNEL_IDENTITY={identity}u"] if identity else [])
        + [
            f'-DNR_FRAME_PROGRAMS_HEADER="{header}"',
            "-o",
            str(output),
            str(LAB / "frame_kernel_test.cpp"),
        ],
        check=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="bundle directory to fill")
    parser.add_argument("--blobs", type=int, default=BLOBS)
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    work = out / "work"
    work.mkdir(exist_ok=True)
    manifest = {"blobs": []}
    # Host header: every eligible program (also what the executor compiles against).
    stub = work / "test-stub"
    build_test(stub, KERNELS / "frame_programs.h")
    subprocess.run([str(stub), "--emit", str(KERNELS / "frame_programs.h")], check=True)
    for blob in range(args.blobs):
        blob_dir = work / f"blob{blob}"
        blob_dir.mkdir(exist_ok=True)
        header = blob_dir / "frame_programs.h"
        subprocess.run([str(stub), "--emit", str(header), "--blob", str(blob)], check=True)
        # The kernel identity: the first 32 bits of the blob header's sha256, written by the device
        # into every output row and checked by the worker against the deploy manifest.
        identity = "0x" + hashlib.sha256(header.read_bytes()).hexdigest()[:8]
        test = blob_dir / "test"
        build_test(test, header, identity)
        fixtures = out / f"frame-fixtures-blob{blob}.bin"
        result = subprocess.run(
            [str(test), "--fixtures", str(fixtures)], check=True, capture_output=True, text=True
        )
        summary = [line for line in result.stdout.splitlines() if "eligible_programs" in line][-1]
        programs = [line for line in result.stdout.splitlines() if '"native":' in line]
        shutil.copy(KERNELS / "frame_core.h", blob_dir / "frame_core.h")
        shutil.copy(KERNELS / "frame.c", blob_dir / "frame.c")
        define = f"#define NR_KERNEL_IDENTITY {identity}u\n".encode()
        # Blobs whose programs never fetch (ld) leave that runtime support out of the code window.
        if b"nr_load(" not in header.read_bytes():
            define += b"#define NR_FRAME_NO_LD 1\n"
        # Likewise the dual-source blend (mode 14) stays out of blobs without a two-output program.
        if b"nr_blend_pixel_dual(" not in header.read_bytes():
            define += b"#define NR_FRAME_NO_DUAL 1\n"
        # Blobs of large programs sit at the code window: the invert blend (mode 15) stays out
        # of them.
        if b"NR_FRAME_LARGE_PROGRAMS" in header.read_bytes():
            define += b"#define NR_FRAME_NO_INVERT_BLEND 1\n"
        source = define + inline_local_includes(blob_dir / "frame.c")
        (out / f"frame_blob{blob}.c").write_bytes(source)
        manifest["blobs"].append(
            dict(
                blob=blob,
                identity=identity,
                programs=len(programs),
                shaders=[json.loads(p)["shader"] for p in programs],
                header_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),
                source_sha256=hashlib.sha256(source).hexdigest(),
                fixtures_sha256=hashlib.sha256(fixtures.read_bytes()).hexdigest(),
                host_summary=json.loads(summary),
            )
        )
        print(f"blob {blob}: {len(programs)} programs, source {len(source)} bytes, {summary}")
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # Rebuild the host header last so the repo copy always holds every program.
    subprocess.run([str(stub), "--emit", str(KERNELS / "frame_programs.h")], check=True)


if __name__ == "__main__":
    main()
