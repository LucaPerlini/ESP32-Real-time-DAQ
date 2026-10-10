/*
 * daq_packet.h  --  Wire format shared by the ESP32 firmware and the host tools.
 *
 * This single header is the contract between the two sides of the system.
 * Both the firmware and the host receiver include it so the binary layout
 * can never drift between them.
 *
 * Framing on the wire (one block per hardware acquisition buffer):
 *
 *   +--------------------------------------------------------------+
 *   | daq_header_t  (fixed size, little-endian)                    |
 *   +--------------------------------------------------------------+
 *   | int16_t  samples[n_channels * n_samples]  (channel-interleaved)|
 *   +--------------------------------------------------------------+
 *   | uint16_t crc16  (CRC-16/CCITT-FALSE over header + payload)    |
 *   +--------------------------------------------------------------+
 *
 * The receiver resynchronises byte-by-byte on the magic word and validates
 * every block with the CRC, so a corrupted or partial block is dropped
 * rather than silently decoded.
 */
#ifndef DAQ_PACKET_H
#define DAQ_PACKET_H

#include <stdint.h>
#include <stddef.h>

#define DAQ_MAGIC        0xDA51u   /* block sync word */
#define DAQ_PROTO_VER    1u
#define DAQ_MAX_CHANNELS 4u
#define DAQ_MAX_SAMPLES  512u      /* samples per channel per block */

/* Signal source reported by the firmware, so the host can label captures. */
enum daq_source {
    DAQ_SRC_ADC_LOOPBACK = 0,  /* DAC output physically wired back to an ADC pin */
    DAQ_SRC_ADC_EXTERNAL = 1,  /* ADC pin driven by an external signal           */
    DAQ_SRC_SYNTHETIC    = 2,  /* samples generated in firmware, ADC not used    */
};

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;        /* DAQ_MAGIC                                         */
    uint8_t  version;      /* DAQ_PROTO_VER                                     */
    uint8_t  source;       /* enum daq_source                                  */
    uint16_t seq;          /* rolling block counter, wraps at 65535 (loss flag) */
    uint16_t n_channels;   /* channels present in this block                   */
    uint16_t n_samples;    /* samples per channel in this block                */
    uint32_t fs_hz;        /* configured per-channel sample rate, Hz           */
    uint64_t t_first_us;   /* device esp_timer time of the first sample, us    */
    uint64_t t_last_us;    /* device esp_timer time of the last sample, us     */
} daq_header_t;
#pragma pack(pop)

#define DAQ_HEADER_SIZE ((size_t)sizeof(daq_header_t))  /* 30 bytes */

/* Byte length of a complete block for the given geometry. */
static inline size_t daq_block_size(uint16_t n_channels, uint16_t n_samples)
{
    return DAQ_HEADER_SIZE
         + (size_t)n_channels * (size_t)n_samples * sizeof(int16_t)
         + sizeof(uint16_t); /* trailing CRC */
}

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor. */
uint16_t daq_crc16(const uint8_t *data, size_t len);

#endif /* DAQ_PACKET_H */
