# esp32-rt-daq

A real-time, multi-channel data-acquisition node on the ESP32-WROOM-32 with a
Linux host receiver, built to measure and document its own timing determinism.

Hardware-timed sampling on the MCU streams channel-interleaved, CRC-protected
blocks over USB-UART to a host process that runs under `SCHED_FIFO`, timestamps
every block, and reports sampling-clock accuracy, end-to-end arrival jitter and
block loss. A pure-Python simulator reproduces the exact wire stream so the host
side can be developed and regression-tested without a board.

## Architecture

```
   ESP32-WROOM-32 (FreeRTOS)                         Linux host
 ┌───────────────────────────┐                ┌──────────────────────────┐
 │ GPTimer ──ISR──▶ tick queue│                │ rt_receiver (C)          │
 │                     │      │   USB-UART     │  SCHED_FIFO + mlockall   │
 │  sampler task (core1)──────┼───▶ framed ───▶│  resync + CRC + arrival  │
 │  ADC / DAC loopback / synth│   binary blocks│  timestamp  ──▶ CSV + raw │
 │  tx task (core0) ──────────┘                │                          │
 └───────────────────────────┘                └──────────┬───────────────┘
                                                          ▼
                                               analyze.py (NumPy/Matplotlib)
                                               timing + spectrum + integrity
```

- **Timing** is driven by a hardware GPTimer; its ISR only timestamps the tick
  and queues it, so the sampling instants are defined by the timer peripheral.
- **Sampling** runs in a top-priority task pinned to core 1; **streaming** runs
  on core 0, keeping UART activity off the acquisition path.
- **Three signal sources** select at build time: a hardware DAC cosine wired
  back to an ADC pin (loopback), an external signal, or a firmware-generated
  sine that needs no wiring.

See [`docs/measurement-method.md`](docs/measurement-method.md) for the metrics
and [`protocol/packet_format.md`](protocol/packet_format.md) for the wire format.

## Repository layout

```
common/            wire format + CRC-16, shared by firmware and host
firmware/          ESP-IDF v5.x application (main/: daq, signal_source, app_main)
host/receiver/     real-time C receiver (serial or stdin)
host/analysis/     Python simulator + timing/signal analysis
tests/             pytest unit tests pinning the codec and CRC
.github/workflows/ CI: firmware build + host build + tests + smoke run
```

## Build and run

### Firmware (ESP-IDF v5.x)

```bash
cd firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

Default configuration (`main/config.h`): 2 channels, 2 kHz, 200-sample blocks,
DAC→ADC loopback source. For the loopback test, jumper **GPIO25 (DAC1)** to
**GPIO34 (ADC1_CH6)**. Set `DAQ_SOURCE` to `DAQ_SRC_SYNTHETIC` to run with no
wiring at all.

### Host receiver (Linux)

```bash
cd host/receiver
cmake -B build -S . && cmake --build build
# live capture from the board, with real-time scheduling (needs CAP_SYS_NICE):
sudo ./build/rt_receiver --port /dev/ttyUSB0 --baud 2000000 \
     --rt --log blocks.csv --raw samples.bin
```

### Analysis

```bash
cd host/analysis
pip install -r requirements.txt
python3 analyze.py --log ../receiver/blocks.csv --raw ../receiver/samples.bin
```

### No board? Full pipeline from the simulator

```bash
cd host/analysis
python3 simulator.py --duration 3 --paced \
  | ../receiver/build/rt_receiver --stdin --log /tmp/b.csv --raw /tmp/s.bin
python3 analyze.py --log /tmp/b.csv --raw /tmp/s.bin --outdir /tmp
```

## What CI verifies

- the ESP-IDF firmware builds for the `esp32` target;
- the host receiver builds with `-Wall -Wextra`;
- the codec/CRC unit tests pass (the CRC is checked against the standard
  `0x29B1` vector, which is also what the C firmware computes);
- the simulator → receiver → analysis pipeline runs end to end and produces the
  timing and signal plots as build artifacts.

## Roadmap

- Wi-Fi/UDP transport as an alternative to USB-UART, for a telemetry-style link.
- `adc_continuous` (I2S-DMA) acquisition backend for CPU-free sampling at higher
  rates, selectable against the current GPTimer backend.
- Rate-sweep script to chart usable real-time bandwidth automatically.

## License

MIT — see [LICENSE](LICENSE).
