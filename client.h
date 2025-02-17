#ifndef CLIENT_H
#define CLIENT_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
// 文件传输协议相关常量
#define PROTO_BEGIN "BEGIN"
#define PROTO_END "END"
#define PROTO_SIZE "SIZE"
#define PROTO_DATA "DATA"
#define PROTO_OK "OK"
#define PROTO_ERROR "ERROR"
#define CHUNK_SIZE 4096

#define BUF_SIZE 1024
#define SERVER_IP "116.205.97.83"
#define PORT 8888

// 函数声明
void handle_error(const char *msg);
void *send_request(void *sockfd);
void *receive_response(void *sockfd);

// 文件传输相关函数声明
void receive_file(int sockfd, const char *file_path);
void send_file(int sockfd, const char *file_path);
int receive_project(int sockfd, const char *project_name);
int send_project(int sockfd, const char *project_name);

#endif
