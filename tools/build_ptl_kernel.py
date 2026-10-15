"""Build an experimental SHAVE kernel with a locally supplied, pinned toolchain.

The toolchain is not downloaded or redistributed. This tool never loads an NPU
graph, installs a driver, or changes the desktop. Qualification is a separate step.
"""

import argparse
import ctypes as C
import hashlib
import json
import os
import subprocess
import sys
import traceback
from datetime import UTC, datetime
from pathlib import Path

from npu_render.ptl_carrier import read_kernel_text

TOOLS = {
    "moviCompile64.dll": "f5e164f0d9e02574ff94676f6bb799597640602ccdbb685724f241a8ed170c2e",
    "moviAsm64.dll": "4f29b3e3a2b9ea5bffd6748494df2efc748af6b181fd4563bbe31cfa709a4fa8",
    "moviLLD64.dll": "7294c2c00727fef46f2a42c896eb409da2ed3bbf5eab89418ac52dcb749af9ef",
}
MAX_OUTPUT = 16 * 1024 * 1024


def checked_toolchain(root):
    for name, expected in TOOLS.items():
        if hashlib.sha256((root / "bin" / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"Untested toolchain DLL: {name}")
    return TOOLS.copy()


def output_bytes(pointer, size, limit=MAX_OUTPUT):
    if not 0 <= size <= limit or (size and not pointer):
        raise ValueError("Invalid tool output extent")
    return C.string_at(pointer, size) if size else b""


def transform(root, source, output, assemble=False):
    name = "moviAsm64.dll" if assemble else "moviCompile64.dll"
    lib = C.CDLL(str(root / "bin" / name), winmode=0x1100)
    main = lib.process if assemble else lib.main
    main.argtypes = [
        C.c_int,
        C.POINTER(C.c_char_p),
        C.c_void_p,
        C.c_uint32 if assemble else C.c_size_t,
        C.POINTER(C.c_void_p),
        C.POINTER(C.c_size_t),
        C.POINTER(C.c_void_p),
        C.POINTER(C.c_size_t),
    ]
    main.restype = C.c_int
    cleanup = lib.freeResults
    cleanup.argtypes, cleanup.restype = [], None
    args = (
        [b"moviAsm.dll", b"--cv", b"5000xx", b"--noSPrefixing"]
        if assemble
        else [
            b"moviCompile.dll",
            b"-cc1",
            b"-triple",
            b"shave",
            b"-target-cpu",
            b"5000xx",
            b"-S",
            b"-O2",
            b"-x",
            b"c",
            b"entry.c",
            b"-ffunction-sections",
            b"-fdata-sections",
            b"-o",
            b"-",
        ]
    )
    argv, data = (C.c_char_p * len(args))(*args), C.create_string_buffer(source)
    result, length, diagnostic, diagnostic_length = (
        C.c_void_p(),
        C.c_size_t(),
        C.c_void_p(),
        C.c_size_t(),
    )
    try:
        code = main(
            len(args),
            argv,
            data,
            len(source),
            C.byref(result),
            C.byref(length),
            C.byref(diagnostic),
            C.byref(diagnostic_length),
        )
        messages = output_bytes(diagnostic, diagnostic_length.value, 1024 * 1024)
        (output / ("assembler.log" if assemble else "compiler.log")).write_bytes(messages)
        if code:
            raise RuntimeError(f"{name} returned {code}; see retained diagnostics")
        value = output_bytes(result, length.value)
        if not value:
            raise RuntimeError(f"{name} returned empty output")
        return value
    finally:
        cleanup()


class Buffer(C.Structure):
    _fields_ = [("data", C.c_void_p), ("size", C.c_size_t)]


class Buffers(C.Structure):
    _fields_ = [("items", C.POINTER(Buffer)), ("count", C.c_size_t)]


def link(root, object_bytes):
    lib = C.CDLL(str(root / "bin/moviLLD64.dll"), winmode=0x1100)
    main = lib.process
    main.argtypes = [C.c_int, C.POINTER(C.c_char_p), C.POINTER(Buffers), C.POINTER(Buffer)]
    main.restype = C.c_int
    cleanup = lib.freeResults
    cleanup.argtypes, cleanup.restype = [], None
    script = (
        b'OUTPUT_FORMAT("elf32-sparc")\nOUTPUT_ARCH(sparc)\n'
        b"SECTIONS { . = 0x1e000000; .arg.data : { *(.arg.data) *(.data*) . = ALIGN(16); } "
        b". = 0x1d000000; .text : { *(.text*) . = ALIGN(16); *(.rodata*) . = ALIGN(16); } }\n"
    )
    buffers = [C.create_string_buffer(object_bytes), C.create_string_buffer(script)]
    items = (Buffer * 2)(
        *[
            Buffer(C.cast(b, C.c_void_p), size)
            for b, size in zip(buffers, [len(object_bytes), len(script)], strict=True)
        ]
    )
    # This exact, zero-initialized storage matches the experimentally qualified DLL ABI.
    inputs, outputs = (Buffers * 8)(), (Buffer * 8)()
    inputs[0] = Buffers(items, 2)
    args = [
        b"moviLLD.dll",
        b"-flavor",
        b"gnu",
        b"-EL",
        b"-e",
        b"nr_probe_kernel",
        b"-z",
        b"max-page-size=0x10",
        b"--gc-sections",
    ]
    argv = (C.c_char_p * len(args))(*args)
    try:
        code = main(
            len(args), argv, C.cast(inputs, C.POINTER(Buffers)), C.cast(outputs, C.POINTER(Buffer))
        )
        if code:
            raise RuntimeError(f"Linker returned {code}")
        return output_bytes(outputs[0].data, outputs[0].size)
    finally:
        cleanup()


def write(path, report):
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    report = {
        "started": datetime.now(UTC).isoformat(),
        "completed": False,
        "passed": False,
        "target": "5000xx",
        "npu_execution_tested": False,
        "driver_changed": False,
    }
    if not args.child:
        args.output.mkdir(parents=True, exist_ok=False)
    process = None
    try:
        if os.name != "nt":
            raise RuntimeError("The qualified toolchain requires Windows")
        root = args.toolchain.resolve()
        report["toolchain_sha256"] = checked_toolchain(root)
        source = args.source.read_bytes()
        if not 0 < len(source) <= 256 * 1024:
            raise ValueError("Kernel source must contain 1..262144 bytes")
        report["source_sha256"] = hashlib.sha256(source).hexdigest()
        if args.child:
            with os.add_dll_directory(str(root / "bin")):
                assembly = transform(root, source, args.output)
                (args.output / "kernel.s").write_bytes(assembly)
                object_bytes = transform(root, assembly, args.output, assemble=True)
                (args.output / "kernel.o").write_bytes(object_bytes)
                elf = link(root, object_bytes)
            text = read_kernel_text(elf)
            (args.output / "kernel.elf").write_bytes(elf)
            report.update(
                elf_sha256=hashlib.sha256(elf).hexdigest(),
                elf_bytes=len(elf),
                text_bytes=len(text),
                text_sha256=hashlib.sha256(text).hexdigest(),
            )
        else:
            # The child reads an immutable owned source snapshot, not a later edit.
            snapshot = args.output / "kernel.c"
            snapshot.write_bytes(source)
            command = [
                sys.executable,
                str(Path(__file__).resolve()),
                "--child",
                "--toolchain",
                str(root),
                "--source",
                str(snapshot.resolve()),
                "--output",
                str(args.output.resolve()),
            ]
            with (
                (args.output / "child.stdout.log").open("xb") as stdout,
                (args.output / "child.stderr.log").open("xb") as stderr,
            ):
                process = subprocess.Popen(
                    command, stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW
                )
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    raise RuntimeError("Owned compiler child exceeded 30 seconds") from None
            report["child_exit"] = process.returncode
            child_path = args.output / "child-report.json"
            result = json.loads(child_path.read_text()) if child_path.exists() else {}
            report["child_result"] = result
            if process.returncode or not result.get("completed") or not result.get("passed"):
                raise RuntimeError("Kernel build failed; see child evidence")
            if result["source_sha256"] != report["source_sha256"]:
                raise RuntimeError("Kernel source changed")
        report["passed"] = True
    except Exception as error:
        report.update(error=repr(error), traceback=traceback.format_exc())
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        report.update(completed=True, finished=datetime.now(UTC).isoformat())
        write(args.output / ("child-report.json" if args.child else "report.json"), report)
    print(json.dumps(report))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
