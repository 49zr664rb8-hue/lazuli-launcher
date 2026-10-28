/*  net.c —— WinHTTP 封装 + 文件队列 + 多线程分块下载 */
#include "net.h"
#include <winhttp.h>

static HINTERNET g_session = NULL;

int http_init(void) {
    if (g_session) return 1;
    /* 先用系统代理设置（不做 WPAD 自动探测，避免每个连接卡几秒） */
    g_session = WinHttpOpen(L"LazuliLauncher/1.0 (Windows NT 10.0; Win64; x64)",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_session)
        g_session = WinHttpOpen(L"LazuliLauncher/1.0",
                                WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_session) return 0;
    WinHttpSetTimeouts(g_session, 20000, 20000, 30000, 60000);
    return 1;
}

void http_shutdown(void) {
    if (g_session) { WinHttpCloseHandle(g_session); g_session = NULL; }
}

void buf_free(Buf *b) { if (b) { free(b->data); b->data = NULL; b->len = 0; } }

char *lz_url_encode(const char *s) {
    if (!s) return NULL;
    Sb b; sb_init(&b); sb_add(&b, "");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            sb_addc(&b, (char)*p);
        } else {
            char t[8]; sprintf(t, "%%%02X", *p); sb_add(&b, t);
        }
    }
    return sb_take(&b);
}

/* ==========================================================================
 *  基础 HTTP
 * ========================================================================== */
typedef int (*RecvCb)(void *ud, const char *data, DWORD len);

typedef struct {
    int status;
    long long content_length;
    char content_range[96];
    char accept_ranges[32];
} HttpInfo;

static int http_do(const char *method, const char *url,
                   const char *body, DWORD bodylen,
                   const char **hdrs, int nhdrs,
                   RecvCb cb, void *ud, HttpInfo *info) {
    if (!http_init()) return 0;
    if (info) { memset(info, 0, sizeof(*info)); info->content_length = -1; }

    wchar_t *wurl = lz_u8_to_w(url);
    if (!wurl) return 0;

    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { free(wurl); return 0; }

    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    wcsncpy(host, uc.lpszHostName, LZ_MIN(uc.dwHostNameLength, 255));
    if (uc.dwUrlPathLength) wcsncpy(path, uc.lpszUrlPath, LZ_MIN(uc.dwUrlPathLength, 2047));
    if (!path[0]) wcscpy(path, L"/");
    if (uc.dwExtraInfoLength && uc.dwExtraInfoLength < 1024) {
        size_t cur = wcslen(path);
        wcsncat(path, uc.lpszExtraInfo, LZ_MIN(uc.dwExtraInfoLength, 2047 - cur));
    }
    int secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = uc.nPort;
    free(wurl);

    HINTERNET hc = WinHttpConnect(g_session, host, port, 0);
    if (!hc) return 0;

    wchar_t *wm = lz_u8_to_w(method);
    HINTERNET hr = WinHttpOpenRequest(hc, wm, path, NULL, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      secure ? WINHTTP_FLAG_SECURE : 0);
    free(wm);
    if (!hr) { WinHttpCloseHandle(hc); return 0; }

    /* 禁用自动解压以外的奇怪行为，允许重定向 */
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hr, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

    for (int i = 0; i < nhdrs; i++) {
        wchar_t *wh = lz_u8_to_w(hdrs[i]);
        WinHttpAddRequestHeaders(hr, wh, (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        free(wh);
    }

    int ok = 0;
    if (!WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)body, bodylen, bodylen, 0)) goto done;
    if (!WinHttpReceiveResponse(hr, NULL)) goto done;

    if (info) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        info->status = (int)code;

        wchar_t buf[512]; DWORD bl = sizeof(buf);
        if (WinHttpQueryHeaders(hr, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                buf, &bl, WINHTTP_NO_HEADER_INDEX)) {
            info->content_length = _wtoi64(buf);
        }
        bl = sizeof(buf);
        if (WinHttpQueryHeaders(hr, WINHTTP_QUERY_CUSTOM, L"Content-Range",
                                buf, &bl, WINHTTP_NO_HEADER_INDEX)) {
            buf[bl / sizeof(wchar_t) < 511 ? bl / sizeof(wchar_t) : 511] = 0;
            char *u = lz_w_to_u8(buf);
            snprintf(info->content_range, sizeof(info->content_range), "%s", u ? u : "");
            free(u);
        }
        bl = sizeof(buf);
        if (WinHttpQueryHeaders(hr, WINHTTP_QUERY_CUSTOM, L"Accept-Ranges",
                                buf, &bl, WINHTTP_NO_HEADER_INDEX)) {
            buf[bl / sizeof(wchar_t) < 511 ? bl / sizeof(wchar_t) : 511] = 0;
            char *u = lz_w_to_u8(buf);
            snprintf(info->accept_ranges, sizeof(info->accept_ranges), "%s", u ? u : "");
            free(u);
        }
    }

    ok = 1;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hr, &avail)) { ok = 0; break; }
        if (avail == 0) break;
        char *tmp = (char *)malloc(avail);
        if (!tmp) { ok = 0; break; }
        DWORD got = 0;
        if (!WinHttpReadData(hr, tmp, avail, &got)) { free(tmp); ok = 0; break; }
        if (got == 0) { free(tmp); break; }
        if (cb && !cb(ud, tmp, got)) { free(tmp); ok = 0; break; }
        if (!cb) { /* 丢弃 */ }
        free(tmp);
    }

done:
    WinHttpCloseHandle(hr);
    WinHttpCloseHandle(hc);
    return ok;
}

typedef struct { Buf *b; } GetCtx;

static int get_cb(void *ud, const char *d, DWORD n) {
    GetCtx *c = (GetCtx *)ud;
    c->b->data = (char *)realloc(c->b->data, c->b->len + n + 1);
    if (!c->b->data) return 0;
    memcpy(c->b->data + c->b->len, d, n);
    c->b->len += n;
    c->b->data[c->b->len] = 0;
    return 1;
}

int http_verb_len(const char *method, const char *url, const char *body, size_t blen,
                  const char *ctype, const char **hdrs, int n, Buf *out, int *status) {
    if (out) { out->data = NULL; out->len = 0; }
    GetCtx ctx; ctx.b = out;
    const char *h[16]; int hn = 0;
    for (int i = 0; i < n && hn < 14; i++) h[hn++] = hdrs[i];
    char ct[256];
    if (ctype) { snprintf(ct, sizeof(ct), "Content-Type: %s", ctype); h[hn++] = ct; }
    HttpInfo info;
    int ok = http_do(method, url, body, body ? (DWORD)blen : 0,
                     h, hn, out ? get_cb : NULL, &ctx, &info);
    if (status) *status = info.status;
    if (!ok) return 0;
    return (info.status >= 200 && info.status < 400) ? 1 : 0;
}

int http_get(const char *url, Buf *out, int *status) {
    return http_verb_len("GET", url, NULL, 0, NULL, NULL, 0, out, status);
}

int http_get_h(const char *url, const char **hdrs, int n, Buf *out, int *status) {
    return http_verb_len("GET", url, NULL, 0, NULL, hdrs, n, out, status);
}

int http_post(const char *url, const char *body, const char *ctype,
              const char **hdrs, int n, Buf *out, int *status) {
    return http_verb_len("POST", url, body ? body : "", body ? strlen(body) : 0,
                         ctype ? ctype : "application/json", hdrs, n, out, status);
}

int http_get_text(const char *url, char **out) {
    Buf b = {0};
    int st = 0;
    int ok = http_get(url, &b, &st);
    if (!ok) { buf_free(&b); return 0; }
    if (!b.data) { b.data = (char *)calloc(1, 1); }
    *out = b.data;
    return 1;
}

int http_head_info(const char *url, long long *size, int *ranges, char *final_url, size_t cap) {
    const char *h[] = { "Range: bytes=0-0" };
    HttpInfo info;
    int ok = http_do("GET", url, NULL, 0, h, 1, NULL, NULL, &info);
    if (!ok) return 0;
    if (size) {
        long long s = -1;
        /* Content-Range: bytes 0-0/123456 */
        const char *p = strchr(info.content_range, '/');
        if (p) s = _atoi64(p + 1);
        if (s < 0 && info.content_length > 0) s = info.content_length;
        *size = s;
    }
    if (ranges) *ranges = (info.content_range[0] != 0) ? 1 : 0;
    if (final_url && cap) snprintf(final_url, cap, "%s", url);
    return 1;
}

/* ==========================================================================
 *  文件队列
 * ========================================================================== */
typedef struct FileJob {
    char url[1024];
    char path[1024];
    long long size;
    int state;              /* 0 待办 1 下载中 2 完成 3 失败 4 取消 */
    struct FileJob *next;
} FileJob;

struct FileQueue {
    CRITICAL_SECTION lock;
    FileJob *head, *tail;
    int total, done, failed;
    long long bytes_done, bytes_total;
    char last_err[600];
    int  last_status;
    volatile LONG cancel;
    int nthreads;
    HANDLE threads[16];
    void (*on_change)(void);
};

static void mkdir_for(const char *path) {
    char dir[MAX_PATH * 3];
    snprintf(dir, sizeof(dir), "%s", path);
    /* 统一分隔符：maven / 资源对象路径里用的是 '/' */
    for (char *c = dir; *c; c++) if (*c == '/') *c = '\\';
    char *p = strrchr(dir, '\\');
    if (p) { *p = 0; lz_dir_make(dir); }
}

typedef struct { HANDLE f; FileQueue *q; int err; } WrCtx;

static int fq_wr(void *ud, const char *d, DWORD n) {
    WrCtx *w = (WrCtx *)ud;
    if (w->q && w->q->cancel) return 0;
    DWORD wr = 0;
    if (!WriteFile(w->f, d, n, &wr, NULL) || wr != n) { w->err = 1; return 0; }
    InterlockedExchangeAdd64((LONG64 *)&w->q->bytes_done, n);
    return 1;
}

static DWORD WINAPI fq_worker(LPVOID param) {
    FileQueue *q = (FileQueue *)param;
    for (;;) {
        FileJob *job = NULL;
        EnterCriticalSection(&q->lock);
        if (q->head) {
            job = q->head;
            q->head = job->next;
            if (!q->head) q->tail = NULL;
            job->state = 1;
        }
        LeaveCriticalSection(&q->lock);
        if (!job) break;

        if (q->cancel) { job->state = 4; break; }

        /* 已存在且大小一致则跳过 */
        if (lz_file_exists(job->path)) {
            long long have = lz_file_size(job->path);
            if (job->size <= 0 || have == job->size) {
                job->state = 2;
                EnterCriticalSection(&q->lock);
                q->done++;
                q->bytes_done += (have > 0 ? have : 0);
                LeaveCriticalSection(&q->lock);
                free(job);
                continue;
            }
        }

        mkdir_for(job->path);
        HANDLE f = CreateFileA(job->path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) {
            job->state = 3;
            EnterCriticalSection(&q->lock);
            q->failed++;
            if (q->last_err[0] == 0)
                snprintf(q->last_err, sizeof(q->last_err), "无法创建文件(%lu) %s",
                         GetLastError(), job->path);
            LeaveCriticalSection(&q->lock);
            free(job);
            continue;
        }
        WrCtx ctx; ctx.f = f; ctx.q = q; ctx.err = 0;
        HttpInfo info;
        int ok = http_do("GET", job->url, NULL, 0, NULL, 0, fq_wr, &ctx, &info);
        CloseHandle(f);
        int good = ok && !ctx.err && info.status >= 200 && info.status < 300;

        EnterCriticalSection(&q->lock);
        if (good) { q->done++; job->state = 2; }
        else {
            q->failed++; job->state = 3;
            if (q->last_err[0] == 0) {
                snprintf(q->last_err, sizeof(q->last_err), "HTTP %d %s", info.status, job->url);
            }
            if (lz_file_exists(job->path)) DeleteFileA(job->path);
        }
        LeaveCriticalSection(&q->lock);
        if (q->on_change) q->on_change();
        free(job);
    }
    return 0;
}

FileQueue *fq_create(int nthreads, void (*on_change)(void)) {
    FileQueue *q = (FileQueue *)calloc(1, sizeof(FileQueue));
    if (!q) return NULL;
    InitializeCriticalSection(&q->lock);
    q->nthreads = LZ_CLAMP(nthreads, 1, 16);
    q->on_change = on_change;
    return q;
}

void fq_add(FileQueue *q, const char *url, const char *path) { fq_add_size(q, url, path, -1); }

void fq_add_size(FileQueue *q, const char *url, const char *path, long long size) {
    if (!q) return;
    FileJob *j = (FileJob *)calloc(1, sizeof(FileJob));
    if (!j) return;
    snprintf(j->url, sizeof(j->url), "%s", url);
    snprintf(j->path, sizeof(j->path), "%s", path);
    j->size = size;
    EnterCriticalSection(&q->lock);
    if (q->tail) q->tail->next = j; else q->head = j;
    q->tail = j;
    q->total++;
    if (size > 0) q->bytes_total += size;
    LeaveCriticalSection(&q->lock);
}

int fq_run(FileQueue *q) {
    if (!q) return 0;
    for (int i = 0; i < q->nthreads; i++)
        q->threads[i] = CreateThread(NULL, 0, fq_worker, q, 0, NULL);
    WaitForMultipleObjects((DWORD)q->nthreads, q->threads, TRUE, INFINITE);
    for (int i = 0; i < q->nthreads; i++) {
        if (q->threads[i]) { CloseHandle(q->threads[i]); q->threads[i] = NULL; }
    }
    return q->failed;
}

void fq_cancel(FileQueue *q) { if (q) q->cancel = 1; }

const char *fq_last_error(FileQueue *q) { return q ? q->last_err : ""; }

void fq_progress(FileQueue *q, int *total, int *done, int *failed,
                 long long *bytes_done, long long *bytes_total) {
    if (!q) return;
    EnterCriticalSection(&q->lock);
    if (total) *total = q->total;
    if (done) *done = q->done;
    if (failed) *failed = q->failed;
    if (bytes_done) *bytes_done = q->bytes_done;
    if (bytes_total) *bytes_total = q->bytes_total;
    LeaveCriticalSection(&q->lock);
}

void fq_destroy(FileQueue *q) {
    if (!q) return;
    for (int i = 0; i < 16; i++) if (q->threads[i]) CloseHandle(q->threads[i]);
    EnterCriticalSection(&q->lock);
    FileJob *j = q->head;
    while (j) { FileJob *n = j->next; free(j); j = n; }
    q->head = q->tail = NULL;
    LeaveCriticalSection(&q->lock);
    DeleteCriticalSection(&q->lock);
    free(q);
}

/* ==========================================================================
 *  多线程分块下载（百宝箱）
 * ========================================================================== */
struct DlTask {
    int id;
    char url[1024];
    char path[1024];
    char name[200];
    char err[200];
    int nthreads;
    volatile LONG state;          /* 0 排队 1 下载中 2 完成 3 失败 4 取消 */
    volatile LONG cancel;
    volatile LONG64 total;
    volatile LONG64 got;
    double speed;
    double t_mark;
    long long got_mark;
    HANDLE thread;
    void (*on_change)(void);
};

static volatile LONG g_dl_id = 0;

typedef struct { DlTask *t; HANDLE f; int err; } DlWr;

static int dl_wr(void *ud, const char *d, DWORD n) {
    DlWr *w = (DlWr *)ud;
    if (w->t->cancel) return 0;
    DWORD wr = 0;
    if (!WriteFile(w->f, d, n, &wr, NULL) || wr != n) { w->err = 1; return 0; }
    InterlockedExchangeAdd64(&w->t->got, n);
    return 1;
}

typedef struct { DlTask *t; long long start, end; int ok; } ChunkArg;

static DWORD WINAPI chunk_worker(LPVOID p) {
    ChunkArg *a = (ChunkArg *)p;
    DlTask *t = a->t;
    char range[128];
    snprintf(range, sizeof(range), "Range: bytes=%lld-%lld", a->start, a->end);
    const char *h[] = { range };

    HANDLE f = CreateFileA(t->path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { a->ok = 0; free(a); return 0; }
    LARGE_INTEGER li; li.QuadPart = a->start;
    SetFilePointerEx(f, li, NULL, FILE_BEGIN);

    DlWr w; w.t = t; w.f = f; w.err = 0;
    HttpInfo info;
    int r = http_do("GET", t->url, NULL, 0, h, 1, dl_wr, &w, &info);
    CloseHandle(f);
    a->ok = r && !w.err && info.status == 206;
    free(a);
    return 0;
}

static void dl_set_err(DlTask *t, const char *msg) {
    snprintf(t->err, sizeof(t->err), "%s", msg ? msg : "未知错误");
    t->state = 3;
    if (t->on_change) t->on_change();
}

static DWORD WINAPI dl_controller(LPVOID p) {
    DlTask *t = (DlTask *)p;
    long long size = -1;
    int ranges = 0;
    if (!http_head_info(t->url, &size, &ranges, NULL, 0)) {
        dl_set_err(t, "无法连接服务器");
        return 0;
    }
    if (size <= 0) {
        /* 未知大小：单线程流式 */
        ranges = 0;
        size = -1;
    }
    t->total = size;

    int n = t->nthreads;
    if (!ranges || size < (2LL << 20) || n < 2) n = 1;

    if (n == 1) {
        HANDLE f = CreateFileA(t->path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) { dl_set_err(t, "无法写入文件"); return 0; }
        DlWr w; w.t = t; w.f = f; w.err = 0;
        HttpInfo info;
        t->state = 1;
        int r = http_do("GET", t->url, NULL, 0, NULL, 0, dl_wr, &w, &info);
        CloseHandle(f);
        if (t->cancel) { t->state = 4; }
        else if (r && !w.err && info.status >= 200 && info.status < 300) t->state = 2;
        else dl_set_err(t, "下载失败");
        if (t->on_change) t->on_change();
        return 0;
    }

    /* 预创建文件 */
    HANDLE f = CreateFileA(t->path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { dl_set_err(t, "无法创建文件"); return 0; }
    CloseHandle(f);

    t->state = 1;
    long long chunk = size / n;
    HANDLE th[32];
    int spawned = 0;
    for (int i = 0; i < n; i++) {
        ChunkArg *a = (ChunkArg *)calloc(1, sizeof(ChunkArg));
        a->t = t;
        a->start = (long long)i * chunk;
        a->end = (i == n - 1) ? size - 1 : a->start + chunk - 1;
        a->ok = 1;
        /* ChunkArg 需要留在堆上给线程用，线程结束时自行释放；
           为了取回 ok 状态，改为在堆上保存指针数组 */
        th[spawned] = CreateThread(NULL, 0, chunk_worker, a, 0, NULL);
        if (th[spawned]) spawned++;
    }

    /* 等待并刷新速度 */
    t->t_mark = lz_now_sec();
    t->got_mark = 0;
    for (;;) {
        DWORD r = WaitForMultipleObjects((DWORD)spawned, th, TRUE, 250);
        double now = lz_now_sec();
        if (now - t->t_mark >= 0.4) {
            long long g = t->got;
            t->speed = (double)(g - t->got_mark) / (now - t->t_mark);
            t->got_mark = g; t->t_mark = now;
            if (t->on_change) t->on_change();
        }
        if (r == WAIT_OBJECT_0) break;
        if (t->cancel) { t->state = 4; break; }
    }
    for (int i = 0; i < spawned; i++) CloseHandle(th[i]);

    if (t->state != 4) {
        if (t->got >= size) t->state = 2;
        else dl_set_err(t, "分块下载未完成");
    }
    t->speed = 0;
    if (t->on_change) t->on_change();
    return 0;
}

DlTask *dl_start(const char *url, const char *path, int nthreads, void (*on_change)(void)) {
    DlTask *t = (DlTask *)calloc(1, sizeof(DlTask));
    if (!t) return NULL;
    t->id = (int)InterlockedIncrement(&g_dl_id);
    snprintf(t->url, sizeof(t->url), "%s", url);
    snprintf(t->path, sizeof(t->path), "%s", path);
    const char *nm = strrchr(path, '\\');
    snprintf(t->name, sizeof(t->name), "%s", nm ? nm + 1 : path);
    t->nthreads = LZ_CLAMP(nthreads, 1, 32);
    t->on_change = on_change;
    mkdir_for(path);
    t->thread = CreateThread(NULL, 0, dl_controller, t, 0, NULL);
    return t;
}

void dl_cancel(DlTask *t) { if (t) t->cancel = 1; }

void dl_free(DlTask *t) {
    if (!t) return;
    t->cancel = 1;
    if (t->thread) { WaitForSingleObject(t->thread, 8000); CloseHandle(t->thread); }
    free(t);
}

int dl_state(DlTask *t) { return t ? (int)t->state : 3; }
long long dl_total(DlTask *t) { return t ? t->total : 0; }
long long dl_got(DlTask *t) { return t ? t->got : 0; }
double dl_speed(DlTask *t) { return t ? t->speed : 0; }
const char *dl_error(DlTask *t) { return t ? t->err : ""; }
const char *dl_path(DlTask *t) { return t ? t->path : ""; }
const char *dl_name(DlTask *t) { return t ? t->name : ""; }
