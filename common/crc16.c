/*
 * crc16.c  --  CRC-16/CCITT-FALSE, shared by firmware and host.
 *
 * Table-free bitwise implementation: small, portable, and identical on both
 * sides. At the block sizes used here (a few kB) the cost is negligible.
 */
#include "daq_packet.h"

uint16_t daq_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000u)
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}
