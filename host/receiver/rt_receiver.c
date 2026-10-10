/*
 * rt_receiver.c  --  Real-time host receiver for the ESP32 DAQ stream.
 *
 * Responsibilities
 *   - Optionally raise the process to a real-time scheduling class
 *     (SCHED_FIFO), lock its memory (mlockall) and pin it to one CPU, so that
 *     block arrivals are serviced with bounded latency.
 *   - Resynchronise on the block magic word, validate each block with CRC-16,
 *     and timestamp its arrival with CLOCK_MONOTONIC the instant the last byte
 *     is read.
 *   - Log per-block timing/status to CSV and (optionally) the raw payloads to
 *     a binary file for offline signal analysis.
 *
 * Input is either a serial device (--port) or stdin (--stdin), the latter so
 * the whole pipeline can be exercised from the Python simulator with no board.
 */
#define _GNU_SOURCE
#include "serial.h"
#include "daq_packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>

#define RT_PRIORITY 80
#define RT_CPU      1

static volatile int g_stop = 0;

/* ---- helpers ------------------------------------------------------------ */
static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Read exactly n bytes (handles short reads). Returns 0 on success, -1 on EOF/err. */
static int read_exact(int fd, uint8_t *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r > 0)       got += (size_t)r;
        else if (r == 0) return -1;              /* EOF */
        else if (errno == EINTR) continue;
        else { perror("read"); return -1; }
    }
    return 0;
}

static void apply_realtime(void)
{
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        fprintf(stderr, "warn: mlockall failed (%s); continuing\n", strerror(errno));

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(RT_CPU, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        fprintf(stderr, "warn: setaffinity CPU%d failed (%s); continuing\n",
                RT_CPU, strerror(errno));

    struct sched_param sp = { .sched_priority = RT_PRIORITY };
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0)
        fprintf(stderr, "warn: SCHED_FIFO prio %d failed (%s); run with privilege "
                "(sudo or CAP_SYS_NICE) for real-time scheduling. Continuing with "
                "normal scheduling.\n", RT_PRIORITY, strerror(errno));
    else
        fprintf(stderr, "info: SCHED_FIFO prio %d on CPU%d, memory locked\n",
                RT_PRIORITY, RT_CPU);
}

/* ---- main --------------------------------------------------------------- */
int main(int argc, char **argv)
{
    const char *port = NULL, *log_path = "blocks.csv", *raw_path = NULL;
    int baud = 2000000, use_stdin = 0, want_rt = 0;
    long max_blocks = -1;

    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--port")   && i+1 < argc) port = argv[++i];
        else if (!strcmp(argv[i], "--baud")   && i+1 < argc) baud = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--stdin"))                use_stdin = 1;
        else if (!strcmp(argv[i], "--log")    && i+1 < argc) log_path = argv[++i];
        else if (!strcmp(argv[i], "--raw")    && i+1 < argc) raw_path = argv[++i];
        else if (!strcmp(argv[i], "--rt"))                   want_rt = 1;
        else if (!strcmp(argv[i], "--blocks") && i+1 < argc) max_blocks = atol(argv[++i]);
        else {
            fprintf(stderr,
                "usage: %s (--port DEV [--baud N] | --stdin) [--log f.csv] "
                "[--raw f.bin] [--rt] [--blocks N]\n", argv[0]);
            return 2;
        }
    }

    int fd;
    if (use_stdin)      fd = 0;
    else if (port)      { fd = serial_open(port, baud); if (fd < 0) return 1; }
    else { fprintf(stderr, "error: specify --port or --stdin\n"); return 2; }

    if (want_rt) apply_realtime();

    FILE *log = fopen(log_path, "w");
    if (!log) { perror("fopen log"); return 1; }
    fprintf(log, "seq,source,fs_hz,n_channels,n_samples,"
                 "t_first_us,t_last_us,t_arrival_ns,crc_ok,resyncs\n");

    FILE *raw = NULL;
    if (raw_path) { raw = fopen(raw_path, "wb"); if (!raw) { perror("fopen raw"); return 1; } }

    uint8_t hdr_rest[DAQ_HEADER_SIZE];           /* full header staging */
    uint8_t payload[DAQ_MAX_CHANNELS * DAQ_MAX_SAMPLES * sizeof(int16_t)];
    uint8_t wire[DAQ_HEADER_SIZE + sizeof(payload)];
    uint64_t resyncs = 0, good = 0;

    /* Byte-wise magic search; little-endian 0xDA51 -> bytes 0x51,0xDA. */
    int prev = -1;
    while (!g_stop) {
        uint8_t b;
        if (read_exact(fd, &b, 1) != 0) break;

        if (!(prev == 0x51 && b == 0xDA)) { prev = b; continue; }
        prev = -1;

        /* Matched magic. Stage it, then read the rest of the header. */
        hdr_rest[0] = 0x51; hdr_rest[1] = 0xDA;
        if (read_exact(fd, hdr_rest + 2, DAQ_HEADER_SIZE - 2) != 0) break;

        const daq_header_t *h = (const daq_header_t *)hdr_rest;
        if (h->version != DAQ_PROTO_VER ||
            h->n_channels == 0 || h->n_channels > DAQ_MAX_CHANNELS ||
            h->n_samples  == 0 || h->n_samples  > DAQ_MAX_SAMPLES) {
            resyncs++;            /* false sync: keep scanning */
            continue;
        }

        size_t pbytes = (size_t)h->n_channels * h->n_samples * sizeof(int16_t);
        uint8_t crc_bytes[2];
        if (read_exact(fd, payload, pbytes) != 0) break;
        if (read_exact(fd, crc_bytes, 2) != 0) break;

        uint64_t t_arrival = monotonic_ns();     /* block fully received */

        memcpy(wire, hdr_rest, DAQ_HEADER_SIZE);
        memcpy(wire + DAQ_HEADER_SIZE, payload, pbytes);
        uint16_t crc_calc = daq_crc16(wire, DAQ_HEADER_SIZE + pbytes);
        uint16_t crc_rx = (uint16_t)crc_bytes[0] | ((uint16_t)crc_bytes[1] << 8);
        int crc_ok = (crc_calc == crc_rx);

        if (!crc_ok) { resyncs++; }

        fprintf(log, "%u,%u,%u,%u,%u,%llu,%llu,%llu,%d,%llu\n",
                h->seq, h->source, h->fs_hz, h->n_channels, h->n_samples,
                (unsigned long long)h->t_first_us, (unsigned long long)h->t_last_us,
                (unsigned long long)t_arrival, crc_ok,
                (unsigned long long)resyncs);

        if (crc_ok) {
            good++;
            if (raw) fwrite(payload, 1, pbytes, raw);
            if (max_blocks > 0 && (long)good >= max_blocks) break;
        }
    }

    fclose(log);
    if (raw) fclose(raw);
    fprintf(stderr, "done: %llu good blocks, %llu resync/CRC events -> %s\n",
            (unsigned long long)good, (unsigned long long)resyncs, log_path);
    return 0;
}
