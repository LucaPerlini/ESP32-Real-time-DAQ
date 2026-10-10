# Wire format

One block is streamed per acquisition buffer. All fields are little-endian.
The authoritative definition is [`common/daq_packet.h`](../common/daq_packet.h);
the Python side mirrors it in [`host/analysis/packet.py`](../host/analysis/packet.py)
and the two are pinned together by the unit tests.

```
+-----------------------------------------------------------+
| header (30 bytes)                                         |
+-----------------------------------------------------------+
| int16 samples[n_channels * n_samples]  (channel-interleaved)|
+-----------------------------------------------------------+
| uint16 crc16   (CRC-16/CCITT-FALSE over header + payload) |
+-----------------------------------------------------------+
```

## Header

| offset | field        | type   | notes                                   |
|-------:|--------------|--------|-----------------------------------------|
| 0      | magic        | u16    | `0xDA51` block sync word                |
| 2      | version      | u8     | protocol version (1)                    |
| 3      | source       | u8     | 0 loopback, 1 external, 2 synthetic     |
| 4      | seq          | u16    | rolling block counter, wraps at 65535   |
| 6      | n_channels   | u16    | channels in this block                  |
| 8      | n_samples    | u16    | samples per channel                     |
| 10     | fs_hz        | u32    | configured per-channel sample rate      |
| 14     | t_first_us   | u64    | device time of first sample (esp_timer) |
| 22     | t_last_us    | u64    | device time of last sample (esp_timer)  |

## Payload

`n_channels * n_samples` signed 16-bit samples, interleaved by sample:
`s0c0, s0c1, s1c0, s1c1, ...`. ADC samples are re-centred to `[-2048, 2047]`.

## Framing and integrity

The receiver scans byte-by-byte for the magic word, reads the fixed header,
range-checks the geometry, then reads the payload and the trailing CRC. A block
whose CRC does not match is discarded and the scan resumes, so a corrupted or
partial block never reaches the decoded output. Lost blocks are detected from
gaps in `seq`.
