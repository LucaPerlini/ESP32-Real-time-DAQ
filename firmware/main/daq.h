/*
 * daq.h  --  Acquisition engine: hardware-timed sampling + block streaming.
 */
#ifndef DAQ_H
#define DAQ_H

/* Bring up the signal source, UART, GPTimer and the sampler/TX tasks, then
 * start acquisition. Returns after the pipeline is running; sampling then
 * proceeds autonomously under the hardware timer. */
void daq_start(void);

#endif /* DAQ_H */
