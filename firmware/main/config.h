/*
 * config.h  --  Compile-time configuration for the DAQ firmware.
 *
 * Defaults are intentionally conservative so that timing stays comfortably
 * deterministic on the classic dual-core ESP32-WROOM-32. Raise DAQ_FS_HZ to
 * characterise where the sampling jitter starts to grow; that measurement is
 * itself a useful result to record.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---- Acquisition geometry ------------------------------------------------ */
#define DAQ_FS_HZ            2000u   /* per-channel sample rate, Hz           */
#define DAQ_N_CHANNELS       2u      /* 1..DAQ_MAX_CHANNELS                    */
#define DAQ_BLOCK_SAMPLES    200u    /* samples per channel per streamed block*/
                                     /* -> 1 block = 100 ms at 2 kHz          */

/* ---- Signal source ------------------------------------------------------- *
 * DAQ_SRC_ADC_LOOPBACK : DAC cosine on GPIO25 jumpered to ADC pin, read back
 * DAQ_SRC_ADC_EXTERNAL : ADC pins driven by an external signal
 * DAQ_SRC_SYNTHETIC    : samples generated in firmware (no wiring needed)
 */
#define DAQ_SOURCE           DAQ_SRC_ADC_LOOPBACK

/* ADC1 channels used, in block order. GPIO34 = CH6, GPIO35 = CH7 on WROOM-32.
 * For the loopback test, jumper GPIO25 (DAC1) to GPIO34 (ADC1_CH6). */
#define DAQ_ADC_CH0          ADC_CHANNEL_6   /* GPIO34 */
#define DAQ_ADC_CH1          ADC_CHANNEL_7   /* GPIO35 */

/* DAC cosine generator (loopback source only). */
#define DAQ_LOOPBACK_FREQ_HZ 100u            /* test tone frequency           */

/* Synthetic source tone (synthetic mode only). */
#define DAQ_SYNTH_FREQ_HZ    100.0f

/* ---- Transport ----------------------------------------------------------- */
#define DAQ_UART_NUM         UART_NUM_0      /* CP2102 USB bridge             */
#define DAQ_UART_BAUD        2000000         /* 2 Mbaud, raw binary           */

/* ---- Task placement ------------------------------------------------------ */
#define DAQ_SAMPLER_CORE     1               /* isolate sampling on core 1    */
#define DAQ_TX_CORE          0               /* streaming + housekeeping      */

#endif /* APP_CONFIG_H */
