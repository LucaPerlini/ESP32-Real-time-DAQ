#!/usr/bin/env python3
"""
simulator.py -- Board-free source of the DAQ wire stream.

Emits exactly the bytes the ESP32 firmware would send, so the host receiver and
all analysis can be developed and tested without any hardware. Device-side
timestamps can be given a configurable clock drift and per-sample jitter, and
blocks can be dropped, so the analysis is exercised on a realistic, known-truth
signal.

Examples
  # pipe a 3 s capture straight into the receiver:
  python3 simulator.py --duration 3 --paced | ./rt_receiver --stdin --raw s.bin

  # write a capture to a file:
  python3 simulator.py --blocks 100 --out capture.bin
"""
from __future__ import annotations

import argparse
import math
import random
import sys
import time

from packet import Block, encode, SRC_SYNTHETIC


def build_samples(n_ch: int, n_s: int, start_idx: int, fs: float, freq: float) -> list[int]:
    out = [0] * (n_ch * n_s)
    w = 2.0 * math.pi * freq / fs
    for s in range(n_s):
        n = start_idx + s
        for c in range(n_ch):
            phase = w * n + c * (math.pi / 2.0)      # 90 deg between channels
            out[s * n_ch + c] = int(round(2047 * math.sin(phase)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="DAQ stream simulator")
    ap.add_argument("--fs", type=int, default=2000, help="sample rate Hz")
    ap.add_argument("--channels", type=int, default=2)
    ap.add_argument("--block", type=int, default=200, help="samples/channel per block")
    ap.add_argument("--freq", type=float, default=100.0, help="tone Hz")
    ap.add_argument("--blocks", type=int, default=0, help="number of blocks (0 = use duration)")
    ap.add_argument("--duration", type=float, default=2.0, help="seconds, if --blocks 0")
    ap.add_argument("--jitter-us", type=float, default=2.0, help="per-sample tick jitter stddev")
    ap.add_argument("--drift-ppm", type=float, default=20.0, help="device clock drift")
    ap.add_argument("--loss", type=float, default=0.0, help="block drop probability 0..1")
    ap.add_argument("--paced", action="store_true", help="emit in real time")
    ap.add_argument("--out", default="-", help="output file ('-' = stdout)")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    random.seed(args.seed)
    out = sys.stdout.buffer if args.out == "-" else open(args.out, "wb")

    dt_us = 1e6 / args.fs
    # A positive --drift-ppm is reported as a positive drift by analyze.py.
    drift = 1.0 - args.drift_ppm * 1e-6
    block_period = args.block / args.fs

    n_blocks = args.blocks if args.blocks > 0 else int(round(args.duration / block_period))
    sample_idx = 0
    seq = 0

    for _ in range(n_blocks):
        first_n = sample_idx
        last_n = sample_idx + args.block - 1
        # device time base: ideal grid * drift, plus independent tick jitter
        t_first = int(round(first_n * dt_us * drift + random.gauss(0, args.jitter_us)))
        t_last = int(round(last_n * dt_us * drift + random.gauss(0, args.jitter_us)))
        samples = build_samples(args.channels, args.block, sample_idx, args.fs, args.freq)

        emit = random.random() >= args.loss
        if emit:
            blk = Block(seq & 0xFFFF, SRC_SYNTHETIC, args.channels, args.block,
                        args.fs, max(0, t_first), max(0, t_last), samples)
            out.write(encode(blk))
            out.flush()

        seq += 1
        sample_idx += args.block
        if args.paced:
            time.sleep(block_period)

    if out is not sys.stdout.buffer:
        out.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
