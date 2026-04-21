<<<<<<< HEAD
=======
/**
 * main.c
 *
 * PicoComm demo application.
 *
 * Connects to the Python host, then enters a loop:
 *   1. Receives a message from Python
 *   2. Prints it
 *   3. Sends back an echo reply with "ECHO: " prefix
 *
 * Author: Kaan Karadag-23083770
 * Date:   April 2026
 * Module: Communications and Protocols (UFCFVR-15-3)
 */

>>>>>>> b8c3f796c339d9b95845d7b1a2e8f8b23072a644
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "protocol.h"

int main() {
    protocol_init();

    while (1) {
        if (protocol_connect() != 0) {
            sleep_ms(1000);
            continue;
        }

        printf("[MAIN] Connected!\n");
        uint8_t buf[PROTO_MAX_PAYLOAD];

        while (1) {
            int n = protocol_receive(buf, sizeof(buf) - 1);

            if (n > 0) {
                /* DATA packet - echo back */
                buf[n] = '\0';
                printf("[MAIN] Received: %s\n", (char *)buf);
                char reply[PROTO_MAX_PAYLOAD];
                int rlen = snprintf(reply, sizeof(reply),
                                    "ECHO: %.*s", n, (char *)buf);
                protocol_send(reply, rlen);

            } else if (n == 0) {
                /* PING or FILE handled internally - just continue */
                continue;

            } else {
                /* -1 = timeout/error - but keep trying, don't disconnect */
                printf("[MAIN] Waiting...\n");
                sleep_ms(10);
            }
        }
    }
    return 0;
}