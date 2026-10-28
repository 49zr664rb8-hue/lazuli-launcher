/*  mp.c —— 快捷联机：局域网发现 + 邀请码中继隧道 */
#include "app.h"
#include <winsock2.h>
#include <ws2tcpip.h>

LanWorld g_lan[MAX_LAN];
int      g_nlan = 0;
Room     g_room;

static volatile int g_lan_stop = 0;

void mp_init(void) {
    static int done = 0;
    if (done) return;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    done = 1;
}

/* ---------------------------------------------------------------- TCP 工具 */
static SOCKET connect_tcp(const char *host, int port, int timeout_ms) {
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return INVALID_SOCKET;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); return INVALID_SOCKET; }

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    connect(s, res->ai_addr, (int)res->ai_addrlen);
    fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
    struct timeval tv; tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(0, NULL, &wf, NULL, &tv);
    freeaddrinfo(res);
    if (r <= 0) { closesocket(s); return INVALID_SOCKET; }
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    int err = 0, el = sizeof(err);
    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &el);
    if (err) { closesocket(s); return INVALID_SOCKET; }
    return s;
}

static int send_all(SOCKET s, const char *d, int n) {
    int sent = 0;
    while (sent < n) {
        int r = send(s, d + sent, n - sent, 0);
        if (r <= 0) return 0;
        sent += r;
    }
    return 1;
}

static int recv_line(SOCKET s, char *out, int cap) {
    int i = 0;
    while (i < cap - 1) {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\n') break;
        if (c != '\r') out[i++] = c;
    }
    out[i] = 0;
    return i;
}

static void pipe_two(SOCKET a, SOCKET b, volatile int *stop) {
    char buf[16384];
    for (;;) {
        if (stop && *stop) break;
        fd_set rd; FD_ZERO(&rd);
        FD_SET(a, &rd); FD_SET(b, &rd);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 400000;
        int n = select(0, &rd, NULL, NULL, &tv);
        if (n <= 0) continue;
        if (FD_ISSET(a, &rd)) {
            int r = recv(a, buf, sizeof(buf), 0);
            if (r <= 0) break;
            if (!send_all(b, buf, r)) break;
        }
        if (FD_ISSET(b, &rd)) {
            int r = recv(b, buf, sizeof(buf), 0);
            if (r <= 0) break;
            if (!send_all(a, buf, r)) break;
        }
    }
}

/* ---------------------------------------------------------------- 局域网发现 */
static DWORD WINAPI lan_thread(LPVOID p) {
    LZ_UNUSED(p);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(4445);
    a.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0) { closesocket(s); return 0; }

    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr("224.0.2.60");
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq));

    char buf[2048];
    while (!g_lan_stop) {
        fd_set rd; FD_ZERO(&rd); FD_SET(s, &rd);
        struct timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
        if (select(0, &rd, NULL, NULL, &tv) <= 0) {
            /* 清理过期条目 */
            double now = lz_now_sec();
            EnterCriticalSection(&g_lock);
            for (int i = 0; i < g_nlan; i++) {
                if (now - g_lan[i].t > 12.0) {
                    for (int k = i; k < g_nlan - 1; k++) g_lan[k] = g_lan[k + 1];
                    g_nlan--; i--;
                }
            }
            LeaveCriticalSection(&g_lock);
            continue;
        }
        struct sockaddr_in from;
        int fl = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) continue;
        buf[n] = 0;

        char motd[160] = "局域网世界";
        int port = 0;
        const char *m = strstr(buf, "[MOTD]");
        if (m) {
            const char *e = strstr(m + 6, "[/MOTD]");
            if (e) {
                int len = (int)(e - m - 6);
                if (len > 155) len = 155;
                memcpy(motd, m + 6, (size_t)len);
                motd[len] = 0;
            }
        }
        const char *ad = strstr(buf, "[AD]");
        if (ad) port = atoi(ad + 4);
        if (port <= 0 || port > 65535) continue;

        char ip[64];
        snprintf(ip, sizeof(ip), "%s", inet_ntoa(from.sin_addr));

        EnterCriticalSection(&g_lock);
        int found = -1;
        for (int i = 0; i < g_nlan; i++)
            if (g_lan[i].port == port && !strcmp(g_lan[i].addr, ip)) { found = i; break; }
        if (found < 0 && g_nlan < MAX_LAN) { found = g_nlan++; }
        if (found >= 0) {
            snprintf(g_lan[found].motd, sizeof(g_lan[found].motd), "%s", motd);
            snprintf(g_lan[found].addr, sizeof(g_lan[found].addr), "%s", ip);
            g_lan[found].port = port;
            g_lan[found].t = lz_now_sec();
        }
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
    }
    closesocket(s);
    return 0;
}

/* ---------------------------------------------------------------- 中继地址 */
static int relay_host_port(char *host, int *port) {
    char tmp[300];
    EnterCriticalSection(&g_lock);
    snprintf(tmp, sizeof(tmp), "%s", g.set.relay);
    LeaveCriticalSection(&g_lock);
    lz_trim(tmp);
    if (!tmp[0]) return 0;
    char *colon = strrchr(tmp, ':');
    if (colon) {
        *colon = 0;
        *port = atoi(colon + 1);
        snprintf(host, 200, "%s", tmp);
    } else {
        snprintf(host, 200, "%s", tmp);
        *port = 8788;
    }
    /* 去掉 http:// 前缀 */
    char *p = strstr(host, "://");
    if (p) memmove(host, p + 3, strlen(p + 3) + 1);
    return host[0] != 0;
}

/* ---------------------------------------------------------------- 主机隧道 */
static DWORD WINAPI host_tunnel(LPVOID p) {
    LZ_UNUSED(p);
    char host[200]; int port = 8788;
    if (!relay_host_port(host, &port)) {
        snprintf(g_room.err, sizeof(g_room.err), "请先在设置里填写中继服务器地址");
        g_room.tunnel_running = 0;
        ui_request_redraw();
        return 0;
    }
    while (!g_room.tunnel_stop) {
        SOCKET r = connect_tcp(host, port, 6000);
        if (r == INVALID_SOCKET) {
            snprintf(g_room.err, sizeof(g_room.err), "无法连接中继服务器 %s:%d", host, port);
            g_room.tunnel_running = 0;
            ui_request_redraw();
            Sleep(4000);
            continue;
        }
        char line[64];
        snprintf(line, sizeof(line), "HOST %s\n", g_room.code);
        send_all(r, line, (int)strlen(line));

        char rb[64];
        int n = recv_line(r, rb, sizeof(rb));
        if (n < 0 || strncmp(rb, "OK", 2)) {
            snprintf(g_room.err, sizeof(g_room.err), "房间码被占用或中继拒绝：%s", rb);
            closesocket(r);
            g_room.tunnel_running = 0;
            ui_request_redraw();
            Sleep(3000);
            continue;
        }

        SOCKET l = connect_tcp("127.0.0.1", g_room.lan_port, 5000);
        if (l == INVALID_SOCKET) {
            snprintf(g_room.err, sizeof(g_room.err),
                     "无法连接本地端口 %d，请先在游戏里「对局域网开放」", g_room.lan_port);
            closesocket(r);
            g_room.tunnel_running = 0;
            ui_request_redraw();
            Sleep(3000);
            continue;
        }
        g_room.err[0] = 0;
        g_room.tunnel_running = 1;
        snprintf(g_room.msg, sizeof(g_room.msg), "好友已接入，正在游戏中…");
        ui_request_redraw();
        pipe_two(r, l, &g_room.tunnel_stop);
        closesocket(l);
        closesocket(r);
        g_room.tunnel_running = 0;
        snprintf(g_room.msg, sizeof(g_room.msg), "等待好友加入…");
        ui_request_redraw();
        Sleep(300);
    }
    return 0;
}

/* ---------------------------------------------------------------- 访客隧道 */
static DWORD WINAPI guest_tunnel(LPVOID p) {
    LZ_UNUSED(p);
    char host[200]; int port = 8788;
    if (!relay_host_port(host, &port)) {
        snprintf(g_room.err, sizeof(g_room.err), "请先在设置里填写中继服务器地址");
        ui_request_redraw();
        return 0;
    }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return 0;
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    a.sin_port = htons((u_short)g_room.local_port);
    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(ls, 4) != 0) {
        snprintf(g_room.err, sizeof(g_room.err), "本地端口 %d 被占用", g_room.local_port);
        closesocket(ls);
        ui_request_redraw();
        return 0;
    }
    snprintf(g_room.msg, sizeof(g_room.msg), "隧道已就绪，可启动游戏");
    ui_request_redraw();

    while (!g_room.tunnel_stop) {
        fd_set rd; FD_ZERO(&rd); FD_SET(ls, &rd);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 400000;
        if (select(0, &rd, NULL, NULL, &tv) <= 0) continue;
        SOCKET c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET) continue;

        SOCKET r = connect_tcp(host, port, 8000);
        if (r == INVALID_SOCKET) {
            snprintf(g_room.err, sizeof(g_room.err), "无法连接中继服务器");
            closesocket(c);
            ui_request_redraw();
            continue;
        }
        char line[64];
        snprintf(line, sizeof(line), "JOIN %s\n", g_room.code);
        send_all(r, line, (int)strlen(line));
        char rb[64];
        int n = recv_line(r, rb, sizeof(rb));
        if (n < 0 || strncmp(rb, "OK", 2)) {
            snprintf(g_room.err, sizeof(g_room.err), "房间不存在或房主已离开");
            closesocket(r); closesocket(c);
            ui_request_redraw();
            continue;
        }
        g_room.tunnel_running = 1;
        snprintf(g_room.msg, sizeof(g_room.msg), "已连接房主，正在游戏中…");
        ui_request_redraw();
        pipe_two(c, r, &g_room.tunnel_stop);
        closesocket(r);
        closesocket(c);
        g_room.tunnel_running = 0;
        snprintf(g_room.msg, sizeof(g_room.msg), "连接已断开，等待重新连接…");
        ui_request_redraw();
    }
    closesocket(ls);
    return 0;
}

/* ---------------------------------------------------------------- 对外接口 */
static void gen_code(char *out, int n) {
    static const char *cs = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < n; i++) out[i] = cs[lz_rand(0, (unsigned)strlen(cs) - 1)];
    out[n] = 0;
}

void mp_create_room_async(int lan_port, const char *motd) {
    mp_leave_room();
    mp_init();
    char host[200]; int port = 8788;
    relay_host_port(host, &port);
    EnterCriticalSection(&g_lock);
    memset(&g_room, 0, sizeof(g_room));
    gen_code(g_room.code, 6);
    snprintf(g_room.host, sizeof(g_room.host), "%s", host);
    g_room.port = port;
    g_room.lan_port = lan_port;
    g_room.active = 1;
    g_room.local_port = lan_port;
    g_room.tunnel_stop = 0;
    snprintf(g_room.msg, sizeof(g_room.msg), "等待好友加入…");
    LeaveCriticalSection(&g_lock);
    if (motd && *motd) app_toast("房间已创建：%s", g_room.code);
    CloseHandle(CreateThread(NULL, 0, host_tunnel, NULL, 0, NULL));
    ui_request_redraw();
}

void mp_join_room_async(const char *code) {
    mp_leave_room();
    mp_init();
    char c[16];
    snprintf(c, sizeof(c), "%s", code);
    for (char *p = c; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    lz_trim(c);
    if (strlen(c) < 4) { app_toast("邀请码不正确"); return; }

    char host[200]; int port = 8788;
    relay_host_port(host, &port);
    EnterCriticalSection(&g_lock);
    memset(&g_room, 0, sizeof(g_room));
    snprintf(g_room.code, sizeof(g_room.code), "%s", c);
    snprintf(g_room.host, sizeof(g_room.host), "%s", host);
    g_room.port = port;
    g_room.active = 1;
    g_room.local_port = 25565;
    g_room.tunnel_stop = 0;
    snprintf(g_room.msg, sizeof(g_room.msg), "正在建立隧道…");
    LeaveCriticalSection(&g_lock);
    CloseHandle(CreateThread(NULL, 0, guest_tunnel, NULL, 0, NULL));
    ui_request_redraw();
}

void mp_leave_room(void) {
    g_room.tunnel_stop = 1;
    Sleep(150);
    EnterCriticalSection(&g_lock);
    g_room.active = 0;
    g_room.tunnel_running = 0;
    g_room.msg[0] = 0;
    g_room.err[0] = 0;
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
}

void mp_scan_start(void) {
    mp_init();
    CloseHandle(CreateThread(NULL, 0, lan_thread, NULL, 0, NULL));
}
