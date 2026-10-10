# Measurement method

The project separates two timing questions that are often conflated, and
measures each where it can be measured cleanly.

## 1. Sampling-clock accuracy (device side)

The sampling cadence is set by a hardware GPTimer on the ESP32, not by software
delays. Each block carries the device `esp_timer` time of its first and last
sample. From a run, the effective per-channel rate is

```
dev_dt   = (t_last_us - t_first_us) / (n_samples - 1)     # per block
eff_fs   = 1e6 / mean(dev_dt)
drift    = (eff_fs / fs_configured - 1) * 1e6              # ppm
```

This isolates how closely the on-chip timer reproduces the configured rate over
the capture, independent of the host.

## 2. End-to-end arrival jitter (host side)

The host receiver timestamps each block with `CLOCK_MONOTONIC` the instant its
last byte arrives, after raising itself to `SCHED_FIFO`, locking its memory with
`mlockall`, and pinning to one CPU. The inter-block interval

```
dt_arrival = diff(t_arrival)
```

and its spread (std, p99, max against the nominal block period) describe the
determinism of the whole acquisition-to-host chain, which is the number that
matters when the stream feeds a downstream consumer in real time.

## 3. Integrity

Every block is CRC-checked; `seq` gaps give block loss. Both are reported so a
timing figure is never quoted without the loss and error context behind it.

## Characterisation suggestion

Hold the geometry fixed and sweep `DAQ_FS_HZ` upward, recording `eff_fs`, the
arrival-jitter std/p99, and loss at each rate. The point where jitter or loss
begins to grow is the usable real-time bandwidth of this configuration, and is
a concrete result worth recording.

## Note on the development path vs. the hardware path

The Python simulator reproduces the exact wire stream, with configurable device
drift, per-sample jitter and block loss, so the receiver and the analysis are
developed and regression-tested with a known ground truth and no board. Arrival
timing from the simulator reflects the host OS scheduler and the simulator's own
pacing, not a physical link; the sampling-clock and real end-to-end arrival
figures come from the ESP32 over USB.
