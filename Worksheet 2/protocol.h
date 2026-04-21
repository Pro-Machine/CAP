/**
 * protocol.h
 *
 * PicoComm Protocol - Custom Binary Communication Protocol
 * for the Raspberry Pi Pico W over USB Serial.
 *
 * Packet Structure:
 * +--------+--------+--------+--------+--------+----------+----------+
 * | MAGIC  | MAGIC  |  TYPE  |  SEQ   | LENGTH | PAYLOAD  | CHECKSUM |
 * | 0xCA   | 0xFE   | 1 byte | 1 byte | 2 bytes| N bytes  | 1 byte   |
 * +--------+--------+--------+--------+--------+----------+----------+
 *
 * Total overhead: 7 bytes per packet
 * Max payload:    256 bytes
 *
 * Features:
 *   - Magic bytes for frame synchronisation
 *   - Sequence numbers for ordering and duplicate detection
 *   - XOR checksum for error detection
 *   - ACK-based stop-and-wait reliability
 *   - Automatic retransmission (up to MAX_RETRIES attempts)
 *   - File transfer support (PKT_FILE / PKT_FILE_ACK)
 *
 * Author: Kaan Karadag-23083770
 * Date:   April 2026
 * Module: Communications and Protocols (UFCFVR-15-3)
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Protocol constants
 * ----------------------------------------------------------------------- */
#define PROTO_MAGIC_0      0xCA
#define PROTO_MAGIC_1      0xFE
#define PROTO_MAX_PAYLOAD  256
#define PROTO_HEADER_SIZE  6
#define PROTO_PACKET_SIZE  (PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD + 1)
#define PROTO_TIMEOUT_MS   3000
#define PROTO_MAX_RETRIES  3     /* Max retransmission attempts */

/* -----------------------------------------------------------------------
 * Packet types
 * ----------------------------------------------------------------------- */
typedef enum {
    PKT_DATA     = 0x01,  /* Data packet carrying a payload        */
    PKT_ACK      = 0x02,  /* Acknowledgement                       */
    PKT_PING     = 0x03,  /* Ping / keep-alive                     */
    PKT_PONG     = 0x04,  /* Pong response                         */
    PKT_ERROR    = 0x05,  /* Error / disconnect                    */
    PKT_FILE     = 0x06,  /* File transfer chunk                   */
    PKT_FILE_ACK = 0x07,  /* File chunk acknowledgement            */
} PacketType;

/* -----------------------------------------------------------------------
 * File transfer header (packed into payload of PKT_FILE)
 *
 * Layout of PKT_FILE payload:
 *   [chunk_index (2B)] [total_chunks (2B)] [filename (20B)] [data (rest)]
 * ----------------------------------------------------------------------- */
#define FILE_HEADER_SIZE   24   /* chunk_index(2) + total(2) + filename(20) */
#define FILE_CHUNK_SIZE    (PROTO_MAX_PAYLOAD - FILE_HEADER_SIZE)

typedef struct __attribute__((packed)) {
    uint16_t chunk_index;       /* Current chunk index (0-based)   */
    uint16_t total_chunks;      /* Total number of chunks          */
    char     filename[20];      /* Destination filename            */
    uint8_t  data[FILE_CHUNK_SIZE]; /* Chunk data                  */
} FileChunk;

/* -----------------------------------------------------------------------
 * Packet structure
 * ----------------------------------------------------------------------- */
typedef struct {
    uint8_t  type;
    uint8_t  seq;
    uint16_t length;
    uint8_t  payload[PROTO_MAX_PAYLOAD];
    uint8_t  checksum;
} Packet;

/* -----------------------------------------------------------------------
 * Statistics (tracked per session)
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t retransmissions;
    uint32_t checksum_errors;
    uint32_t timeouts;
} ProtoStats;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void protocol_init(void);
int  protocol_connect(void);
int  protocol_send(const void *data, int data_len);
int  protocol_receive(void *buffer, int buffer_len);
int  protocol_ping(void);
void protocol_disconnect(void);
void protocol_cleanup(void);
void protocol_print_stats(void);

/* File transfer */
int  protocol_receive_file(const char *save_path);

/* Low-level helpers (exposed for testing) */
uint8_t proto_checksum(const Packet *pkt);
bool    proto_encode(const Packet *pkt, uint8_t *buf, size_t *out_len);
bool    proto_decode(const uint8_t *buf, size_t buf_len, Packet *pkt);

#endif /* PROTOCOL_H */
