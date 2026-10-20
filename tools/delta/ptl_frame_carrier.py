"""Pinned code-only admission for the ScatterElementsUpdate twelve-row carrier.

Twelve 258 KiB rows execute as two sequential tasks on each of six SHAVE
engines, like the Softmax carrier, with a 24,496-byte code window. Each
invocation carries four tensor descriptors: data, indices, updates and output.
Only the data (0) and output (3) planes are used by the frame kernel.
"""

import hashlib
import struct

from ptl_elf import Elf, check

ORIGINAL_SHA = "06d57d6fe2d3468abdfbf4c8c697dc9ce09ffcb549353c17cc44dad2b6af1577"
WIDTH = 132096
SPAN = WIDTH * 2
CODE_BYTES = 24496


def inspect(blob):
    e = Elf(blob)
    check(not any("dpu" in s["name"].lower() for s in e.sections), "no DPU tasks")
    expected = {
        f"task.shave.{kind}.{tile}.{slot}"
        for kind in ["range", "invocation"]
        for tile in range(3)
        for slot in range(2)
    }
    names = {s["name"] for s in e.sections if s["name"].startswith("task.shave.")}
    check(names == expected, "six explicit task arrays")
    params, code, data = [e.by_name[n] for n in ["shave.params", "shave.text", "shave.data"]]
    check(
        params["size"] == 4728
        and code["size"] == 24624
        and data["size"] == 128
        and all(x["type"] == 1 for x in [params, code, data])
        and not e.relocations(code),
        "bounded file-backed code and metadata",
    )
    pr = e.relocations(params)
    calls = []
    for tile in range(3):
        for slot in range(2):
            ranges = e.by_name[f"task.shave.range.{tile}.{slot}"]
            call = e.by_name[f"task.shave.invocation.{tile}.{slot}"]
            check(
                ranges["size"] == 80
                and call["size"] == 192
                and ranges["type"] == call["type"] == 1,
                "two tasks on one engine",
            )
            check(
                e.relocations(ranges)
                == {
                    16 + row * 40: dict(kind=0, section="shave.text", addend=0) for row in range(2)
                },
                "both tasks share bounded code",
            )
            refs = e.relocations(call)
            for row in range(2):
                check(
                    struct.unpack_from("<B7xQQIIII", ranges["data"], row * 40)
                    == (0, 0x1D000000, 0, CODE_BYTES, 0, 0, 0),
                    "owned code extent",
                )
                check(
                    struct.unpack_from("<I", call["data"], row * 96 + 84)[0] == tile, "correct tile"
                )
                base = refs[row * 96 + 8]
                check(
                    base["kind"] == 0 and base["section"] == "shave.params", "parameter reference"
                )
                check(refs[row * 96 + 16]["section"] == "shave.data", "data reference")
                descriptors = []
                for n in range(4):
                    at = base["addend"] + n * 40
                    address, static, rank, dp, sp, dtype, order, location = struct.unpack_from(
                        "<QIIIIIQI", params["data"], at
                    )
                    dims = struct.unpack_from("<4i", params["data"], pr[at + 16]["addend"])
                    check(
                        static == 1
                        and rank == 4
                        and dp == sp == 0
                        and order == 0x4321
                        and location == 2,
                        "descriptor",
                    )
                    descriptors.append(dict(address=address, dtype=dtype, dims=dims))
                check(
                    descriptors[0]["dtype"] == 2
                    and descriptors[3]["dtype"] == 2
                    and descriptors[0]["dims"] == (WIDTH, 1, 1, 1)
                    and descriptors[3]["dims"] == (WIDTH, 1, 1, 1),
                    "data and output planes",
                )
                check(
                    descriptors[1]["dims"] == (16, 1, 1, 1)
                    and descriptors[2]["dims"] == (16, 1, 1, 1),
                    "indices and updates planes",
                )
                calls.append(
                    dict(
                        tile=tile,
                        engine=slot,
                        row=row,
                        parameter_offset=base["addend"],
                        input_address=descriptors[0]["address"],
                        output_address=descriptors[3]["address"],
                        bytes_per_plane=SPAN,
                    )
                )
    check(
        len({c["input_address"] for c in calls}) == 6 and len(calls) == 12,
        "twelve rows on six engines",
    )
    return dict(
        accepted=True,
        sha256=hashlib.sha256(blob).hexdigest(),
        code_offset=code["offset"],
        code_bytes=CODE_BYTES,
        invocations=calls,
        compiler_schedule_preserved=True,
        distinct_engines=6,
        hardware_custom_execution_qualified=False,
    )


def patch(blob, text):
    check(hashlib.sha256(blob).hexdigest() == ORIGINAL_SHA, "pinned unmodified compiler output")
    proof = inspect(blob)
    check(0 < len(text) <= proof["code_bytes"] and len(text) % 16 == 0, "bounded replacement code")
    at, size = proof["code_offset"], proof["code_bytes"]
    modified = blob[:at] + text + bytes(size - len(text)) + blob[at + size :]
    check(
        len(modified) == len(blob)
        and modified[:at] == blob[:at]
        and modified[at + size :] == blob[at + size :],
        "all non-code bytes preserved",
    )
    inspect(modified)
    return modified, proof
