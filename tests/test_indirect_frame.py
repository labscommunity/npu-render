import os

import pytest

from npu_render.indirect_frame import HEADER, MAX_BYTES, confirmed_sole_display, parse_frame


def packet(**changes):
    values = dict(
        magic=0x31454D415246524E,
        version=1,
        size=64,
        seq=3,
        w=2,
        h=1,
        fmt=87,
        pitch=8,
        count=8,
        qpc=999,
        pid=123,
        reserved=0,
    )
    values.update(changes)
    return HEADER.pack(*values.values()) + bytes(range(8))


def test_packed_formats_and_identity():
    for fmt in (28, 87):
        frame = parse_frame(packet(fmt=fmt))
        assert frame.identity == (123, 3, 999)
        assert (frame.width, frame.height, frame.format) == (2, 1, fmt)
        assert frame.pixels == bytes(range(8))


@pytest.mark.parametrize(
    "change",
    [
        {"magic": 0},
        {"version": 2},
        {"size": 32},
        {"seq": 0},
        {"pid": 0},
        {"qpc": 0},
        {"w": 0},
        {"h": 0},
        {"w": 8193},
        {"w": 8192, "h": 8192},
        {"fmt": 29},
        {"pitch": 256},
        {"count": 7},
        {"reserved": 1},
    ],
)
def test_reject_invalid_header(change):
    with pytest.raises(ValueError):
        parse_frame(packet(**change))


def test_reject_truncation_trailer_and_oversized_payload():
    for data in (b"", packet()[:63], packet()[:-1], packet() + b"x", bytes(MAX_BYTES + 1)):
        with pytest.raises(ValueError):
            parse_frame(data)


def test_input_requires_fresh_valid_sole_layout(tmp_path):
    path = tmp_path / "state.json"
    assert not confirmed_sole_display(path, 100)
    valid = b'{"topology_name":"idd-topology-test","sole_indirect":true}'
    for content in (
        b"",
        b"{",
        b"[]",
        b"true",
        b"x" * 4097,
        valid.replace(b"true", b"false"),
        valid.replace(b"true", b"1"),
        valid.replace(b"idd-topology-", b"unrelated-"),
    ):
        path.write_bytes(content)
        os.utime(path, (100, 100))
        assert not confirmed_sole_display(path, 105)
    path.write_bytes(valid)
    os.utime(path, (100, 100))
    assert confirmed_sole_display(path, 100)
    assert confirmed_sole_display(path, 115)
    assert not confirmed_sole_display(path, 115.01)
    assert not confirmed_sole_display(path, 99.99)
