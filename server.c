#include "server.h"
#include <sqlite3.h>

void handle_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void trim_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';  // 去除换行符
    }
    if (len > 0 && str[len - 1] == '\r') {
        str[len - 1] = '\0';  // 去除回车符
    }
}

static sqlite3 *db = NULL;

// 初始化数据库
int init_database(void) {
    int rc = sqlite3_open("users.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // 创建用户表
    const char *sql = "CREATE TABLE IF NOT EXISTS users ("
                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "username TEXT UNIQUE NOT NULL,"
                     "password TEXT NOT NULL,"
                     "status INTEGER DEFAULT 1"
                     ");";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    // 输出当前数据库中的用户数量
    int user_count = db_get_user_count();
    printf("Database initialized with %d users\n", user_count);
    return 0;
}

// 添加一个函数来获取用户数量
int db_get_user_count(void) {
    const char *sql = "SELECT COUNT(*) FROM users;";
    sqlite3_stmt *stmt;
    int count = 0;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

// 添加用户
int db_add_user(const char *username, const char *password) {
    const char *sql = "INSERT INTO users (username, password) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

// 检查用户登录
int db_check_user(const char *username, const char *password) {
    const char *sql = "SELECT id FROM users WHERE username = ? AND password = ?;";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_ROW) ? 1 : 0;
}

// 检查用户是否存在
int db_user_exists(const char *username) {
    const char *sql = "SELECT id FROM users WHERE username = ?;";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_ROW) ? 1 : 0;
}

// 关闭数据库
void close_database(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

// 用户注册函数
int user_register(int client_fd, user_info users[], int user_count) {
    char username[128], password[128];
    
    // 获取用户名
    send(client_fd, "Please enter your username:", 26, 0);
    ssize_t len = recv(client_fd, username, sizeof(username) - 1, 0);
    if (len < 0) {
        perror("recv");
        return -1;
    }
    username[len] = '\0';
    trim_newline(username);

    // 检查用户是否已存在
    if (db_user_exists(username) > 0) {
        send(client_fd, "Username already exists\n", 24, 0);
        return -1;
    }

    // 获取密码
    send(client_fd, "Please enter your password:", 26, 0);
    len = recv(client_fd, password, sizeof(password) - 1, 0);
    if (len < 0) {
        perror("recv");
        return -1;
    }
    password[len] = '\0';
    trim_newline(password);

    // 添加用户到数据库
    if (db_add_user(username, password) == 0) {
        send(client_fd, "Registration successful!\n", 24, 0);
        return 0;
    }

    send(client_fd, "Registration failed\n", 19, 0);
    return -1;
}

// 用户登录函数
int user_login(int client_fd, user_info *user, int max) {
    char username[128], password[128];

    // 获取用户名和密码
    send(client_fd, "Please enter your username:", 26, 0);
    ssize_t len = recv(client_fd, username, sizeof(username) - 1, 0);
    if (len < 0) return -1;
    username[len] = '\0';
    trim_newline(username);

    send(client_fd, "Please enter your password:", 26, 0);
    len = recv(client_fd, password, sizeof(password) - 1, 0);
    if (len < 0) return -1;
    password[len] = '\0';
    trim_newline(password);

    // 验证用户
    if (db_check_user(username, password) > 0) {
        send(client_fd, "Login successful!\n", 18, 0);
        strncpy(user->username, username, sizeof(user->username) - 1);
        user->status = 1;
        return 1;
    }

    send(client_fd, "Invalid username or password\n", 29, 0);
    return -1;
}

// 用户信息初始化
int user_info_init(user_info *user) {
    memset(user, 0, sizeof(user_info));
    user->status = 0;
    user->menu_flag = 0;
    return 0;
}

// 创建用户工作空间
int create_workspace(const char *username) {
    char dir_path[128];
    
    // 先创建 workspaces 根目录（如果不存在）
    if (mkdir("./workspaces", 0755) == -1 && errno != EEXIST) {
        perror("mkdir workspaces");
        return -1;
    }

    snprintf(dir_path, sizeof(dir_path), "./workspaces/%s", username);

    // 创建目录，只有在目录不存在时才会创建
    if (mkdir(dir_path, 0755) == -1) {
        // 如果错误是因为目录已经存在，就忽略错误
        if (errno != EEXIST) {
            perror("mkdir userdir");
            return -1;
        }
    }
    
    return 0;
}

// 日志记录
void log_version(const char *username, const char *file_name, const char *action) {
    FILE *log_fp = fopen("version_log.txt", "a");
    if (!log_fp) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log_fp, "%s | User: %s | File: %s | Action: %s\n", time_str, username, file_name, action);
    fclose(log_fp);
}

// 保存文件

void save_file(int client_socket, const char *filepath) {
    FILE *file = fopen(filepath, "wb");
    if (!file) {
        perror("Failed to open file for writing");
        return;
    }

    // 接收文件大小
    int file_size;
    if (recv(client_socket, &file_size, sizeof(file_size), 0) <= 0) {
        perror("Failed to receive file size");
        fclose(file);
        return;
    }

    file_size = ntohl(file_size); // 转换为本地字节序

    printf("Receiving file: %s, Size: %d bytes\n", filepath, file_size);

    char buffer[BUF_SIZE];
    int bytes_received = 0;
    while (bytes_received < file_size) {
        ssize_t bytes = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            perror("Failed to receive file content");
            break;
        }
        fwrite(buffer, 1, bytes, file);
        bytes_received += bytes;
        printf("Received %d bytes, Total: %d/%d bytes\n", (int)bytes, bytes_received, file_size);
    }

    printf("File received and saved: %s\n", filepath);
    fclose(file);
}


// 编辑文件
int edit_file(int client_fd, const char *username, const char *project_name, const char *filename) {
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "./workspaces/%s/%s/%s", username, project_name, filename);
    
    // 检查文件是否存在
    if (access(file_path, F_OK) != 0) {
        send(client_fd, "File does not exist\n", 19, 0);
        return -1;
    }
    
    send(client_fd, "Enter file content (end with EOF):\n", 34, 0);
    
    FILE *fp = fopen(file_path, "a");
    if (!fp) return -1;
    
    char buffer[1024];
    while (1) {
        ssize_t len = recv(client_fd, buffer, sizeof(buffer)-1, 0);
        if (len <= 0) break;
        buffer[len] = '\0';
        
        // 检查是否收到结束标记
        if (strstr(buffer, "EOF") != NULL) {
            break;
        }
        
        fprintf(fp, "%s", buffer);
    }
    
    fclose(fp);
    log_version(username, filename, "edited");
    send(client_fd, "File edited successfully\n", 24, 0);
    return 0;
}

// 删除文件
int delete_file(int client_fd, const char *username, const char *filename) {
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "./workspaces/%s/%s", username, filename);
    
    if (remove(file_path) != 0) {
        send(client_fd, "Failed to delete file\n", 21, 0);
        return -1;
    }
    
    log_version(username, filename, "deleted");
    send(client_fd, "File deleted successfully\n", 25, 0);
    return 0;
}

// 创建项目目录
int create_project_directory(int client_fd, const char *username, const char *project_name) {
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "./workspaces/%s/%s", username, project_name);
    
    // 检查目录是否已存在
    if (access(dir_path, F_OK) == 0) {
        send(client_fd, "Project directory already exists\n", 33, 0);
        return -1;
    }
    
    if (mkdir(dir_path, 0755) == -1) {
        perror("mkdir projectdir");
        send(client_fd, "Failed to create project directory\n", 34, 0);
        return -1;
    }
    
    send(client_fd, "Project directory created successfully\n", 38, 0);
    return 0;
}

// 创建项目文件
int create_project_file(int client_fd, const char *username, const char *project_name, const char *filename) {
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "./workspaces/%s/%s/%s", username, project_name, filename);
    
    // 检查文件是否已存在
    if (access(file_path, F_OK) == 0) {
        send(client_fd, "File already exists\n", 19, 0);
        return -1;
    }
    
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        send(client_fd, "Failed to create file\n", 21, 0);
        return -1;
    }
    
    fclose(fp);
    log_version(username, filename, "created");
    send(client_fd, "File created successfully\n", 25, 0);
    return 0;
}

// 列出所有项目
int list_projects(int client_fd, const char *username) {
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "./workspaces/%s", username);
    
    DIR *dir = opendir(dir_path);
    if (!dir) {
        send(client_fd, "Failed to open workspace\n", 24, 0);
        return -1;
    }

    struct dirent *entry;
    char project_list[1024] = "Your projects:\n";
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            strcat(project_list, entry->d_name);
            strcat(project_list, "\n");
        }
    }
    
    send(client_fd, project_list, strlen(project_list), 0);
    closedir(dir);
    return 0;
}
// 检查项目是否存在
int check_project_exists(const char *username, const char *project_name) {
    char project_path[256];
    snprintf(project_path, sizeof(project_path), "./workspaces/%s/%s", username, project_name);
    struct stat statbuf;
    return (stat(project_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode));  // 如果存在并且是目录
}

// 列举项目中的文件
void list_files_in_project(int client_fd, const char *username, const char *project_name) {
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "./workspaces/%s/%s", username, project_name);
    DIR *dir = opendir(dir_path);
    if (!dir) {
        send(client_fd, "Failed to open project directory.\n", 34, 0);
        return;
    }

    struct dirent *entry;
    char file_list[1024] = "Files in project:\n";
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            strcat(file_list, entry->d_name);
            strcat(file_list, "\n");
        }
    }
    closedir(dir);
    send(client_fd, file_list, strlen(file_list), 0);
}

// 创建新文件
void create_new_file(int client_fd, const char *username, const char *project_name) {
    send(client_fd, "Enter file name: ", 16, 0);
    char filename[128];
    ssize_t len = recv(client_fd, filename, sizeof(filename) - 1, 0);
    if (len > 0) {
        filename[len] = '\0';
        trim_newline(filename);
        create_project_file(client_fd, username, project_name, filename);
    }
}

// 打开或编辑文件
void open_or_edit_file(int client_fd, const char *username, const char *project_name) {
    send(client_fd, "Enter file name: ", 16, 0);
    char filename[128];
    ssize_t len = recv(client_fd, filename, sizeof(filename) - 1, 0);
    if (len > 0) {
        filename[len] = '\0';
        trim_newline(filename);

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "./workspaces/%s/%s/%s", username, project_name, filename);
        
        // 显示文件内容
        FILE *fp = fopen(filepath, "r");
        if (fp) {
            char content[4096] = {0};
            size_t bytes = fread(content, 1, sizeof(content) - 1, fp);
            fclose(fp);
            if (bytes > 0) {
                content[bytes] = '\0';
                send(client_fd, "Current file content:\n", 21, 0);
                send(client_fd, content, bytes, 0);
            }

            // 询问是否要编辑
            send(client_fd, "\nDo you want to edit this file? (yes/no): ", 40, 0);
            char answer[8];
            len = recv(client_fd, answer, sizeof(answer) - 1, 0);
            if (len > 0) {
                answer[len] = '\0';
                trim_newline(answer);
                if (strcmp(answer, "yes") == 0) {
                    edit_file(client_fd, username, project_name, filename);
                }
            }
        } else {
            send(client_fd, "Failed to open file.\n", 21, 0);
        }
    }
}

// 上传文件
void upload_file(int client_fd, const char *username, const char *project_name) {
    send(client_fd, "Enter file name to upload: ", 26, 0);
    char filename[128];
    ssize_t len = recv(client_fd, filename, sizeof(filename) - 1, 0);
    if (len > 0) {
        filename[len] = '\0';
        trim_newline(filename);

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "./workspaces/%s/%s/%s", username, project_name, filename);
        printf("UPLOAD_CODE\n");
        save_file(client_fd, filepath);
        log_version(username, filename, "uploaded");
        send(client_fd, "File uploaded successfully.\n", 27, 0);
    }
    else {
        printf("BLOCK\n");
    }
}

int handle_project_menu(int client_fd, const char *username, const char *project_name) {
    // 首先检查项目是否存在
    if (!check_project_exists(username, project_name)) {
        send(client_fd, "Project does not exist.\n", 24, 0);
        return -1;  // 如果项目不存在，直接返回
    }

    while (1) {
        const char submenu[] = 
            "Project Menu:\n"
            "a. List Files\n"
            "b. Create New File\n"
            "c. Open/Edit File\n"
            "d. Upload File\n"
            "e. Download File\n"
            "f. Return to Main Menu\n";
        
        send(client_fd, submenu, strlen(submenu), 0);

        char buffer[BUF_SIZE];
        ssize_t len = recv(client_fd, buffer, sizeof(buffer), 0);
        if (len <= 0) return -1;  // 客户端断开连接
        buffer[len] = '\0';
        trim_newline(buffer);

        switch (buffer[0]) {
            case 'a':
                list_files_in_project(client_fd, username, project_name);
                break;
            case 'b':
                create_new_file(client_fd, username, project_name);
                break;
            case 'c':
                open_or_edit_file(client_fd, username, project_name);
                break;
            case 'd':
                upload_file(client_fd, username, project_name);
                break;
            case 'e':
                //download_file(client_fd, username, project_name);
                break;
            case 'f':
                send(client_fd, "\nReturning to Main Menu...\n", 28, 0);
                return 0;
            default:
                send(client_fd, "Invalid option. Please choose a valid option.\n", 46, 0);
        }
    }
    return 0;
}





// 删除项目目录及其所有内容
int delete_project(int client_fd, const char *username, const char *project_name) {
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "./workspaces/%s/%s", username, project_name);
    
    // 检查项目目录是否存在
    if (access(dir_path, F_OK) != 0) {
        send(client_fd, "Project does not exist\n", 22, 0);
        return -1;
    }

    // 遍历删除目录中的所有文件
    DIR *dir = opendir(dir_path);
    if (!dir) {
        send(client_fd, "Failed to open project directory\n", 31, 0);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
        
        if (remove(file_path) != 0) {
            closedir(dir);
            send(client_fd, "Failed to delete project files\n", 30, 0);
            return -1;
        }
    }
    closedir(dir);

    // 删除项目目录
    if (rmdir(dir_path) != 0) {
        send(client_fd, "Failed to delete project directory\n", 33, 0);
        return -1;
    }

    log_version(username, project_name, "project deleted");
    send(client_fd, "Project deleted successfully\n", 27, 0);
    return 0;
}

// 远程终端命令执行函数
int execute_remote_command(int client_fd, const char *username) {
    char command[256];
    char user_dir[256];
    snprintf(user_dir, sizeof(user_dir), "./workspaces/%s", username);

    // 保存当前目录
    char original_dir[256];
    if (getcwd(original_dir, sizeof(original_dir)) == NULL) {
        send(client_fd, "Failed to get current directory\n", 31, 0);
        return -1;
    }

    // 切换到用户目录
    if (chdir(user_dir) != 0) {
        send(client_fd, "Failed to change directory\n", 27, 0);
        return -1;
    }

    while (1) {
        send(client_fd, "Enter command to execute (or 'exit' to quit): ", 45, 0);
        ssize_t len = recv(client_fd, command, sizeof(command) - 1, 0);
        if (len <= 0) break;
        command[len] = '\0';
        trim_newline(command);

        if (strcmp(command, "exit") == 0) {
            break;
        }

        // 处理cd命令
        if (strncmp(command, "cd ", 3) == 0) {
            const char *path = command + 3;
            if (chdir(path) != 0) {
                send(client_fd, "Failed to change directory\n", 27, 0);
            }
            continue;
        }

        FILE *fp = popen(command, "r");
        if (fp == NULL) {
            send(client_fd, "Failed to execute command\n", 25, 0);
            continue;
        }

        char result[1024];
        while (fgets(result, sizeof(result), fp) != NULL) {
            send(client_fd, result, strlen(result), 0);
        }

        pclose(fp);
    }

    chdir(original_dir); // 切换回原目录
    return 0;
}
void create_directory(const char *dir_path) {
    // 如果目录已经存在，就不报错
    if (mkdir(dir_path, 0755) < 0) {
        if (errno != EEXIST) {
            perror("mkdir failed");
        }
    }
}
// 接收文件或目录
void recv_directory(int client_socket) {
    int type;
    char dir_name[BUF_SIZE];
        send(client_socket, "Enter project name to upload: ", 30,0);
    // 接收客户端传送过来的目录名称
     recv(client_socket, dir_name, sizeof(dir_name), 0);
    printf("Receiving directory: %s\n", dir_name);

    // 创建客户端传送过来的目录
    create_directory(dir_name);
    recv(client_socket, &type, sizeof(type), 0);  // 接收文件类型标志

    char filepath[BUF_SIZE];
    while(1){
    size_t received = recv(client_socket, filepath, sizeof(filepath), 0);  // 接收路径
    if(received<=0){break;}
    printf("Received path: %s\n", filepath);

    if (type == 1) {  // 普通文件
        printf("It's a regular file: %s\n", filepath);
        save_file(client_socket, filepath);
    } else if (type == 2) {  // 目录
        printf("It's a directory: %s\n", filepath);
        create_directory(filepath);
    } else {
        printf("Unknown file type\n");
        break;
    }
    }
}
int handle_client(int client_fd, user_info *user) {
    char buffer[BUF_SIZE];
    ssize_t len = recv(client_fd, buffer, sizeof(buffer),0);
    printf("%s\n",buffer);
    if (len <= 0) {
        return -1; // 客户端断开连接
    }

    buffer[len] = '\0';
    trim_newline(buffer);
    // 原有的处理逻辑保持不变
    if (strcmp(buffer, "1") == 0) {
        send(client_fd, "Option 1 selected\n", 18, 0);
    } else if (strcmp(buffer, "2") == 0) {
        user_register(client_fd, user, 50);
    } else if (strcmp(buffer, "3") == 0) {
        int rev = user_login(client_fd, user, 50);
        if(rev == 1) {
            create_workspace(user->username);
            while(1) {
            const    char main_menu[] = 
                    "Main Menu:\n"
                    "1. List Projects\n"
                    "2. Create New Project\n"
                    "3. Open Project\n"
                    "4. Delete Project\n"
                    "5. Upload Project\n"
                    "6. Download Project\n"
                    "7. Execute Remote Command\n"  // 新增选项
                    "8. Logout\n";
                
                send(client_fd, main_menu, strlen(main_menu), 0);
                
                char buffer[BUF_SIZE];
                ssize_t len = recv(client_fd, buffer, sizeof(buffer), 0);
                if (len <= 0) break;
                buffer[len] = '\0';
                trim_newline(buffer);

                switch(buffer[0]) {
                    case '1':
                        list_projects(client_fd, user->username);
                        break;
                    case '2': {
                        send(client_fd, "Enter project name: ", 19, 0);
                        char project_name[128];
                        len = recv(client_fd, project_name, sizeof(project_name)-1, 0);
                        if (len > 0) {
                            project_name[len] = '\0';
                            trim_newline(project_name);
                            create_project_directory(client_fd, user->username, project_name);
                        }
                        break;
                    }
                    case '3': {
                        send(client_fd, "Enter project name: ", 19, 0);
                        char project_name[128];
                        len = recv(client_fd, project_name, sizeof(project_name)-1, 0);
                        if (len > 0) {
                            project_name[len] = '\0';
                            trim_newline(project_name);
                            handle_project_menu(client_fd, user->username, project_name);

                        }
                        break;
                    }
                    case '4': {
                        send(client_fd, "Enter project name to delete: ", 29, 0);
                        char project_name[128];
                        len = recv(client_fd, project_name, sizeof(project_name)-1, 0);
                        if (len > 0) {
                            project_name[len] = '\0';
                            trim_newline(project_name);
                            // 添加确认步骤
                            send(client_fd, "Are you sure to delete this project? (yes/no): ", 45, 0);
                            char confirm[8];
                            len = recv(client_fd, confirm, sizeof(confirm)-1, 0);
                            if (len > 0) {
                                confirm[len] = '\0';
                                trim_newline(confirm);
                                if (strcmp(confirm, "yes") == 0) {
                                    delete_project(client_fd, user->username, project_name);
                                } else {
                                    send(client_fd, "Project deletion cancelled\n", 25, 0);
                                }
                            }
                        }
                        break;
                    }
                    case '5':
                        recv_directory(client_fd);
                        break;
                    case '6':
                        //download_project(client_fd, user->username);
                        break;
                    case '7':
                        execute_remote_command(client_fd, user->username);
                        break;
                    case '8':
                        send(client_fd, "Logging out...\n", 14, 0);
                        return 0;
                    default:
                        send(client_fd, "Invalid option\n", 14, 0);
                }
            }
        }
    } else if (strcmp(buffer, "4") == 0) {
        send(client_fd, "Goodbye!\n", 9, 0);
        return -1; // 客户端主动退出
    } else {
        send(client_fd, "Invalid option.\n", 17, 0);
    }
    return 0; // 正常处理完成
}
