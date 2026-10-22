"""Read-only bounded NPU50XX metadata inspection; never admits replacement code."""

import argparse
import hashlib
import json
import struct
from pathlib import Path


def check(ok, message):
    if not ok:
        raise ValueError(message)


class Elf:
    def __init__(self, data):
        check(64 <= len(data) <= 32 * 1024 * 1024, "file bounds")
        self.data = data
        h = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
        check(
            data[:6] == b"\x7fELF\x02\x01"
            and h[1:6] == (1, 0, 0, 0, 0)
            and h[7:12] == (0, 64, 0, 0, 64),
            "PTL ELF header",
        )
        check(
            0 < h[12] < 8192 and h[13] < h[12] and h[6] + h[12] * 64 <= len(data), "section bounds"
        )
        raw = [struct.unpack_from("<IIQQQQIIQQ", data, h[6] + i * 64) for i in range(h[12])]
        strings = self.region(raw[h[13]][4], raw[h[13]][5])
        self.sections = []
        self.by_name = {}
        for i, s in enumerate(raw):
            check(s[0] < len(strings), "name offset")
            end = strings.find(b"\0", s[0])
            check(end >= 0, "terminated name")
            name = strings[s[0] : end].decode("ascii")
            check(name not in self.by_name, "unique name")
            entry = dict(
                index=i,
                name=name,
                type=s[1],
                flags=s[2],
                offset=s[4],
                size=s[5],
                link=s[6],
                info=s[7],
                entry=s[9],
                data=None if s[1] in (8, 0x8AAAAAAC, 0x8AAAAAAD) else self.region(s[4], s[5]),
            )
            self.sections.append(entry)
            self.by_name[name] = entry

    def region(self, at, size):
        check(0 <= at <= len(self.data) and 0 <= size <= len(self.data) - at, "file region")
        return self.data[at : at + size]

    def relocations(self, target):
        result = {}
        for s in self.sections:
            if s["type"] != 4 or s["info"] != target["index"]:
                continue
            check(
                s["entry"] == 24 and s["size"] % 24 == 0 and s["link"] < len(self.sections),
                "relocations",
            )
            sym = self.sections[s["link"]]
            check(sym["type"] == 2 and sym["entry"] == 24 and sym["size"] % 24 == 0, "symbols")
            for at in range(0, s["size"], 24):
                offset, info, addend = struct.unpack_from("<QQq", s["data"], at)
                symbol = info >> 32
                kind = info & 0xFFFFFFFF
                check(symbol < sym["size"] // 24 and offset not in result, "symbol identity")
                st = struct.unpack_from("<IBBHQQ", sym["data"], symbol * 24)
                check(
                    st[3] < len(self.sections) and 0 <= offset < target["size"], "relocation extent"
                )
                result[offset] = dict(
                    kind=kind, section=self.sections[st[3]]["name"], addend=addend + st[4]
                )
        return result

    def describe(self, tensor_count=3):
        check(1 <= tensor_count <= 16, "explicit tensor count")
        result = dict(
            blob_sha256=hashlib.sha256(self.data).hexdigest(),
            blob_bytes=len(self.data),
            replacement_admitted=False,
            ranges=[],
            invocations=[],
        )
        for s in self.sections:
            if s["name"].startswith("task.shave.range."):
                check(s["size"] == 40, "range size")
                kind, entry, base, size, deprecated, count, pad = struct.unpack(
                    "<B7xQQIIII", s["data"]
                )
                result["ranges"].append(
                    dict(
                        name=s["name"],
                        kind=kind,
                        entry=entry,
                        base=base,
                        code_bytes=size,
                        relocations=self.relocations(s),
                    )
                )
            elif s["name"].startswith("task.shave.invocation."):
                check(s["size"] == 96, "invocation size")
                rels = self.relocations(s)
                record = dict(
                    name=s["name"],
                    tile=struct.unpack_from("<I", s["data"], 84)[0],
                    range_address=struct.unpack_from("<Q", s["data"])[0],
                    relocations=rels,
                    tensors=[],
                )
                ref = rels.get(8)
                check(ref and ref["kind"] == 0, "parameter reference")
                params = self.by_name[ref["section"]]
                check(params["data"] is not None, "parameters file backed")
                base = ref["addend"]
                pr = self.relocations(params)
                for n in range(tensor_count):
                    at = base + n * 40
                    check(0 <= at and at + 8 <= params["size"], "descriptor head")
                    check(at + 40 <= params["size"], "descriptor body")
                    d = struct.unpack_from("<QIIIIIQI", params["data"], at)
                    address, static, rank, dp, sp, dtype, order, location = d
                    check(
                        static == 1 and 0 < rank <= 8 and dp == sp == 0 and dtype < 22,
                        "static descriptor shape",
                    )
                    dims = []
                    for field, width, fmt in [
                        (16, rank * 4, "<" + str(rank) + "i"),
                        (20, rank * 8, "<" + str(rank) + "q"),
                    ]:
                        ref = pr.get(at + field)
                        check(
                            ref and ref["kind"] == 4 and ref["section"] == params["name"],
                            "shape relocation",
                        )
                        off = ref["addend"]
                        check(0 <= off and off + width <= params["size"], "shape extent")
                        dims.append(list(struct.unpack_from(fmt, params["data"], off)))
                    record["tensors"].append(
                        dict(
                            descriptor_offset=at,
                            address=address,
                            address_relocation=pr.get(at),
                            rank=rank,
                            dims=dims[0],
                            strides_bits=dims[1],
                            dtype=dtype,
                            order=order,
                            location=location,
                        )
                    )
                record["tensor_count_explicit"] = tensor_count
                result["invocations"].append(record)
        return result


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("blob", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--tensor-count", type=int, default=3)
    args = p.parse_args()
    report = Elf(args.blob.read_bytes()).describe(args.tensor_count)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            dict(
                invocations=len(report["invocations"]),
                ranges=len(report["ranges"]),
                replacement_admitted=False,
            )
        )
    )
