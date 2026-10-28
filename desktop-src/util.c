/*  util.c —— 字符串、路径、文件、MD5、杂项 */
#include "common.h"
#include <shellapi.h>
#include <ctype.h>
#include <time.h>

/* ---------------- 字符串构建器 ---------------- */
void sb_init(Sb *b) { b->s = NULL; b->n = 0; b->cap = 0; }

void sb_free(Sb *b) { free(b->s); b->s = NULL; b->n = b->cap = 0; }

void sb_clear(Sb *b) { b->n = 0; if (b->s) b->s[0] = 0; }

static void sb_grow(Sb *b, size_t need) {
    if (b->n + need + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->n + need + 1) cap *= 2;
    char *p = (char *)realloc(b->s, cap);
    if (!p) return;
    b->s = p; b->cap = cap;
    if (b->n == 0) b->s[0] = 0;
}

void sb_addn(Sb *b, const char *s, size_t n) {
    if (!s || !n) return;
    sb_grow(b, n);
    if (!b->s) return;
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

void sb_add(Sb *b, const char *s) { if (s) sb_addn(b, s, strlen(s)); }

void sb_addc(Sb *b, char c) { sb_addn(b, &c, 1); }

void sb_printf(Sb *b, const char *fmt, ...) {
    char tmp[2048];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n < (int)sizeof(tmp)) { sb_addn(b, tmp, (size_t)n); return; }
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb_addn(b, big, (size_t)n);
    free(big);
}

char *sb_take(Sb *b) {
    char *r = b->s;
    if (!r) { r = (char *)calloc(1, 1); }
    b->s = NULL; b->n = b->cap = 0;
    return r;
}

/* ---------------- 编码转换 ---------------- */
wchar_t *lz_u8_to_w(const char *s) {
    if (!s) s = "";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) n = 1;
    wchar_t *w = (wchar_t *)calloc((size_t)n + 1, sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

char *lz_w_to_u8(const wchar_t *w) {
    if (!w) w = L"";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) n = 1;
    char *s = (char *)calloc((size_t)n + 1, 1);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

/* ---------------- 路径 ---------------- */
void lz_path_join(char *out, size_t cap, const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    if (!*a) { snprintf(out, cap, "%s", b); return; }
    if (!*b) { snprintf(out, cap, "%s", a); return; }
    size_t la = strlen(a);
    int need = (a[la - 1] != '\\' && a[la - 1] != '/');
    if (b[0] == '\\' || b[0] == '/') b++;
    snprintf(out, cap, "%s%s%s", a, need ? "\\" : "", b);
}

void lz_path_slash(char *p) { for (; *p; p++) if (*p == '/') *p = '\\'; }

int lz_file_exists(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES;
}

long long lz_file_size(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return -1;
    return ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

int lz_dir_make(const char *path) {
    if (!path || !*path) return 0;
    char tmp[MAX_PATH * 2];
    snprintf(tmp, sizeof(tmp), "%s", path);
    lz_path_slash(tmp);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryA(tmp, NULL);
            *p = '\\';
        }
    }
    CreateDirectoryA(tmp, NULL);
    return lz_file_exists(tmp);
}

int lz_file_read(const char *path, char **out, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return 0; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    if (out) *out = buf; else free(buf);
    if (len) *len = rd;
    return 1;
}

int lz_file_write(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (len) fwrite(data, 1, len, f);
    fclose(f);
    return 1;
}

void lz_dir_size(const char *path, long long *bytes, int *files) {
    long long b = 0; int n = 0;
    char pat[MAX_PATH * 2];
    snprintf(pat, sizeof(pat), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char sub[MAX_PATH * 2];
        lz_path_join(sub, sizeof(sub), path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            lz_dir_size(sub, &b, &n);
        } else {
            b += ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (bytes) *bytes = b;
    if (files) *files = n;
}

int lz_dir_remove(const char *path) {
    char pat[MAX_PATH * 2];
    snprintf(pat, sizeof(pat), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char sub[MAX_PATH * 2];
            lz_path_join(sub, sizeof(sub), path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                lz_dir_remove(sub);
            } else {
                SetFileAttributesA(sub, FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(sub);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryA(path) ? 1 : 0;
}

void lz_appdata(char *out, size_t cap) {
    char tmp[MAX_PATH] = {0};
    if (!GetEnvironmentVariableA("APPDATA", tmp, MAX_PATH))
        GetTempPathA(MAX_PATH, tmp);
    snprintf(out, cap, "%s", tmp);
}

void lz_exe_dir(char *out, size_t cap) {
    char tmp[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, tmp, MAX_PATH);
    char *p = strrchr(tmp, '\\');
    if (p) *p = 0;
    snprintf(out, cap, "%s", tmp);
}

/* ---------------- MD5 ---------------- */
typedef struct { uint32_t h[4]; uint64_t len; unsigned char buf[64]; size_t n; } Md5;
static const uint32_t MD5_K[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
static const int MD5_S[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };

static uint32_t rotl32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5_block(Md5 *m, const unsigned char *p) {
    uint32_t a = m->h[0], b = m->h[1], c = m->h[2], d = m->h[3];
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i*4] | ((uint32_t)p[i*4+1] << 8) |
               ((uint32_t)p[i*4+2] << 16) | ((uint32_t)p[i*4+3] << 24);
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16)      { f = (b & c) | (~b & d);       g = i; }
        else if (i < 32) { f = (d & b) | (~d & c);       g = (5*i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d;                g = (3*i + 5) & 15; }
        else             { f = c ^ (b | ~d);             g = (7*i) & 15; }
        uint32_t tmp = d;
        d = c; c = b;
        b = b + rotl32(a + f + MD5_K[i] + w[g], MD5_S[i]);
        a = tmp;
    }
    m->h[0] += a; m->h[1] += b; m->h[2] += c; m->h[3] += d;
}

static void md5_init(Md5 *m) {
    m->h[0] = 0x67452301; m->h[1] = 0xefcdab89;
    m->h[2] = 0x98badcfe; m->h[3] = 0x10325476;
    m->len = 0; m->n = 0;
}

static void md5_update(Md5 *m, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    m->len += len;
    while (len) {
        size_t take = 64 - m->n;
        if (take > len) take = len;
        memcpy(m->buf + m->n, p, take);
        m->n += take; p += take; len -= take;
        if (m->n == 64) { md5_block(m, m->buf); m->n = 0; }
    }
}

static void md5_final(Md5 *m, unsigned char out[16]) {
    uint64_t bits = m->len * 8;
    unsigned char pad = 0x80;
    md5_update(m, &pad, 1);
    unsigned char z = 0;
    while (m->n != 56) md5_update(m, &z, 1);
    unsigned char l[8];
    for (int i = 0; i < 8; i++) l[i] = (unsigned char)(bits >> (8 * i));
    md5_update(m, l, 8);
    for (int i = 0; i < 4; i++) {
        out[i*4]   = (unsigned char)(m->h[i]);
        out[i*4+1] = (unsigned char)(m->h[i] >> 8);
        out[i*4+2] = (unsigned char)(m->h[i] >> 16);
        out[i*4+3] = (unsigned char)(m->h[i] >> 24);
    }
}

void lz_md5_hex(const void *data, size_t len, char out[33]) {
    Md5 m; md5_init(&m);
    md5_update(&m, data, len);
    unsigned char d[16]; md5_final(&m, d);
    for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", d[i]);
    out[32] = 0;
}

/* ---------------- 杂项 ---------------- */
uint64_t lz_now_ms(void) {
    static LARGE_INTEGER freq = {0};
    static LARGE_INTEGER base = {0};
    LARGE_INTEGER c;
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&base); }
    QueryPerformanceCounter(&c);
    return (uint64_t)((c.QuadPart - base.QuadPart) * 1000 / freq.QuadPart);
}

double lz_now_sec(void) { return (double)GetTickCount64() / 1000.0; }

unsigned lz_rand(unsigned lo, unsigned hi) {
    static int seeded = 0;
    if (!seeded) { srand((unsigned)(GetTickCount64() ^ (uintptr_t)&seeded)); seeded = 1; }
    if (hi <= lo) return lo;
    unsigned span = hi - lo + 1;
    unsigned r = ((unsigned)rand() << 15) ^ (unsigned)rand();
    return lo + (r % span);
}

void lz_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && (unsigned char)*p <= ' ') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (unsigned char)s[n-1] <= ' ') s[--n] = 0;
}

int lz_stricmp(const char *a, const char *b) { return _stricmp(a, b); }

int lz_icontains(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    if (!*needle) return 1;
    size_t ln = strlen(needle), lh = strlen(hay);
    if (ln > lh) return 0;
    for (size_t i = 0; i + ln <= lh; i++) {
        size_t j = 0;
        while (j < ln && tolower((unsigned char)hay[i+j]) == tolower((unsigned char)needle[j])) j++;
        if (j == ln) return 1;
    }
    return 0;
}

void lz_sanitize_filename(char *s) {
    for (; *s; s++) {
        if (strchr("\\/:*?\"<>|", *s)) *s = '_';
    }
}

int lz_shell_open(const char *url) {
    wchar_t *w = lz_u8_to_w(url);
    HINSTANCE r = ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
    return (INT_PTR)r > 32;
}

void lz_clipboard_set(const char *text) {
    if (!text) return;
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();
    wchar_t *w = lz_u8_to_w(text);
    size_t bytes = (wcslen(w) + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void *p = GlobalLock(h);
        memcpy(p, w, bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
    free(w);
}

char *lz_clipboard_get(void) {
    char *out = NULL;
    if (!OpenClipboard(NULL)) return NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t *w = (wchar_t *)GlobalLock(h);
        if (w) { out = lz_w_to_u8(w); GlobalUnlock(h); }
    }
    CloseClipboard();
    return out;
}
