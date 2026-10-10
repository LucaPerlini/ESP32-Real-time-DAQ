/*
 * app_main.c  --  Entry point for the ESP32 real-time DAQ node.
 *
 * Streams channel-interleaved, hardware-timed samples over the CP2102 USB-UART
 * as framed binary blocks. See protocol/packet_format.md and the top-level
 * README for the wire format and the measurement method.
 */
#include "daq.h"
#include "esp_log.h"

void app_main(void)
{
    ESP_LOGI("main", "esp32-rt-daq node booting");
    daq_start();
    /* daq_start() leaves the pipeline running under the hardware timer;
     * nothing further is required on the main task. */
}
