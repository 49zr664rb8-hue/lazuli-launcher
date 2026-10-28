/*  skin.c —— 皮肤与披风：拉取、导入、上传、切换 */
#include "app.h"

#define SKIN_PROFILE "https://api.minecraftservices.com/minecraft/profile"
#define SKIN_UPLOAD  "https://api.minecraftservices.com/minecraft/profile/skins"
#define CAPE_ACTIVE  "https://api.minecraftservices.com/minecraft/profile/capes/active"

#define MAX_WARDROBE 24
static char g_wardrobe[MAX_WARDROBE][400];
static int  g_wardrobe_n = 0;

void skin_dir(char *out, size_t cap) {
    char ad[MAX_PATH];
    lz_appdata(ad, sizeof(ad));
    char d[MAX_PATH * 2];
    lz_path_join(d, sizeof(d), ad, "Lazuli");
    lz_dir_make(d);
    lz_path_join(out, cap, d, "skins");
    lz_dir_make(out);
}

int  skin_wardrobe_count(void) { return g_wardrobe_n; }
const char *skin_wardrobe_item(int i) {
    return (i >= 0 && i < g_wardrobe_n) ? g_wardrobe[i] : NULL;
}

void skin_scan(void) {
    g_wardrobe_n = 0;
    char d[MAX_PATH * 2], pat[MAX_PATH * 3];
    skin_dir(d, sizeof(d));
    snprintf(pat, sizeof(pat), "%s\\*.png", d);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (g_wardrobe_n >= MAX_WARDROBE) break;
        snprintf(g_wardrobe[g_wardrobe_n], sizeof(g_wardrobe[0]), "%s\\%s", d, fd.cFileName);
        g_wardrobe_n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void skin_set_err(const char *msg) {
    EnterCriticalSection(&g_lock);
    g.skin.state = 3;
    snprintf(g.skin.err, sizeof(g.skin.err), "%s", msg ? msg : "操作失败");
    LeaveCriticalSection(&g_lock);
    app_toast("%s", msg ? msg : "操作失败");
    ui_request_redraw();
}

/* ---------------------------------------------------------------- 拉取 */
static DWORD WINAPI refresh_thread(LPVOID p) {
    LZ_UNUSED(p);
    char token[4100] = {0};
    EnterCriticalSection(&g_lock);
    snprintf(token, sizeof(token), "%s", g.auth.token);
    LeaveCriticalSection(&g_lock);

    if (!token[0]) { skin_set_err("请先登录微软账号"); return 0; }

    char hdr[4300];
    snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s", token);
    const char *hs[] = { hdr };
    Buf b = {0};
    int st = 0;
    if (!http_get_h(SKIN_PROFILE, hs, 1, &b, &st) || !b.data) {
        buf_free(&b);
        skin_set_err(st == 401 ? "登录已过期，请重新登录" : "无法获取皮肤信息");
        return 0;
    }

    JVal *root = json_parse(b.data);
    buf_free(&b);
    if (!root) { skin_set_err("皮肤信息解析失败"); return 0; }

    char url[1024] = {0}, variant[32] = "CLASSIC";
    JVal *skins = json_get(root, "skins");
    if (json_arr_n(skins) > 0) {
        JVal *s0 = json_arr_at(skins, 0);
        snprintf(url, sizeof(url), "%s", json_str(json_get(s0, "url"), ""));
        snprintf(variant, sizeof(variant), "%s", json_str(json_get(s0, "variant"), "CLASSIC"));
    }

    int nc = 0;
    struct { char id[80]; char alias[64]; int active; } capes[MAX_CAPES];
    JVal *capesJ = json_get(root, "capes");
    for (int i = 0; i < json_arr_n(capesJ) && nc < MAX_CAPES; i++) {
        JVal *c = json_arr_at(capesJ, i);
        snprintf(capes[nc].id, sizeof(capes[nc].id), "%s", json_str(json_get(c, "id"), ""));
        snprintf(capes[nc].alias, sizeof(capes[nc].alias), "%s", json_str(json_get(c, "alias"), "披风"));
        const char *state = json_str(json_get(c, "state"), "");
        capes[nc].active = !strcmp(state, "ACTIVE");
        if (capes[nc].id[0]) nc++;
    }
    json_free(root);

    /* 下载皮肤 png 到本地 */
    char dir[MAX_PATH * 2], png[MAX_PATH * 3];
    skin_dir(dir, sizeof(dir));
    lz_path_join(png, sizeof(png), dir, "current.png");
    if (url[0]) {
        FileQueue *q = fq_create(1, NULL);
        fq_add(q, url, png);
        fq_run(q);
        fq_destroy(q);
    }

    EnterCriticalSection(&g_lock);
    snprintf(g.skin.png, sizeof(g.skin.png), "%s", png);
    g.skin.model = !strcmp(variant, "SLIM") ? 1 : 0;
    g.skin.ncapes = nc;
    for (int i = 0; i < nc; i++) {
        snprintf(g.skin.capes[i].id, sizeof(g.skin.capes[i].id), "%s", capes[i].id);
        snprintf(g.skin.capes[i].alias, sizeof(g.skin.capes[i].alias), "%s", capes[i].alias);
        g.skin.capes[i].active = capes[i].active;
    }
    g.skin.state = 2;
    g.skin.err[0] = 0;
    LeaveCriticalSection(&g_lock);
    skin_scan();
    ui_request_redraw();
    return 0;
}

void skin_refresh_async(void) {
    if (g.skin.state == 1) return;
    g.skin.state = 1;
    g.skin.err[0] = 0;
    CloseHandle(CreateThread(NULL, 0, refresh_thread, NULL, 0, NULL));
    ui_request_redraw();
}

/* ---------------------------------------------------------------- 上传 */
static unsigned char *read_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 1024 * 1024) { fclose(f); return NULL; }
    unsigned char *d = (unsigned char *)malloc((size_t)n);
    if (!d) { fclose(f); return NULL; }
    size_t got = fread(d, 1, (size_t)n, f);
    fclose(f);
    *out_len = got;
    return d;
}

typedef struct { char path[MAX_PATH * 3]; int slim; } SkinJob;

static DWORD WINAPI upload_thread(LPVOID p) {
    SkinJob *job = (SkinJob *)p;
    size_t flen = 0;
    unsigned char *file = read_all(job->path, &flen);
    if (!file) { free(job); skin_set_err("读不到这个皮肤文件"); return 0; }

    char token[4100] = {0};
    EnterCriticalSection(&g_lock);
    snprintf(token, sizeof(token), "%s", g.auth.token);
    LeaveCriticalSection(&g_lock);
    if (!token[0]) { free(file); free(job); skin_set_err("请先登录微软账号"); return 0; }

    const char *bound = "----LazuliBoundary7d3a91";
    const char *variant = job->slim ? "slim" : "classic";
    char head[512], tail[128];
    int hn = snprintf(head, sizeof(head),
                      "--%s\r\nContent-Disposition: form-data; name=\"variant\"\r\n\r\n%s\r\n"
                      "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"skin.png\"\r\n"
                      "Content-Type: image/png\r\n\r\n",
                      bound, variant, bound);
    int tn = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", bound);
    size_t total = (size_t)hn + flen + (size_t)tn;
    unsigned char *body = (unsigned char *)malloc(total);
    if (!body) { free(file); free(job); skin_set_err("内存不足"); return 0; }
    memcpy(body, head, (size_t)hn);
    memcpy(body + hn, file, flen);
    memcpy(body + hn + flen, tail, (size_t)tn);
    free(file);

    char hdr[4300], ct[128];
    snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s", token);
    snprintf(ct, sizeof(ct), "multipart/form-data; boundary=%s", bound);
    const char *hs[] = { hdr };
    Buf out = {0};
    int st = 0;
    int ok = http_verb_len("POST", SKIN_UPLOAD, (const char *)body, total, ct, hs, 1, &out, &st);
    buf_free(&out);
    free(body);
    free(job);

    if (!ok) {
        skin_set_err(st == 401 ? "登录已过期，请重新登录"
                              : (st == 400 ? "皮肤格式不对（需要 64x64 的 PNG）" : "上传失败"));
        return 0;
    }
    EnterCriticalSection(&g_lock);
    g.skin.state = 0;
    LeaveCriticalSection(&g_lock);
    app_toast("皮肤已保存到账号");
    skin_refresh_async();
    return 0;
}

void skin_upload_async(const char *path, int slim) {
    if (g.auth.state != AUTH_OK) { app_toast("请先登录微软账号"); return; }
    if (!path || !*path) return;
    SkinJob *job = (SkinJob *)calloc(1, sizeof(SkinJob));
    snprintf(job->path, sizeof(job->path), "%s", path);
    job->slim = slim;
    g.skin.state = 1;
    CloseHandle(CreateThread(NULL, 0, upload_thread, job, 0, NULL));
    ui_request_redraw();
}

/* ---------------------------------------------------------------- 披风 */
typedef struct { char id[80]; } CapeJob;

static DWORD WINAPI cape_thread(LPVOID p) {
    CapeJob *job = (CapeJob *)p;
    char token[4100] = {0};
    EnterCriticalSection(&g_lock);
    snprintf(token, sizeof(token), "%s", g.auth.token);
    LeaveCriticalSection(&g_lock);
    if (!token[0]) { free(job); skin_set_err("请先登录微软账号"); return 0; }

    char hdr[4300], body[256];
    snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s", token);
    snprintf(body, sizeof(body), "{\"capeId\":\"%s\"}", job->id);
    free(job);
    const char *hs[] = { hdr };
    Buf out = {0};
    int st = 0;
    int ok = http_verb_len("PUT", CAPE_ACTIVE, body, strlen(body),
                           "application/json", hs, 1, &out, &st);
    buf_free(&out);
    if (!ok) { skin_set_err(st == 401 ? "登录已过期" : "切换披风失败"); return 0; }
    app_toast("披风已切换");
    skin_refresh_async();
    return 0;
}

void skin_cape_async(const char *cape_id) {
    if (g.auth.state != AUTH_OK) { app_toast("请先登录微软账号"); return; }
    CapeJob *job = (CapeJob *)calloc(1, sizeof(CapeJob));
    snprintf(job->id, sizeof(job->id), "%s", cape_id ? cape_id : "");
    CloseHandle(CreateThread(NULL, 0, cape_thread, job, 0, NULL));
}

/* 把选中的本地皮肤复制成当前并上传 */
void skin_apply_local(const char *path, int slim) {
    if (!path || !*path) return;
    skin_upload_async(path, slim);
}
