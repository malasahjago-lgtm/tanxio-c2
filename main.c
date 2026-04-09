#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <signal.h>

#define PORT 1337
#define BUFFER_SIZE 4096

typedef struct {
    char username[64];
    char password[64];
} User;

User users[100];
int user_count = 0;

void sendall(int fd, const char *msg) {
    send(fd, msg, strlen(msg), 0);
}

void sendln(int fd) {
    send(fd, "\n", 1, 0);
}

char* trim(char *str) {
    while (isspace(*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    end[1] = 0;
    return str;
}

void load_users() {
    FILE *fp = fopen("config/users.json", "r");
    if (!fp) {
        printf("[ERROR] users.json not found\n");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *data = malloc(size + 1);
    fread(data, 1, size, fp);
    data[size] = 0;
    fclose(fp);

    char *p = data;
    while ((p = strstr(p, "\"username\"")) != NULL && user_count < 100) {
        char u[64] = {0};
        char pw[64] = {0};

        sscanf(p, "\"username\"%*[^:]:\"%63[^\"]\"", u);

        char *pp = strstr(p, "\"password\"");
        if (pp) {
            sscanf(pp, "\"password\"%*[^:]:\"%63[^\"]\"", pw);
        }

        if (strlen(u) && strlen(pw)) {
            strcpy(users[user_count].username, u);
            strcpy(users[user_count].password, pw);
            user_count++;
        }

        p += 10;
    }

    free(data);
    printf("[INFO] Loaded %d users\n", user_count);
}

void print_banner(int fd) {
    sendall(fd, "\n");
    sendall(fd, "================================\n");
    sendall(fd, "       [ TANXIO CNC ]\n");
    sendall(fd, "================================\n");
    sendall(fd, "\n");
}

void print_login(int fd) {
    print_banner(fd);
    sendall(fd, "  Username: ");
}

void terminal(int fd, char *username) {
    char buf[256];

    print_banner(fd);
    sendall(fd, "  Welcome "); sendall(fd, username); sendln(fd);
    sendall(fd, "  Type !help for commands\n");

    while (1) {
        sendall(fd, "\ntanxio@server$ ");

        char buffer[BUFFER_SIZE];
        memset(buffer, 0, sizeof(buffer));

        int bytes = recv(fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;

        char *line = strtok(buffer, "\r\n");
        if (!line) continue;

        char *cmd = trim(line);
        if (strlen(cmd) == 0) continue;

        if (strcmp(cmd, "!logout") == 0) {
            sendall(fd, "\n  Goodbye!\n");
            sleep(1);
            break;
        }
        else if (strcmp(cmd, "!help") == 0) {
            sendall(fd, "\n  Commands:\n");
            sendall(fd, "  !help    - Show commands\n");
            sendall(fd, "  !clear   - Clear screen\n");
            sendall(fd, "  !logout  - Logout\n");
        }
        else if (strcmp(cmd, "!clear") == 0) {
            print_banner(fd);
        }
        else {
            sendall(fd, "\n  Unknown command: "); sendall(fd, cmd); sendln(fd);
        }
    }
}

void handle_client(int fd) {
    char username[64];
    char password[64];

    if (user_count == 0) {
        sendall(fd, "\nNo users loaded\n");
        close(fd);
        return;
    }

    print_login(fd);

    int bytes = recv(fd, username, 63, 0);
    if (bytes <= 0) { close(fd); return; }
    username[bytes] = 0;
    char *u = trim(username);

    sendall(fd, "\n  Password: ");

    bytes = recv(fd, password, 63, 0);
    if (bytes <= 0) { close(fd); return; }
    password[bytes] = 0;
    char *p = trim(password);

    int ok = 0;
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, u) == 0 &&
            strcmp(users[i].password, p) == 0) {
            ok = 1;
            break;
        }
    }

    if (!ok) {
        sendall(fd, "\n\n  Login failed\n");
        close(fd);
        return;
    }

    printf("[+] Login: %s\n", u);
    fflush(stdout);

    terminal(fd, u);
    close(fd);
}

int main() {
    signal(SIGCHLD, SIG_IGN);

    load_users();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    printf("Server running on port %d\n", PORT);
    fflush(stdout);

    while (1) {
        int client = accept(server_fd, NULL, NULL);
        printf("[+] Client connected\n");
        fflush(stdout);

        if (!fork()) {
            close(server_fd);
            handle_client(client);
            exit(0);
        }

        close(client);
    }

    return 0;
}
