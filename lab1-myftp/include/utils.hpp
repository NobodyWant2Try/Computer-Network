#include <iostream>
#include <limits.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
#include <filesystem>

#define MAGIC_NUMBER_LENGTH 6
#define unused -1
#define OPEN_CONN_REQUEST 0xA1
#define OPEN_CONN_REPLY 0xA2
#define LIST_REQUEST 0xA3
#define LIST_REPLY 0xA4
#define CHANGE_DIR_REQUEST 0xA5
#define CHANGE_DIR_REPLY 0xA6
#define GET_REQUEST 0xA7
#define GET_REPLY 0xA8
#define FILE_DATA 0xFF
#define PUT_REQUEST 0xA9
#define PUT_REPLY 0xAA
#define SHA_REQUEST 0xAB
#define SHA_REPLY 0xAC
#define QUIT_REQUEST 0xAD
#define QUIT_REPLY 0xAE
#define MAXLENGTH 2050
#define gettid() syscall(__NR_gettid)
#define MAXCLIENTS 50

const char my_protocol[MAGIC_NUMBER_LENGTH] = {'\xc1', '\xa1', '\x10', 'f', 't', 'p'};

struct Message {
    char m_protocol[MAGIC_NUMBER_LENGTH] = {0};
    uint8_t  m_type;
    uint8_t  m_status;
    uint32_t  m_length;   
} __attribute__ ((packed));

struct Cmd {
    int op = 0;
    char info1[MAXLENGTH] = {0};
    char info2[MAXLENGTH] = {0};
} __attribute__ ((packed));

struct Pthread_data {
    pthread_t pthread_id;
    int sock_fd;
    struct sockaddr_in client_addr;
    std::filesystem::path cwd;
};

inline void build_message(Message *message, uint8_t type, uint8_t status, uint32_t length){
    memcpy(message->m_protocol, my_protocol, sizeof(my_protocol));
    message->m_type = type;
    message->m_status = status;
    message->m_length = htonl(length);
    return ;
}

//防止传输中断
inline ssize_t send_all(int sock_fd, const void *buffer, size_t size){
    ssize_t ret = 0;
    while (ret < size){
        ssize_t tmp = send(sock_fd, (const char *)buffer + ret, size - ret, 0);
        if (tmp <= 0){
            std::cout << "Send error." << std::endl;
            return 0;
        }
        ret += tmp;
    }
    return ret;
}

// 防止传输中断
inline ssize_t recv_all(int sock_fd, void *buffer, size_t size){
    ssize_t ret = 0;
    while (ret < size){
        ssize_t tmp = recv(sock_fd, (char *)buffer + ret, size - ret, 0);
        if (tmp <= 0){
            std::cout << "Receive error." << std::endl;
            return 0;
        }
        ret += tmp;
    }
    return ret;
}

