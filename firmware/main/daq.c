/*
 * daq.c  --  Deterministic acquisition pipeline.
 *
 * Timing model
 * ------------
 *   GPTimer (1 MHz, auto-reload) raises an alarm every 1/DAQ_FS_HZ seconds.
 *   Its ISR reads the authoritative esp_timer tick time and queues it to the
 *   sampler task. The hardware timer, not software, sets the cadence, so the
 *   sampling instants are defined by the timer peripheral.
 *
 *   The sampler task (pinned to core 1, top priority) consumes one tick at a
 *   time, reads every channel, and fills the active block buffer. On a full
 *   block it stamps t_first / t_last from the ISR tick times, hands the buffer
 *   to the TX task, and swaps buffers.
 *
 *   The TX task (core 0) serialises header + payload + CRC into one wire frame
 *   and writes it to the UART. Decoupling TX from sampling keeps UART activity
 *   off the acquisition path.
 *
 * All block state lives in the single sampler task, so there are no shared-
 * buffer races; the ISR only ever pushes a timestamp onto a queue.
 */
#include "daq.h"
#include "config.h"
#include "signal_source.h"
#include "daq_packet.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gptimer.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "daq";

#define TIMER_RES_HZ 1000000ULL   /* 1 us tick */
#define N_CH         DAQ_N_CHANNELS
#define BLK          DAQ_BLOCK_SAMPLES

/* Two acquisition buffers, interleaved by sample: [s0c0 s0c1 s1c0 s1c1 ...]. */
static int16_t  s_buf[2][N_CH * BLK];

typedef struct {
    uint8_t  idx;        /* which s_buf is being handed over */
    uint16_t seq;
    uint64_t t_first_us;
    uint64_t t_last_us;
} block_msg_t;

static QueueHandle_t s_tick_q;   /* ISR -> sampler: int64_t tick times   */
static QueueHandle_t s_tx_q;     /* sampler -> TX: block_msg_t            */
static gptimer_handle_t s_timer;

/* ---- GPTimer ISR: minimal, IRAM-resident -------------------------------- */
static bool IRAM_ATTR on_alarm(gptimer_handle_t timer,
                               const gptimer_alarm_event_data_t *edata,
                               void *user)
{
    (void)timer; (void)user;
    BaseType_t hpw = pdFALSE;
    int64_t now = esp_timer_get_time();
    /* Drop silently if the sampler ever falls behind; the host detects the
     * resulting sample-count/seq gap. Overrun is a measurable condition, not
     * a crash. */
    xQueueSendFromISR(s_tick_q, &now, &hpw);
    return hpw == pdTRUE;
}

/* ---- Sampler task ------------------------------------------------------- */
static void sampler_task(void *arg)
{
    (void)arg;
    uint8_t  active = 0;
    uint32_t widx = 0;
    uint16_t seq = 0;
    uint64_t t_first = 0;

    for (;;) {
        int64_t t_tick;
        if (xQueueReceive(s_tick_q, &t_tick, portMAX_DELAY) != pdTRUE)
            continue;

        int16_t *dst = &s_buf[active][widx * N_CH];
        for (uint32_t ch = 0; ch < N_CH; ++ch)
            dst[ch] = signal_source_read(ch, widx);

        if (widx == 0)
            t_first = (uint64_t)t_tick;

        if (++widx == BLK) {
            block_msg_t msg = {
                .idx = active,
                .seq = seq++,
                .t_first_us = t_first,
                .t_last_us = (uint64_t)t_tick,
            };
            /* Block here rather than drop: TX is far faster than one block
             * period, so this only serialises buffer reuse. */
            xQueueSend(s_tx_q, &msg, portMAX_DELAY);
            active ^= 1;
            widx = 0;
        }
    }
}

/* ---- TX task ------------------------------------------------------------ */
static void tx_task(void *arg)
{
    (void)arg;
    static uint8_t wire[DAQ_HEADER_SIZE + N_CH * BLK * sizeof(int16_t) + sizeof(uint16_t)];
    const size_t payload_bytes = (size_t)N_CH * BLK * sizeof(int16_t);
    const size_t crc_off = DAQ_HEADER_SIZE + payload_bytes;

    for (;;) {
        block_msg_t msg;
        if (xQueueReceive(s_tx_q, &msg, portMAX_DELAY) != pdTRUE)
            continue;

        daq_header_t *h = (daq_header_t *)wire;
        h->magic      = DAQ_MAGIC;
        h->version    = DAQ_PROTO_VER;
        h->source     = signal_source_kind();
        h->seq        = msg.seq;
        h->n_channels = N_CH;
        h->n_samples  = BLK;
        h->fs_hz      = DAQ_FS_HZ;
        h->t_first_us = msg.t_first_us;
        h->t_last_us  = msg.t_last_us;

        memcpy(wire + DAQ_HEADER_SIZE, s_buf[msg.idx], payload_bytes);

        uint16_t crc = daq_crc16(wire, crc_off);
        wire[crc_off]     = (uint8_t)(crc & 0xFF);
        wire[crc_off + 1] = (uint8_t)(crc >> 8);

        uart_write_bytes(DAQ_UART_NUM, (const char *)wire, crc_off + 2);
    }
}

/* ---- Bring-up ----------------------------------------------------------- */
static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = DAQ_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(DAQ_UART_NUM, 2048, 4096, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(DAQ_UART_NUM, &cfg));
}

static void timer_init(void)
{
    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = TIMER_RES_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &s_timer));

    gptimer_event_callbacks_t cbs = { .on_alarm = on_alarm };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));

    gptimer_alarm_config_t acfg = {
        .reload_count = 0,
        .alarm_count = TIMER_RES_HZ / DAQ_FS_HZ,  /* ticks per sample period */
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &acfg));
}

void daq_start(void)
{
    s_tick_q = xQueueCreate(8, sizeof(int64_t));
    s_tx_q   = xQueueCreate(2, sizeof(block_msg_t));
    configASSERT(s_tick_q && s_tx_q);

    signal_source_init();
    uart_init();
    timer_init();

    ESP_LOGI(TAG, "starting: %u ch @ %u Hz, block %u samples (%.1f ms)",
             (unsigned)N_CH, (unsigned)DAQ_FS_HZ, (unsigned)BLK,
             1000.0 * BLK / DAQ_FS_HZ);

    /* Binary stream begins now; silence the log UART so it cannot interleave. */
    esp_log_level_set("*", ESP_LOG_NONE);

    xTaskCreatePinnedToCore(tx_task, "daq_tx", 4096, NULL,
                            configMAX_PRIORITIES - 3, NULL, DAQ_TX_CORE);
    xTaskCreatePinnedToCore(sampler_task, "daq_sampler", 4096, NULL,
                            configMAX_PRIORITIES - 1, NULL, DAQ_SAMPLER_CORE);

    ESP_ERROR_CHECK(gptimer_start(s_timer));
}
