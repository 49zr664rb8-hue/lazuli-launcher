/* ==========================================================================
 *  relay.c —— 琉璃启动器联机中继服务器（独立小程序，纯 Win32 Socket）
 *
 *  协议（纯文本，行结束 \n）：
 *    房主连接 → "HOST <邀请码>\n"   ← 服务器回 "OK\n" 或 "ERR <原因>\n"
 *    好友连接 → "JOIN <邀请码>\n"   ← 服务器回 "OK\n" 或 "ERR <原因>\n"
 *    配对成功后服务器双向转发字节流，直到任意一方断开。
 *
 *  编译：  gcc -O2 relay.c -o relay.exe -lws2_32
 *  运行：  relay.exe [端口]        默认 8788
 * ========================================================================== */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_ROOMS 128
#define MAX_CODE  16

typedef struct {
    char  code[MAX_CODE];
    SOCKET sock;
    int   used;
} Room;

static Room  g_rooms[MAX_ROOMS];
static CRITICAL_SECTION g_lock;

typedef struct { SOCKET a, b; } Pair;

static void logmsg(const char *fmt, ...) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
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

static DWORD WINAPI pipe_thread(LPVOID p) {
    Pair *pr = (Pair *)p;
    SOCKET a = pr->a, b = pr->b;
    free(pr);
    char buf[16384];
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(a, &rd);
        FD_SET(b, &rd);
        struct timeval tv = { 30, 0 };
        int n = select(0, &rd, NULL, NULL, &tv);
        if (n <= 0) break;                    /* 超时或出错 → 结束会话 */
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
    closesocket(a);
    closesocket(b);
    return 0;
}

/* 注册房主，返回 1 成功 */
static int room_register(const char *code, SOCKET s) {
    int ok = 0;
    EnterCriticalSection(&g_lock);
    int free_slot = -1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].used && !strcmp(g_rooms[i].code, code)) { free_slot = -2; break; }
        if (!g_rooms[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        g_rooms[free_slot].used = 1;
        g_rooms[free_slot].sock = s;
        snprintf(g_rooms[free_slot].code, MAX_CODE, "%s", code);
        ok = 1;
    }
    LeaveCriticalSection(&g_lock);
    return ok;
}

/* 取出房主，返回 SOCKET（失败 INVALID_SOCKET） */
static SOCKET room_take(const char *code) {
    SOCKET s = INVALID_SOCKET;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].used && !strcmp(g_rooms[i].code, code)) {
            s = g_rooms[i].sock;
            g_rooms[i].used = 0;
            g_rooms[i].sock = INVALID_SOCKET;
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
    return s;
}

static void room_drop(SOCKET s) {
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_ROOMS; i++)
        if (g_rooms[i].used && g_rooms[i].sock == s) { g_rooms[i].used = 0; break; }
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI client_thread(LPVOID p) {
    SOCKET s = (SOCKET)(uintptr_t)p;
    char line[256];
    if (recv_line(s, line, sizeof(line)) <= 0) { closesocket(s); return 0; }

    char role[16] = {0}, code[MAX_CODE] = {0};
    if (sscanf(line, "%15s %15s", role, code) != 2) {
        send_all(s, "ERR bad_request\n", 17);
        closesocket(s);
        return 0;
    }

    if (!_stricmp(role, "HOST")) {
        if (!room_register(code, s)) {
            send_all(s, "ERR busy\n", 9);
            logmsg("房主 %s 注册失败（邀请码已被占用）", code);
            closesocket(s);
            return 0;
        }
        send_all(s, "OK\n", 3);
        logmsg("房主上线，邀请码 %s", code);
        /* 等待被配对：这里阻塞在 select 上，配对后由另一方接管 */
        for (;;) {
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(s, &rd);
            struct timeval tv = { 1, 0 };
            int n = select(0, &rd, NULL, NULL, &tv);
            if (n < 0) break;
            if (n > 0) {
                /* 有数据说明好友已接入并被 pipe 线程接管？不 —— 房主侧只等待被取出 */
                char tmp[64];
                int r = recv(s, tmp, sizeof(tmp), MSG_PEEK);
                if (r <= 0) break;      /* 房主断开 */
            }
            /* 检查是否已被取出 */
            EnterCriticalSection(&g_lock);
            int still = 0;
            for (int i = 0; i < MAX_ROOMS; i++)
                if (g_rooms[i].used && g_rooms[i].sock == s) still = 1;
            LeaveCriticalSection(&g_lock);
            if (!still) {
                /* 已被 JOIN 取走，本线程退出，由配对线程负责转发 */
                logmsg("房主 %s 已与好友配对", code);
                return 0;
            }
        }
        room_drop(s);
        closesocket(s);
        logmsg("房主 %s 下线", code);
        return 0;
    }

    if (!_stricmp(role, "JOIN")) {
        SOCKET host = room_take(code);
        if (host == INVALID_SOCKET) {
            send_all(s, "ERR no_room\n", 12);
            logmsg("加入 %s 失败（房间不存在）", code);
            closesocket(s);
            return 0;
        }
        send_all(s, "OK\n", 3);
        logmsg("好友加入 %s，开始转发", code);
        Pair *pr = (Pair *)malloc(sizeof(Pair));
        pr->a = host;
        pr->b = s;
        pipe_thread(pr);
        logmsg("房间 %s 会话结束", code);
        return 0;
    }

    send_all(s, "ERR bad_role\n", 13);
    closesocket(s);
    return 0;
}

int main(int argc, char **argv) {
    int port = 8788;
    if (argc > 1) port = atoi(argv[1]);
    if (port <= 0 || port > 65535) port = 8788;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup 失败\n");
        return 1;
    }
    InitializeCriticalSection(&g_lock);

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { printf("创建套接字失败\n"); return 1; }
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((u_short)port);
    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) != 0) {
        printf("端口 %d 绑定失败\n", port);
        return 1;
    }
    listen(ls, 16);
    logmsg("琉璃联机中继已启动，端口 %d", port);
    logmsg("把本机地址填进启动器「设置 → 联机中继」，例如 your.ip:%d", port);

    for (;;) {
        struct sockaddr_in from;
        int fl = sizeof(from);
        SOCKET c = accept(ls, (struct sockaddr *)&from, &fl);
        if (c == INVALID_SOCKET) continue;
        logmsg("新连接来自 %s", inet_ntoa(from.sin_addr));
        CloseHandle(CreateThread(NULL, 0, client_thread, (LPVOID)(uintptr_t)c, 0, NULL));
    }
    return 0;
}
