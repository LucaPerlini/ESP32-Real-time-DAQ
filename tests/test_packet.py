"""
Unit tests for the DAQ codec and CRC.

Run from the repo root:  pytest -q
These exercise the Python side against fixed vectors and round-trips, and pin
the wire layout so firmware/host drift is caught early.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host", "analysis"))

import struct
import pytest

from packet import (
    Block, encode, decode, crc16,
    HEADER_SIZE, MAGIC, PROTO_VER, SRC_SYNTHETIC,
)


def test_header_size_is_fixed():
    assert HEADER_SIZE == 30


def test_crc16_known_vector():
    # CRC-16/CCITT-FALSE("123456789") == 0x29B1 (standard check value)
    assert crc16(b"123456789") == 0x29B1


def test_crc16_empty():
    assert crc16(b"") == 0xFFFF


def make_block(seq=0, n_ch=2, n_s=4):
    samples = [((-1) ** i) * (i * 7 % 2048) for i in range(n_ch * n_s)]
    return Block(seq, SRC_SYNTHETIC, n_ch, n_s, 2000, 1000, 1000 + n_s - 1, samples)


def test_roundtrip_identity():
    b = make_block(seq=1234, n_ch=2, n_s=16)
    out = decode(encode(b))
    assert out == b


def test_block_size_matches_encoding():
    b = make_block(n_ch=3, n_s=10)
    assert len(encode(b)) == b.block_size()


def test_magic_and_version_in_wire():
    b = make_block()
    wire = encode(b)
    magic, ver = struct.unpack_from("<HB", wire, 0)
    assert magic == MAGIC and ver == PROTO_VER


def test_bad_magic_rejected():
    wire = bytearray(encode(make_block()))
    wire[0] ^= 0xFF
    with pytest.raises(ValueError):
        decode(bytes(wire))


def test_crc_corruption_rejected():
    wire = bytearray(encode(make_block()))
    wire[-1] ^= 0xFF  # flip a CRC byte
    with pytest.raises(ValueError):
        decode(bytes(wire))


def test_payload_corruption_rejected():
    wire = bytearray(encode(make_block(n_ch=1, n_s=8)))
    wire[HEADER_SIZE] ^= 0x01  # flip a sample bit; CRC must catch it
    with pytest.raises(ValueError):
        decode(bytes(wire))


@pytest.mark.parametrize("n_ch,n_s", [(1, 1), (1, 200), (2, 200), (4, 64)])
def test_geometries(n_ch, n_s):
    b = make_block(n_ch=n_ch, n_s=n_s)
    assert decode(encode(b)) == b
