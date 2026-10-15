"""Synthetic metadata only: these tests neither embed nor execute vendor code."""

import struct

import pytest

from npu_render.ptl_carrier import inspect_identity_carrier, replace_identity_code


def carrier():
    sections = []

    def add(name, data=b"", kind=1, link=0, info=0, entry=0):
        sections.append(dict(name=name, data=data, kind=kind, link=link, info=info, entry=entry))
        return len(sections) - 1

    add("", kind=0)
    add(".shstrtab", kind=3)
    code = add("shave.text", bytes(range(32)))
    params = add("shave.params", bytes(1992))
    data = add("shave.data", bytes(16))
    symbols = add(
        ".symtab",
        b"".join(
            struct.pack("<IBBHQQ", 0, 0, 0, section, 0, 0) for section in (code, params, data)
        ),
        kind=2,
        entry=24,
    )
    parameter_data = bytearray(1992)
    relocations = []
    for tile in range(3):
        for chunk in range(2):
            base = (tile * 2 + chunk) * 320
            address = 0x40218000 + tile * 0x200000 + chunk * 16
            addresses = (address, address + 128 - chunk * 16, address + 64)
            for tensor, pointer in enumerate(addresses):
                at = base + tensor * 40
                count = 1 if tensor == 1 else 8
                struct.pack_into(
                    "<10I", parameter_data, at, pointer, 0, 1, 4, 0, 0, 2, 0x4321, 0, 2
                )
                dims = base + 120 + tensor * 48
                strides = dims + 16
                struct.pack_into("<4i", parameter_data, dims, count, 1, 1, 1)
                struct.pack_into(
                    "<4q", parameter_data, strides, 16, count * 16, count * 16, count * 16
                )
                for field, target in ((16, dims), (20, strides)):
                    relocations.append(struct.pack("<QQq", at + field, (1 << 32) | 4, target))
            section = add(
                f"task.shave.range.{tile}.{chunk}",
                struct.pack("<B7xQQIIII", 0, 0x1D000000, 0, 32, 0, 0, 0),
            )
            add(
                f"rela.range.{tile}.{chunk}", struct.pack("<QQq", 16, 0, 0), 4, symbols, section, 24
            )
            invocation = bytearray(96)
            struct.pack_into("<I", invocation, 84, tile)
            section = add(f"task.shave.invocation.{tile}.{chunk}", bytes(invocation))
            add(
                f"rela.invocation.{tile}.{chunk}",
                struct.pack("<QQq", 8, 1 << 32, base) + struct.pack("<QQq", 16, 2 << 32, 0),
                4,
                symbols,
                section,
                24,
            )
    sections[params]["data"] = bytes(parameter_data)
    add("rela.params", b"".join(relocations), 4, symbols, params, 24)
    add("cmx.workspace", b"", 0x8AAAAAAD)
    names = bytearray()
    for section in sections:
        section["name_offset"] = len(names)
        names += section["name"].encode() + b"\0"
    sections[1]["data"] = bytes(names)
    blob = bytearray(64 + 64 * len(sections))
    locations = {}
    for i, section in enumerate(sections):
        while len(blob) % 8:
            blob.append(0)
        offset, size = len(blob), len(section["data"])
        locations[section["name"]] = offset
        blob += section["data"]
        if section["kind"] == 0x8AAAAAAD:
            offset, size = 64 * 1024 * 1024, 65536
        struct.pack_into(
            "<IIQQQQIIQQ",
            blob,
            64 + i * 64,
            section["name_offset"],
            section["kind"],
            0,
            0,
            offset,
            size,
            section["link"],
            section["info"],
            8,
            section["entry"],
        )
    struct.pack_into(
        "<16sHHIQQQIHHHHHH",
        blob,
        0,
        b"\x7fELF\x02\x01" + bytes(10),
        1,
        0,
        0,
        0,
        0,
        64,
        0,
        64,
        0,
        0,
        64,
        len(sections),
        1,
    )
    return bytes(blob), locations


def test_metadata_contract_and_patch_preserve_all_noncode_bytes():
    blob, locations = carrier()
    result = inspect_identity_carrier(blob)
    assert len(result["invocations"]) == 6
    assert result["replica_tiles"] == 3
    assert result["partition_elements_per_tile"] == 16
    assert not result["custom_kernel_execution_qualified"]
    assert result["code_offset"] == locations["shave.text"]
    patched, inspected = replace_identity_code(blob, b"\x5a" * 16)
    start, size = inspected["code_offset"], inspected["code_bytes"]
    assert patched[:start] == blob[:start]
    assert patched[start : start + size] == b"\x5a" * 16 + bytes(16)
    assert patched[start + size :] == blob[start + size :]


@pytest.mark.parametrize("size", [0, 1, 63, 200])
def test_truncated_metadata_rejected(size):
    blob, _ = carrier()
    with pytest.raises(ValueError):
        inspect_identity_carrier(blob[:size])


@pytest.mark.parametrize(
    ("section", "offset", "fmt", "value"),
    [
        ("task.shave.range.0.0", 24, "<I", 0xFFFFFFFF),
        ("task.shave.range.0.0", 8, "<Q", 1),
        ("task.shave.invocation.0.0", 84, "<I", 2),
        ("shave.params", 24, "<I", 1),
        ("shave.params", 8, "<I", 0),
        ("shave.params", 4, "<I", 1),
        ("shave.params", 80, "<I", 0x40218000),
        ("rela.params", 16, "<q", 2**62),
        ("rela.params", 8, "<Q", 999 << 32),
    ],
)
def test_malformed_tensor_code_or_relocation_rejected(section, offset, fmt, value):
    blob, locations = carrier()
    modified = bytearray(blob)
    struct.pack_into(fmt, modified, locations[section] + offset, value)
    with pytest.raises(ValueError):
        inspect_identity_carrier(bytes(modified))


@pytest.mark.parametrize("code", [b"", b"abc", bytes(48)])
def test_invalid_replacement_extent_rejected(code):
    blob, _ = carrier()
    with pytest.raises(ValueError):
        replace_identity_code(blob, code)


def kernel_elf():
    """Construct ELF32 metadata and dummy code, never an executable NPU fixture."""
    names = b"\0.text\0.shstrtab\0.arg.data\0"
    data = bytearray(52 + 4 * 40)
    start = len(data)
    data += bytes(range(16))
    name_offset = len(data)
    data += names
    struct.pack_into(
        "<16sHHIIIIIHHHHHH",
        data,
        0,
        b"\x7fELF\x01\x01\x01" + bytes(9),
        2,
        2,
        1,
        0x1D000000,
        0,
        52,
        0,
        52,
        0,
        0,
        40,
        4,
        2,
    )
    struct.pack_into("<10I", data, 52 + 40, 1, 1, 6, 0x1D000000, start, 16, 0, 0, 16, 0)
    struct.pack_into("<10I", data, 52 + 80, 7, 3, 0, 0, name_offset, len(names), 0, 0, 1, 0)
    struct.pack_into("<10I", data, 52 + 120, 17, 1, 2, 0x1E000000, start, 0, 0, 0, 16, 0)
    return bytes(data)


def test_static_kernel_text_extraction():
    from npu_render.ptl_carrier import read_kernel_text

    assert read_kernel_text(kernel_elf()) == bytes(range(16))


@pytest.mark.parametrize(
    ("offset", "value"),
    [
        (24, 1),  # Wrong entry point.
        (32, 0xFFFFFFFF),  # Section table outside the file.
        (52 + 40 + 4, 4),  # Unresolved relocation section.
        (52 + 40 + 12, 0),  # Text at the wrong address.
        (52 + 40 + 20, 15),  # Unaligned text extent.
        (52 + 120 + 20, 4),  # Mutable data.
        (52 + 120, 1),  # Duplicate section name.
    ],
)
def test_unsupported_linked_kernel_rejected(offset, value):
    from npu_render.ptl_carrier import read_kernel_text

    data = bytearray(kernel_elf())
    struct.pack_into("<I", data, offset, value)
    with pytest.raises(ValueError):
        read_kernel_text(bytes(data))


def test_cli_requires_artifact_pins_and_reports_offline_scope(tmp_path, monkeypatch):
    import hashlib
    import importlib.util
    import json
    from pathlib import Path

    spec = importlib.util.spec_from_file_location(
        "validate_ptl_kernel", Path(__file__).parents[1] / "tools/validate_ptl_kernel.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    blob, _ = carrier()
    elf = kernel_elf()
    source, code = tmp_path / "carrier", tmp_path / "kernel"
    source.write_bytes(blob)
    code.write_bytes(elf)
    for valid in (False, True):
        output = tmp_path / ("valid" if valid else "invalid")
        checksum = hashlib.sha256(blob).hexdigest() if valid else "0" * 64
        monkeypatch.setattr(
            "sys.argv",
            [
                "validate_ptl_kernel",
                "--carrier",
                str(source),
                "--kernel-elf",
                str(code),
                "--carrier-sha256",
                checksum,
                "--kernel-sha256",
                hashlib.sha256(elf).hexdigest(),
                "--output",
                str(output),
            ],
        )
        assert module.main() == (0 if valid else 1)
        report = json.loads((output / "report.json").read_text())
        assert report["passed"] is valid
        assert not report["hardware_execution_tested"]
        assert not report["custom_kernel_execution_qualified"]
        assert not report["full_desktop_offload"]
        assert not (output / "patched.blob").exists()
