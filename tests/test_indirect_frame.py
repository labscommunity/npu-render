import pytest

from npu_render.indirect_frame import HEADER, MAX_BYTES, parse_frame


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
