/*
 * signal_source.c  --  Backend implementations for the sample source.
 */
#include "signal_source.h"
#include "config.h"
#include "daq_packet.h"

#include <math.h>

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#if DAQ_SOURCE == DAQ_SRC_ADC_LOOPBACK
#include "driver/dac_cosine.h"
#endif

static const char *TAG = "src";

#if (DAQ_SOURCE == DAQ_SRC_ADC_LOOPBACK) || (DAQ_SOURCE == DAQ_SRC_ADC_EXTERNAL)
static adc_oneshot_unit_handle_t s_adc;
static const adc_channel_t s_ch[DAQ_MAX_CHANNELS] = { DAQ_ADC_CH0, DAQ_ADC_CH1 };
#endif

#if DAQ_SOURCE == DAQ_SRC_SYNTHETIC
/* Precomputed sine LUT, centred on 0 with full-scale +/-2047. */
#define SYNTH_LUT_LEN 1024
static int16_t s_lut[SYNTH_LUT_LEN];
static uint32_t s_phase_step;   /* Q16 phase increment per sample */
static uint32_t s_phase[DAQ_MAX_CHANNELS];
#endif

void signal_source_init(void)
{
#if (DAQ_SOURCE == DAQ_SRC_ADC_LOOPBACK) || (DAQ_SOURCE == DAQ_SRC_ADC_EXTERNAL)
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten = ADC_ATTEN_DB_12,      /* full-scale ~0..3.1 V */
        .bitwidth = ADC_BITWIDTH_12,
    };
    for (uint32_t i = 0; i < DAQ_N_CHANNELS; ++i)
        ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_ch[i], &ch_cfg));
    ESP_LOGI(TAG, "ADC1 oneshot ready, %u channel(s)", (unsigned)DAQ_N_CHANNELS);
#endif

#if DAQ_SOURCE == DAQ_SRC_ADC_LOOPBACK
    dac_cosine_handle_t cw;
    dac_cosine_config_t cw_cfg = {
        .chan_id = DAC_CHAN_0,          /* GPIO25 */
        .freq_hz = DAQ_LOOPBACK_FREQ_HZ,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .offset = 0,
        .phase = DAC_COSINE_PHASE_0,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .flags.force_set_freq = false,
    };
    ESP_ERROR_CHECK(dac_cosine_new_channel(&cw_cfg, &cw));
    ESP_ERROR_CHECK(dac_cosine_start(cw));
    ESP_LOGI(TAG, "DAC cosine on GPIO25 @ %u Hz (jumper to GPIO34)",
             (unsigned)DAQ_LOOPBACK_FREQ_HZ);
#endif

#if DAQ_SOURCE == DAQ_SRC_SYNTHETIC
    for (int i = 0; i < SYNTH_LUT_LEN; ++i)
        s_lut[i] = (int16_t)lrintf(2047.0f * sinf(2.0f * (float)M_PI * i / SYNTH_LUT_LEN));
    /* phase step in Q16 table units per sample */
    s_phase_step = (uint32_t)lrintf((DAQ_SYNTH_FREQ_HZ * SYNTH_LUT_LEN / (float)DAQ_FS_HZ) * 65536.0f);
    for (uint32_t c = 0; c < DAQ_MAX_CHANNELS; ++c)
        s_phase[c] = c * (65536u * SYNTH_LUT_LEN / 4u); /* 90 deg per channel */
    ESP_LOGI(TAG, "synthetic sine source @ %.1f Hz", (double)DAQ_SYNTH_FREQ_HZ);
#endif
}

uint8_t signal_source_kind(void)
{
    return (uint8_t)DAQ_SOURCE;
}

int16_t signal_source_read(uint32_t ch, uint32_t sample_index)
{
#if (DAQ_SOURCE == DAQ_SRC_ADC_LOOPBACK) || (DAQ_SOURCE == DAQ_SRC_ADC_EXTERNAL)
    (void)sample_index;
    int raw = 0;
    adc_oneshot_read(s_adc, s_ch[ch], &raw);  /* 0..4095 */
    return (int16_t)(raw - 2048);
#elif DAQ_SOURCE == DAQ_SRC_SYNTHETIC
    (void)sample_index;
    uint32_t idx = (s_phase[ch] >> 16) & (SYNTH_LUT_LEN - 1);
    s_phase[ch] += s_phase_step;
    return s_lut[idx];
#else
    (void)ch; (void)sample_index;
    return 0;
#endif
}
