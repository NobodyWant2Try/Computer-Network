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

#include "utils.hpp"


Cmd analyse_cmd(); // 获取命令
void open_connection(const char *ip, const char *port);
void cd(const char *directory);
void ls(); 
void download(const char *file);
void upload(const char *file);
void sha_256(const char *file);
void quit();

static int connection_flag = 0; // 记录当前的连接状况
static char IP[MAXLENGTH] = {0};
static char Port[MAXLENGTH] = {0};
int sock_fd = -1;

void debug(Cmd cmd){
    std::cout << cmd.op << std::endl;
    std::cout << cmd.info1 << std::endl;
    std::cout << cmd.info2 << std::endl;
    return ;
}

int main() {
    // std::ofstream log("debug.out", std::ios::out | std::ios::trunc);
    // if (log.is_open()) {
    //     std::cout.rdbuf(log.rdbuf());
    //     std::cerr.rdbuf(log.rdbuf());
    //     std::cout.setf(std::ios::unitbuf);
    //     std::cerr.setf(std::ios::unitbuf);
    // }
    std::cout << "Hello from ftp client!" << std::endl;
    while (true){
        if (connection_flag) std::cout<<"Client("<<IP<<":"<<Port<<")>";
        else std::cout<<"Client(none)>";
        Cmd cmd = analyse_cmd();
        // debug(cmd);
        switch (cmd.op){
            case 1: open_connection(cmd.info1, cmd.info2); break;
            case 2: cd(cmd.info1); break;
            case 3: ls(); break;
            case 4: download(cmd.info1); break;
            case 5: upload(cmd.info1); break;
            case 6: sha_256(cmd.info1); break;
            case 7: quit(); break;
            default: std::cout<<"Invalid command, please try again." << std::endl;
        }
    }
    return 0;
}

Cmd analyse_cmd(){
    Cmd ret;
    std::string line;
    std::getline(std::cin, line);
    std::istringstream iss(line);
    std::string cmd_name;
    iss >> cmd_name;
    if (cmd_name == "open"){
        ret.op = 1;
        iss >> ret.info1 >> ret.info2;
    }
    else if (cmd_name == "cd"){
        ret.op = 2;
        iss >> ret.info1;
    }
    else if (cmd_name == "ls"){
        ret.op = 3;
    }
    else if (cmd_name == "get"){
        ret.op = 4;
        iss >> ret.info1;
    }
    else if (cmd_name == "put"){
        ret.op = 5;
        iss >> ret.info1;
    }
    else if (cmd_name == "sha256"){
        ret.op = 6;
        iss >> ret.info1;
    }
    else if (cmd_name == "quit"){
        ret.op = 7;
    }
    return ret;
}

void open_connection(const char *ip, const char *port){
    // 检查状态合法性
    if (connection_flag) {
        std::cout << "Error, please disconnect current server first." << std::endl;
        return ;
    }
    // 连接
    strncpy(IP, ip, MAXLENGTH-1);
    strncpy(Port, port, MAXLENGTH-1);
    struct sockaddr_in servaddr;
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cout << "Error, failed to create socket." << std::endl;
        return ;
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    int port_ = std::atoi(port);
    servaddr.sin_port = htons(port_);
    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0) {
        std::cout << "Error, failed to transfer IP address." << std::endl;
        return ;
    }
    if (connect(sock_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        std::cout << "Error, connect failed." << std::endl;
        close(sock_fd);
        sock_fd = -1;
        return ;
    }
    // 发报文
    Message open_request;
    build_message(&open_request, OPEN_CONN_REQUEST, unused, 12);
    if (send_all(sock_fd, &open_request, 12) == 0){
        std::cout << "Error, failed to send message." << std::endl;
        return ;
    }
    // 收报文
    Message open_reply;
    if (recv_all(sock_fd, &open_reply, 12) == 0){
        std::cout << "Error, failed to receive message." << std::endl;
        return ;
    }
    if (open_reply.m_status == 0){
        std::cout << "Error, connection refused." << std::endl;
        return ;
    }
    if (open_reply.m_type != OPEN_CONN_REPLY) {
        std::cout << "Error, get a wrong type reply." << std::endl;
        return ;
    }
    // 更新连接状态
    connection_flag = 1;
    FILE *mf = fopen(".client_open_ok", "w");
    if (mf) {
        fputs("ok", mf);
        fclose(mf);
    }
    std::cout << "Connection established" << std::endl;
    return ;
}

void cd(const char *directory){
    // 检查状态合法性
    if (connection_flag == 0){
        std::cout << "Error, please open a server first." << std::endl;
        return ;
    }
    // 发报文和 cd 目录名
    Message cd_request;
    uint32_t length = strlen(directory);
    build_message(&cd_request, CHANGE_DIR_REQUEST, unused, 12 + length + 1);
    if (send_all(sock_fd, &cd_request, 12) == 0){
        std::cout << "Error, failed to send message." << std::endl;
        return ;
    }
    if (send_all(sock_fd, directory, length + 1) == 0){
        std::cout << "Error, failed to send directory name." << std::endl;
        return ;
    }
    // 收报文
    Message cd_reply;
    if (recv_all(sock_fd, &cd_reply, 12) == 0){
        std::cout << "Error, failed to receive message." << std::endl;
        return ;
    }
    if (cd_reply.m_type != CHANGE_DIR_REPLY) {
        std::cout << "Error, get a wrong type reply." << std::endl;
        return ;
    }
    if (cd_reply.m_status) {
        std::cout << "The directory exists, success to change the directory." << std::endl;
    }
    else {
        std::cout << "Can't find the directory, failed to change the directory." << std::endl;
    }
    return ;
}

void ls(){
    // 检查状态合法性
    if (connection_flag == 0){
        std::cout << "Error, please open a server first." << std::endl;
        return ;
    }
    // 发报文
    Message ls_request;
    build_message(&ls_request, LIST_REQUEST, unused, 12);
    if (send_all(sock_fd, &ls_request, 12) == 0){
        std::cout << "Error, failed to send message." << std::endl;
        return ;
    }
    // 收报文
    Message ls_reply;
    if (recv_all(sock_fd, &ls_reply, 12) == 0){
        std::cout << "Error, failed to receive message." << std::endl;
        return ;
    }
    if (ls_reply.m_type != LIST_REPLY) {
        std::cout << "Error, get a wrong type reply." << std::endl;
        return ;
    }
    // 收数据
    char list_data[MAXLENGTH] = {0};
    size_t length = size_t(ntohl(ls_reply.m_length)) - 12;
    if (recv_all(sock_fd, list_data, length) == 0){ // 注意这里的 length 包括了末尾\0，不用再+1
        std::cout << "Error, failed to receive data." << std::endl;
        return ;
    }
    // 打印输出
    std::cout << list_data;
    return ;
}

void download(const char *file){
    // 检查状态合法性
    if (connection_flag == 0){
        std::cout << "Error, please open a server first." << std::endl;
        return ;
    }
    // 发报文和文件名
    Message get_request;
    uint32_t length = strlen(file);
    build_message(&get_request, GET_REQUEST, unused, 12 + length + 1);
    if (send_all(sock_fd, &get_request, 12) == 0){
        std::cout << "Error, failed to send message." << std::endl;
        return ;
    }
    if (send_all(sock_fd, file, length + 1) == 0){ // 注意一定是 length + 1 否则传不出\0 导致错误！！其他函数中此处的逻辑也是这样。
        std::cout << "Error, failed to send filename." << std::endl;
        return ;
    }
    // 收报文
    Message get_reply;
    if (recv_all(sock_fd, &get_reply, 12) == 0){
        std::cout << "Error, failed to receive message." << std::endl;
        return ;
    }
    if (get_reply.m_type != GET_REPLY) {
        std::cout << "Error, get a wrong type reply." << std::endl;
        return ;
    }
    if (get_reply.m_status == 0){
        std::cout << "Can't find file." << std::endl;
        return ;
    }
    else {
        Message get_data;
        if (recv_all(sock_fd, &get_data, 12) == 0){
            std::cout << "Error, failed to receive data message." << std::endl;
            return ;
        }
        if (get_data.m_type != FILE_DATA) {
            std::cout << "Error, get a wrong type data reply." << std::endl;
            return ;
        }
        size_t file_length = size_t(ntohl(get_data.m_length)) - 12;
        std::ofstream output_file(file, std::ios::binary | std::ios::trunc);
        if (output_file.is_open() == false){
            std::cout << "Error, can't open the file " << file << std::endl;
            return ;
        }
        std::cout << "File exists, begin to download." << std::endl;
        char buffer[MAXLENGTH] = {0};
        ssize_t cnt = 0;
        while (cnt < file_length){
            size_t batch = std::min(size_t(MAXLENGTH), file_length - cnt);
            // std::cout << batch << std::endl;
            ssize_t tmp = recv_all(sock_fd, buffer, batch);
            if (tmp <= 0) {
                std::cout << "Error, failed to download file." << std::endl;
                output_file.close();
                return ;
            }
            output_file.write(buffer, tmp);
            cnt += tmp;
        }
        output_file.close();
        std::cout << "Success to download file " << file << "." << std::endl;
    }
    return ;
}

void upload(const char *file){
    // 检查状态合法性
    if (connection_flag == 0){
        std::cout << "Error, please open a server first." << std::endl;
        return ;
    }
    // 检查文件是否存在
    if (access(file, F_OK) != 0) {
        std::cout << "Can't find this file, please check your command." << std::endl;
        return ;
    }
    // 发报文
    Message put_request;
    uint32_t length = strlen(file);
    build_message(&put_request, PUT_REQUEST, unused, 12 + length + 1);
    if (send_all(sock_fd, &put_request, 12) == 0){
        std::cout << "Error, failed to send message." << std::endl;
        return ;
    }
    if (send_all(sock_fd, file, length + 1) == 0){
        std::cout << "Error, failed to send filename." << std::endl;
        return ;
    }
    // 收报文
    Message put_reply;
    if (recv_all(sock_fd, &put_reply, 12) == 0){
        std::cout << "Error, failed to receive message." << std::endl;
        return ;
    }
    if (put_reply.m_type != PUT_REPLY){
        std::cout << "Error, get a wrong type of reply." << std::endl;
        return ;
    }
    // 传文件
    std::ifstream input_file(file, std::ios::binary);
    if (input_file.is_open() == false) {
        std::cout << "Error, failed to open the file." << std::endl;
        return ;
    }
    char buffer[MAXLENGTH] = {0};
    uint32_t file_length = 0;
    struct stat st;
    if (stat(file, &st) == 0) file_length = st.st_size;
    else {
        std::cout << "Error, failed to calculate the file size." << std::endl;
        return ;
    }
    Message put_data;
    build_message(&put_data, FILE_DATA, unused, file_length + 12);
    if (send_all(sock_fd, &put_data, 12) == 0){
        std::cout << "Error, failed to send data message." << std::endl;
        return ;
    }
    ssize_t cnt = 0;
    while (cnt < file_length){
        size_t batch = std::min(long(MAXLENGTH), file_length - cnt);
        input_file.read(buffer, batch);
        ssize_t tmp = send_all(sock_fd, buffer, batch);
        if (tmp <= 0){
            std::cout << "Error, failed to upload file." << std::endl;
            input_file.close();
            return ;
        }
        cnt += tmp;
    }
    input_file.close();
    std::cout << "Success to upload file " << file << "." << std::endl;
    return ;
}

void sha_256(const char *file){
    // 检查状态合法性
    if (connection_flag == 0){
        std::cout << "Error, please open a server first." << std::endl;
        return ;
    }
    // 发报文和文件名
    Message sha_request;
    uint32_t length = strlen(file);
    build_message(&sha_request, SHA_REQUEST, unused, 12 + length + 1);
    if (send_all(sock_fd, &sha_request, 12) == 0){
        std::cout << "Error, failed to send message." << std::endl;
        return ;
    }
    if (send_all(sock_fd, file, length + 1) == 0){
        std::cout << "Error, failed to send filename." << std::endl;
        return ;
    }
    // 收报文
    Message sha_reply;
    if (recv_all(sock_fd, &sha_reply, 12) == 0){
        std::cout << "Error, failed to receive message." << std::endl;
        return ;
    }
    if (sha_reply.m_type != SHA_REPLY) {
        std::cout << "Error, get a wrong type reply." << std::endl;
        return ;
    }
    if (sha_reply.m_status == 0){
        std::cout << "Can't find file." << std::endl;
        return ;
    }
    else{
        Message sha_data;
        if (recv_all(sock_fd, &sha_data, 12) == 0 ){
            std::cout << "Error, failed to receive data message." << std::endl;
            return ;
        }
        if (sha_data.m_type != FILE_DATA) {
            std::cout << "Error, get a wrong type date reply." <<std::endl;
            return ;
        }
        size_t file_length = size_t(ntohl(sha_data.m_length)) - 12;
        char sha_value[MAXLENGTH] = {0};
        if (recv_all(sock_fd, sha_value, file_length) == 0){
            std::cout << "Error, failed to receive sha256 value." << std::endl;
            return ;
        }
        uint32_t sha_length = strlen(sha_value);
        sha_value[sha_length] = '\0';
        std::cout << sha_value << std::endl;
    }
    return ;
}

void quit(){
    // 已连接
    if (connection_flag){
        Message quit_request;
        build_message(&quit_request, QUIT_REQUEST, unused, 12);
        if (send_all(sock_fd, &quit_request, 12) == 0){
            std::cout << "Error, failed to send message." << std::endl;
            return ;
        }
        Message quit_reply;
        if (recv_all(sock_fd, &quit_reply, 12) == 0){
            std::cout << "Error, failed to receive message." << std::endl;
            return ;
        }
        if (quit_reply.m_type != QUIT_REPLY){
            std::cout << "Error, get a wrong type reply." << std::endl;
            return ;
        }
        close(sock_fd);
        connection_flag = 0;
        std::cout << "Connection close ok." << std::endl;
    }
    // 未连接
    else{
        exit(0);
    }
    return ;
}