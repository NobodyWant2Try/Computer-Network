/*有关 Pthread 编程的内容参考了 https://www.cnblogs.com/caojun97/p/17411101.html*/
#include <iostream>
#include <string>
#include <sstream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <filesystem>
#include <vector>

#include "utils.hpp"

void init_pthread_data(); // 初始化线程
int find_first_pthread(); // 找到第一个空位置，开启一个新线程
void *ptrhead_handle(void *arg); // 每个线程的对 client 的操作
void build_connection(int sock_fd);
void operate_cd(struct Pthread_data *pthread, size_t directory_length);
void get_ls(struct Pthread_data *pthread);
void operate_download(struct Pthread_data *pthread, size_t file_name_length);
void operate_upload(struct Pthread_data *pthread, size_t file_name_length);
void get_sha256(struct Pthread_data *pthread, size_t file_name_length);
void disconnection(int sock_fd);


static struct Pthread_data pthread_data[MAXCLIENTS];
static char IP[MAXLENGTH] = {0};
static char Port[MAXLENGTH] = {0};

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    std::cout << "Hello from ftp server!" << std::endl;

    if (argc != 3){
        std::cout << "Error, please enter ./ftp_server <IP> <Port> to run Server." << std::endl;
        return 0;
    }
    
    const int port = std::atoi(argv[2]);
    strncpy(IP, argv[1], MAXLENGTH - 1);
    strncpy(Port, argv[2], MAXLENGTH - 1);
    int listen_fd = -1, sock_fd = -1;
    
    struct sockaddr_in servaddr, cliaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, IP, &servaddr.sin_addr) <= 0) { //inet_pton() 中 IP 遇到 ‘\0’ 就会截断
        std::cout << "Error, failed to transfer server IP address." << std::endl;
        return 0;
    }
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cout << "Error, failed to create listen socket." << std::endl;
        return 0;
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // 快速重启，端口复用

    if (bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        std::cout << "Error, failed to bind socket." << std::endl;
        return 0;
    }
    if (listen(listen_fd, MAXCLIENTS) < 0) {
        std::cout << "Error, failed to listen." << std::endl;
        return 0;
    }

    init_pthread_data();
    while (true){
        bzero(&cliaddr, sizeof(cliaddr));
        socklen_t addr_len = sizeof(cliaddr);
        sock_fd = accept(listen_fd, (struct sockaddr *)&cliaddr, &addr_len);
        if (sock_fd < 0) {
            std::cout << "Error, failed to accept client connection." << std::endl;
            return 0;
        }
        int idx = find_first_pthread();
        if (idx == -1) {
            std::cout << "Error, the number of clients exceeds the capacity of the server." << std::endl;
            return 0;
        }
        pthread_data[idx].sock_fd = sock_fd;
        pthread_create(&pthread_data[idx].pthread_id, NULL, ptrhead_handle, &(pthread_data[idx]));
        pthread_detach(pthread_data[idx].pthread_id);
    }
    close(listen_fd);
    return 0;
}

void init_pthread_data(){
    for (int i = 0; i < MAXCLIENTS; i++) {
        memset(&pthread_data[i], 0, sizeof(struct Pthread_data));
        pthread_data[i].sock_fd = -1; // 表示空位置
    }
    return ;
}

int find_first_pthread(){
    int ret = -1; // 线程满的话返回 -1
    for (int i = 0; i < MAXCLIENTS; i++){
        if (pthread_data[i].sock_fd == -1) {
            ret = i;
            break;
        }
    }
    return ret;
}

void *ptrhead_handle(void *arg){
    struct Pthread_data *pthread = (struct Pthread_data *)arg;
    int sock_fd = pthread->sock_fd;
    pthread->cwd = std::filesystem::current_path();
    while (true){
        Message message;
        // 收报文
        if (recv_all(sock_fd, &message, 12) == 0){
            std::cout << "Error, failed to receive message." << std::endl;
            close(sock_fd);
            pthread->sock_fd = -1;
            pthread_exit(NULL);
        }
        // 判断要执行什么操作
        if (message.m_type == OPEN_CONN_REQUEST){
            build_connection(sock_fd);
        }
        else if (message.m_type == LIST_REQUEST) {
            get_ls(pthread);
        }
        else if (message.m_type == CHANGE_DIR_REQUEST){
            operate_cd(pthread, ntohl(size_t(message.m_length)) - 12);
        }
        else if (message.m_type == GET_REQUEST){
            operate_download(pthread, ntohl(size_t(message.m_length)) - 12);
        }
        else if (message.m_type == PUT_REQUEST) {
            operate_upload(pthread, ntohl(size_t(message.m_length)) - 12);
        }
        else if (message.m_type == SHA_REQUEST) {
            // std::cout << "debug" << std::endl;
            get_sha256(pthread, ntohl(size_t(message.m_length)) - 12);
        }
        else if (message.m_type == QUIT_REQUEST) {
            disconnection(sock_fd);
            close(sock_fd);
            pthread->sock_fd = -1;
            pthread_exit(NULL);
        }
        else {
            std::cout << "Error, get a wrong type request." << std::endl; // 不直接结束线程，等待 client 重新发送有效类型的报文
            continue ;
        }
    }
}

void build_connection(int sock_fd){
    Message reply;
    build_message(&reply, OPEN_CONN_REPLY, 1, 12);
    if (send_all(sock_fd, &reply, 12) == 0){
        std::cout << "Error, failed to send reply message." << std::endl;
        return ;
    }
    return ;
}

void get_ls(struct Pthread_data *pthread){
    int sock_fd = pthread->sock_fd;
    // 读取工作目录下的文件名
    std::vector<std::string> filenames;
    std::string files;
    std::filesystem::path dir = pthread->cwd;
    for (const auto& file : std::filesystem::directory_iterator(dir)){ // directory_iterator 不按任何逻辑顺序遍历，与 ls 的顺序不一样！！
        std::string filename = file.path().filename().string();
        filenames.push_back(filename);
    }
    sort(filenames.begin(), filenames.end());
    for (const auto& fn : filenames){
        files += (fn + "\n");
    }
    uint32_t length = files.size();
    char buffer[MAXLENGTH] = {0};
    strncpy(buffer, files.c_str(), MAXLENGTH);
    length = strlen(buffer);
    if (buffer[length] != '\0') buffer[length] = '\0';
    // 发报文
    Message reply;
    build_message(&reply, LIST_REPLY, unused, 12 + length + 1);
    if (send_all(sock_fd, &reply, 12) == 0){
        std::cout << "Error, failed to send reply message." << std::endl;
        return ;
    }
    // 发数据
    // std::cout << "debug:\n" << buffer << std::endl;
    if (send_all(sock_fd, buffer, length + 1) == 0){
        std::cout <<"Error, failed to send ls data." << std::endl;
        return ;
    }
    return ;
}

void operate_cd(struct Pthread_data *pthread, size_t directory_length){
    int sock_fd = pthread->sock_fd;
    // 收目录数据
    char directory[MAXLENGTH] = {0};
    if (recv_all(sock_fd, directory, directory_length) == 0){
        std::cout << "Error, failed to receive directory name." << std::endl;
        return ;
    }
    // 检查是否存在，更改目录并发报文
    std::filesystem::path dir = pthread->cwd / directory;
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        // 不存在
        Message reply;
        build_message(&reply, CHANGE_DIR_REPLY, 0, 12);
        if (send_all(sock_fd, &reply, 12) == 0){
            std::cout << "Error, failed to send the reply message." << std::endl;
            return;
        }
    }
    else {
        // 存在目录
        pthread->cwd = dir;
        Message reply;
        build_message(&reply, CHANGE_DIR_REPLY, 1, 12);
        if (send_all(sock_fd, &reply, 12) == 0){
            std::cout << "Error, failed to send the reply message." << std::endl;
            return;
        }

    }
    return ;
}

void operate_download(struct Pthread_data *pthread, size_t file_name_length){
    int sock_fd = pthread->sock_fd;
    // 收文件名
    char buffer[MAXLENGTH] = {0};
    if (recv_all(sock_fd, buffer, file_name_length) == 0){ // 这里的 length 已经是 client 逻辑中的 length+1
        std::cout << "Error, failed to receive filename." << std::endl;
        return ;
    }
    // 检查是否存在，下载文件并发报文
    std::filesystem::path filename = pthread->cwd / buffer;
    if (!std::filesystem::exists(filename)){
        // 不存在
        Message reply;
        build_message(&reply, GET_REPLY, 0, 12);
        if (send_all(sock_fd, &reply, 12) == 0){
            std::cout << "Error, failed to send the reply message." << std::endl;
            return;
        }
    }
    else {// 存在文件
        // 发报文
        Message reply;
        build_message(&reply, GET_REPLY, 1, 12);
        if (send_all(sock_fd, &reply, 12) == 0){
            std::cout << "Error, failed to send the reply message." << std::endl;
            return;
        }
        // 发数据报文
        std::ifstream download_file(filename.c_str(), std::ios::binary);
        if (download_file.is_open() == false) {
            std::cout << "Error, failed to open the file." << std::endl;
            return ;
        }
        char buffer_[MAXLENGTH] = {0};
        uint32_t file_length = 0;
        struct stat st;
        if (stat(filename.c_str(), &st) == 0) file_length = st.st_size;
        else {
            std::cout << "Error, failed to calculate the file size." << std::endl;
            return ;
        }   
        Message data_reply;
        build_message(&data_reply, FILE_DATA, unused, 12 + file_length);
        if (send_all(sock_fd, &data_reply, 12) == 0){
            std::cout << "Error, failed to send data message." << std::endl;
            return;
        }
        // 发数据
        ssize_t cnt = 0;
        while (cnt < file_length) {
            size_t batch = std::min(long(MAXLENGTH), file_length - cnt);
            download_file.read(buffer_, batch);
            ssize_t tmp = send_all(sock_fd, buffer_, batch);
            if (tmp <= 0){
                std::cout << "Error, failed to send data." << std::endl;
                download_file.close();
                return ;
            }
            cnt += tmp;
        }
        download_file.close();
    }
    return ;
}

void operate_upload(struct Pthread_data *pthread, size_t file_name_length){
    int sock_fd = pthread->sock_fd;
    // 收文件名
    char buffer[MAXLENGTH] = {0};
    if (recv_all(sock_fd, buffer, file_name_length) == 0){// 同理 这里不应该+1
        std::cout << "Error, failed to receive filename." << std::endl;
        return ;
    }
    // 发报文
    Message reply;
    build_message(&reply, PUT_REPLY, unused, 12);
    if (send_all(sock_fd, &reply, 12) == 0){
        std::cout << "Error, failed to send reply message." << std::endl;
        return ;
    }
    // 收数据报文
    Message data_message;
    if (recv_all(sock_fd, &data_message, 12) == 0){
        std::cout << "Error, failed to receive data message." << std::endl;
        return ;
    }
    // 收数据
    size_t file_length = size_t(ntohl(data_message.m_length)) - 12;
    std::filesystem::path filename = pthread->cwd / buffer;
    std::ofstream upload_file(filename.c_str(), std::ios::binary | std::ios::trunc);
    if (upload_file.is_open() == false){
        std::cout << "Error, Server can't open the file " << buffer << std::endl;
        return ;
    }
    ssize_t cnt = 0;
    char buffer_[MAXLENGTH] = {0};
    while (cnt <file_length){
        size_t batch = std::min(size_t(MAXLENGTH), file_length - cnt);
        ssize_t tmp = recv_all(sock_fd, buffer_, batch);
        if (tmp <= 0){
            std::cout << "Error, failed to receive data." << std::endl;
            upload_file.close();
            return ;
        }
        upload_file.write(buffer_, tmp);
        cnt += tmp;
    }
    upload_file.close();
    return ;
}

void get_sha256(struct Pthread_data *pthread, size_t file_name_length){
    int sock_fd = pthread->sock_fd;
    // 收文件名
    char buffer[MAXLENGTH] = {0};
    if (recv_all(sock_fd, buffer, file_name_length) == 0){ // file_name_length + 1 就一直卡住
        std::cout << "Error, failed to receive filename." << std::endl;
        return ;
    }
    // std::cout << "debug" << std::endl;
    std::filesystem::path filename = pthread->cwd / buffer;
    // 文件不存在
    if (!std::filesystem::exists(filename)){
        Message reply;
        build_message(&reply, SHA_REPLY, 0, 12);
        if (send_all(sock_fd, &reply, 12) == 0){
            std::cout << "Error, failed to send reply message." << std::endl;
            return ;
        }
    }
    // 文件存在
    else{// sha256sum 返回的数据会在字符串后跟一个文件名，要特殊处理。
        // 发报文
        Message reply;
        build_message(&reply, SHA_REPLY, 1, 12);
        if (send_all(sock_fd, &reply, 12) == 0){
            std::cout << "Error, failed to send the reply message." << std::endl;
            return;
        }
        // 发数据报文
        std::string command = "sha256sum " + filename.string();
        FILE *fp = popen(command.c_str(), "r");
        if (!fp){
            std::cout << "Error, failed to caculate sha256 value." << std::endl;
            return ;
        }
        char buffer_[MAXLENGTH] = {0};
        fgets(buffer_, sizeof(buffer_), fp);
        pclose(fp);
        std::string line(buffer_); 
        size_t start_poss = line.find_first_not_of(" \t\n");
        size_t end_pos = line.find_last_not_of(" \t\n");
        std::string shavalue = line.substr(start_poss, end_pos - start_poss + 1);
        strncpy(buffer_, shavalue.c_str(), MAXLENGTH);
        uint32_t length = strlen(buffer_);
        if (buffer_[length] != '\0') buffer_[length] = '\0';
        Message data_reply;
        build_message(&data_reply, FILE_DATA, unused, 12 + length + 1);
        if (send_all(sock_fd, &data_reply, 12) == 0){
            std::cout << "Error, failed to send data message." << std::endl;
            return ;
        }
        if (send_all(sock_fd, buffer_, length + 1) == 0){
            std::cout << "Error, failed to send data." << std::endl;
            return ;
        }
    }
    return ;
}

void disconnection(int sock_fd){
    // 发报文
    Message reply;
    build_message(&reply, QUIT_REPLY, unused, 12);
    if (send_all(sock_fd, &reply, 12) == 0){
        std::cout << "Error, failed to send reply." << std::endl;
        return ;
    }
    return ;
}