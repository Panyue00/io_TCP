#include "client.h"
#include <sys/select.h>

int has_input() {
    fd_set fds;
    struct timeval tv = {0, 0};  // 非阻塞检查
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) == 1;
}
pthread_mutex_t stdin_mutex = PTHREAD_MUTEX_INITIALIZER;


// 错误处理函数
void handle_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
void create_directory(const char *dir_path) {
    // 如果目录已经存在，就不报错
    if (mkdir(dir_path, 0755) < 0) {
        if (errno != EEXIST) {
            perror("mkdir failed");
        }
    }
}
void send_file(int sockfd, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    file_size = htonl(file_size); // 转换为网络字节序

    // 发送文件大小
    if (send(sockfd, &file_size, sizeof(file_size), 0) == -1) {
        perror("Failed to send file size");
        fclose(file);
        return;
    }

    printf("Sending file: %s (%ld bytes)\n", filepath, ntohl(file_size));

    // 发送文件内容
    char buffer[BUF_SIZE];
    size_t bytes_read = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(sockfd, buffer, bytes_read, 0) == -1) {
            perror("Failed to send file content");
            fclose(file);
            return;
        }
        printf("Sent %zu bytes\n", bytes_read);
    }

    printf("File sent: %s\n", filepath);
    fclose(file);
}


// 发送路径和类型标志（文件或目录）
void send_path(int sockfd, const char *path, int type) {
    // 发送文件/目录类型标志
    send(sockfd, &type, sizeof(type), 0);
    
    // 发送路径
    send(sockfd, path, strlen(path) + 1, 0);
}

// 客户端发送目录
void send_directory(int sockfd, const char *dirpath) {
    DIR *dir = opendir(dirpath);
    if (!dir) {
        perror("Failed to open directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // 跳过"."和".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char filepath[BUF_SIZE];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

        struct stat path_stat;
        stat(filepath, &path_stat);

        if (S_ISDIR(path_stat.st_mode)) {
            // 发送目录路径和类型标志：目录（2）
            send_path(sockfd, filepath, 2);

            // 递归发送目录中的内容
            send_directory(sockfd, filepath);
        } else if (S_ISREG(path_stat.st_mode)) {
            // 发送文件路径和类型标志：普通文件（1）
            send_path(sockfd, filepath, 1);
            printf("Sending file: %s\n", filepath);
            // 发送文件内容
            send_file(sockfd, filepath);
        }
    }

    closedir(dir);
}
void save_file(int server_socket, const char *filepath) {
    FILE *file = fopen(filepath, "wb");
    if (file == NULL) {
        perror("Failed to open file for writing");
        return;
    }

    // 接收文件大小
    long file_size;
    char len[8];
    ssize_t bytes_received = recv(server_socket, len, 8, 0);
    if (bytes_received <= 0) {
        perror("Failed to receive file size");
        fclose(file);
        return;
    }
    file_size = atoi(len);
    printf("Receiving file: %s, Size: %ld bytes\n", filepath, file_size);

    // 接收文件内容
    char buffer[BUF_SIZE];
    size_t bytes_received_total = 0;
    while (bytes_received_total < file_size) {
        ssize_t bytes = recv(server_socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            perror("Failed to receive file content");
            break;
        }
        fwrite(buffer, 1, bytes, file);
        bytes_received_total += bytes;
    }

    printf("File received and saved: %s\n", filepath);
    fclose(file);
}

// 客户端接收路径和类型标志（文件或目录）
void recv_path(int server_socket, char *path, int *type) {
    // 接收文件/目录类型标志
    recv(server_socket, type, sizeof(int), 0);
    
    // 接收路径
    recv(server_socket, path, BUF_SIZE, 0);
}

// 客户端接收目录
void recv_directory(int server_socket) {
    int type;
    char filepath[BUF_SIZE];
    
    while (1) {
        recv_path(server_socket, filepath, &type);
        
        if (type == 0) {
            break;  // 结束标志
        }

        printf("Received path: %s, Type: %d\n", filepath, type);

        if (type == 1) {  // 普通文件
            printf("It's a regular file: %s\n", filepath);
            save_file(server_socket, filepath);
        } else if (type == 2) {  // 目录
            printf("It's a directory: %s\n", filepath);
            // 创建目录，假设函数create_directory存在
            create_directory(filepath);
        } else {
            printf("Unknown file type\n");
            break;
        }
    }
}
// 发送请求的线程函数
void *send_request(void *sockfd) {
    char message[BUF_SIZE];
    while (1) {
        if (has_input()) {
            pthread_mutex_lock(&stdin_mutex);  // 加锁
            fgets(message, BUF_SIZE, stdin);
            pthread_mutex_unlock(&stdin_mutex);  // 解锁
            message[strcspn(message, "\n")] = '\0';
            send(*(int *)sockfd, message, strlen(message), 0);
        }
        // 其他逻辑
    }
    return NULL;
}

// 接收响应的线程函数
void *receive_response(void *sockfd) {
    char buffer[BUF_SIZE];
    ssize_t len;
    while (1) {
        len = recv(*(int *)sockfd, buffer, sizeof(buffer), 0);
        if (len == -1) {
            handle_error("recv");
        } else if (len == 0) {
            printf("Server closed the connection\n");
            break;
        }

        buffer[len] = '\0';  // 确保字符串结尾是 '\0'
        printf("%s\n", buffer);

          // 处理上传和下载项目的请求
          if (strncmp(buffer, "Enter project name to upload: ", 29) == 0) {
            printf("Prepare to Upload\n");
            char buf[100];
            // 使用 fgets 获取用户输入
            printf("Locking mutex\n");
            pthread_mutex_lock(&stdin_mutex);  // 加锁
            printf("Locking mutex\n");
            fgets(buf, sizeof(buf), stdin);
            pthread_mutex_unlock(&stdin_mutex);  // 解锁
            buf[strcspn(buf, "\n")] = '\0';
                // 发送到服务器
                send(*(int *)sockfd, buf, strlen(buf), 0);  // 发送去除换行符后的数据
                printf("You entered: %s\n", buf);
                // 调用发送项目函数
                send_directory(*(int *)sockfd, buf);
                    printf("Project uploaded successfully\n"); 
     
        } /* else if (strncmp(buffer, "Enter project name to download: ", 30) == 0) {
            printf("Enter project name to download: ");
            pthread_mutex_lock(&stdin_mutex);  // 加锁
            fgets(buffer, BUF_SIZE, stdin);
            pthread_mutex_unlock(&stdin_mutex);  // 解锁
            buffer[strcspn(buffer, "\n")] = '\0';
            send(*(int *)sockfd, buffer, strlen(buffer), 0);
            if (receive_project(*(int *)sockfd, buffer) == 0) {
                printf("Project downloaded successfully\n");
            } else {
                printf("Failed to download project\n");
            }
        }  */     
    }
    return NULL;
}

int main(int argc ,char*argv[]) {
   /*  if (argc < 3) {
        fprintf(stderr, "Usage: %s <domain> <port>\n", argv[0]);
        return -1;
    } */

    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;
    
    // 获取域名对应的主机信息
/*     server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr, "Error: No such host\n");
        return -1;
    } */

    // 创建套接字
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) handle_error("socket");

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888/* atoi *//* argv[2] */);  // 使用传入的端口号
    //memcpy((char *)&server_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    server_addr.sin_addr.s_addr = inet_addr("47.109.85.43");
    // 连接服务器
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        handle_error("connect");
    }

    // 创建发送请求和接收响应的线程
    pthread_t send_thread, recv_thread;
    if (pthread_create(&send_thread, NULL, send_request, (void *)&sockfd) != 0) {
        handle_error("pthread_create send_thread");
    }
    if (pthread_create(&recv_thread, NULL, receive_response, (void *)&sockfd) != 0) {
        handle_error("pthread_create recv_thread");
    }

    // 等待两个线程结束
    pthread_detach(send_thread);
    pthread_detach(recv_thread);
    while(1);  // 无限循环保持线程活动

    close(sockfd);
    return 0;
}
