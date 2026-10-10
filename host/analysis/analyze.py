#!/usr/bin/env python3
"""
analyze.py -- Timing and signal analysis of a capture.

Consumes blocks.csv (written by rt_receiver) and, optionally, the raw payload
file. Reports the numbers that matter for an instrumentation / flight-test-
instrumentation context:

  Host arrival timing   inter-block interval mean / std / p99 / max  (end-to-end
                        jitter of the acquisition-to-host chain)
  Device time base      effective sample rate and drift from the device
                        timestamps  (sampling-clock accuracy)
  Integrity             block loss from sequence gaps, CRC/resync events
  Signal               reconstructed waveform and its spectrum (if --raw given)

Figures are written with the Agg backend so this runs headless and in CI.
"""
from __future__ import annotations

import argparse
import csv
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from packet import HEADER_SIZE  # noqa: F401  (kept for parity / future use)


def load_blocks(path: str) -> dict:
    cols: dict[str, list] = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            for k, v in row.items():
                cols.setdefault(k, []).append(v)
    out = {}
    intcols = ["seq", "source", "fs_hz", "n_channels", "n_samples",
               "t_first_us", "t_last_us", "t_arrival_ns", "crc_ok", "resyncs"]
    for k in intcols:
        out[k] = np.array([int(x) for x in cols[k]], dtype=np.int64)
    return out


def pct(a: np.ndarray, p: float) -> float:
    return float(np.percentile(a, p)) if a.size else float("nan")


def analyze(path: str, raw: str | None, outdir: str) -> int:
    b = load_blocks(path)
    n = b["seq"].size
    if n == 0:
        print("no blocks in log", file=sys.stderr)
        return 1

    fs = int(b["fs_hz"][0])
    n_ch = int(b["n_channels"][0])
    n_s = int(b["n_samples"][0])
    block_period_us = 1e6 * n_s / fs

    # ---- host arrival jitter ------------------------------------------------
    t_arr = b["t_arrival_ns"].astype(np.float64) / 1e3  # -> us
    dt_arr = np.diff(t_arr)                              # inter-block, us
    # ---- device time base ---------------------------------------------------
    dev_dt = (b["t_last_us"] - b["t_first_us"]).astype(np.float64) / (n_s - 1)
    eff_fs = 1e6 / np.mean(dev_dt)
    drift_ppm = (eff_fs / fs - 1.0) * 1e6
    # ---- integrity ----------------------------------------------------------
    seq = b["seq"].astype(np.int64)
    gaps = np.diff(seq)
    gaps = np.where(gaps < 0, gaps + 65536, gaps)       # handle wrap
    lost = int(np.sum(gaps - 1))
    crc_fail = int(np.sum(b["crc_ok"] == 0))

    print("=" * 60)
    print(f"blocks received      {n}")
    print(f"geometry             {n_ch} ch x {n_s} samples @ {fs} Hz")
    print(f"block period (nom.)  {block_period_us/1000:.3f} ms")
    print("-" * 60)
    print("HOST ARRIVAL INTERVAL (end-to-end chain jitter)")
    print(f"  mean   {np.mean(dt_arr)/1000:9.4f} ms   (nominal {block_period_us/1000:.4f})")
    print(f"  std    {np.std(dt_arr):9.2f} us")
    print(f"  p99    {pct(dt_arr,99):9.2f} us")
    print(f"  max    {np.max(dt_arr):9.2f} us")
    print("-" * 60)
    print("DEVICE SAMPLING CLOCK")
    print(f"  effective fs   {eff_fs:10.3f} Hz")
    print(f"  drift          {drift_ppm:+10.1f} ppm")
    print("-" * 60)
    print(f"block loss (seq gaps)   {lost}")
    print(f"CRC-failed blocks        {crc_fail}")
    print("=" * 60)

    # ---- figures ------------------------------------------------------------
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))
    ax[0].hist(dt_arr, bins=60, color="#3b6ea5")
    ax[0].axvline(block_period_us, color="#c23b22", ls="--", lw=1, label="nominal")
    ax[0].set_title("Host inter-block interval")
    ax[0].set_xlabel("interval (us)"); ax[0].set_ylabel("count"); ax[0].legend()

    ax[1].plot(dt_arr, lw=0.6, color="#3b6ea5")
    ax[1].axhline(block_period_us, color="#c23b22", ls="--", lw=1)
    ax[1].set_title("Inter-block interval over time")
    ax[1].set_xlabel("block index"); ax[1].set_ylabel("interval (us)")
    fig.tight_layout()
    fig.savefig(f"{outdir}/timing.png", dpi=110)
    print(f"wrote {outdir}/timing.png")

    if raw:
        data = np.fromfile(raw, dtype="<i2")
        usable = (data.size // (n_ch * n_s)) * (n_ch * n_s)
        data = data[:usable].reshape(-1, n_ch)          # columns = channels
        t = np.arange(min(data.shape[0], 2 * n_s)) / fs
        fig2, ax2 = plt.subplots(1, 2, figsize=(11, 4))
        for c in range(n_ch):
            ax2[0].plot(t * 1000, data[:t.size, c], lw=0.8, label=f"ch{c}")
        ax2[0].set_title("Reconstructed signal")
        ax2[0].set_xlabel("time (ms)"); ax2[0].set_ylabel("code"); ax2[0].legend()

        seg = data[:, 0].astype(np.float64)
        seg = seg - seg.mean()
        win = np.hanning(seg.size)
        spec = np.abs(np.fft.rfft(seg * win))
        f = np.fft.rfftfreq(seg.size, 1.0 / fs)
        ax2[1].plot(f, 20 * np.log10(spec + 1e-9), lw=0.8, color="#3b6ea5")
        peak = f[int(np.argmax(spec[1:]) + 1)]
        ax2[1].set_title(f"Spectrum ch0 (peak {peak:.1f} Hz)")
        ax2[1].set_xlabel("frequency (Hz)"); ax2[1].set_ylabel("dB")
        fig2.tight_layout()
        fig2.savefig(f"{outdir}/signal.png", dpi=110)
        print(f"wrote {outdir}/signal.png  (tone peak {peak:.1f} Hz)")

    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="DAQ capture analysis")
    ap.add_argument("--log", default="blocks.csv")
    ap.add_argument("--raw", default=None)
    ap.add_argument("--outdir", default=".")
    args = ap.parse_args()
    return analyze(args.log, args.raw, args.outdir)


if __name__ == "__main__":
    raise SystemExit(main())
