#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "rtp.h"

#define MAX_WINDOW 20000
#define MAX_LENGTH 1500
#define MAX_RETRY 50
#define RCV_OK 0
#define RCV_TIMEOUT 1
#define RCV_ERROR 2
#define RCV_INCOMPLETE 3
#define RCV_BAD_LENGTH 4
#define RCV_BAD_CHECKSUM 5
#define RCV_BAD_FLAG 6
#define MOD (1073741824)

uint32_t compute_checksum(const void *pkt, size_t n_bytes);

// Use it to display a help message
#define LOG_MSG(...)                                                    \
    do {                                                                \
        fprintf(stdout, "\033[40;32m[ INFO     ] \033[0m" __VA_ARGS__); \
        fflush(stdout);                                                 \
    } while (0)

// Use it to display debug information. Turn it on/off in CMakeLists.txt
#ifdef LDEBUG
#define LOG_DEBUG(...)                                                  \
    do {                                                                \
        fprintf(stderr, "\033[40;33m[ DEBUG    ] \033[0m" __VA_ARGS__); \
        fflush(stderr);                                                 \
    } while (0)
#else
#define LOG_DEBUG(...)
#endif

// Use it when an unrecoverable error happened
#define LOG_FATAL(...)                                                  \
    do {                                                                \
        fprintf(stderr, "\033[40;31m[ FATAL    ] \033[0m" __VA_ARGS__); \
        fflush(stderr);                                                 \
        exit(1);                                                        \
    } while (0)

#ifdef __cplusplus
}
#endif

struct SenderGBNWindow{
    uint32_t base;
    uint32_t next;
    int size;
};

struct ReceiverGBNWindow{
    uint32_t base;
    int size;
};

struct SenderSRWindow{
    uint32_t base;
    uint32_t next;
    int size;

    bool ACKed[MAX_WINDOW + 5] = {0}; //是否收到ACK
};

struct ReceiverSRWindow{
    uint32_t base;
    int size;

    bool received[MAX_WINDOW + 5] = {0}; //是否接收到某seq
    char *payload[MAX_WINDOW + 5] = {0}; //缓存数据
    uint16_t lengths[MAX_WINDOW + 5] = {0}; //数据长度
};

uint32_t get_random();

int get_idx(uint32_t base, uint32_t seq_num);

bool in_window(uint32_t seq_num, uint32_t base, uint32_t win_size);

void make_pkt(rtp_packet_t *pkt, const void *msg, uint32_t seq_num, uint16_t length, uint8_t flag);

void rtp_send(int sockfd, rtp_packet_t send_pkt, uint16_t length, struct sockaddr *receiver_add, socklen_t addr_len);
 
rtp_packet_t *rtp_recv(int sockfd, uint8_t expected_flag, struct sockaddr *sender, socklen_t *addr_len, int *state);

#endif
