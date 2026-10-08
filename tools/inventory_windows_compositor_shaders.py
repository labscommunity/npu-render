"""Inventory embedded compositor DXBC instruction counts without copying system code.

Static module inventory is not a trace of shaders used by the active desktop.
No process is injected, driver registered or system binary modified.
"""

import argparse
import ctypes
import hashlib
import json
import os
import re
import struct
from collections import Counter
from datetime import UTC, datetime
from pathlib import Path


def containers(data):
    offset = 0
    while True:
        offset = data.find(b"DXBC", offset)
        if offset < 0:
            return
        start = offset
        offset += 4
        if start + 32 > len(data):
            continue
        version, size, count = struct.unpack_from("<III", data, start + 20)
        if version != 1 or not 1 <= count <= 64 or not 32 + 4 * count <= size <= 16 * 1024 * 1024:
            continue
        if start + size > len(data):
            continue
        chunks = []
        valid = True
        for i in range(count):
            relative = struct.unpack_from("<I", data, start + 32 + i * 4)[0]
            if relative < 32 + count * 4 or relative + 8 > size:
                valid = False
                break
            tag = data[start + relative : start + relative + 4]
            length = struct.unpack_from("<I", data, start + relative + 4)[0]
            if relative + 8 + length > size:
                valid = False
                break
            chunks.append(tag.decode("ascii", errors="replace"))
        if valid and ("SHDR" in chunks or "SHEX" in chunks):
            yield start, data[start : start + size], chunks
            offset = start + size


class Disassembler:
    def __init__(self, dll):
        self.library = ctypes.WinDLL(str(dll))
        self.call = self.library.D3DDisassemble
        self.call.argtypes = (
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_uint,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_void_p),
        )
        self.call.restype = ctypes.c_long

    def __call__(self, data):
        buffer = ctypes.create_string_buffer(data)
        blob = ctypes.c_void_p()
        status = self.call(buffer, len(data), 0x10, None, ctypes.byref(blob))
        if status < 0 or not blob:
            raise ValueError(f"D3DDisassemble rejected container: 0x{status & 0xFFFFFFFF:08x}")
        table = ctypes.cast(blob, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(table[2])
        pointer = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(table[3])
        size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(table[4])
        try:
            length = size(blob)
            if length > 16 * 1024 * 1024:
                raise ValueError("unexpected disassembly extent")
            return ctypes.string_at(pointer(blob), length).decode("utf-8", errors="strict")
        finally:
            release(blob)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt":
        raise ValueError("Windows system compositor modules are required")
    if args.output.exists():
        raise ValueError("refusing to overwrite inventory evidence")
    system = Path(os.environ["SystemRoot"]) / "System32"
    compiler = system / "d3dcompiler_47.dll"
    disassemble = Disassembler(compiler)
    sha = lambda data: hashlib.sha256(data).hexdigest()  # noqa: E731
    report = {
        "scope": "Static embedded DXBC inventory; not observed desktop execution",
        "captured_utc": datetime.now(UTC).isoformat(),
        "full_desktop_offload": False,
        "scanner_sha256": sha(Path(__file__).read_bytes()),
        "compiler_sha256": sha(compiler.read_bytes()),
        "modules": [],
    }
    for name in ("dwmcore.dll", "dcomp.dll", "udwm.dll"):
        data = (system / name).read_bytes()
        module = {
            "name": name,
            "bytes": len(data),
            "sha256": sha(data),
            "dxbc_markers": data.count(b"DXBC"),
            "shaders": [],
        }
        opcodes, profiles = Counter(), Counter()
        for offset, binary, chunks in containers(data):
            entry = {
                "offset": offset,
                "bytes": len(binary),
                "sha256": sha(binary),
                "chunks": chunks,
            }
            try:
                text = disassemble(binary)
                found_profiles, ops = [], Counter()
                for line in text.splitlines():
                    line = line.strip().rstrip("\x00")
                    if not line or line.startswith("//"):
                        continue
                    if re.fullmatch(
                        r"(?:ps|vs|gs|hs|ds|cs|lib)_\d_(?:\d|x)(?:_ps_\d_(?:\d|x))?", line
                    ):
                        found_profiles.append(line)
                        continue
                    mnemonic = line.split()[0]
                    if re.fullmatch(r"[a-z][a-z0-9_]*", mnemonic):
                        ops[mnemonic] += 1
                if not found_profiles:
                    raise ValueError("disassembly has no shader profile")
                entry.update(profiles=found_profiles, instruction_counts=dict(sorted(ops.items())))
                profiles.update(found_profiles)
                opcodes.update(ops)
            except (ValueError, UnicodeError) as exc:
                entry["error"] = str(exc)
            module["shaders"].append(entry)
        module["profiles"] = dict(sorted(profiles.items()))
        module["instruction_counts"] = dict(sorted(opcodes.items()))
        report["modules"].append(module)
        print(
            json.dumps(
                {
                    "module": name,
                    "shaders": len(module["shaders"]),
                    "profiles": module["profiles"],
                    "mnemonics": len(opcodes),
                }
            ),
            flush=True,
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
