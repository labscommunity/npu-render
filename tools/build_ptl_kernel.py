"""Build an experimental SHAVE kernel with a locally supplied, pinned toolchain.

The toolchain is not downloaded or redistributed. This tool never loads an NPU
graph, installs a driver, or changes the desktop. Qualification is a separate step.
"""

import argparse
import ctypes as C
import hashlib
import json
import os
import re
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


OPTIMIZATION = b"-O2"
CC1_EXTRA = []
ASM_EXTRA = []


def transform(root, source, output, assemble=False, strict=True):
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
        [b"moviAsm.dll", b"--cv", b"5000xx", b"--noSPrefixing", *ASM_EXTRA]
        if assemble
        else [
            b"moviCompile.dll",
            b"-cc1",
            b"-triple",
            b"shave",
            b"-target-cpu",
            b"5000xx",
            b"-S",
            OPTIMIZATION,
            b"-fno-jump-tables",
            b"-ffp-contract=off",
            b"-DNR_SHAVE=1",
            *CC1_EXTRA,
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
        if not strict:
            return code, messages, (output_bytes(result, length.value) if not code else b"")
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


def inline_local_includes(path):
    """Expand `#include "name.h"` from the source directory; the compiler has no filesystem."""
    text = path.read_bytes()
    seen = set()

    def expand(data, directory):
        def replace(match):
            name = match.group(1).decode()
            target = (directory / name).resolve()
            if not target.is_file() or target in seen:
                raise ValueError(f"Unresolvable or repeated local include: {name}")
            seen.add(target)
            return expand(target.read_bytes(), target.parent)

        return re.sub(rb'^[ \t]*#include[ \t]+"([^"]+)"[ \t]*$', replace, data, flags=re.M)

    return expand(text, path.resolve().parent)


CONFLICT = re.compile(
    rb"ERROR: Instruction .*? is conflicting:.*?at line (\d+)\.?\s*$", re.S | re.M
)


BRANCH_DELAY_SLOTS = 6


def nop_insertion_point(lines, index):
    """Line index at which a NOP delays the bundle starting at `index`.

    The bundle's own continuation lines are skipped backwards. A branch is followed by
    BRANCH_DELAY_SLOTS bundles that execute whatever it decides; a NOP inside that window
    would move one of them out of it, so the NOP goes before the branch bundle instead
    (and before a branch whose window holds that branch, recursively).
    """
    at = index
    while at > 0 and lines[at].lstrip().startswith(b"||"):
        at -= 1

    def bundle_head(n):
        while n > 0 and lines[n].lstrip().startswith(b"||"):
            n -= 1
        return n

    def is_code(text):
        return bool(re.match(rb"[A-Z]{3}[0-9]?\.", text)) or text.startswith(b"NOP")

    def bundle_text(head):
        parts = [lines[head].split(b"//")[0].strip()]
        n = head + 1
        while n < len(lines) and lines[n].lstrip().startswith(b"||"):
            parts.append(lines[n].split(b"//")[0].strip()[2:].strip())
            n += 1
        return parts

    while True:
        # Walk back over the preceding bundles; stop after the delay-slot window.
        slots = 0
        n = at - 1
        moved = False
        while n >= 0 and slots < BRANCH_DELAY_SLOTS:
            text = lines[n].split(b"//")[0].strip()
            if not text or text.startswith(b"."):
                n -= 1
                continue
            if text.endswith(b":"):
                break  # a label: the window cannot cross a basic block boundary
            head = bundle_head(n)
            parts = bundle_text(head)
            if not any(is_code(part) for part in parts):
                n = head - 1
                continue
            if any(part.startswith(b"BRU.") for part in parts):
                at = head
                moved = True
                break
            for part in parts:
                m = re.match(rb"NOP\s*(\d*)$", part)
                slots += int(m.group(1) or 1) if m else 0
            if not any(part.startswith(b"NOP") for part in parts):
                slots += 1
            n = head - 1
        if not moved:
            # A predicate bundle (PEU) without its branch conditions the bundle that follows it:
            # a NOP between them would take the predicate, so the NOP goes before the PEU bundle.
            n = at - 1
            while n >= 0:
                text = lines[n].split(b"//")[0].strip()
                if not text or text.startswith(b"."):
                    n -= 1
                    continue
                if text.endswith(b":"):
                    break
                head = bundle_head(n)
                if any(part.startswith(b"PEU.") for part in bundle_text(head)):
                    at = head
                    moved = True
                break
        if not moved:
            return at


def pad_read_after_write(assembly, units, distance, functions=None, waw=False):
    """Delay every consumer that reads a register `distance` or fewer bundles after the
    producing instruction of one of `units` (for example VAU, CMU).

    The compiler's latency model for 5000xx lets a consumer read a vector result in
    the very next bundle; the assembler flags only write-port conflicts, so a stale
    read passes silently. A NOP before the consumer never changes the data flow.
    Returns the padded assembly and the padding sites.
    """
    lines = assembly.split(b"\n")
    bundle_starts = []  # line index of every bundle (NOP lines included)
    in_scope = functions is None
    for n, line in enumerate(lines):
        text = line.split(b"//")[0].strip()
        if text.startswith(b".code "):
            # `.code .text.<function>` opens a function's section; `functions` limits the pass.
            name = text.split()[-1].rsplit(b".", 1)[-1]
            in_scope = functions is None or name in functions
            continue
        if not text or text.startswith(b"||") or text.startswith(b".") or text.endswith(b":"):
            continue
        if in_scope and (re.match(rb"[A-Z]{3}[0-9]?\.", text) or text.startswith(b"NOP")):
            bundle_starts.append(n)

    def ops(index):
        result = [lines[index].split(b"//")[0].strip()]
        n = index + 1
        while n < len(lines) and lines[n].lstrip().startswith(b"||"):
            result.append(lines[n].split(b"//")[0].strip()[2:].strip())
            n += 1
        return result

    def parse(op):
        m = re.match(rb"([A-Z0-9]+)\.(\S+)\s*(.*)", op)
        return (m.group(1), m.group(2), m.group(3).split()) if m else None

    def nop_count(op):
        m = re.match(rb"NOP\s*(\d*)", op)
        return int(m.group(1) or 1) if m else 0

    sites = []
    inserts = {}  # line index -> NOP count to insert before it
    for b, start in enumerate(bundle_starts):
        for op in ops(start):
            parsed = parse(op)
            if not parsed or not parsed[2]:
                continue
            # LSUMEM names memory loads only (LD/LDO/LDI with a base register), not immediates.
            memory_load = (
                parsed[0] in (b"LSU0", b"LSU1")
                and parsed[1].startswith(b"LD")
                and not parsed[1].startswith((b"LDIL", b"LDIH"))
                and len(parsed[2]) >= 2
            )
            if not (parsed[0] in units or (b"LSUMEM" in units and memory_load)):
                continue
            dest = parsed[2][0].split(b".")[0]
            if not re.match(rb"[vi]\d+$", dest):
                continue
            gap = 0
            for k in range(1, distance + 1):
                if b + k >= len(bundle_starts):
                    break
                consumer = bundle_starts[b + k]
                consumer_ops = ops(consumer)
                if all(o.startswith(b"NOP") for o in consumer_ops):
                    gap += sum(nop_count(o) for o in consumer_ops)
                    if gap >= distance:
                        break
                    continue
                reads = False
                for o in consumer_ops:
                    q = parse(o)
                    if not q:
                        continue
                    store = q[0] in (b"LSU0", b"LSU1") and q[1].startswith(b"ST")
                    sources = [t.split(b".")[0] for t in (q[2] if store else q[2][1:])]
                    reads = reads or dest in sources
                    # waw: a second write to the same register (another lane, say) counts too.
                    if waw and not store and q[2] and q[2][0].split(b".")[0] == dest:
                        reads = True
                if reads and gap + (k - 1) < distance:
                    needed = distance - gap - (k - 1)
                    at = nop_insertion_point(lines, consumer)
                    inserts[at] = max(inserts.get(at, 0), needed)
                    sites.append(
                        dict(
                            producer=op.decode(errors="replace"),
                            consumer=b" || ".join(consumer_ops).decode(errors="replace"),
                            bundles_apart=k,
                            nops=needed,
                        )
                    )
                break  # the first non-NOP bundle after the producer decides
    for at in sorted(inserts, reverse=True):
        for _ in range(inserts[at]):
            lines.insert(at, b"        NOP")
    return b"\n".join(lines), sites


def pad_stack_reuse(assembly, distance, functions=None):
    """Delay every load from a stack slot (i19-relative) issued fewer than `distance`
    bundles after a store that overlaps it.

    Spilled registers of a long shader are written and read back within two or three
    bundles; the device returns the slot's previous contents at that distance (the
    compiler's memory model assumes forwarding). Returns the padded assembly and the sites.
    """
    lines = assembly.split(b"\n")
    bundle_starts = []
    in_scope = functions is None
    for n, line in enumerate(lines):
        text = line.split(b"//")[0].strip()
        if text.startswith(b".code "):
            name = text.split()[-1].rsplit(b".", 1)[-1]
            in_scope = functions is None or name in functions
            continue
        if not text or text.startswith(b"||") or text.startswith(b"."):
            continue
        if text.endswith(b":"):
            if in_scope:
                bundle_starts.append(-n)  # a label: stores before it are not tracked across
            continue
        if in_scope and (re.match(rb"[A-Z]{3}[0-9]?\.", text) or text.startswith(b"NOP")):
            bundle_starts.append(n)

    def ops(index):
        result = [lines[index].split(b"//")[0].strip()]
        n = index + 1
        while n < len(lines) and lines[n].lstrip().startswith(b"||"):
            result.append(lines[n].split(b"//")[0].strip()[2:].strip())
            n += 1
        return result

    access = re.compile(rb"LSU[01]\.(ST|LD)O?\.(\d+)\s+(\S+)\s+i19(?:\s+(\d+))?$")
    sites = []
    inserts = {}
    stores = {}  # (offset, width) -> slot position of the last store
    position = 0
    for start in bundle_starts:
        if start < 0:
            stores.clear()
            continue
        bundle = ops(start)
        width_slots = 1
        for op in bundle:
            m = re.match(rb"NOP\s*(\d*)$", op)
            if m:
                width_slots = int(m.group(1) or 1)
        loads, new_stores = [], []
        for op in bundle:
            m = access.match(op)
            if not m:
                continue
            kind, width, _reg, offset = m.groups()
            offset, width = int(offset or 0), int(width) // 8
            (new_stores if kind == b"ST" else loads).append((offset, width))
        needed = 0
        for offset, width in loads:
            for (soff, swidth), spos in stores.items():
                if soff < offset + width and offset < soff + swidth:
                    gap = position - spos
                    if gap < distance:
                        needed = max(needed, distance - gap)
                        sites.append(
                            dict(
                                load=b" || ".join(bundle).decode(errors="replace"),
                                slot=offset,
                                bundles_apart=gap,
                                nops=distance - gap,
                            )
                        )
        if needed:
            at = nop_insertion_point(lines, start)
            inserts[at] = max(inserts.get(at, 0), needed)
            position += needed
        for offset, width in new_stores:
            stores[(offset, width)] = position
        position += width_slots
    for at in sorted(inserts, reverse=True):
        for _ in range(inserts[at]):
            lines.insert(at, b"        NOP")
    return b"\n".join(lines), sites


def fix_hazards(root, assembly, output, limit=400):
    """Delay bundles the assembler rejects for a write-port conflict by one NOP each.

    The compiler's VAU latency model and the assembler's disagree for 5000xx;
    delaying a consumer never changes the data flow. A NOP goes before the
    conflicting bundle, or before a preceding branch so delay slots stay intact.
    """
    inserted = []
    for _ in range(limit):
        code, messages, _object = transform(root, assembly, output, assemble=True, strict=False)
        if not code:
            break
        lines = assembly.split(b"\n")
        targets = set()
        for block in messages.split(b"moviAsm: ERROR"):
            numbers = re.findall(rb"at line (\d+)", block)
            if b"conflicting" in block and numbers:
                targets.add(int(numbers[-1]))
        if not targets:
            raise RuntimeError(
                "Assembler failed without a conflict to fix; see retained diagnostics"
            )
        for line in sorted(targets, reverse=True):
            at = nop_insertion_point(lines, line - 1)
            lines.insert(at, b"        NOP")
            inserted.append(
                dict(
                    reported_line=line,
                    inserted_before=at + 1,
                    context=[
                        x.decode(errors="replace").strip() for x in lines[max(0, at - 3) : at + 5]
                    ],
                )
            )
        assembly = b"\n".join(lines)
    else:
        raise RuntimeError("Hazard fixing did not converge")
    return assembly, inserted


def write(path, report):
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "--pad-raw-units",
        default="",
        help="comma-separated units (VAU,CMU,SAU) whose results get NOPs before an early consumer",
    )
    parser.add_argument(
        "--pad-raw-functions",
        default="",
        help="comma-separated function names the padding is limited to (default: all)",
    )
    parser.add_argument(
        "--pad-stack-distance",
        type=int,
        default=0,
        help="minimum bundles between a store to a stack slot and a load of it (0: off)",
    )
    parser.add_argument(
        "--pad-raw-waw",
        action="store_true",
        help="also delay a second write to a register the producer wrote",
    )
    parser.add_argument(
        "--pad-raw-distance",
        type=int,
        default=1,
        help="minimum bundles between such a producer and a consumer of its result",
    )
    parser.add_argument(
        "--optimize", choices=["O1", "O2", "O3", "Os", "Oz"], default="O2", help="compiler level"
    )
    parser.add_argument(
        "--cc1-extra", action="append", default=[], help="extra cc1 flag (repeatable, recorded)"
    )
    parser.add_argument(
        "--asm-extra",
        action="append",
        default=[],
        help="extra assembler flag (repeatable, recorded)",
    )
    parser.add_argument(
        "--fix-hazards",
        action="store_true",
        help="insert NOPs where the assembler reports a write conflict the compiler missed",
    )
    args = parser.parse_args()
    global OPTIMIZATION, CC1_EXTRA, ASM_EXTRA
    OPTIMIZATION = ("-" + args.optimize).encode()
    CC1_EXTRA = [flag.encode() for flag in args.cc1_extra]
    ASM_EXTRA = [flag.encode() for flag in args.asm_extra]
    report = {
        "started": datetime.now(UTC).isoformat(),
        "completed": False,
        "passed": False,
        "target": "5000xx",
        "npu_execution_tested": False,
        "driver_changed": False,
        "optimization": args.optimize,
        "cc1_extra": args.cc1_extra,
        "asm_extra": args.asm_extra,
        "fix_hazards": args.fix_hazards,
        "pad_raw_units": args.pad_raw_units,
        "pad_raw_distance": args.pad_raw_distance,
        "pad_raw_functions": args.pad_raw_functions,
        "pad_stack_distance": args.pad_stack_distance,
    }
    if not args.child:
        args.output.mkdir(parents=True, exist_ok=False)
    process = None
    try:
        if os.name != "nt":
            raise RuntimeError("The qualified toolchain requires Windows")
        root = args.toolchain.resolve()
        report["toolchain_sha256"] = checked_toolchain(root)
        source = inline_local_includes(args.source)
        if not 0 < len(source) <= 1024 * 1024:
            raise ValueError("Kernel source must contain 1..1048576 bytes")
        report["source_sha256"] = hashlib.sha256(source).hexdigest()
        if args.child:
            with os.add_dll_directory(str(root / "bin")):
                assembly = transform(root, source, args.output)
                if args.pad_raw_units:
                    units = [u.strip().encode() for u in args.pad_raw_units.split(",") if u.strip()]
                    functions = [
                        f.strip().encode() for f in args.pad_raw_functions.split(",") if f.strip()
                    ] or None
                    assembly, raw_sites = pad_read_after_write(
                        assembly, units, args.pad_raw_distance, functions, args.pad_raw_waw
                    )
                    report["raw_nops"] = sum(site["nops"] for site in raw_sites)
                    report["raw_sites"] = raw_sites
                if args.pad_stack_distance:
                    functions = [
                        f.strip().encode() for f in args.pad_raw_functions.split(",") if f.strip()
                    ] or None
                    assembly, stack_sites = pad_stack_reuse(
                        assembly, args.pad_stack_distance, functions
                    )
                    report["stack_nops"] = sum(site["nops"] for site in stack_sites)
                    report["stack_sites"] = stack_sites
                if args.fix_hazards:
                    assembly, sites = fix_hazards(root, assembly, args.output)
                    report["hazard_nops"] = len(sites)
                    report["hazard_sites"] = sites
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
                "--optimize",
                args.optimize,
                # Joined form: the values may start with a dash.
                *[f"--cc1-extra={extra}" for extra in args.cc1_extra],
                *(
                    [
                        f"--pad-stack-distance={args.pad_stack_distance}",
                        f"--pad-raw-functions={args.pad_raw_functions}",
                    ]
                    if args.pad_stack_distance
                    else []
                ),
                *(
                    [
                        f"--pad-raw-units={args.pad_raw_units}",
                        f"--pad-raw-distance={args.pad_raw_distance}",
                        f"--pad-raw-functions={args.pad_raw_functions}",
                        *(["--pad-raw-waw"] if args.pad_raw_waw else []),
                    ]
                    if args.pad_raw_units
                    else []
                ),
                *[f"--asm-extra={extra}" for extra in args.asm_extra],
                *(["--fix-hazards"] if args.fix_hazards else []),
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
