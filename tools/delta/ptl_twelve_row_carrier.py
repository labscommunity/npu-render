"""Pinned code-only admission for twelve rows, sequential pairs on six engines."""
import hashlib
import struct
from ptl_elf import Elf, check

ORIGINAL_SHA = "e549e56964aadbad1adc9e77e208960dd79aa3b0baedc0d4da0be2253a484fd0"
WIDTH = 132096
SPAN = WIDTH * 2


def inspect(blob):
    e = Elf(blob)
    check(not any("dpu" in s["name"].lower() for s in e.sections), "no DPU tasks")
    expected = {f"task.shave.{kind}.{tile}.{slot}"
                for kind in ["range", "invocation"] for tile in range(3) for slot in range(2)}
    check({s["name"] for s in e.sections if s["name"].startswith("task.shave.")}
          == expected, "six explicit task arrays")
    params, code, data = [e.by_name[n] for n in
                          ["shave.params", "shave.text", "shave.data"]]
    check((params["size"], code["size"], data["size"]) == (3184, 18384, 128)
          and all(x["type"] == 1 for x in [params, code, data])
          and not e.relocations(code), "bounded file-backed code and metadata")
    pr = e.relocations(params)
    check(set(pr) == {n * 256 + i * 40 + field for n in range(12)
                      for i in range(2) for field in [16, 20]},
          "exact parameter relocations")
    calls = []
    for tile, slot in [(tile, slot) for tile in range(3) for slot in range(2)]:
        ranges = e.by_name[f"task.shave.range.{tile}.{slot}"]
        call = e.by_name[f"task.shave.invocation.{tile}.{slot}"]
        check(ranges["size"] == 80 and call["size"] == 192
              and ranges["type"] == call["type"] == 1, "two tasks on one engine")
        check(e.relocations(ranges) ==
              {16 + row * 40: dict(kind=0, section="shave.text", addend=0)
               for row in range(2)}, "both tasks share bounded code")
        expected_refs = {}
        for row in range(2):
            expected_refs[row * 96 + 8] = dict(
                kind=0, section="shave.params", addend=(row * 6 + slot * 3 + tile) * 256)
            expected_refs[row * 96 + 16] = dict(
                kind=0, section="shave.data", addend=0)
        check(e.relocations(call) == expected_refs, "exact invocation windows")
        tile_base = 0x40000000 | (1 << (21 + tile))
        memory = e.by_name[f"buffer.CMX_NN.{tile}"]
        memory_size = 1419264
        check(memory["type"] == 0x8aaaaaad and memory["offset"] == 0
              and memory["size"] == memory_size, "declared local allocation")
        for row in range(2):
            base = (row * 6 + slot * 3 + tile) * 256
            check(struct.unpack_from("<B7xQQIIII", ranges["data"], row * 40)
                  == (0, 0x1d000000, 0, 18256, 0, 0, 0), "owned code extent")
            check(struct.unpack_from("<I", call["data"], row * 96 + 84)[0]
                  == tile, "correct tile")
            addresses = []
            for i in range(2):
                at = base + i * 40
                fields = struct.unpack_from("<QIIIIIQI", params["data"], at)
                address, static, rank, dp, sp, dtype, order, location = fields
                check((static, rank, dp, sp, dtype, order, location)
                      == (1, 4, 0, 0, 2, 0x4321, 2), "static dense f16 tensor")
                for field, offset, fmt, wanted in [
                    (16, base + 144 + i * 48, "<4i", (WIDTH, 1, 1, 1)),
                    (20, base + 160 + i * 48, "<4q",
                     (16, WIDTH * 16, WIDTH * 16, WIDTH * 16)),
                ]:
                    check(pr[at + field] == dict(
                        kind=4, section="shave.params", addend=offset),
                        "exact shape reference")
                    check(struct.unpack_from(fmt, params["data"], offset)
                          == wanted, "complete shape and strides")
                check(address == tile_base + 0x18000 + slot * SPAN + i * 2 * SPAN,
                      "compiler-owned reusable input/output address")
                addresses.append(address)
            check(addresses[1] + SPAN <= tile_base + memory_size,
                  "output within declared allocation")
            calls.append(dict(tile=tile, engine=slot, row=row, parameter_offset=base,
                              input_address=addresses[0],
                              output_address=addresses[1], bytes_per_plane=SPAN))
    return dict(accepted=True, sha256=hashlib.sha256(blob).hexdigest(),
                code_offset=code["offset"], code_bytes=18256, invocations=calls,
                compiler_schedule_preserved=True, distinct_engines=6,
                sequential_pairs_reuse_addresses=True,
                hardware_custom_execution_qualified=False)


def patch(blob, text):
    check(hashlib.sha256(blob).hexdigest() == ORIGINAL_SHA,
          "pinned unmodified compiler output")
    proof = inspect(blob)
    check(0 < len(text) <= proof["code_bytes"] and len(text) % 16 == 0,
          "bounded replacement code")
    at, size = proof["code_offset"], proof["code_bytes"]
    modified = blob[:at] + text + bytes(size - len(text)) + blob[at + size:]
    check(len(modified) == len(blob) and modified[:at] == blob[:at]
          and modified[at + size:] == blob[at + size:],
          "all non-code bytes preserved")
    inspect(modified)
    return modified, proof
