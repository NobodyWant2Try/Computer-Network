#include "util.h"
#include "rtp.h"
#include <cstring>
#include <stdlib.h>
#include <sys/socket.h>
#include <random>
#include <errno.h>

static uint32_t crc32_for_byte(uint32_t r) {
    for (int j = 0; j < 8; ++j) r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
    return r ^ (uint32_t)0xFF000000L;
}

static void crc32(const void* data, size_t n_bytes, uint32_t* crc) {
    static uint32_t table[0x100];
    if (!*table)
        for (size_t i = 0; i < 0x100; ++i) table[i] = crc32_for_byte(i);
    for (size_t i = 0; i < n_bytes; ++i)
        *crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
}

// Computes checksum for `n_bytes` of data
//
// Hint 1: Before computing the checksum, you should set everything up
// and set the "checksum" field to 0. And when checking if a packet
// has the correct check sum, don't forget to set the "checksum" field
// back to 0 before invoking this function.
//
// Hint 2: `len + sizeof(rtp_header_t)` is the real length of a rtp
// data packet.
uint32_t compute_checksum(const void* pkt, size_t n_bytes) {
    uint32_t crc = 0;
    crc32(pkt, n_bytes, &crc);
    return crc;
}

//初始时随机生成序列号
// uint32_t get_random(){
//     static std::random_device rd;
//     static std::mt19937 gen(rd());
//     static std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
//     return dis(gen);
// }

uint32_t get_random(){
    static std::random_device rd;
    static std::mt19937 gen(rd());
    // 限制范围在 0 到 2^30 - 1 (即 1073741823)
    static std::uniform_int_distribution<uint32_t> dis(0, (1<<30) - 1); 
    return dis(gen);
}


//检查 seq_num 是否在窗口内，进行防溢出处理
bool in_window(uint32_t seq_num, uint32_t base, uint32_t win_size){
    return (uint32_t)(seq_num - base) < win_size; 
}

//找 seq_num 在窗口内对应的索引
int get_idx(uint32_t base, uint32_t seq_num){
    return (uint32_t)(seq_num - base);
}

//建立一个数据包
void make_pkt(rtp_packet_t *pkt, const void *msg, uint32_t seq_num, uint16_t length, uint8_t flag){
    pkt->rtp.seq_num = seq_num % MOD;
    pkt->rtp.checksum = 0;
    pkt->rtp.length = length;
    pkt->rtp.flags = flag;
    if (length && msg){
        memcpy(pkt->payload, msg, length);
    }
    uint32_t sum = compute_checksum(pkt, sizeof(rtp_header_t) + length);
    pkt->rtp.checksum = sum;
    return ;
}

//发送时候错误直接退出
void rtp_send(int sockfd, rtp_packet_t send_pkt, uint16_t length, struct sockaddr *receiver_add, socklen_t addr_len){
    ssize_t tmp = sendto(sockfd, &send_pkt, sizeof(rtp_header_t) + length, 0, receiver_add, addr_len);
    if (tmp <= 0){
        LOG_FATAL("Error, failed to send packet!\n");
    }
    else if(tmp != ssize_t(sizeof(rtp_header_t) + length)){
        LOG_FATAL("Error, failed to send expected length packet!\n");
    }
    return ;
}

//接收时候错误可能是因为丢包，只接收完全正确的数据包
rtp_packet_t *rtp_recv(int sockfd, uint8_t expected_flag, struct sockaddr *sender, socklen_t *addr_len, int *state){
    rtp_packet_t *receive_pkt = (rtp_packet_t *)malloc(sizeof(rtp_header_t) + PAYLOAD_MAX);
    *state = RCV_OK;
    errno = 0;
    if (!receive_pkt){
        LOG_FATAL("Error, failed to allocate memory!\n");
    }
    ssize_t tmp = recvfrom(sockfd, receive_pkt, sizeof(rtp_header_t) + PAYLOAD_MAX, 0, sender, addr_len);
    if (tmp <= 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *state = RCV_TIMEOUT;
            LOG_DEBUG("Timeout when receive packet\n");
        }
        else{
            *state = RCV_ERROR;
            LOG_DEBUG("Error, failed to receive packet!\n");
        }
        return NULL;
    }
    else if ((size_t)tmp < sizeof(rtp_header_t)){
        free(receive_pkt);
        *state = RCV_INCOMPLETE;
        LOG_DEBUG("Error, failed to receive a complete packet!\n");
        return NULL;
    }
    uint16_t length = receive_pkt->rtp.length;
    if (length > PAYLOAD_MAX){
        free(receive_pkt);
        *state = RCV_BAD_LENGTH;
        LOG_DEBUG("Error, failed to receive a legal length %d of payload!\n", length);
        return NULL;
    }
    if ((size_t)tmp != sizeof(rtp_header_t) + (size_t)length){
        free(receive_pkt);
        *state = RCV_INCOMPLETE;
        LOG_DEBUG("Error, failed to receive a packet with expected length!\n");
        return NULL;
    }
    uint32_t expected_checksum = receive_pkt->rtp.checksum;
    receive_pkt->rtp.checksum = 0;
    uint32_t checksum = compute_checksum((void*)receive_pkt, sizeof(rtp_header_t) + length);
    if (expected_checksum != checksum){
        free(receive_pkt);
        *state = RCV_BAD_CHECKSUM;
        LOG_DEBUG("Receive a packet with wrong checksum!\n");
        return NULL;
    }
    if (receive_pkt->rtp.flags != expected_flag){
        *state = RCV_BAD_FLAG;
        LOG_DEBUG("Receive a packet with wrong flag! got=%u expected=%u\n",
        receive_pkt->rtp.flags, expected_flag);
    }
    receive_pkt->rtp.checksum = expected_checksum;
    return receive_pkt;
}
