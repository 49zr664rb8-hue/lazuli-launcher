/*  modpack.c —— .mrpack 整合包一键安装 */
#include "app.h"

static void mp_dir(char *out, size_t cap) {
    char ad[MAX_PATH];
    lz_appdata(ad, sizeof(ad));
    char d[MAX_PATH * 2];
    lz_path_join(d, sizeof(d), ad, "Lazuli");
    lz_dir_make(d);
    lz_path_join(out, cap, d, "packs");
    lz_dir_make(out);
}

/* 找 tar.exe：32 位进程在 64 位系统上 System32 会被重定向到 SysWOW64（没有 tar），
   所以先试 Sysnative，再试 System32，最后退回 PATH */
static int find_tar(char *out, size_t cap) {
    char win[MAX_PATH];
    if (GetWindowsDirectoryA(win, sizeof(win))) {
        const char *sub[] = { "\\Sysnative\\tar.exe", "\\System32\\tar.exe" };
        for (int i = 0; i < 2; i++) {
            snprintf(out, cap, "%s%s", win, sub[i]);
            if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return 1;
        }
    }
    snprintf(out, cap, "tar.exe");
    return 0;
}

static int run_hidden(const char *cmdline, DWORD timeout_ms) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    memset(&pi, 0, sizeof(pi));
    char *cl = _strdup(cmdline);
    BOOL ok = CreateProcessA(NULL, cl, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cl);
    if (!ok) return 0;
    WaitForSingleObject(pi.hProcess, timeout_ms);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

/* 用系统 tar 解压（zip / tar.gz 都吃），失败再用 PS 的 Expand-Archive */
static int extract_to(const char *archive, const char *dest) {
    lz_dir_make(dest);
    char tarp[MAX_PATH];
    find_tar(tarp, sizeof(tarp));
    char cmd[MAX_PATH * 6];
    snprintf(cmd, sizeof(cmd), "%c%s%c -xf %c%s%c -C %c%s%c",
             34, tarp, 34, 34, archive, 34, 34, dest, 34);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    memset(&pi, 0, sizeof(pi));
    (void)si;
    (void)pi;
    if (run_hidden(cmd, 10 * 60 * 1000)) return 1;

    /* tar 失败（老系统没 tar，或路径含非 ASCII 字符）→ 换 PS 解压 */
    char ps[MAX_PATH * 8];
    snprintf(ps, sizeof(ps),
             "powershell -NoProfile -NonInteractive -Command "
             "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
             archive, dest);
    if (run_hidden(ps, 10 * 60 * 1000)) return 1;
    return 0;
}

/* 递归复制目录（用 xcopy，省得手写） */
static void copy_tree(const char *src, const char *dst) {
    char xcp[MAX_PATH];
    snprintf(xcp, sizeof(xcp), "C:%cWindows%cSystem32%cxcopy.exe", 92, 92, 92);
    char cmd[MAX_PATH * 6];
    snprintf(cmd, sizeof(cmd), "%c%s%c %c%s%c %c%s%c /E /Y /I /Q",
             34, xcp, 34, 34, src, 34, 34, dst, 34);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    memset(&pi, 0, sizeof(pi));
    char *cl = _strdup(cmd);
    BOOL ok = CreateProcessA(NULL, cl, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cl);
    if (!ok) return;
    WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

typedef struct { char url[1024]; char name[200]; } MpArg;

static void mp_stage(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_lock);
    snprintf(g.inst.stage, sizeof(g.inst.stage), "%s", buf);
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
}

static DWORD WINAPI mp_thread(LPVOID p) {
    MpArg *a = (MpArg *)p;
    char err[300] = {0};
    char dir[MAX_PATH * 2], pkg[MAX_PATH * 3], tmp[MAX_PATH * 3];
    mp_dir(dir, sizeof(dir));
    lz_sanitize_filename(a->name);
    lz_path_join(pkg, sizeof(pkg), dir, a->name);
    lz_path_join(tmp, sizeof(tmp), dir, "unpacked");

    /* 1) 下载整合包 */
    mp_stage("下载整合包…");
    FileQueue *q = fq_create(g.set.dl_threads, ui_request_redraw);
    EnterCriticalSection(&g_lock);
    g.inst.q = q;
    LeaveCriticalSection(&g_lock);
    fq_add(q, a->url, pkg);
    int failed = fq_run(q);
    EnterCriticalSection(&g_lock);
    FileQueue *mine = g.inst.q;
    g.inst.q = NULL;
    LeaveCriticalSection(&g_lock);
    if (mine == q) fq_destroy(q);
    if (failed) { snprintf(err, sizeof(err), "整合包下载失败"); goto done; }

    /* 2) 解压 */
    mp_stage("解压整合包…");
    if (!extract_to(pkg, tmp)) { snprintf(err, sizeof(err), "解压失败"); goto done; }

    char idx[MAX_PATH * 3];
    lz_path_join(idx, sizeof(idx), tmp, "modrinth.index.json");
    char *txt = NULL;
    if (!lz_file_read(idx, &txt, NULL)) { snprintf(err, sizeof(err), "不是有效的 mrpack（缺 modrinth.index.json）"); goto done; }
    JVal *root = json_parse(txt);
    free(txt);
    if (!root) { snprintf(err, sizeof(err), "modrinth.index.json 解析失败"); goto done; }

    /* 3) 装游戏版本 + 加载器 */
    const char *mc = json_str(json_path(root, "dependencies.minecraft"), "");
    if (!mc[0]) { json_free(root); snprintf(err, sizeof(err), "整合包没写 Minecraft 版本"); goto done; }

    int lk = -1;
    char lv[64] = {0};
    struct { const char *key; int kind; } LDMAP[] = {
        { "fabric-loader", LD_FABRIC }, { "quilt-loader", LD_QUILT },
        { "forge", LD_FORGE }, { "neoforge", LD_NEOFORGE },
    };
    for (int i = 0; i < 4; i++) {
        JVal *deps = json_get(root, "dependencies");
        const char *val = json_str(json_get(deps, LDMAP[i].key), NULL);
        if (val && *val) { lk = LDMAP[i].kind; snprintf(lv, sizeof(lv), "%s", val); break; }
    }

    if (lk < 0) {
        mp_stage("安装原版 %s…", mc);
        if (!install_impl_public(mc, err, sizeof(err))) { json_free(root); goto done; }
    } else {
        mp_stage("安装 %s %s…", mc, loader_name(lk));
        if (!loader_install_sync(mc, lk, lv, err, sizeof(err))) { json_free(root); goto done; }
    }

    /* 4) 下载整合包里的文件 */
    mp_stage("下载整合包内容…");
    JVal *files = json_get(root, "files");
    FileQueue *q2 = fq_create(g.set.dl_threads, ui_request_redraw);
    EnterCriticalSection(&g_lock);
    g.inst.q = q2;
    LeaveCriticalSection(&g_lock);
    int added = 0;
    for (int i = 0; i < json_arr_n(files); i++) {
        JVal *f = json_arr_at(files, i);
        const char *path = json_str(json_get(f, "path"), "");
        JVal *dl = json_get(f, "downloads");
        const char *url = json_arr_n(dl) > 0 ? json_str(json_arr_at(dl, 0), "") : "";
        if (!path[0] || !url[0]) continue;
        if (strstr(path, "..")) continue;   /* 防目录穿越 */
        char dest[MAX_PATH * 3];
        lz_path_join(dest, sizeof(dest), g.set.game_dir, path);
        fq_add(q2, url, dest);
        added++;
    }
    int failed2 = fq_run(q2);
    EnterCriticalSection(&g_lock);
    FileQueue *mine2 = g.inst.q;
    g.inst.q = NULL;
    LeaveCriticalSection(&g_lock);
    if (mine2 == q2) fq_destroy(q2);

    /* 5) overrides 覆盖到游戏目录 */
    mp_stage("应用配置覆盖…");
    char ov[MAX_PATH * 3];
    lz_path_join(ov, sizeof(ov), tmp, "overrides");
    copy_tree(ov, g.set.game_dir);

    json_free(root);
    if (failed2) snprintf(err, sizeof(err), "有 %d 个文件没下载成功", failed2);

done:
    EnterCriticalSection(&g_lock);
    if (err[0]) snprintf(g.inst.err, sizeof(g.inst.err), "%s", err);
    snprintf(g.inst.stage, sizeof(g.inst.stage), err[0] ? "整合包安装失败" : "整合包安装完成");
    g.inst.finished = 1;
    LeaveCriticalSection(&g_lock);
    mc_refresh_installed();
    ui_request_redraw();
    if (err[0]) app_toast("%s", err);
    else app_toast("整合包已装好，可以启动了");
    free(a);
    return 0;
}

void modpack_install_async(const char *url, const char *name) {
    if (g.inst.active) { app_toast("已有安装任务在进行"); return; }
    if (!url || !*url) return;
    g.inst.active = 1;
    g.inst.finished = 0;
    g.inst.err[0] = 0;
    g.inst.t0 = lz_now_sec();
    snprintf(g.inst.version, sizeof(g.inst.version), "%s", name ? name : "整合包");
    MpArg *a = (MpArg *)calloc(1, sizeof(MpArg));
    snprintf(a->url, sizeof(a->url), "%s", url);
    snprintf(a->name, sizeof(a->name), "%s", name && *name ? name : "pack.mrpack");
    CloseHandle(CreateThread(NULL, 0, mp_thread, a, 0, NULL));
    ui_request_redraw();
}
