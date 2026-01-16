#include "rtp.h"
#include "util.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>


void handshake(int sockfd);
void GBN(int sockfd);
void SR(int sockfd);
void disconnect(int sockfd);
void write_data(const char *buffer, uint16_t length);


static char listenProt[MAX_LENGTH] = {0};
static char file_path[MAX_LENGTH] = {0};
static int window_size;
static int mode;
struct sockaddr_in receiver_addr;
struct sockaddr_in sender_addr;
uint32_t next_seq_num;
socklen_t addr_len;
FILE *fp;

char buffer[MAX_LENGTH] = {0};

int main(int argc, char **argv) {
    if (argc != 5) {
        // [listen port] [file path] [window size] [mode]
        LOG_FATAL("Usage: ./receiver [listen port] [file path] [window size] "
                  "[mode]\n");
        
    }

    // your code here
    
    // 处理命令行输入

    strncpy(listenProt, argv[1], MAX_LENGTH - 1);
    strncpy(file_path, argv[2], MAX_LENGTH - 1);
    window_size = std::atoi(argv[3]);
    mode = std::atoi(argv[4]);
    const int port = std::atoi(listenProt);

    // 初始化 Receiver 端

    int listen_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_fd < 0){
        LOG_FATAL("Error, failed to create socket!\n");
    }
    bzero(&receiver_addr, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    receiver_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) < 0){
        LOG_FATAL("Error, failed to bind socket!\n");
    }

    fp = fopen(file_path, "wb");
    if (!fp){
        LOG_FATAL("Error, failed to open file!\n");
    }

    // 建立连接

    handshake(listen_fd);

    // 接收数据

    if (mode == 0){
        GBN(listen_fd);
    }
    else if (mode == 1){
        SR(listen_fd);
    }
    else {
        LOG_FATAL("Error, failed to recognize mode!\n");
    }
    if (fclose(fp) < 0){
        LOG_FATAL("Error, failed to close file!\n");
    }

    // 终止连接

    disconnect(listen_fd);

    LOG_DEBUG("Receiver: exiting...\n");
    return 0;
}

void handshake(int sockfd){
    bzero(&sender_addr, sizeof(sender_addr));
    addr_len = sizeof(sender_addr);
    struct sockaddr_in temp_addr;
    socklen_t temp_addr_len = sizeof(temp_addr);
    //第一次握手 等待接收 SYN
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    rtp_packet_t *recv_pkt = NULL;
    while(true){
        int state = 0;
        recv_pkt = rtp_recv(sockfd, RTP_SYN, (struct sockaddr *)&sender_addr, &addr_len, &state);
        //收到 SYN 报文
        if(recv_pkt && state == RCV_OK){
            LOG_MSG("Received SYN from sender.\n");
            next_seq_num = (recv_pkt->rtp.seq_num + 1) % MOD;
            free(recv_pkt);
            break;
        }
        //超时
        else if (!recv_pkt && state == RCV_TIMEOUT){
            free(recv_pkt);
            LOG_FATAL("Error, failed to connect with sender!\n");
        }
        //收到错误报文
        else{
            free(recv_pkt);
            continue;
        }
    }

    //第二次握手 发送 SYN 和 ACK
    //第三次握手 等待接收 ACK
    tv.tv_sec = 0;
    tv.tv_usec = 100000; //100ms
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    int cnt = 0;
    rtp_packet_t syn_ack_pkt;
    memset(&syn_ack_pkt, 0, sizeof(syn_ack_pkt));
    make_pkt(&syn_ack_pkt, NULL, next_seq_num, 0, RTP_SYN | RTP_ACK);
    recv_pkt = NULL;
    while (true){
        int state = 0;
        rtp_send(sockfd, syn_ack_pkt, 0, (struct sockaddr *)&sender_addr, addr_len);
        cnt++;
        recv_pkt = rtp_recv(sockfd, RTP_ACK, (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
        //收到正确报文 建立连接
        if (recv_pkt && state == RCV_OK && recv_pkt->rtp.seq_num == next_seq_num){
            LOG_MSG("Success to establish connection.\n");
            free(recv_pkt);
            break;
        }
        //发送次数超限 终止
        if (cnt == MAX_RETRY + 1) {
            LOG_FATAL("Error, failed to receive ACK from sender!\n");
        }
        //超时或者接收错误报文 重发
        else{
            continue;
        }
    }
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    return ;
}

void GBN(int sockfd){
    //ACK seq_num 代表希望接收到的下一个报文编号
    ReceiverGBNWindow *win = (ReceiverGBNWindow *)malloc(sizeof(ReceiverGBNWindow));
    struct sockaddr_in temp_addr;
    socklen_t temp_addr_len = sizeof(temp_addr);
    win->base = next_seq_num;
    win->size = window_size;
    struct timeval tv;
    tv.tv_sec = 5; //5s
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    rtp_packet_t *recv_pkt = NULL;
    rtp_packet_t ack_pkt;
    memset(&ack_pkt, 0, sizeof(ack_pkt));
    make_pkt(&ack_pkt, NULL, next_seq_num, 0, RTP_ACK);
    while (true){
        int state = 0;
        recv_pkt = rtp_recv(sockfd, 0, (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
        
        //超过 5s 没收到包 连接断开
        if (!recv_pkt && state == RCV_TIMEOUT){
            free(recv_pkt);
            LOG_FATAL("Error, loss connection when receive data!\n");
        }
        
        //数据包有问题 or 编号不再窗口内 重发 ACK
        else if ((!recv_pkt && state != RCV_TIMEOUT) || (recv_pkt && state == RCV_BAD_FLAG && recv_pkt->rtp.flags != RTP_FIN) || (recv_pkt && state == RCV_OK && !in_window(recv_pkt->rtp.seq_num, win->base, 1))){
            rtp_send(sockfd, ack_pkt, 0, (struct sockaddr *)&sender_addr, addr_len);
            free(recv_pkt);
            continue;
        }
        
        //收到期望报文 更新 next_seq_num 发送 ACK
        else if (recv_pkt && state == RCV_OK && in_window(recv_pkt->rtp.seq_num, win->base, 1)){
            write_data(recv_pkt->payload, recv_pkt->rtp.length);
            next_seq_num = (recv_pkt->rtp.seq_num + 1) % MOD;
            win->base = (win->base + 1) % MOD;
            make_pkt(&ack_pkt, NULL, next_seq_num, 0, RTP_ACK);
            rtp_send(sockfd, ack_pkt, 0, (struct sockaddr *)&sender_addr, addr_len);
            // LOG_DEBUG("Receiver got data and send ACK.\n");
            free(recv_pkt);
            continue;
        }
        
        //收到 FIN 跳出循环，准备挥手
        else if (recv_pkt && state == RCV_BAD_FLAG && recv_pkt->rtp.flags == RTP_FIN){
            next_seq_num = recv_pkt->rtp.seq_num;
            free(recv_pkt);
            LOG_MSG("Receiver get FIN packet.\n");
            break;
        }
        else{
            free(recv_pkt);
            LOG_FATAL("Error, unexpected condition!\n");
        }
    }
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    return ;
}

void SR(int sockfd){
    //ACK seq_num 代表收到的报文编号
    ReceiverSRWindow *win = (ReceiverSRWindow *)malloc(sizeof(ReceiverSRWindow));
    struct sockaddr_in temp_addr;
    socklen_t temp_addr_len = sizeof(temp_addr);
    win->base = next_seq_num;
    win->size = window_size;
    struct timeval tv;
    tv.tv_sec = 5; //5s
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    rtp_packet_t *recv_pkt = NULL;
    rtp_packet_t ack_pkt;
    memset(&ack_pkt, 0, sizeof(ack_pkt));
    while(true){
        int state = 0;
        recv_pkt = rtp_recv(sockfd, 0, (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
    
        //超过 5s 没收到包 连接断开
        if (!recv_pkt && state == RCV_TIMEOUT){
            free(recv_pkt);
            LOG_FATAL("Error, loss connection when receive data!\n");
        }

        //接收到窗口内的报文 确认报文 判断要缓存还是写入数据 发送 ACK
        else if (recv_pkt && state == RCV_OK && in_window(recv_pkt->rtp.seq_num, win->base, win->size)){
            int win_idx = get_idx(win->base, recv_pkt->rtp.seq_num);
            if (win->received[win_idx]) {//已经收到了 回复 ACK
                memset(&ack_pkt, 0, sizeof(ack_pkt));
                make_pkt(&ack_pkt, NULL, recv_pkt->rtp.seq_num, 0, RTP_ACK);
                rtp_send(sockfd, ack_pkt, 0, (struct sockaddr *)&sender_addr, addr_len);
                free(recv_pkt);
                continue;
            }
            win->payload[win_idx] = (char *)malloc(recv_pkt->rtp.length);
            memcpy(win->payload[win_idx], recv_pkt->payload, recv_pkt->rtp.length);
            win->lengths[win_idx] = recv_pkt->rtp.length;
            win->received[win_idx] = true;
            memset(&ack_pkt, 0, sizeof(ack_pkt));
            make_pkt(&ack_pkt, NULL, recv_pkt->rtp.seq_num, 0, RTP_ACK);
            rtp_send(sockfd, ack_pkt, 0, (struct sockaddr *)&sender_addr, addr_len);
            free(recv_pkt);
            
            //滑动窗口
            while(win->received[0]){
                write_data(win->payload[0], win->lengths[0]);
                free(win->payload[0]);
                for (int i = 0; i < win->size - 1; i++){
                    win->payload[i] = win->payload[i + 1];
                    win->lengths[i] = win->lengths[i + 1];
                    win->received[i] = win->received[i + 1];
                }
                win->payload[win->size - 1] = NULL;
                win->received[win->size - 1] = false;
                win->lengths[win->size - 1] = 0;
                win->base = (win->base + 1) % MOD;
            }
            continue;
        }

        //收到[base-size,base-1]报文 发送确认 ACK
        else if (recv_pkt && state == RCV_OK && in_window(recv_pkt->rtp.seq_num, win->base - win->size, win->size)){
            memset(&ack_pkt, 0, sizeof(ack_pkt));
            make_pkt(&ack_pkt, NULL, recv_pkt->rtp.seq_num, 0, RTP_ACK);
            rtp_send(sockfd, ack_pkt, 0, (struct sockaddr *)&sender_addr, addr_len);
            free(recv_pkt);
            continue;
        }

        //收到 FIN 跳出循环 准备挥手
        else if (recv_pkt && state == RCV_BAD_FLAG && recv_pkt->rtp.flags == RTP_FIN){
            next_seq_num = recv_pkt->rtp.seq_num;
            free(recv_pkt);
            break;
        }
        //其他 忽略数据
        else{
            free(recv_pkt);
            continue;
        }
    }
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    return ;
}

void disconnect(int sockfd){
    //收到了 FIN
    //第二次挥手 发送 FIN ACK
    struct timeval tv;
    tv.tv_sec = 2; //2s 未收到 FIN 表示连接中断
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    rtp_packet_t fin_ack_pkt;
    memset(&fin_ack_pkt, 0, sizeof(fin_ack_pkt));
    make_pkt(&fin_ack_pkt, NULL, next_seq_num, 0, RTP_FIN | RTP_ACK);
    rtp_send(sockfd, fin_ack_pkt, 0, (struct sockaddr *)&sender_addr, addr_len);
    rtp_packet_t *recv_pkt = NULL;
    struct sockaddr_in temp_addr;
    socklen_t temp_addr_len = sizeof(temp_addr);
    while(true){
        int state = 0;
        recv_pkt = rtp_recv(sockfd, RTP_FIN, (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
        //超时 连接终止
        if (!recv_pkt && state == RCV_TIMEOUT){
            free(recv_pkt);
            LOG_MSG("Success to disconnect.\n");
            break;
        }
        //收到再次 FIN 说明 Sender 没有收到 FIN ACK
        else if (recv_pkt && state == RCV_OK && recv_pkt->rtp.seq_num == next_seq_num){
            rtp_send(sockfd, fin_ack_pkt, 0, (struct sockaddr *)&sender_addr, addr_len);
            continue;
        }
        //不再处理其他类型报文 报文错误
        else{
            continue;
        }
    }
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    return ;
}

void write_data(const char *buffer, uint16_t length){
    fwrite(buffer, 1, length, fp);
    return ;
}