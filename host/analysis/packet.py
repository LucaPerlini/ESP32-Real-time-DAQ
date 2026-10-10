"""
packet.py -- Python encoder/decoder for the DAQ wire format.

This mirrors common/daq_packet.h exactly. It is the single source of truth on
the Python side, used by both the simulator (encode) and the analysis (decode),
and exercised by the unit tests against known vectors.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = 0xDA51
PROTO_VER = 1
MAX_CHANNELS = 4
MAX_SAMPLES = 512

SRC_ADC_LOOPBACK = 0
SRC_ADC_EXTERNAL = 1
SRC_SYNTHETIC = 2

# < little-endian, no padding; matches the #pragma pack(1) C struct (30 bytes).
_HDR = struct.Struct("<HBBHHHIQQ")
HEADER_SIZE = _HDR.size
assert HEADER_SIZE == 30, HEADER_SIZE


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


@dataclass
class Block:
    seq: int
    source: int
    n_channels: int
    n_samples: int
    fs_hz: int
    t_first_us: int
    t_last_us: int
    samples: list[int]  # interleaved: s0c0, s0c1, s1c0, ...

    def block_size(self) -> int:
        return HEADER_SIZE + self.n_channels * self.n_samples * 2 + 2


def encode(b: Block) -> bytes:
    """Serialise a Block to its on-wire bytes (header + payload + CRC)."""
    hdr = _HDR.pack(
        MAGIC, PROTO_VER, b.source, b.seq,
        b.n_channels, b.n_samples, b.fs_hz,
        b.t_first_us, b.t_last_us,
    )
    payload = struct.pack("<%dh" % len(b.samples), *b.samples)
    crc = crc16(hdr + payload)
    return hdr + payload + struct.pack("<H", crc)


def decode(buf: bytes) -> Block:
    """Parse one complete block. Raises ValueError on bad magic/CRC/geometry."""
    if len(buf) < HEADER_SIZE + 2:
        raise ValueError("buffer shorter than a minimal block")
    (magic, ver, source, seq, n_ch, n_s, fs, t0, t1) = _HDR.unpack_from(buf, 0)
    if magic != MAGIC:
        raise ValueError(f"bad magic 0x{magic:04X}")
    if ver != PROTO_VER:
        raise ValueError(f"unsupported version {ver}")
    if not (1 <= n_ch <= MAX_CHANNELS) or not (1 <= n_s <= MAX_SAMPLES):
        raise ValueError("geometry out of range")
    pbytes = n_ch * n_s * 2
    end = HEADER_SIZE + pbytes
    if len(buf) < end + 2:
        raise ValueError("buffer shorter than declared block")
    crc_rx = struct.unpack_from("<H", buf, end)[0]
    if crc16(buf[:end]) != crc_rx:
        raise ValueError("CRC mismatch")
    samples = list(struct.unpack_from("<%dh" % (n_ch * n_s), buf, HEADER_SIZE))
    return Block(seq, source, n_ch, n_s, fs, t0, t1, samples)
