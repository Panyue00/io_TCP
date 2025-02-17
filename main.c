#include "server.h"
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>

// 全局变量
int g_user_count = 0;
int epfd;
volatile sig_atomic_t server_shutdown = 0;  // 用于标识服务器是否需要停止

// 信号处理函数
void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\nReceived termination signal. Closing database and exiting...\n");
        server_shutdown = 1;  // 设置服务器停止标识
    }
}

// 客户端处理函数
void *client_thread(void *arg1) {
    int client_fd = *(int *)arg1;
    free(arg1);

    // 为每个线程创建一个局部的用户信息副本
    user_info *user = malloc(sizeof(user_info));
    user_info_init(user);  // 初始化局部用户信息

    const char menu[] = "Welcome to PanHub!\n1. Introduction\n2. Register\n3. Login\n4. Exit\n";

    while (!server_shutdown) {
        send(client_fd, menu, strlen(menu), 0); // 发送菜单给客户端
        if (handle_client(client_fd, user) < 0) {
            printf("Client %d disconnected\n", client_fd);
            close(client_fd);
            free(user);  // 释放用户信息
            break; // 客户端断开连接后退出线程
        } else {
            // 更新用户信息
            user->status = 1;  // 假设修改了状态
            strncpy(user->username, "new_username", sizeof(user->username) - 1);
        }

    }

    return NULL;
}

int main() {
    // 初始化数据库
    if (init_database() < 0) {
        fprintf(stderr, "Failed to initialize database\n");
        return -1;
    }

    int sockfd, nfds;
    struct epoll_event ev, events[MAX_EVENTS];
    struct sockaddr_in server_addr, client_addr;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) handle_error("socket");

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) handle_error("bind");
    if (listen(sockfd, MAX_EVENTS) == -1) handle_error("listen");

    epfd = epoll_create(1);
    if (epfd == -1) handle_error("epoll_create");

    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) handle_error("epoll_ctl");

    while (!server_shutdown) {
        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1) handle_error("epoll_wait");

        for (int i = 0; i < nfds; i++) {
            int client_fd = events[i].data.fd;
            if (client_fd == sockfd) {
                socklen_t client_len = sizeof(client_addr);
                int new_client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
                if (new_client_fd == -1) {
                    perror("accept");
                    continue;
                }

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = new_client_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, new_client_fd, &ev) == -1) {
                    perror("epoll_ctl");
                    continue;
                }

                printf("New client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                // 创建新线程处理客户端
                int *client_fd_ptr = malloc(sizeof(int));
                *client_fd_ptr = new_client_fd;
                pthread_t tid;
                if (pthread_create(&tid, NULL, client_thread, client_fd_ptr) != 0) {
                    perror("pthread_create");
                    free(client_fd_ptr);
                    continue;
                }
                pthread_detach(tid); // 分离线程，自动清理资源
            }
        }
    }

    close(sockfd);
    close(epfd);
    close_database();
    return 0;
}
