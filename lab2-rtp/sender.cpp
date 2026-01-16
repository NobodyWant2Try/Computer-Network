#include "rtp.h"
#include "util.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <vector>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>


void handshake(int sockfd);
void GBN(int sockfd);
void SR(int sockfd);
void disconnect(int sockfd);
void start_timer();
void stop_timer();
long timer_expired();

static char receiverIP[MAX_LENGTH] = {0};
static char receiverPort[MAX_LENGTH] = {0};
static char file_path[MAX_LENGTH] = {0};
static int window_size;
static int mode;
static bool timer_running = false;
static struct timespec timer;
struct sockaddr_in receive_addr;
uint32_t init_seq_num;
uint32_t next_seq_num;
uint32_t start_seq_num;
uint32_t end_seq_num;
std::vector<rtp_packet_t> send_pkts;
char buffer[MAX_LENGTH] = {0};


int main(int argc, char **argv) {
    if (argc != 6) {
        //[receiver ip] [receiver port] [file path] [window size] [mode]
        LOG_FATAL("Usage: ./sender [receiver ip] [receiver port] [file path] "
                  "[window size] [mode]\n");
    }

    // your code here

    // 处理命令行输入
    
    strncpy(receiverIP, argv[1], MAX_LENGTH - 1);
    strncpy(receiverPort, argv[2], MAX_LENGTH - 1);
    strncpy(file_path, argv[3], MAX_LENGTH - 1);
    window_size = std::atoi(argv[4]);
    mode = std::atoi(argv[5]);
    const int port = std::atoi(receiverPort);

    // 初始化 Sender 端
    
    int send_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_fd < 0){
        LOG_FATAL("Error, failed to create socket!\n");
    }
    bzero(&receive_addr, sizeof(receive_addr));
    receive_addr.sin_family = AF_INET;
    receive_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, receiverIP, &receive_addr.sin_addr) <= 0){
        LOG_FATAL("Error, failed to transfer Receiver IP address!\n");
        return 0;
    }


    init_seq_num = get_random();
    // init_seq_num = (1 << 30) - 1; 测试极端情况

    FILE *fp = fopen(file_path, "r");
    if (!fp){
        LOG_FATAL("Error, failed to open file!\n");
    }
    next_seq_num = (init_seq_num + 1) % MOD; //下一个发送数据包 seq_num
    start_seq_num = (init_seq_num + 1) % MOD; //第一个数据包 seq_num=x+1
    end_seq_num = start_seq_num;

    while (!feof(fp)){
        rtp_packet_t send_pkt;
        memset(buffer, 0, sizeof(buffer));
        size_t length = fread(buffer, sizeof(char), PAYLOAD_MAX, fp);
        if (length < 0){
            fclose(fp);
            LOG_FATAL("Error, failed to read file!\n");
        }
        make_pkt(&send_pkt, buffer, end_seq_num, (uint16_t)length, 0);
        // LOG_DEBUG("build send_pkt seq=%u length=%zu\n", end_seq_num, length);
        end_seq_num = (end_seq_num + 1) % MOD;
        send_pkts.push_back(send_pkt);
    }

    if(fclose(fp) < 0){
        LOG_FATAL("Error, failed to close file!\n");
    }

    // 建立连接
    
    handshake(send_fd);

    // 传输数据

    if (mode == 0){
        GBN(send_fd);
    }
    else if (mode == 1){
        SR(send_fd);
    }
    else {
        LOG_FATAL("Error, failed to recognize mode!\n");
    }

    // 终止连接

    disconnect(send_fd);

    LOG_DEBUG("Sender: exiting...\n");
    return 0;
}

void handshake(int sockfd){
    //第一次握手 发送 SYN
    rtp_packet_t  syn_pkt;
    memset(&syn_pkt, 0, sizeof(syn_pkt));
    make_pkt(&syn_pkt, NULL, init_seq_num, 0, RTP_SYN);
    socklen_t addr_len = sizeof(receive_addr); 
    //将发送信息步骤与第二次握手写在一起
    struct sockaddr_in temp_addr;
    socklen_t temp_addr_len = sizeof(temp_addr);
    //第二次握手 等待 Receiver 的 SYN 和 ACK
    int cnt = 0;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; //100ms
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    rtp_send(sockfd, syn_pkt, 0, (struct sockaddr *)&receive_addr, addr_len);
    LOG_DEBUG("Sent SYN seq=%u\n", init_seq_num);
    rtp_packet_t *recv_pkt = NULL;
    while(true){
        int state = 0;
        recv_pkt = rtp_recv(sockfd, (RTP_SYN | RTP_ACK), (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
        if (recv_pkt && state == RCV_OK && recv_pkt->rtp.seq_num == (init_seq_num + 1) % MOD){
            LOG_MSG("Received SYN and ACK from receiver.\n");
            free(recv_pkt);
            break;
        }
        else if (!recv_pkt && state == RCV_TIMEOUT){
            rtp_send(sockfd, syn_pkt, 0, (struct sockaddr *)&receive_addr, addr_len);
            LOG_DEBUG("Sent SYN seq=%u\n", init_seq_num);
            cnt++;
        }
        else {
            free(recv_pkt);
        }
        if (cnt == MAX_RETRY) break;
    }
    //防止影响后续接收
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    if (cnt == MAX_RETRY){
        LOG_FATAL("Error, failed to receive SYN and ACK from receiver!\n");
    }

    //第三次握手 此时已经收到 SYN and ACK
    rtp_packet_t ack_pkt;
    memset(&ack_pkt, 0, sizeof(ack_pkt));
    make_pkt(&ack_pkt, NULL, next_seq_num, 0, RTP_ACK);
    rtp_send(sockfd, ack_pkt, 0, (struct sockaddr *)&receive_addr, addr_len);
    tv.tv_sec = 2; //2s
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    cnt = 0;
    while(true){
        int state = 0;
        rtp_packet_t *recv_pkt = rtp_recv(sockfd, RTP_SYN | RTP_ACK, (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
        if (!recv_pkt && state == RCV_TIMEOUT){
            //Timeout!
            LOG_MSG("Succeed to establish connection.\n");
            free(recv_pkt);
            break;
        }
        else if (cnt == MAX_RETRY) break;
        else{
            //收到了 SYN and ACK 或者收报文错误
            rtp_send(sockfd, ack_pkt, 0, (struct sockaddr *)&receive_addr, addr_len);
            cnt++;
            free(recv_pkt);
            continue;
        }
    }
    if (cnt == MAX_RETRY){
        LOG_FATAL("Error, failed to establish connection!\n");
    }
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    return ;
}

void GBN(int sockfd){
    //Receiver 的 seq_num 表示下一个期望收到的编号
    if (window_size > 32) window_size = 32;
    socklen_t addr_len = sizeof(receive_addr);
    SenderGBNWindow *win = (SenderGBNWindow *)malloc(sizeof(SenderGBNWindow));
    struct sockaddr_in temp_addr;
    socklen_t temp_addr_len = sizeof(temp_addr);
    win->base = next_seq_num;
    win->next = next_seq_num;
    win->size = window_size;
    int pkt_idx = 0;
    int first_not_ack_idx = 0;
    while (win->next != end_seq_num){ //还有数据要传
        // 发送窗口内的包
        while (in_window(win->next, win->base, win->size) && win->next != end_seq_num){ //窗口内数据没发完并且还有数据要发
            pkt_idx = win->next - start_seq_num;
            rtp_send(sockfd, send_pkts[pkt_idx], send_pkts[pkt_idx].rtp.length, (struct sockaddr *)&receive_addr, addr_len);
            if (win->base == win->next){
                start_timer();
            }
            win->next = (win->next + 1) % MOD;
        }
        // 等待ACK或超时
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000 - timer_expired(); //距离100ms还有多久

        if (tv.tv_usec <= 0){
            //超时重传
            int idx = first_not_ack_idx;
            uint32_t cnt = 0;
            while (in_window(win->base + cnt, win->base, win->next - win->base)) {
                rtp_send(sockfd, send_pkts[idx], send_pkts[idx].rtp.length, (struct sockaddr *)&receive_addr, addr_len);
                idx++;
                cnt++;
            }
            start_timer();
            continue;
        }

        int tmp = select(sockfd + 1, &fds, NULL, NULL, &tv);
        if (tmp < 0){
            LOG_FATAL("Error, failed to get a correct select return!\n");
        }
        else if (tmp == 0){
            //超时重传
            int idx = first_not_ack_idx;
            uint32_t cnt = 0;
            while (in_window(win->base + cnt, win->base, win->next - win->base)) {
                rtp_send(sockfd, send_pkts[idx], send_pkts[idx].rtp.length, (struct sockaddr *)&receive_addr, addr_len);
                idx++;
                cnt++;
            }
            start_timer();
            continue;
        }
        if (FD_ISSET(sockfd, &fds)){
            //收到ACK
            int state = 0;
            rtp_packet_t *recv_pkt = rtp_recv(sockfd, RTP_ACK, (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
            if (recv_pkt && state == RCV_OK && in_window((recv_pkt->rtp.seq_num - 1) % MOD, win->base, win->size)){
                win->base = recv_pkt->rtp.seq_num;
                first_not_ack_idx = recv_pkt->rtp.seq_num - start_seq_num;
                if (win->base == win->next){
                    stop_timer();
                }
                else{
                    start_timer();
                }
            }
            free(recv_pkt);
        }
    }
    stop_timer();
    free(win);
    return ;
}

void SR(int sockfd){
    //Receiver 的 seq_num 表示收到的编号
    if (window_size > 32) window_size = 32;
    socklen_t addr_len = sizeof(receive_addr);
    SenderSRWindow *win = (SenderSRWindow *)malloc(sizeof(SenderSRWindow));
    struct sockaddr_in temp_addr;
    socklen_t temp_addr_len = sizeof(temp_addr);
    win->base = next_seq_num;
    win->next = next_seq_num;
    win->size = window_size;
    int pkt_idx = 0;
    while (win->next != end_seq_num){ //还有数据要传
        while (in_window(win->next, win->base, win->size) && win->next != end_seq_num) {//窗口内没发完并且还有数据要发
            pkt_idx = win->next - start_seq_num;
            rtp_send(sockfd, send_pkts[pkt_idx], send_pkts[pkt_idx].rtp.length, (struct sockaddr *)&receive_addr, addr_len);
            int win_idx = get_idx(win->base, win->next);
            win->ACKed[win_idx] = false;
            if (win->base == win->next){
                start_timer();
            }
            win->next = (win->next + 1) % MOD;
        }
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000 - timer_expired(); //距离100ms超时还有多久
        if (tv.tv_usec <= 0){
            //超时重传
            uint32_t cnt = 0;
            while (in_window(win->base + cnt, win->base, win->next - win->base)) {
                if (win->ACKed[cnt]) {
                    cnt++;
                    continue;
                } //已经确认了
                pkt_idx = win->base + cnt - start_seq_num;
                rtp_send(sockfd, send_pkts[pkt_idx], send_pkts[pkt_idx].rtp.length, (struct sockaddr *)&receive_addr, addr_len);
                cnt++;
            }
            start_timer();
            continue;
        }

        int tmp = select(sockfd + 1, &fds, NULL, NULL, &tv);
        if (tmp < 0){
            LOG_FATAL("Error, failed to get a correct select return!\n");
        }
        else if (tmp == 0){
            //超时重传
            uint32_t cnt = 0;
            while (in_window(win->base + cnt, win->base, win->next - win->base)) {
                if (win->ACKed[cnt]) {
                    cnt++;
                    continue;
                } //已经确认了
                pkt_idx = win->base + cnt - start_seq_num;
                rtp_send(sockfd, send_pkts[pkt_idx], send_pkts[pkt_idx].rtp.length, (struct sockaddr *)&receive_addr, addr_len);
                cnt++;
            }
            start_timer();
            continue;
        }
        if (FD_ISSET(sockfd, &fds)){
            //收到ACK
            int state = 0;
            rtp_packet_t *recv_pkt = rtp_recv(sockfd, RTP_ACK, (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
            if (recv_pkt && state == RCV_OK && in_window(recv_pkt->rtp.seq_num, win->base, win->size)){
                int win_idx = get_idx(win->base, recv_pkt->rtp.seq_num);
                win->ACKed[win_idx] = true;
                while(win->ACKed[0]){ //窗口滑动
                    for (int i = 0; i < win->size - 1; i++){
                        win->ACKed[i] = win->ACKed[i + 1];
                    }
                    win->ACKed[win->size - 1] = false;
                    win->base = (win->base + 1) % MOD;
                }
                if (win->base == win->next){
                    stop_timer();
                }
                else{
                    start_timer();
                }
            }
            free(recv_pkt);
        }
    }
    stop_timer();
    free(win);
    return ;
}

void disconnect(int sockfd){
    //第一次挥手 发送 FIN
    rtp_packet_t fin_pkt;
    memset(&fin_pkt, 0, sizeof(fin_pkt));
    make_pkt(&fin_pkt, NULL, end_seq_num, 0, RTP_FIN);
    socklen_t addr_len = sizeof(receive_addr);
    struct sockaddr_in temp_addr;
    socklen_t temp_addr_len = sizeof(temp_addr);
    //将发送信息步骤与第二次挥手写在一起

    //第二次挥手 等待接收 FIN ACK
    int cnt = 0;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; //100ms
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    rtp_packet_t *recv_pkt = NULL;
    while(true){
        int state = 0;
        rtp_send(sockfd, fin_pkt, 0, (struct sockaddr *)&receive_addr, addr_len);
        cnt++;
        recv_pkt = rtp_recv(sockfd, (RTP_FIN | RTP_ACK), (struct sockaddr *)&temp_addr, &temp_addr_len, &state);
        if (recv_pkt && state == RCV_OK && recv_pkt->rtp.seq_num == end_seq_num){
            LOG_MSG("Received FIN and ACK from receiver.\n");
            free(recv_pkt);
            break;
        }
        if (cnt == MAX_RETRY + 1) break;
    }
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        LOG_FATAL("Error, failed to setsockopt SO_RCVTIMEO!\n");
    }
    if (cnt == MAX_RETRY + 1){
        free(recv_pkt);
        LOG_FATAL("Error, failed to receive FIN and ACK from receiver!\n");
    }
    return ;

}

void start_timer(){
    clock_gettime(CLOCK_MONOTONIC, &timer);
    if (!timer_running){
        timer_running = true;
    }
}

void stop_timer(){
    timer_running = false;
}

long timer_expired(){
    if (!timer_running) return 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ret = (now.tv_sec - timer.tv_sec) * 1000000L + (long)(now.tv_nsec - timer.tv_nsec) / 1000;
    return ret;
}