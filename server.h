#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
// 新增头文件
#include <dirent.h>     // 用于目录操作
#include <sys/types.h>  // 基本系统数据类型
#include <limits.h>     // 用于 PATH_MAX 等常量
#include <sqlite3.h>  // 添加SQLite3头文件

// 如果 DT_REG 未定义，手动定义它
#ifndef DT_REG
#define DT_REG 8    // 普通文件的类型值
#endif

#define PORT 8888
#define MAX_EVENTS 50
#define BUF_SIZE 1024
#define SERVER_IP " 127.0.0.1"

// 文件传输协议相关常量
#define PROTO_BEGIN "BEGIN"
#define PROTO_END "END"
#define PROTO_SIZE "SIZE"
#define PROTO_DATA "DATA"
#define PROTO_OK "OK"
#define PROTO_ERROR "ERROR"
#define CHUNK_SIZE 4096

// 用户信息结构体
typedef struct {
    char username[128];
    char password[128];
    int status; // 0: 未注册, 1: 已注册
    int menu_flag; // 用于控制菜单状态
} user_info;

//用户相关函数声明
void handle_error(const char *msg);
int create_workspace(const char *username);
void log_version(const char *username, const char *file_name, const char *action);
int user_register(int client_fd, user_info users[], int user_count);
int user_login(int client_fd, user_info *user,int max);
int user_info_init(user_info *user);
int handle_client(int client_fd, user_info *user);
void trim_newline(char *str);

// 文件相关函数声明
int list_workspace_files(int client_fd, const char *username);
int create_file(int client_fd, const char *username, const char *filename);
int delete_file(int client_fd, const char *username, const char *filename);
int edit_file(int client_fd, const char *username, const char *project_name, const char *filename);
int create_project_file(int client_fd, const char *username, const char *project_name, const char *filename);
void send_file(int client_fd, const char *file_path);
int receive_file(int client_fd, const char *file_path);

// 项目相关函数声明
int list_projects(int client_fd, const char *username);
int create_project_directory(int client_fd, const char *username, const char *project_name);
int create_project(int client_fd, const char *username);
int open_project(int client_fd, const char *username);
int upload_project(int client_fd, const char *username);
int download_project(int client_fd, const char *username);
int handle_project_menu(int client_fd, const char *username, const char *project_name);
int delete_project(int client_fd, const char *username, const char *project_name);
int receive_project(int client_fd, const char *username, const char *project_name);
int send_project(int client_fd, const char *username, const char *project_name);

// 数据库相关函数声明
int init_database(void);
int db_add_user(const char *username, const char *password);
int db_check_user(const char *username, const char *password);
int db_user_exists(const char *username);
void close_database(void);

#endif
