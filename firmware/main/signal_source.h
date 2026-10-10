/*
 * signal_source.h  --  Abstraction over where a channel sample comes from.
 *
 * One of three backends is selected at build time via DAQ_SOURCE:
 *   - loopback : a hardware DAC cosine wired back into an ADC pin
 *   - external : an external signal on the ADC pins
 *   - synthetic: a firmware-generated sine (no hardware required)
 *
 * The sampler calls signal_source_read() once per channel per tick. The call
 * is kept short and allocation-free so it can run inside the high-priority
 * sampling task without disturbing the acquisition cadence.
 */
#ifndef SIGNAL_SOURCE_H
#define SIGNAL_SOURCE_H

#include <stdint.h>

/* Initialise the configured source (ADC unit, DAC cosine, or LUT). */
void signal_source_init(void);

/* Report which daq_source value is active, for the block header. */
uint8_t signal_source_kind(void);

/* Read one raw sample for channel `ch` (0-based). Returns a signed value
 * centred on 0: ADC samples are re-centred from [0,4095] to [-2048,2047],
 * synthetic samples are already centred. */
int16_t signal_source_read(uint32_t ch, uint32_t sample_index);

#endif /* SIGNAL_SOURCE_H */
