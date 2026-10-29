/*  loader.c —— 模组加载器（Fabric / Quilt / Forge / NeoForge）版本列表与安装 */
#include "app.h"

LoaderVer    g_lvers[MAX_LVERS];
int          g_nlvers = 0;
volatile int g_lvers_state = 0;
char         g_lvers_err[220] = {0};
volatile int g_lvers_kind = -1;
char         g_lvers_mc[40] = {0};

static char *dup_str(const char *s) {
    size_t n = strlen(s ? s : "") + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s ? s : "", n);
    return p;
}

const char *loader_name(int kind) {
    switch (kind) {
        case LD_FABRIC:   return "Fabric";
        case LD_QUILT:    return "Quilt";
        case LD_FORGE:    return "Forge";
        case LD_NEOFORGE: return "NeoForge";
        default:          return "原版";
    }
}

int loader_version_id(int kind, const char *mcver, const char *lver, char *out, size_t cap) {
    switch (kind) {
        case LD_FABRIC:   snprintf(out, cap, "fabric-loader-%s-%s", lver, mcver); return 1;
        case LD_QUILT:    snprintf(out, cap, "quilt-loader-%s-%s", lver, mcver); return 1;
        case LD_FORGE:    snprintf(out, cap, "%s-forge-%s", mcver, lver); return 1;
        case LD_NEOFORGE: snprintf(out, cap, "neoforge-%s", lver); return 1;
        default:          snprintf(out, cap, "%s", mcver); return 1;
    }
}

const char *loader_pretty(int kind, const char *mcver, const char *lver, char *out, size_t cap) {
    if (kind == LD_VANILLA) snprintf(out, cap, "%s", mcver);
    else snprintf(out, cap, "%s · %s %s", mcver, loader_name(kind), lver);
    return out;
}

/* ---------------------------------------------------------------- 进程 */
static int run_cmd(const char *cmd, const char *workdir, int timeout_ms, const char *logfile) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hlog = CreateFileA(logfile, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hlog;
    si.hStdError = hlog;
    si.hStdInput = NULL;
    memset(&pi, 0, sizeof(pi));

    char *cl = dup_str(cmd);
    BOOL ok = CreateProcessA(NULL, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, workdir, &si, &pi);
    free(cl);
    if (hlog != INVALID_HANDLE_VALUE) CloseHandle(hlog);
    if (!ok) return -1;
    DWORD w = WaitForSingleObject(pi.hProcess, (DWORD)timeout_ms);
    DWORD code = 1;
    if (w == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
    else GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

/* ---------------------------------------------------------------- 版本列表 */
static void lv_add(const char *ver, const char *label) {
    if (g_nlvers >= MAX_LVERS || !ver || !*ver) return;
    for (int i = 0; i < g_nlvers; i++)
        if (!strcmp(g_lvers[i].ver, ver)) return;
    snprintf(g_lvers[g_nlvers].ver, sizeof(g_lvers[0].ver), "%s", ver);
    snprintf(g_lvers[g_nlvers].label, sizeof(g_lvers[0].label), "%s", label ? label : ver);
    g_nlvers++;
}

typedef struct { int kind; char mc[40]; } ListArg;

static DWORD WINAPI list_thread(LPVOID param) {
    ListArg *la = (ListArg *)param;
    int kind = la->kind;
    char mc[40];
    snprintf(mc, sizeof(mc), "%s", la->mc);
    free(la);

    char url[600];
    char *txt = NULL;
    int ok = 0;

    if (kind == LD_FABRIC)
        snprintf(url, sizeof(url), "https://meta.fabricmc.net/v2/versions/loader/%s", mc);
    else if (kind == LD_QUILT)
        snprintf(url, sizeof(url), "https://meta.quiltmc.org/v3/versions/loader/%s", mc);
    else if (kind == LD_NEOFORGE)
        snprintf(url, sizeof(url), "https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge");
    else
        snprintf(url, sizeof(url), "https://files.minecraftforge.net/net/minecraftforge/forge/maven-metadata.json");

    ok = http_get_text(url, &txt);

    EnterCriticalSection(&g_lock);
    g_nlvers = 0;
    if (!ok || !txt) {
        g_lvers_state = 3;
        snprintf(g_lvers_err, sizeof(g_lvers_err), "无法获取 %s 版本列表", loader_name(kind));
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        return 0;
    }

    JVal *root = json_parse(txt);
    free(txt);
    if (!root) {
        g_lvers_state = 3;
        snprintf(g_lvers_err, sizeof(g_lvers_err), "版本列表解析失败");
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        return 0;
    }

    if (kind == LD_FABRIC || kind == LD_QUILT) {
        int n = json_arr_n(root);
        for (int i = 0; i < n && g_nlvers < MAX_LVERS; i++) {
            JVal *it = json_arr_at(root, i);
            const char *v = json_str(json_path(it, "loader.version"), NULL);
            int stable = json_bool(json_path(it, "loader.stable"), 1);
            if (!v) continue;
            char lb[96];
            if (i == 0) snprintf(lb, sizeof(lb), "%s（最新）", v);
            else if (!stable) snprintf(lb, sizeof(lb), "%s（测试）", v);
            else snprintf(lb, sizeof(lb), "%s", v);
            lv_add(v, lb);
        }
    } else if (kind == LD_NEOFORGE) {
        /* 版本号形如 21.1.72 对应 MC 1.21.1：去掉前缀 "1."，无小版本补 .0 */
        char pref[32];
        if (!strncmp(mc, "1.", 2)) snprintf(pref, sizeof(pref), "%s", mc + 2);
        else snprintf(pref, sizeof(pref), "%s", mc);
        if (!strchr(pref, '.')) strncat(pref, ".0", sizeof(pref) - strlen(pref) - 1);
        char prefix[40];
        snprintf(prefix, sizeof(prefix), "%s.", pref);

        JVal *arr = json_get(root, "versions");
        int n = json_arr_n(arr);
        for (int i = n - 1; i >= 0 && g_nlvers < MAX_LVERS; i--) {
            const char *v = json_str(json_arr_at(arr, i), NULL);
            if (!v || strncmp(v, prefix, strlen(prefix))) continue;
            lv_add(v, i == n - 1 ? NULL : v);
        }
        if (g_nlvers > 0)
            snprintf(g_lvers[0].label, sizeof(g_lvers[0].label), "%s（最新）", g_lvers[0].ver);
    } else {
        /* maven-metadata.json：{ "26.3": ["26.3-66.0.0", "26.3-66.0.2", ...] } */
        JVal *arr = json_get(root, mc);
        size_t ml = strlen(mc);
        int n = json_arr_n(arr);
        for (int i = n - 1; i >= 0 && g_nlvers < MAX_LVERS; i--) {
            const char *full = json_str(json_arr_at(arr, i), NULL);
            if (!full || !*full) continue;
            const char *v = full;
            if (!strncmp(full, mc, ml) && full[ml] == '-') v = full + ml + 1;
            char lb[96];
            if (g_nlvers == 0) snprintf(lb, sizeof(lb), "%s（最新）", v);
            else snprintf(lb, sizeof(lb), "%s", v);
            lv_add(v, lb);
        }
    }

    json_free(root);
    g_lvers_state = g_nlvers > 0 ? 2 : 3;
    if (g_nlvers == 0) snprintf(g_lvers_err, sizeof(g_lvers_err), "该版本没有可用的 %s", loader_name(kind));
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
    return 0;
}

void loader_list_async(int kind, const char *mcver) {
    EnterCriticalSection(&g_lock);
    g_lvers_kind = kind;
    snprintf(g_lvers_mc, sizeof(g_lvers_mc), "%s", mcver ? mcver : "");
    g_nlvers = 0;
    g_lvers_err[0] = 0;
    g_lvers_state = (kind == LD_VANILLA) ? 2 : 1;
    LeaveCriticalSection(&g_lock);
    if (kind == LD_VANILLA) { ui_request_redraw(); return; }

    ListArg *la = (ListArg *)calloc(1, sizeof(ListArg));
    la->kind = kind;
    snprintf(la->mc, sizeof(la->mc), "%s", mcver ? mcver : "");
    CloseHandle(CreateThread(NULL, 0, list_thread, la, 0, NULL));
    ui_request_redraw();
}

/* ---------------------------------------------------------------- 安装 */
typedef struct { char mc[40]; int kind; char lver[64]; } InstArg;

int loader_install_sync(const char *mc, int kind, const char *lver, char *err, size_t errcap) {
    char gd[MAX_PATH * 2];
    snprintf(gd, sizeof(gd), "%s", g.set.game_dir);
    lz_dir_make(gd);

    char vid[96];
    loader_version_id(kind, mc, lver, vid, sizeof(vid));

    /* 1) 先把原版装好（client.jar / 资源 / 原版库） */
    EnterCriticalSection(&g_lock);
    snprintf(g.inst.stage, sizeof(g.inst.stage), "安装原版 %s", mc);
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
    if (!install_impl_public(mc, err, errcap)) return 0;   /* 原版必须完整 */

    if (kind == LD_FABRIC || kind == LD_QUILT) {
        char url[600];
        if (kind == LD_FABRIC)
            snprintf(url, sizeof(url), "https://meta.fabricmc.net/v2/versions/loader/%s/%s/profile/json", mc, lver);
        else
            snprintf(url, sizeof(url), "https://meta.quiltmc.org/v3/versions/loader/%s/%s/profile/json", mc, lver);

        EnterCriticalSection(&g_lock);
        snprintf(g.inst.stage, sizeof(g.inst.stage), "获取 %s 安装描述", loader_name(kind));
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();

        char *txt = NULL;
        if (!http_get_text(url, &txt)) { snprintf(err, errcap, "无法获取加载器安装描述"); return 0; }
        char d[MAX_PATH * 2];
        char p[MAX_PATH * 2];
        lz_path_join(d, sizeof(d), g.set.game_dir, "versions");
        char sub[MAX_PATH * 2];
        lz_path_join(sub, sizeof(sub), d, vid);
        lz_dir_make(sub);
        char nm[128];
        snprintf(nm, sizeof(nm), "%s.json", vid);
        lz_path_join(p, sizeof(p), sub, nm);
        lz_file_write(p, txt, strlen(txt));
        free(txt);
    } else {
        /* Forge / NeoForge：跑官方安装器 */
        char java[MAX_PATH * 3] = {0};
        if (!java_ensure(mc, java, sizeof(java))) {
            snprintf(err, errcap, "需要 Java 才能安装 %s", loader_name(kind));
            return 0;
        }
        /* 安装器要求存在 launcher_profiles.json */
        char lp[MAX_PATH * 2];
        lz_path_join(lp, sizeof(lp), gd, "launcher_profiles.json");
        if (!lz_file_exists(lp))
            lz_file_write(lp, "{\"profiles\":{},\"settings\":{},\"version\":3}", 42);

        char inst_url[600];
        if (kind == LD_FORGE)
            snprintf(inst_url, sizeof(inst_url),
                     "https://maven.minecraftforge.net/net/minecraftforge/forge/%s-%s/forge-%s-%s-installer.jar",
                     mc, lver, mc, lver);
        else
            snprintf(inst_url, sizeof(inst_url),
                     "https://maven.neoforged.net/releases/net/neoforged/neoforge/%s/neoforge-%s-installer.jar",
                     lver, lver);

        char cache[MAX_PATH * 2], cacheDir[MAX_PATH * 2];
        lz_path_join(cacheDir, sizeof(cacheDir), gd, "cache");
        lz_dir_make(cacheDir);
        snprintf(cache, sizeof(cache), "%s\\%s-installer.jar", cacheDir,
                 kind == LD_FORGE ? "forge" : "neoforge");

        EnterCriticalSection(&g_lock);
        snprintf(g.inst.stage, sizeof(g.inst.stage), "下载 %s 安装器", loader_name(kind));
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();

        FileQueue *q = fq_create(g.set.dl_threads, ui_request_redraw);
        EnterCriticalSection(&g_lock);
        g.inst.q = q;
        LeaveCriticalSection(&g_lock);
        fq_add(q, inst_url, cache);
        if (fq_run(q) > 0) {
            snprintf(err, errcap, "安装器下载失败");
            return 0;
        }

        EnterCriticalSection(&g_lock);
        snprintf(g.inst.stage, sizeof(g.inst.stage), "运行 %s 安装器（可能需要几分钟）", loader_name(kind));
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();

        char cmd[MAX_PATH * 5], logp[MAX_PATH * 2];
        snprintf(logp, sizeof(logp), "%s\\installer.log", cacheDir);
        /* --installClient 不接受路径参数：安装器按「当前工作目录」找 launcher_profiles.json，
           所以只传开关，工作目录由 run_cmd 的 workdir 指定 */
        snprintf(cmd, sizeof(cmd), "\"%s\" -jar \"%s\" --installClient", java, cache);
        app_log("[加载器] %s", cmd);
        int rc = run_cmd(cmd, gd, 15 * 60 * 1000, logp);
        if (rc != 0) {
            snprintf(err, errcap, "%s 安装器返回 %d，详见 cache\\installer.log", loader_name(kind), rc);
            return 0;
        }

        /* 校验：安装器必须在本游戏目录的 versions 下生成 <vid>.json */
        char vjson[MAX_PATH * 4];
        lz_path_join(vjson, sizeof(vjson), gd, "versions");
        lz_path_join(vjson, sizeof(vjson), vjson, vid);
        lz_path_join(vjson, sizeof(vjson), vjson, "");
        {
            char jf[MAX_PATH * 5];
            snprintf(jf, sizeof(jf), "%s%s.json", vjson, vid);
            if (!lz_file_exists(jf)) {
                snprintf(err, errcap,
                         "%s 安装器跑完了，但没在游戏目录生成版本 %s（可能装到了默认 .minecraft）。"
                         "详见 cache\\installer.log", loader_name(kind), vid);
                return 0;
            }
        }
    }

    /* 2) 装加载器自身的库 */
    EnterCriticalSection(&g_lock);
    snprintf(g.inst.stage, sizeof(g.inst.stage), "安装 %s 依赖库", loader_name(kind));
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
    if (!install_impl_public(vid, err, errcap)) return 0;

    EnterCriticalSection(&g_lock);
    snprintf(g.inst.version, sizeof(g.inst.version), "%s", vid);
    LeaveCriticalSection(&g_lock);
    return 1;
}

static DWORD WINAPI install_thread2(LPVOID param) {
    InstArg *ia = (InstArg *)param;
    char err[240] = {0};
    app_log("[加载器] 开始安装 %s %s %s", ia->mc, loader_name(ia->kind), ia->lver);
    int ok = loader_install_sync(ia->mc, ia->kind, ia->lver, err, sizeof(err));

    EnterCriticalSection(&g_lock);
    if (!ok) snprintf(g.inst.err, sizeof(g.inst.err), "%s", err);
    snprintf(g.inst.stage, sizeof(g.inst.stage), ok ? "安装完成" : "安装失败");
    g.inst.finished = 1;
    LeaveCriticalSection(&g_lock);

    mc_refresh_installed();
    ui_request_redraw();
    if (ok) {
        char vid[96];
        loader_version_id(ia->kind, ia->mc, ia->lver, vid, sizeof(vid));
        app_toast("已安装 %s", vid);
    } else {
        app_toast("安装失败：%s", err);
        app_log("[加载器] 失败 %s", err);
    }
    free(ia);
    return 0;
}

void loader_install_async(const char *mcver, int kind, const char *lver) {
    if (g.inst.active) { app_toast("已有安装任务在进行"); return; }
    g.inst.active = 1;
    g.inst.finished = 0;
    g.inst.err[0] = 0;
    g.inst.t0 = lz_now_sec();
    char vid[96];
    loader_version_id(kind, mcver, lver, vid, sizeof(vid));
    snprintf(g.inst.version, sizeof(g.inst.version), "%s", vid);
    InstArg *ia = (InstArg *)calloc(1, sizeof(InstArg));
    snprintf(ia->mc, sizeof(ia->mc), "%s", mcver);
    ia->kind = kind;
    snprintf(ia->lver, sizeof(ia->lver), "%s", lver);
    CloseHandle(CreateThread(NULL, 0, install_thread2, ia, 0, NULL));
    ui_request_redraw();
}
