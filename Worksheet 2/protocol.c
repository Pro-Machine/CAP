/**
 * protocol.c
 *
 * PicoComm Protocol implementation for the Raspberry Pi Pico W.
 *
 * Key features:
 *   - Stop-and-wait reliability with ACK per packet
 *   - Automatic retransmission up to PROTO_MAX_RETRIES times
 *   - Mid-session PING handling
 *   - Inline file transfer via chunked PKT_FILE packets
 *   - Session statistics tracking
 *
 * Author: Kaan Karadag-23083770
 * Date:   April 2026
 * Module: Communications and Protocols (UFCFVR-15-3)
 */

#include "protocol.h"
#include "flash_ops.h"

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"

/* -----------------------------------------------------------------------
 * Internal state
 * ----------------------------------------------------------------------- */
static uint8_t    tx_seq = 0;
static ProtoStats stats  = {0};

/* -----------------------------------------------------------------------
 * Checksum
 * ----------------------------------------------------------------------- */
uint8_t proto_checksum(const Packet *pkt) {
    uint8_t cs = 0;
    cs ^= pkt->type;
    cs ^= pkt->seq;
    cs ^= (uint8_t)(pkt->length & 0xFF);
    cs ^= (uint8_t)(pkt->length >> 8);
    for (uint16_t i = 0; i < pkt->length; i++) cs ^= pkt->payload[i];
    return cs;
}

/* -----------------------------------------------------------------------
 * Encode / Decode
 * ----------------------------------------------------------------------- */
bool proto_encode(const Packet *pkt, uint8_t *buf, size_t *out_len) {
    if (pkt->length > PROTO_MAX_PAYLOAD) return false;
    size_t i = 0;
    buf[i++] = PROTO_MAGIC_0;
    buf[i++] = PROTO_MAGIC_1;
    buf[i++] = pkt->type;
    buf[i++] = pkt->seq;
    buf[i++] = (uint8_t)(pkt->length & 0xFF);
    buf[i++] = (uint8_t)(pkt->length >> 8);
    memcpy(buf + i, pkt->payload, pkt->length);
    i += pkt->length;
    buf[i++] = proto_checksum(pkt);
    *out_len = i;
    return true;
}

bool proto_decode(const uint8_t *buf, size_t buf_len, Packet *pkt) {
    if (buf_len < PROTO_HEADER_SIZE + 1) return false;
    if (buf[0] != PROTO_MAGIC_0 || buf[1] != PROTO_MAGIC_1) return false;
    pkt->type   = buf[2];
    pkt->seq    = buf[3];
    pkt->length = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    if (pkt->length > PROTO_MAX_PAYLOAD) return false;
    if (buf_len < (size_t)(PROTO_HEADER_SIZE + pkt->length + 1)) return false;
    memcpy(pkt->payload, buf + PROTO_HEADER_SIZE, pkt->length);
    uint8_t expected = proto_checksum(pkt);
    uint8_t received = buf[PROTO_HEADER_SIZE + pkt->length];
    if (expected != received) {
        stats.checksum_errors++;
        printf("[PROTO] Checksum mismatch: expected 0x%02X got 0x%02X\n",
               expected, received);
        return false;
    }
    return true;
}

/* -----------------------------------------------------------------------
 * Serial I/O
 * ----------------------------------------------------------------------- */
static void proto_write_bytes(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) putchar_raw(buf[i]);
}

static int proto_read_byte(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        int c = getchar_timeout_us(1000);
        if (c != PICO_ERROR_TIMEOUT) return c;
    }
    return -1;
}

static bool proto_recv_packet(Packet *pkt, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    uint8_t buf[PROTO_PACKET_SIZE];
    int b;

    while (!time_reached(deadline)) {
        b = proto_read_byte(50);
        if (b == PROTO_MAGIC_0) {
            b = proto_read_byte(100);
            if (b == PROTO_MAGIC_1) break;
        }
    }
    if (time_reached(deadline)) { stats.timeouts++; return false; }

    buf[0] = PROTO_MAGIC_0;
    buf[1] = PROTO_MAGIC_1;
    for (int i = 2; i < PROTO_HEADER_SIZE; i++) {
        b = proto_read_byte(500);
        if (b < 0) return false;
        buf[i] = (uint8_t)b;
    }

    uint16_t payload_len = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    if (payload_len > PROTO_MAX_PAYLOAD) return false;

    for (int i = 0; i < (int)payload_len + 1; i++) {
        b = proto_read_byte(500);
        if (b < 0) return false;
        buf[PROTO_HEADER_SIZE + i] = (uint8_t)b;
    }

    if (!proto_decode(buf, PROTO_HEADER_SIZE + payload_len + 1, pkt))
        return false;

    stats.packets_received++;
    return true;
}

static void proto_send_packet(Packet *pkt) {
    uint8_t buf[PROTO_PACKET_SIZE];
    size_t  len = 0;
    if (proto_encode(pkt, buf, &len)) {
        proto_write_bytes(buf, len);
        stats.packets_sent++;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void protocol_init(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);
    tx_seq = 0;
    memset(&stats, 0, sizeof(stats));
    printf("[PROTO] PicoComm initialised. Waiting for host...\n");
}

int protocol_connect(void) {
    printf("[PROTO] Waiting for PING...\n");
    Packet pkt;
    if (!proto_recv_packet(&pkt, 60000)) {
        printf("[PROTO] Connect timeout\n");
        return -1;
    }
    if (pkt.type != PKT_PING) {
        printf("[PROTO] Expected PING, got 0x%02X\n", pkt.type);
        return -1;
    }
    Packet pong;
    pong.type   = PKT_PONG;
    pong.seq    = pkt.seq;
    pong.length = 0;
    proto_send_packet(&pong);
    printf("[PROTO] Connected!\n");
    return 0;
}

int protocol_send(const void *data, int data_len) {
    if (!data || data_len <= 0 || data_len > PROTO_MAX_PAYLOAD) return -1;

    Packet pkt;
    pkt.type   = PKT_DATA;
    pkt.seq    = tx_seq++;
    pkt.length = (uint16_t)data_len;
    memcpy(pkt.payload, data, data_len);

    for (int attempt = 0; attempt <= PROTO_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            printf("[PROTO] Retransmit attempt %d for seq=%d\n", attempt, pkt.seq);
            stats.retransmissions++;
        }
        proto_send_packet(&pkt);
        Packet ack;
        if (!proto_recv_packet(&ack, PROTO_TIMEOUT_MS)) continue;
        if (ack.type == PKT_ACK && ack.seq == pkt.seq) {
            printf("[PROTO] Sent %d bytes (seq=%d) ACK'd\n", data_len, pkt.seq);
            return data_len;
        }
    }
    printf("[PROTO] Send failed after %d retries\n", PROTO_MAX_RETRIES);
    return -1;
}

/**
 * protocol_receive() - Receive a packet and handle all types.
 *
 * Returns:
 *   >0  : bytes received (DATA packet)
 *    0  : PING handled internally, call again
 *   -1  : timeout or error
 */
int protocol_receive(void *buffer, int buffer_len) {
    if (!buffer || buffer_len <= 0) return -1;

    Packet pkt;
    if (!proto_recv_packet(&pkt, PROTO_TIMEOUT_MS)) {
        printf("[PROTO] Receive timeout\n");
        return -1;
    }

    /* Handle mid-session PING */
    if (pkt.type == PKT_PING) {
        Packet pong;
        pong.type   = PKT_PONG;
        pong.seq    = pkt.seq;
        pong.length = 0;
        proto_send_packet(&pong);
        printf("[PROTO] Mid-session PING handled\n");
        return 0;
    }

    /* Handle file transfer inline */
    if (pkt.type == PKT_FILE) {
        printf("[PROTO] File transfer initiated\n");

        static uint8_t file_buf[FLASH_SECTOR_SIZE];
        memset(file_buf, 0, sizeof(file_buf));

        uint32_t total_bytes    = 0;
        uint16_t expected_total = 0;

        /* Process this first chunk */
        if (pkt.length >= FILE_HEADER_SIZE) {
            FileChunk *chunk  = (FileChunk *)pkt.payload;
            expected_total    = chunk->total_chunks;
            uint16_t data_sz  = pkt.length - FILE_HEADER_SIZE;
            memcpy(file_buf, chunk->data, data_sz);
            total_bytes = data_sz;

            /* ACK first chunk */
            Packet fa;
            fa.type   = PKT_FILE_ACK;
            fa.seq    = pkt.seq;
            fa.length = 0;
            proto_send_packet(&fa);
            printf("[PROTO] Chunk 1/%d received (%d bytes)\n",
                   expected_total, data_sz);

            /* Receive remaining chunks */
            for (uint16_t i = 1; i < expected_total; i++) {
                Packet cp;
                if (!proto_recv_packet(&cp, PROTO_TIMEOUT_MS * 2)) {
                    printf("[PROTO] Timeout waiting for chunk %d\n", i + 1);
                    break;
                }
                if (cp.type != PKT_FILE) break;

                FileChunk *c2  = (FileChunk *)cp.payload;
                uint16_t dsz   = cp.length - FILE_HEADER_SIZE;
                uint32_t offset = (uint32_t)c2->chunk_index * FILE_CHUNK_SIZE;

                if (offset + dsz <= sizeof(file_buf)) {
                    memcpy(file_buf + offset, c2->data, dsz);
                    total_bytes = offset + dsz;
                }

                Packet fa2;
                fa2.type   = PKT_FILE_ACK;
                fa2.seq    = cp.seq;
                fa2.length = 0;
                proto_send_packet(&fa2);
                printf("[PROTO] Chunk %d/%d received (%d bytes)\n",
                       i + 1, expected_total, dsz);
            }
        }

        /* Write to flash sector 11 */
        size_t wlen = ((total_bytes + 255) / 256) * 256;
        if (wlen > FLASH_SECTOR_SIZE) wlen = FLASH_SECTOR_SIZE;
        flash_write_safe(11 * FLASH_SECTOR_SIZE, file_buf, wlen);
        printf("[PROTO] File saved to flash (%lu bytes)\n",
               (unsigned long)total_bytes);

        return 0;
    }

    if (pkt.type != PKT_DATA) {
        printf("[PROTO] Expected DATA, got 0x%02X\n", pkt.type);
        return -1;
    }

    int to_copy = (pkt.length < (uint16_t)buffer_len)
                  ? (int)pkt.length : buffer_len;
    memcpy(buffer, pkt.payload, to_copy);

    Packet ack;
    ack.type   = PKT_ACK;
    ack.seq    = pkt.seq;
    ack.length = 0;
    proto_send_packet(&ack);

    printf("[PROTO] Received %d bytes (seq=%d)\n", to_copy, pkt.seq);
    return to_copy;
}

int protocol_ping(void) {
    Packet ping;
    ping.type   = PKT_PING;
    ping.seq    = tx_seq++;
    ping.length = 0;
    proto_send_packet(&ping);
    Packet pong;
    if (!proto_recv_packet(&pong, PROTO_TIMEOUT_MS)) {
        printf("[PROTO] PING timeout\n");
        return -1;
    }
    if (pong.type != PKT_PONG) return -1;
    printf("[PROTO] PING/PONG OK (seq=%d)\n", ping.seq);
    return 0;
}

void protocol_disconnect(void) {
    Packet err;
    err.type   = PKT_ERROR;
    err.seq    = tx_seq++;
    err.length = 0;
    proto_send_packet(&err);
    printf("[PROTO] Disconnect sent.\n");
}

void protocol_cleanup(void) {
    tx_seq = 0;
    printf("[PROTO] Cleaned up.\n");
}

void protocol_print_stats(void) {
    printf("\n--- PicoComm Session Stats ---\n");
    printf("  Packets sent     : %lu\n", (unsigned long)stats.packets_sent);
    printf("  Packets received : %lu\n", (unsigned long)stats.packets_received);
    printf("  Retransmissions  : %lu\n", (unsigned long)stats.retransmissions);
    printf("  Checksum errors  : %lu\n", (unsigned long)stats.checksum_errors);
    printf("  Timeouts         : %lu\n", (unsigned long)stats.timeouts);
    printf("------------------------------\n\n");
}

int protocol_receive_file(const char *save_path) {
    (void)save_path;
    /* File transfer is now handled inline in protocol_receive() */
    return 0;
}
