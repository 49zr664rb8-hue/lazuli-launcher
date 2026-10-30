/*  launcher.c —— 版本清单 / 安装 / Java 运行时 / 启动 */
#include "app.h"
#include <shlobj.h>

#define MANIFEST_URL "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json"
#define RUNTIME_URL  "https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json"
#define RES_BASE     "https://resources.download.minecraft.net/"
#define LIB_BASE     "https://libraries.minecraft.net/"

/* ---------------------------------------------------------------- 路径 */
static void ver_dir(char *out, size_t cap, const char *id) {
    char a[MAX_PATH * 2];
    lz_path_join(a, sizeof(a), g.set.game_dir, "versions");
    lz_path_join(out, cap, a, id);
}

static void ver_file(char *out, size_t cap, const char *id, const char *ext) {
    char d[MAX_PATH * 2];
    ver_dir(d, sizeof(d), id);
    char name[160];
    snprintf(name, sizeof(name), "%s.%s", id, ext);
    lz_path_join(out, cap, d, name);
}

static void natives_dir(char *out, size_t cap, const char *id) {
    char d[MAX_PATH * 2];
    ver_dir(d, sizeof(d), id);
    lz_path_join(out, cap, d, "natives");
}

/* ---------------------------------------------------------------- 工具 */
void lz_fmt_size(char *out, size_t cap, double b) {
    const char *u[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;
    while (b >= 1024.0 && i < 4) { b /= 1024.0; i++; }
    if (i == 0) snprintf(out, cap, "%.0f %s", b, u[i]);
    else snprintf(out, cap, "%.1f %s", b, u[i]);
}

void lz_fmt_count(char *out, size_t cap, long long n) {
    if (n >= 100000000) snprintf(out, cap, "%.1f亿", n / 100000000.0);
    else if (n >= 10000) snprintf(out, cap, "%.1f万", n / 10000.0);
    else snprintf(out, cap, "%lld", n);
}

static char *fetch_json(const char *url) {
    char *txt = NULL;
    if (!http_get_text(url, &txt)) return NULL;
    return txt;
}

/* 规则判定：无 rules 默认允许；有 rules 则逐条匹配 */
static int rule_allow(JVal *rules) {
    if (!rules || rules->t != J_ARR || rules->u.arr.n == 0) return 1;
    int allow = 0;
    for (int i = 0; i < rules->u.arr.n; i++) {
        JVal *r = json_arr_at(rules, i);
        const char *action = json_str(json_get(r, "action"), "allow");
        JVal *os = json_get(r, "os");
        int match = 1;
        if (os) {
            const char *nm = json_str(json_get(os, "name"), NULL);
            if (nm && strcmp(nm, "windows")) match = 0;
            const char *ar = json_str(json_get(os, "arch"), NULL);
            if (ar && match) {
                SYSTEM_INFO si; GetNativeSystemInfo(&si);
                if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
                    if (strcmp(ar, "x86_64") && strcmp(ar, "x64")) match = 0;
                } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
                    if (strcmp(ar, "arm64")) match = 0;
                } else {
                    if (strcmp(ar, "x86")) match = 0;
                }
            }
        }
        JVal *feat = json_get(r, "features");
        if (feat && match) {
            for (int k = 0; k < json_obj_n(feat); k++) {
                if (json_bool(json_val_at(feat, k), 0)) match = 0; /* 所有特性均为 false */
            }
        }
        if (match) allow = (strcmp(action, "allow") == 0);
    }
    return allow;
}

/* maven 坐标 → 路径 */
static void maven_path(const char *name, const char *ext, char *out, size_t cap) {
    char buf[300];
    snprintf(buf, sizeof(buf), "%s", name);
    char *parts[4] = {0};
    int np = 0;
    char *save = NULL;
    for (char *t = strtok_r(buf, ":", &save); t && np < 4; t = strtok_r(NULL, ":", &save))
        parts[np++] = t;
    if (np < 3) { snprintf(out, cap, "%s.%s", name, ext); return; }
    char g[200]; snprintf(g, sizeof(g), "%s", parts[0]);
    for (char *p = g; *p; p++) if (*p == '.') *p = '/';
    snprintf(out, cap, "%s/%s/%s/%s-%s.%s", g, parts[1], parts[2], parts[1], parts[2], ext);
}

/* ---------------------------------------------------------------- 版本清单 */
static DWORD WINAPI manifest_thread(LPVOID p) {
    LZ_UNUSED(p);
    char *txt = fetch_json(MANIFEST_URL);
    if (!txt) {
        EnterCriticalSection(&g_lock);
        g.manifest_state = 3;
        snprintf(g.manifest_err, sizeof(g.manifest_err), "无法获取版本清单，请检查网络");
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        return 0;
    }
    JVal *root = json_parse(txt);
    free(txt);
    if (!root) {
        EnterCriticalSection(&g_lock);
        g.manifest_state = 3;
        snprintf(g.manifest_err, sizeof(g.manifest_err), "版本清单解析失败");
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        return 0;
    }
    JVal *arr = json_get(root, "versions");
    int n = json_arr_n(arr);
    EnterCriticalSection(&g_lock);
    g.nversions = 0;
    for (int i = 0; i < n && g.nversions < MAX_VERSIONS; i++) {
        JVal *v = json_arr_at(arr, i);
        McVer *mv = &g.versions[g.nversions];
        memset(mv, 0, sizeof(*mv));
        snprintf(mv->id, sizeof(mv->id), "%s", json_str(json_get(v, "id"), "?"));
        snprintf(mv->type, sizeof(mv->type), "%s", json_str(json_get(v, "type"), "release"));
        snprintf(mv->url, sizeof(mv->url), "%s", json_str(json_get(v, "url"), ""));
        snprintf(mv->rel, sizeof(mv->rel), "%.10s", json_str(json_get(v, "releaseTime"), ""));
        g.nversions++;
    }
    g.manifest_state = 2;
    LeaveCriticalSection(&g_lock);
    json_free(root);
    mc_refresh_installed();
    ui_request_redraw();
    return 0;
}

void mc_load_manifest_async(void) {
    if (g.manifest_state == 1) return;
    g.manifest_state = 1;
    CloseHandle(CreateThread(NULL, 0, manifest_thread, NULL, 0, NULL));
}

void mc_refresh_installed(void) {
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g.nversions; i++)
        g.versions[i].installed = mc_is_installed(g.versions[i].id);
    LeaveCriticalSection(&g_lock);
}

/* ---------------------------------------------------------------- 解压 */
static int extract_zip(const char *zip, const char *dest) {
    lz_dir_make(dest);

    /* 不用 tar.exe：它会按「当前目录」搜 DLL，被别的 libarchive.dll 污染后
       会弹「无法定位程序输入点 archive_parse_date」的模态框，把安装卡死。
       改成复制成 .zip 后缀，交给 Expand-Archive（不依赖 libarchive）。 */
    char tmpzip[MAX_PATH * 3];
    snprintf(tmpzip, sizeof(tmpzip), "%s.lz.zip", zip);
    CopyFileA(zip, tmpzip, FALSE);

    char ps[MAX_PATH * 8];
    snprintf(ps, sizeof(ps),
             "powershell -NoProfile -NonInteractive -Command "
             "\"$ProgressPreference='SilentlyContinue'; Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
             tmpzip, dest);

    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, ps, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DeleteFileA(tmpzip);
        return 0;
    }
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    DeleteFileA(tmpzip);
    return code == 0;
}


/* ==========================================================================
 *  inheritsFrom 版本合并（Fabric / Forge / NeoForge 的版本都继承原版）
 * ========================================================================== */
static char *dupstr(const char *s) {
    size_t n = strlen(s ? s : "") + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s ? s : "", n);
    return p;
}

static JVal *jnew(JType t) { JVal *v = (JVal *)calloc(1, sizeof(JVal)); if (v) v->t = t; return v; }

static JVal *jclone(JVal *v) {
    if (!v) return NULL;
    JVal *n = jnew(v->t);
    if (!n) return NULL;
    switch (v->t) {
        case J_STR: n->u.str = dupstr(v->u.str); break;
        case J_NUM: n->u.num = v->u.num; break;
        case J_BOOL: n->u.b = v->u.b; break;
        case J_ARR: {
            int c = v->u.arr.n ? v->u.arr.n : 1;
            n->u.arr.items = (JVal **)calloc((size_t)c, sizeof(JVal *));
            for (int i = 0; i < v->u.arr.n; i++) n->u.arr.items[i] = jclone(v->u.arr.items[i]);
            n->u.arr.n = v->u.arr.n;
            break;
        }
        case J_OBJ: {
            int c = v->u.obj.n ? v->u.obj.n : 1;
            n->u.obj.keys = (char **)calloc((size_t)c, sizeof(char *));
            n->u.obj.vals = (JVal **)calloc((size_t)c, sizeof(JVal *));
            for (int i = 0; i < v->u.obj.n; i++) {
                n->u.obj.keys[i] = dupstr(v->u.obj.keys[i]);
                n->u.obj.vals[i] = jclone(v->u.obj.vals[i]);
            }
            n->u.obj.n = v->u.obj.n;
            break;
        }
        default: break;
    }
    return n;
}

static void jobj_set(JVal *o, const char *k, JVal *v) {
    if (!o || o->t != J_OBJ) { json_free(v); return; }
    for (int i = 0; i < o->u.obj.n; i++) {
        if (!strcmp(o->u.obj.keys[i], k)) {
            json_free(o->u.obj.vals[i]);
            o->u.obj.vals[i] = v;
            return;
        }
    }
    int n = o->u.obj.n + 1;
    o->u.obj.keys = (char **)realloc(o->u.obj.keys, sizeof(char *) * (size_t)n);
    o->u.obj.vals = (JVal **)realloc(o->u.obj.vals, sizeof(JVal *) * (size_t)n);
    o->u.obj.keys[n - 1] = dupstr(k);
    o->u.obj.vals[n - 1] = v;
    o->u.obj.n = n;
}

static void jarr_push(JVal *a, JVal *v) {
    if (!a || a->t != J_ARR) { json_free(v); return; }
    int n = a->u.arr.n + 1;
    a->u.arr.items = (JVal **)realloc(a->u.arr.items, sizeof(JVal *) * (size_t)n);
    a->u.arr.items[n - 1] = v;
    a->u.arr.n = n;
}

/* "group:artifact:version" → "group:artifact" */
static void lib_key(const char *name, char *out, size_t cap) {
    int colons = 0;
    size_t i = 0;
    out[0] = 0;
    if (!name) return;
    for (; name[i] && i + 1 < cap; i++) {
        if (name[i] == ':') {
            colons++;
            if (colons == 2) break;
        }
        out[i] = name[i];
    }
    out[i] = 0;
}

static JVal *merge_versions(JVal *parent, JVal *child) {
    JVal *out = jclone(parent);
    if (!out) return NULL;
    static const char *scalars[] = {
        "mainClass", "assetIndex", "assets", "downloads", "javaVersion",
        "minecraftArguments", "id", "type", "releaseTime", "time"
    };
    for (int i = 0; i < LZ_COUNT(scalars); i++) {
        JVal *v = json_get(child, scalars[i]);
        if (v) jobj_set(out, scalars[i], jclone(v));
    }
    /* arguments：父在前、子在后 */
    JVal *pa = json_get(parent, "arguments");
    JVal *ca = json_get(child, "arguments");
    if (pa || ca) {
        JVal *na = jnew(J_OBJ);
        const char *sec[2] = { "game", "jvm" };
        for (int k = 0; k < 2; k++) {
            JVal *arr = jnew(J_ARR);
            JVal *pv = pa ? json_get(pa, sec[k]) : NULL;
            JVal *cv = ca ? json_get(ca, sec[k]) : NULL;
            for (int i = 0; i < json_arr_n(pv); i++) jarr_push(arr, jclone(json_arr_at(pv, i)));
            for (int i = 0; i < json_arr_n(cv); i++) jarr_push(arr, jclone(json_arr_at(cv, i)));
            jobj_set(na, sec[k], arr);
        }
        jobj_set(out, "arguments", na);
    }
    /* libraries：父在前，子覆盖同名 group:artifact */
    JVal *libs = jnew(J_ARR);
    JVal *pl = json_get(parent, "libraries");
    JVal *cl = json_get(child, "libraries");
    for (int i = 0; i < json_arr_n(pl); i++) jarr_push(libs, jclone(json_arr_at(pl, i)));
    for (int i = 0; i < json_arr_n(cl); i++) {
        JVal *clib = json_arr_at(cl, i);
        char key[256], k2[256];
        lib_key(json_str(json_get(clib, "name"), ""), key, sizeof(key));
        int replaced = 0;
        for (int j = 0; j < libs->u.arr.n; j++) {
            lib_key(json_str(json_get(libs->u.arr.items[j], "name"), ""), k2, sizeof(k2));
            if (!strcmp(key, k2)) {
                json_free(libs->u.arr.items[j]);
                libs->u.arr.items[j] = jclone(clib);
                replaced = 1;
                break;
            }
        }
        if (!replaced) jarr_push(libs, jclone(clib));
    }
    jobj_set(out, "libraries", libs);
    return out;
}

/* 读出版本 JSON 并沿 inheritsFrom 合并；jar_id 返回真正持有 client.jar 的版本号 */
static JVal *merged_root_depth(const char *id, char *jar_id, size_t cap, int depth) {
    char p[MAX_PATH * 2];
    ver_file(p, sizeof(p), id, "json");
    char *txt = NULL;
    if (!lz_file_read(p, &txt, NULL)) return NULL;
    JVal *root = json_parse(txt);
    free(txt);
    if (!root) return NULL;

    const char *inh = json_str(json_get(root, "inheritsFrom"), NULL);
    if (inh && *inh && depth < 4) {
        char jid[64] = {0};
        JVal *parent = merged_root_depth(inh, jid, sizeof(jid), depth + 1);
        if (parent) {
            JVal *m = merge_versions(parent, root);
            json_free(parent);
            json_free(root);
            if (jar_id) snprintf(jar_id, cap, "%s", jid[0] ? jid : inh);
            return m;
        }
    }
    if (jar_id) snprintf(jar_id, cap, "%s", id);
    return root;
}

JVal *mc_merged_root(const char *id, char *jar_id, size_t cap) {
    return merged_root_depth(id, jar_id, cap, 0);
}

/* 已安装判定：json 存在 + 真正持有 client.jar 的版本也装好了 */
int mc_is_installed(const char *id) {
    char js[MAX_PATH * 2];
    ver_file(js, sizeof(js), id, "json");
    if (!lz_file_exists(js)) return 0;
    char jid[64] = {0};
    JVal *root = mc_merged_root(id, jid, sizeof(jid));
    if (!root) return 0;
    json_free(root);
    char jar[MAX_PATH * 2];
    ver_file(jar, sizeof(jar), jid, "jar");
    return lz_file_exists(jar);
}

/* ---------------------------------------------------------------- 安装 */
typedef struct { char path[MAX_PATH * 2]; } NativesJar;

typedef struct {
    const char *id;
    JVal *root;
} InstallArg;

static int install_impl(const char *id, char *err, size_t errcap) {
    char jpath[MAX_PATH * 2];
    ver_file(jpath, sizeof(jpath), id, "json");

    if (!lz_file_exists(jpath)) {
        char vurl[512] = {0};
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < g.nversions; i++)
            if (!strcmp(g.versions[i].id, id)) { snprintf(vurl, sizeof(vurl), "%s", g.versions[i].url); break; }
        LeaveCriticalSection(&g_lock);
        if (!vurl[0]) { snprintf(err, errcap, "找不到该版本信息"); return 0; }
        char *txt = fetch_json(vurl);
        if (!txt) { snprintf(err, errcap, "版本 JSON 下载失败"); return 0; }
        char d[MAX_PATH * 2];
        ver_dir(d, sizeof(d), id);
        lz_dir_make(d);
        lz_file_write(jpath, txt, strlen(txt));
        free(txt);
    }

    char jar_id[64] = {0};
    JVal *root = mc_merged_root(id, jar_id, sizeof(jar_id));
    if (!root) { snprintf(err, errcap, "版本 JSON 解析失败"); return 0; }

    FileQueue *q = fq_create(g.set.dl_threads, ui_request_redraw);
    EnterCriticalSection(&g_lock);
    g.inst.q = q;
    snprintf(g.inst.stage, sizeof(g.inst.stage), "准备文件列表");
    LeaveCriticalSection(&g_lock);

    char libs[MAX_PATH * 2], assets[MAX_PATH * 2], natives[MAX_PATH * 2];
    lz_path_join(libs, sizeof(libs), g.set.game_dir, "libraries");
    lz_path_join(assets, sizeof(assets), g.set.game_dir, "assets");
    natives_dir(natives, sizeof(natives), id);
    lz_dir_make(libs); lz_dir_make(assets); lz_dir_make(natives);

    /* 客户端 jar */
    JVal *client = json_path(root, "downloads.client");
    if (client) {
        char jar[MAX_PATH * 2];
        ver_file(jar, sizeof(jar), jar_id[0] ? jar_id : id, "jar");
        fq_add_size(q, json_str(json_get(client, "url"), ""), jar,
                    (long long)json_num(json_get(client, "size"), -1));
    }

    /* 库 */
    NativesJar njar[64]; int nnjar = 0;
    JVal *libarr = json_get(root, "libraries");
    int nl = json_arr_n(libarr);
    for (int i = 0; i < nl; i++) {
        JVal *lib = json_arr_at(libarr, i);
        if (!rule_allow(json_get(lib, "rules"))) continue;

        JVal *art = json_path(lib, "downloads.artifact");
        const char *lname = json_str(json_get(lib, "name"), "");
        if (art) {
            const char *p = json_str(json_get(art, "path"), NULL);
            const char *u = json_str(json_get(art, "url"), NULL);
            if (p && u) {
                char full[MAX_PATH * 2];
                lz_path_join(full, sizeof(full), libs, p);
                fq_add_size(q, u, full, (long long)json_num(json_get(art, "size"), -1));
                if (strstr(p, "natives") && nnjar < 64) {
                    snprintf(njar[nnjar].path, sizeof(njar[0].path), "%s", full);
                    nnjar++;
                }
            }
        } else if (lname[0]) {
            /* 旧版：由 maven 坐标推导 */
            char rel[400];
            maven_path(lname, "jar", rel, sizeof(rel));
            char full[MAX_PATH * 2];
            lz_path_join(full, sizeof(full), libs, rel);
            /* 库自己带了 url 就用它（Fabric / Forge 的 maven 仓库），
               否则才回落到 Mojang 的 libraries CDN */
            const char *lurl = json_str(json_get(lib, "url"), NULL);
            char url[600];
            snprintf(url, sizeof(url), "%s%s",
                     (lurl && lurl[0]) ? lurl : LIB_BASE, rel);
            fq_add(q, url, full);
        }

        JVal *cls = json_path(lib, "downloads.classifiers");
        if (cls) {
            JVal *nat = json_get(cls, "natives-windows");
            if (!nat) nat = json_get(cls, "natives-windows-64");
            if (nat) {
                const char *p = json_str(json_get(nat, "path"), NULL);
                const char *u = json_str(json_get(nat, "url"), NULL);
                if (p && u && nnjar < 64) {
                    char full[MAX_PATH * 2];
                    lz_path_join(full, sizeof(full), libs, p);
                    fq_add_size(q, u, full, (long long)json_num(json_get(nat, "size"), -1));
                    snprintf(njar[nnjar].path, sizeof(njar[0].path), "%s", full);
                    nnjar++;
                }
            }
        }
    }

    /* 资源索引 */
    JVal *ai = json_get(root, "assetIndex");
    char assets_id[128] = {0};
    if (ai) {
        snprintf(assets_id, sizeof(assets_id), "%s", json_str(json_get(ai, "id"), "legacy"));
        const char *u = json_str(json_get(ai, "url"), NULL);
        if (u) {
            char idx[MAX_PATH * 2];
            char d[MAX_PATH * 2];
            lz_path_join(d, sizeof(d), assets, "indexes");
            lz_dir_make(d);
            char nm[160];
            snprintf(nm, sizeof(nm), "%s.json", assets_id);
            lz_path_join(idx, sizeof(idx), d, nm);
            char *itxt = NULL;
            if (!lz_file_read(idx, &itxt, NULL)) {
                EnterCriticalSection(&g_lock);
                snprintf(g.inst.stage, sizeof(g.inst.stage), "获取资源索引");
                LeaveCriticalSection(&g_lock);
                itxt = fetch_json(u);
                if (itxt) lz_file_write(idx, itxt, strlen(itxt));
            }
            if (itxt) {
                JVal *ij = json_parse(itxt);
                free(itxt);
                if (ij) {
                    int virt = json_bool(json_get(ij, "virtual"), 0);
                    int mapres = json_bool(json_get(ij, "map_to_resources"), 0);
                    JVal *objs = json_get(ij, "objects");
                    EnterCriticalSection(&g_lock);
                    snprintf(g.inst.stage, sizeof(g.inst.stage),
                             g.set.full_assets ? "下载游戏资源" : "准备资源索引");
                    LeaveCriticalSection(&g_lock);
                    if (g.set.full_assets || virt || mapres) {
                        for (int i = 0; i < json_obj_n(objs); i++) {
                            const char *key = json_key_at(objs, i);
                            JVal *o = json_val_at(objs, i);
                            const char *hash = json_str(json_get(o, "hash"), NULL);
                            if (!hash || strlen(hash) < 2) continue;
                            char rel[512];
                            if (virt) {
                                snprintf(rel, sizeof(rel), "virtual/%s/%s", assets_id, key);
                            } else if (mapres) {
                                snprintf(rel, sizeof(rel), "resources/%s", key);
                            } else {
                                snprintf(rel, sizeof(rel), "objects/%c%c/%s", hash[0], hash[1], hash);
                            }
                            char full[MAX_PATH * 2];
                            lz_path_join(full, sizeof(full), assets, rel);
                            char url[256];
                            snprintf(url, sizeof(url), "%s%c%c/%s", RES_BASE, hash[0], hash[1], hash);
                            fq_add_size(q, url, full, (long long)json_num(json_get(o, "size"), -1));
                        }
                    }
                    json_free(ij);
                }
            }
        }
    }

    /* 开始下载 */
    EnterCriticalSection(&g_lock);
    snprintf(g.inst.stage, sizeof(g.inst.stage), "下载游戏文件");
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
    int failed = fq_run(q);
    {
        int tt = 0, dd = 0, ff = 0;
        long long bd = 0, bt = 0;
        fq_progress(q, &tt, &dd, &ff, &bd, &bt);
        app_log("[安装] 队列 total=%d done=%d failed=%d bytes=%lld/%lld %s",
                tt, dd, ff, bd, bt, fq_last_error(q));
    }

    /* 解压 natives */
    if (nnjar > 0) {
        EnterCriticalSection(&g_lock);
        snprintf(g.inst.stage, sizeof(g.inst.stage), "解压本地库");
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        for (int i = 0; i < nnjar; i++) extract_zip(njar[i].path, natives);
    }

    /* 处理旧版 resources 目录映射 */
    {
        char resdir[MAX_PATH * 2];
        lz_path_join(resdir, sizeof(resdir), g.set.game_dir, "resources");
        char vres[MAX_PATH * 2];
        lz_path_join(vres, sizeof(vres), assets, "resources");
        if (lz_file_exists(vres)) lz_dir_make(resdir);
    }

    json_free(root);

    /* 归还队列：置空后由本函数销毁，避免连续安装时指针被覆盖泄漏 */
    EnterCriticalSection(&g_lock);
    FileQueue *mine = g.inst.q;
    g.inst.q = NULL;
    LeaveCriticalSection(&g_lock);
    if (mine == q) fq_destroy(q);

    if (failed > 0) {
        /* 资源文件（assets）缺失不阻断：游戏照样能起，声音/语言可能不全。
           只有库或主 jar 失败才算真失败——否则一个被杀软锁住的文件就能毁掉整个安装。 */
        const char *le = fq_last_error(q);
        if (le && (strstr(le, "\\assets\\") || strstr(le, "/assets/"))) {
            app_log("[安装] 忽略 %d 个资源文件失败：%s", failed, le);
            failed = 0;
        } else {
            snprintf(err, errcap, "%d 个文件下载失败：%s", failed, le ? le : "");
        }
    }
    return failed == 0;
}

int install_impl_public(const char *id, char *err, size_t errcap) {
    return install_impl(id, err, errcap);
}

static DWORD WINAPI install_thread(LPVOID p) {
    char *id = (char *)p;
    char err[240] = {0};
    app_log("[安装] 开始 %s", id);
    int ok = install_impl(id, err, sizeof(err));

    EnterCriticalSection(&g_lock);
    if (!ok) { snprintf(g.inst.err, sizeof(g.inst.err), "%s", err); app_log("[安装] 失败: %s", err); }
    else app_log("[安装] 完成 %s", id);
    snprintf(g.inst.stage, sizeof(g.inst.stage), ok ? "安装完成" : "安装失败");
    g.inst.finished = 1;
    LeaveCriticalSection(&g_lock);

    mc_refresh_installed();
    ui_request_redraw();
    if (ok) app_toast("版本 %s 安装完成", id);
    else app_toast("安装失败：%s", err);
    free(id);
    return 0;
}

void mc_install_async(const char *id) {
    if (g.inst.active) { app_toast("已有安装任务在进行"); return; }
    g.inst.active = 1;
    g.inst.finished = 0;
    g.inst.err[0] = 0;
    g.inst.t0 = lz_now_sec();
    snprintf(g.inst.version, sizeof(g.inst.version), "%s", id);
    char *dup = (char *)calloc(1, strlen(id) + 1);
    if (!dup) return;
    strcpy(dup, id);
    CloseHandle(CreateThread(NULL, 0, install_thread, dup, 0, NULL));
    ui_request_redraw();
}

/* ---------------------------------------------------------------- Java */
int java_probe(const char *path, char *ver_out, size_t cap) {
    if (!path || !*path || !lz_file_exists(path)) return 0;
    char cmd[MAX_PATH * 3];
    snprintf(cmd, sizeof(cmd), "\"%s\" -version", path);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = NULL;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        return 0;
    }
    CloseHandle(wr);
    char buf[1024] = {0};
    DWORD got = 0;
    WaitForSingleObject(pi.hProcess, 8000);
    ReadFile(rd, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(rd);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (ver_out && cap) snprintf(ver_out, cap, "%s", buf);
    return strstr(buf, "version") != NULL;
}

int java_detect(char *out, size_t cap) {
    /* 1. 启动器自带运行时 */
    char rt[MAX_PATH * 2];
    lz_path_join(rt, sizeof(rt), g.set.game_dir, "runtime");
    WIN32_FIND_DATAA fd;
    char pat[MAX_PATH * 3];
    snprintf(pat, sizeof(pat), "%s\\*", rt);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            char j[MAX_PATH * 3];
            char sub[MAX_PATH * 3];
            lz_path_join(sub, sizeof(sub), rt, fd.cFileName);
            lz_path_join(j, sizeof(j), sub, "bin\\java.exe");
            if (lz_file_exists(j)) {
                snprintf(out, cap, "%s", j);
                FindClose(h);
                return 1;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    /* 2. 常见安装位置 */
    const char *roots[] = {
        "C:\\Program Files\\Java", "C:\\Program Files (x86)\\Java",
        "C:\\Program Files\\Eclipse Adoptium", "C:\\Program Files\\Microsoft\\jdk",
        "C:\\Program Files\\Zulu", "C:\\Program Files\\BellSoft",
        "C:\\Program Files\\Amazon Corretto"
    };
    for (int i = 0; i < LZ_COUNT(roots); i++) {
        snprintf(pat, sizeof(pat), "%s\\*", roots[i]);
        HANDLE hh = FindFirstFileA(pat, &fd);
        if (hh == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            char j[MAX_PATH * 3], sub[MAX_PATH * 3];
            lz_path_join(sub, sizeof(sub), roots[i], fd.cFileName);
            lz_path_join(j, sizeof(j), sub, "bin\\java.exe");
            if (lz_file_exists(j)) {
                snprintf(out, cap, "%s", j);
                FindClose(hh);
                return 1;
            }
        } while (FindNextFileA(hh, &fd));
        FindClose(hh);
    }
    /* 3. PATH */
    char pv[32768];
    if (GetEnvironmentVariableA("PATH", pv, sizeof(pv))) {
        char *save = NULL;
        for (char *t = strtok_r(pv, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
            char j[MAX_PATH * 3];
            lz_path_join(j, sizeof(j), t, "java.exe");
            if (lz_file_exists(j)) { snprintf(out, cap, "%s", j); return 1; }
        }
    }
    return 0;
}

static const char *runtime_name_for(int major) {
    if (major <= 8)  return "jre-legacy";
    if (major <= 16) return "java-runtime-alpha";
    if (major <= 17) return "java-runtime-gamma";
    if (major <= 21) return "java-runtime-delta";
    return "java-runtime-delta";
}

typedef struct { int major; char comp[64]; } JavaArg;

static DWORD WINAPI java_thread(LPVOID p) {
    JavaArg *ja = (JavaArg *)p;
    int major = ja->major;
    char ja_comp[64];
    snprintf(ja_comp, sizeof(ja_comp), "%s", ja->comp);
    free(ja);

    char *txt = fetch_json(RUNTIME_URL);
    if (!txt) { app_toast("无法获取 Java 运行时清单"); return 0; }
    JVal *root = json_parse(txt); free(txt);
    if (!root) { app_toast("运行时清单解析失败"); return 0; }

    JVal *plat = json_get(root, "windows-x64");

    /* 优先用版本 JSON 里指定的运行时组件名（javaVersion.component） */
    const char *rname = NULL;
    JVal *arr = NULL;
    if (ja_comp[0]) {
        JVal *v = json_get(plat, ja_comp);
        if (v && json_arr_n(v) > 0) { arr = v; rname = ja_comp; }
    }
    if (!arr) {
        /* 否则按 Java 主版本挑：能满足要求的最小版本；都不满足则取最大的 */
        const char *best_ge = NULL, *best_any = NULL;
        int ge_major = 0, any_major = 0;
        for (int i = 0; i < json_obj_n(plat); i++) {
            const char *k = json_key_at(plat, i);
            if (!k || !strncmp(k, "minecraft", 9)) continue;
            JVal *v = json_val_at(plat, i);
            if (json_arr_n(v) == 0) continue;
            const char *vn = json_str(json_path(json_arr_at(v, 0), "version.name"), "");
            int maj = atoi(vn);
            if (maj <= 0) continue;
            if (maj >= major && (!best_ge || maj < ge_major)) { best_ge = k; ge_major = maj; }
            if (maj > any_major) { any_major = maj; best_any = k; }
        }
        rname = best_ge ? best_ge : best_any;
        if (rname) arr = json_get(plat, rname);
    }
    if (!arr || json_arr_n(arr) == 0) {
        for (int i = 0; i < json_obj_n(plat); i++) {
            JVal *v = json_val_at(plat, i);
            if (json_arr_n(v) > 0) { arr = v; rname = json_key_at(plat, i); break; }
        }
    }
    if (!arr || json_arr_n(arr) == 0) {
        json_free(root);
        app_toast("没有可用的 Java 运行时");
        return 0;
    }
    const char *murl = json_path(json_arr_at(arr, 0), "manifest.url") ?
                       json_str(json_path(json_arr_at(arr, 0), "manifest.url"), NULL) : NULL;
    if (!murl) { json_free(root); app_toast("运行时清单缺少地址"); return 0; }

    char *mtxt = fetch_json(murl);
    if (!mtxt) { json_free(root); app_toast("运行时文件清单下载失败"); return 0; }
    JVal *mj = json_parse(mtxt); free(mtxt);
    if (!mj) { json_free(root); app_toast("运行时文件清单解析失败"); return 0; }

    char rdir[MAX_PATH * 2];
    char tmp[MAX_PATH * 2];
    lz_path_join(tmp, sizeof(tmp), g.set.game_dir, "runtime");
    lz_path_join(rdir, sizeof(rdir), tmp, rname);
    lz_dir_make(rdir);

    JVal *files = json_get(mj, "files");
    FileQueue *q = fq_create(g.set.dl_threads, ui_request_redraw);
    EnterCriticalSection(&g_lock);
    g.inst.q = q;
    g.inst.active = 1;
    snprintf(g.inst.stage, sizeof(g.inst.stage), "下载 Java 运行时");
    LeaveCriticalSection(&g_lock);

    int nf = json_obj_n(files);
    for (int i = 0; i < nf; i++) {
        const char *rel = json_key_at(files, i);
        JVal *f = json_val_at(files, i);
        if (!strcmp(json_str(json_get(f, "type"), ""), "directory")) continue;
        const char *u = json_path(f, "downloads.raw.url") ?
                        json_str(json_path(f, "downloads.raw.url"), NULL) : NULL;
        if (!u) continue;
        char fixed[512];
        snprintf(fixed, sizeof(fixed), "%s", rel);
        for (char *c = fixed; *c; c++) if (*c == '/') *c = '\\';
        char full[MAX_PATH * 3];
        lz_path_join(full, sizeof(full), rdir, fixed);
        fq_add_size(q, u, full, (long long)json_num(json_path(f, "downloads.raw.size"), -1));
    }
    fq_run(q);

    char javaexe[MAX_PATH * 3];
    lz_path_join(javaexe, sizeof(javaexe), rdir, "bin\\java.exe");
    int ok = lz_file_exists(javaexe);
    if (ok) {
        EnterCriticalSection(&g_lock);
        snprintf(g.set.java_path, sizeof(g.set.java_path), "%s", javaexe);
        LeaveCriticalSection(&g_lock);
        app_toast("Java 运行时安装完成");
    } else {
        app_toast("Java 运行时安装失败");
    }

    json_free(mj);
    json_free(root);
    EnterCriticalSection(&g_lock);
    g.inst.stage[0] = 0;
    g.inst.finished = 1;
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
    return 0;
}

void java_install_async(int major) {
    if (g.inst.active) { app_toast("有任务正在进行"); return; }
    g.inst.active = 1;
    g.inst.finished = 0;
    JavaArg *ja = (JavaArg *)calloc(1, sizeof(JavaArg));
    ja->major = major;
    CloseHandle(CreateThread(NULL, 0, java_thread, ja, 0, NULL));
    ui_request_redraw();
}

/* 读取版本要求的 Java 组件名（如 java-runtime-epsilon） */
/* Forge/Fabric 的版本 json 里没有 javaVersion，要沿 inheritsFrom 往上找 */
static JVal *ver_json_chain(const char *id) {
    char cur[96];
    snprintf(cur, sizeof(cur), "%s", id);
    for (int depth = 0; depth < 4; depth++) {
        char p[MAX_PATH * 2];
        ver_file(p, sizeof(p), cur, "json");
        char *txt = NULL;
        if (!lz_file_read(p, &txt, NULL)) return NULL;
        JVal *root = json_parse(txt);
        free(txt);
        if (!root) return NULL;
        if (json_path(root, "javaVersion.majorVersion")) return root;
        const char *parent = json_str(json_get(root, "inheritsFrom"), "");
        if (!parent[0]) return root;
        snprintf(cur, sizeof(cur), "%s", parent);
        json_free(root);
    }
    return NULL;
}

static void mc_java_component(const char *id, char *out, size_t cap) {
    out[0] = 0;
    JVal *root = ver_json_chain(id);
    if (!root) return;
    snprintf(out, cap, "%s", json_str(json_path(root, "javaVersion.component"), ""));
    json_free(root);
}

int mc_get_java_major(const char *id) {
    JVal *root = ver_json_chain(id);
    if (!root) return 8;
    int major = (int)json_num(json_path(root, "javaVersion.majorVersion"), 0);
    if (major == 0) {
        const char *rel = json_str(json_get(root, "releaseTime"), "");
        int year = atoi(rel);
        major = (year >= 2023) ? 17 : (year >= 2021 ? 16 : 8);
    }
    json_free(root);
    return major;
}

/* ---------------------------------------------------------------- 启动 */
typedef struct { const char *k; const char *v; } KV;

static const char *kv_lookup(KV *kv, int n, const char *key) {
    for (int i = 0; i < n; i++) if (!strcmp(kv[i].k, key)) return kv[i].v;
    return NULL;
}

static void expand(const char *in, KV *kv, int n, Sb *out) {
    const char *p = in;
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            const char *e = strchr(p + 2, '}');
            if (!e) { sb_addc(out, *p++); continue; }
            char key[96];
            size_t len = (size_t)(e - p - 2);
            if (len >= sizeof(key)) len = sizeof(key) - 1;
            memcpy(key, p + 2, len); key[len] = 0;
            const char *val = kv_lookup(kv, n, key);
            if (val) sb_add(out, val);
            p = e + 1;
        } else {
            sb_addc(out, *p++);
        }
    }
}

static void add_arg(Sb *cmd, const char *arg) {
    sb_add(cmd, " ");
    int need = 0;
    for (const char *p = arg; *p; p++) if (*p == ' ' || *p == '\t') { need = 1; break; }
    if (!*arg) need = 1;
    if (need) {
        sb_addc(cmd, '"');
        sb_add(cmd, arg);
        sb_addc(cmd, '"');
    } else {
        sb_add(cmd, arg);
    }
}

static void add_arg_expanded(Sb *cmd, const char *raw, KV *kv, int n) {
    Sb tmp; sb_init(&tmp); sb_add(&tmp, "");
    expand(raw, kv, n, &tmp);
    if (tmp.n) add_arg(cmd, tmp.s ? tmp.s : "");
    sb_free(&tmp);
}

/* Forge 的 jvm 参数里有 -DignoreList=...,${version_name}.jar，
   作用是让 bootstraplauncher 忽略原版 client jar（它自己提供打过补丁的类）。
   继承版本的 jar 实际来自父版本（比如 1.19.2.jar），跟 version_name 对不上，
   忽略不掉就会变成自动模块 _1._19._2，和 forge 的 minecraft 模块重复导出同一个包：
   "Modules minecraft and _1._19._2 export package net.minecraft.client.main"
   这里把 ignoreList 里的 <id>.jar 换成真实的 jar 文件名。 */
static void fix_forge_ignorelist(Sb *cmd, const char *id, const char *jar) {
    if (!cmd || !cmd->s || !id || !id[0] || !jar || !jar[0]) return;
    const char *bn = strrchr(jar, '\\');
    if (!bn) bn = strrchr(jar, '/');
    bn = bn ? bn + 1 : jar;
    if (!strcmp(bn, id)) return;

    char from[160], to[160];
    snprintf(from, sizeof(from), "%s.jar", id);
    snprintf(to, sizeof(to), "%s", bn);
    if (strlen(to) >= strlen(from)) return;   /* 只在变短时原地替换 */

    size_t flen = strlen(from), tlen = strlen(to);
    char *q = cmd->s;
    while ((q = strstr(q, from)) != NULL) {
        memmove(q + tlen, q + flen, strlen(q + flen) + 1);
        memcpy(q, to, tlen);
        q += tlen;
    }
    cmd->n = strlen(cmd->s);
}

static HANDLE build_and_run(const char *id, const char *java, const char *server, int port,
                            char *err, size_t errcap) {
    if (g.auth.state != AUTH_OK || !g.auth.token[0]) {
        snprintf(err, errcap, "请先登录微软账号");
        return NULL;
    }
    char jp[MAX_PATH * 2];
    ver_file(jp, sizeof(jp), id, "json");
    if (!lz_file_exists(jp)) { snprintf(err, errcap, "版本未安装"); return NULL; }
    char jar_id[64] = {0};
    JVal *root = mc_merged_root(id, jar_id, sizeof(jar_id));
    if (!root) { snprintf(err, errcap, "版本 JSON 损坏"); return NULL; }

    char libs[MAX_PATH * 2], assets[MAX_PATH * 2], natives[MAX_PATH * 3];
    char jar[MAX_PATH * 2];
    lz_path_join(libs, sizeof(libs), g.set.game_dir, "libraries");
    lz_path_join(assets, sizeof(assets), g.set.game_dir, "assets");
    natives_dir(natives, sizeof(natives), id);
    ver_file(jar, sizeof(jar), jar_id[0] ? jar_id : id, "jar");

    /* classpath */
    Sb cp; sb_init(&cp); sb_add(&cp, "");
    sb_add(&cp, jar);
    JVal *libarr = json_get(root, "libraries");
    int nl = json_arr_n(libarr);
    for (int i = 0; i < nl; i++) {
        JVal *lib = json_arr_at(libarr, i);
        if (!rule_allow(json_get(lib, "rules"))) continue;
        JVal *art = json_path(lib, "downloads.artifact");
        const char *rel = art ? json_str(json_get(art, "path"), NULL) : NULL;
        /* 新版 MC（1.19+ / 26.x）自己从 classpath 加载 natives，
           所以 natives jar 也要进 classpath，同时仍解压到 natives 目录兜底 */
        if (rel) {
            char full[MAX_PATH * 2];
            lz_path_join(full, sizeof(full), libs, rel);
            sb_addc(&cp, ';');
            sb_add(&cp, full);
        } else {
            const char *lname = json_str(json_get(lib, "name"), NULL);
            if (lname) {
                char r2[400], full[MAX_PATH * 2];
                maven_path(lname, "jar", r2, sizeof(r2));
                lz_path_join(full, sizeof(full), libs, r2);
                sb_addc(&cp, ';');
                sb_add(&cp, full);
            }
        }
    }

    const char *assets_id = json_str(json_path(root, "assetIndex.id"), "legacy");
    const char *main_class = json_str(json_get(root, "mainClass"), "net.minecraft.client.main.Main");
    const char *vtype = json_str(json_get(root, "type"), "release");

    /* 占位符表 */
    char mem[64];
    int memmb = LZ_CLAMP(g.set.mem_mb, 1024, 65536);
    snprintf(mem, sizeof(mem), "-Xmx%dM", memmb);

    char uuid[64], token[4100], uname[80];
    EnterCriticalSection(&g_lock);
    int logged = (g.auth.state == AUTH_OK && g.auth.token[0]);
    if (logged) {
        snprintf(uname, sizeof(uname), "%s", g.auth.name);
        snprintf(uuid, sizeof(uuid), "%s", g.auth.uuid);
        snprintf(token, sizeof(token), "%s", g.auth.token);
    } else {
        uname[0] = uuid[0] = token[0] = 0;
    }
    char resdir[MAX_PATH * 2];
    lz_path_join(resdir, sizeof(resdir), g.set.game_dir, "resources");
    LeaveCriticalSection(&g_lock);

    KV kv[32]; int nkv = 0;
#define KVADD(K, V) do { kv[nkv].k = K; kv[nkv].v = V; nkv++; } while (0)
    KVADD("auth_player_name", uname);
    KVADD("version_name", id);
    /* 版本隔离：每个版本自己的 mods / saves / config */
    static char iso_dir[MAX_PATH * 4];
    if (g.set.version_isolate && id && id[0]) {
        char vroot[MAX_PATH * 3];
        lz_path_join(vroot, sizeof(vroot), g.set.game_dir, "versions");
        lz_path_join(iso_dir, sizeof(iso_dir), vroot, id);
        lz_dir_make(iso_dir);
        const char *subs[] = { "mods", "saves", "config", "resourcepacks", "shaderpacks" };
        for (int si = 0; si < 5; si++) {
            char sd[MAX_PATH * 5];
            lz_path_join(sd, sizeof(sd), iso_dir, subs[si]);
            lz_dir_make(sd);
        }
    } else {
        snprintf(iso_dir, sizeof(iso_dir), "%s", g.set.game_dir);
    }
    KVADD("game_directory", iso_dir);
    KVADD("assets_root", assets);
    KVADD("assets_index_name", assets_id);
    KVADD("auth_uuid", uuid);
    KVADD("auth_access_token", token);
    KVADD("clientid", "lazuli");
    KVADD("auth_xuid", logged ? g.auth.xuid : "0");
    KVADD("user_type", "msa");
    KVADD("version_type", vtype);
    KVADD("natives_directory", natives);
    KVADD("launcher_name", "LazuliLauncher");
    KVADD("launcher_version", LZ_VERSION);
    KVADD("classpath", cp.s ? cp.s : "");
    KVADD("classpath_separator", ";");
    KVADD("library_directory", libs);
    KVADD("resolution_width", "1280");
    KVADD("resolution_height", "720");
    KVADD("game_assets", resdir);
    KVADD("user_properties", "{}");
    KVADD("user_properties", "{}");
#undef KVADD

    Sb cmd; sb_init(&cmd); sb_add(&cmd, "");
    sb_addc(&cmd, '"'); sb_add(&cmd, java); sb_addc(&cmd, '"');
    sb_add(&cmd, " "); sb_add(&cmd, mem);
    sb_add(&cmd, " -Djava.library.path=\""); sb_add(&cmd, natives); sb_add(&cmd, "\"");
    sb_add(&cmd, " -Dminecraft.launcher.brand=LazuliLauncher");
    sb_add(&cmd, " -Dminecraft.launcher.version="); sb_add(&cmd, LZ_VERSION);
    sb_add(&cmd, " -Dfile.encoding=UTF-8 -Dstdout.encoding=UTF-8 -Dstderr.encoding=UTF-8");

    JVal *jvmargs = json_path(root, "arguments.jvm");
    if (jvmargs) {
        for (int i = 0; i < json_arr_n(jvmargs); i++) {
            JVal *a = json_arr_at(jvmargs, i);
            if (a->t == J_STR) {
                add_arg_expanded(&cmd, a->u.str, kv, nkv);
            } else if (a->t == J_OBJ) {
                if (!rule_allow(json_get(a, "rules"))) continue;
                JVal *val = json_get(a, "value");
                if (val && val->t == J_STR) add_arg_expanded(&cmd, val->u.str, kv, nkv);
                else if (val && val->t == J_ARR)
                    for (int k = 0; k < json_arr_n(val); k++)
                        add_arg_expanded(&cmd, json_str(json_arr_at(val, k), ""), kv, nkv);
            }
        }
    } else {
        sb_add(&cmd, " -cp \"");
        sb_add(&cmd, cp.s ? cp.s : "");
        sb_add(&cmd, "\"");
    }

    sb_add(&cmd, " "); sb_add(&cmd, main_class);

    JVal *gargs = json_path(root, "arguments.game");
    if (gargs) {
        for (int i = 0; i < json_arr_n(gargs); i++) {
            JVal *a = json_arr_at(gargs, i);
            if (a->t == J_STR) add_arg_expanded(&cmd, a->u.str, kv, nkv);
            else if (a->t == J_OBJ) {
                if (!rule_allow(json_get(a, "rules"))) continue;
                JVal *val = json_get(a, "value");
                if (val && val->t == J_STR) add_arg_expanded(&cmd, val->u.str, kv, nkv);
                else if (val && val->t == J_ARR)
                    for (int k = 0; k < json_arr_n(val); k++)
                        add_arg_expanded(&cmd, json_str(json_arr_at(val, k), ""), kv, nkv);
            }
        }
    } else {
        const char *legacy = json_str(json_get(root, "minecraftArguments"), NULL);
        if (legacy) {
            char buf[4096];
            snprintf(buf, sizeof(buf), "%s", legacy);
            char *save = NULL;
            for (char *t = strtok_r(buf, " ", &save); t; t = strtok_r(NULL, " ", &save))
                add_arg_expanded(&cmd, t, kv, nkv);
        }
    }

    /* 快捷联机：自动连接服务器 */
    if (server && *server) {
        add_arg(&cmd, "--server"); add_arg(&cmd, server);
        char ps[16]; snprintf(ps, sizeof(ps), "%d", port);
        add_arg(&cmd, "--port"); add_arg(&cmd, ps);
    }

    /* 日志文件 */
    char logdir[MAX_PATH * 2], logfile[MAX_PATH * 3];
    lz_path_join(logdir, sizeof(logdir), g.set.game_dir, "logs");
    lz_dir_make(logdir);
    SYSTEMTIME st; GetLocalTime(&st);
    char ln[128];
    snprintf(ln, sizeof(ln), "lazuli-%04d%02d%02d-%02d%02d%02d.log",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    lz_path_join(logfile, sizeof(logfile), logdir, ln);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hlog = CreateFileA(logfile, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hlog; si.hStdError = hlog;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    fix_forge_ignorelist(&cmd, id, jar);

    BOOL ok = CreateProcessA(NULL, cmd.s, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, g.set.game_dir, &si, &pi);
    if (hlog != INVALID_HANDLE_VALUE) CloseHandle(hlog);
    sb_free(&cp);
    sb_free(&cmd);
    json_free(root);

    if (!ok) {
        snprintf(err, errcap, "启动失败（错误码 %lu）", GetLastError());
        return NULL;
    }
    CloseHandle(pi.hThread);
    EnterCriticalSection(&g_lock);
    g.game.running = 1;
    snprintf(g.game.version, sizeof(g.game.version), "%s", id);
    snprintf(g.set.last_version, sizeof(g.set.last_version), "%s", id);
    snprintf(g.game.log_path, sizeof(g.game.log_path), "%s", logfile);
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
    return pi.hProcess;
}

typedef struct { char id[64]; char server[128]; int port; } LaunchArg;

/* 探测某个 java.exe 的主版本号，失败返回 0 */
static int java_major_of(const char *path) {
    char out[1024] = {0};
    if (!java_probe(path, out, sizeof(out))) return 0;
    const char *q = strstr(out, "version \"");
    if (!q) return 0;
    q += 9;
    int major = atoi(q);
    if (major == 1) {
        const char *dot = strchr(q, '.');
        if (dot) major = atoi(dot + 1);
    }
    return major;
}

/* 下载与版本匹配的 Java 运行时，返回 1 成功 */
/* 在 <game_dir>/runtime/* 里找 major 匹配的 java.exe */
static int find_runtime_for(int need, char *out, size_t cap) {
    char rd[MAX_PATH * 3], pat[MAX_PATH * 4];
    lz_path_join(rd, sizeof(rd), g.set.game_dir, "runtime");
    snprintf(pat, sizeof(pat), "%s\\*", rd);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int found = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        char jp[MAX_PATH * 5];
        lz_path_join(jp, sizeof(jp), rd, fd.cFileName);
        lz_path_join(jp, sizeof(jp), jp, "bin\\java.exe");
        if (!lz_file_exists(jp)) continue;
        if (java_major_of(jp) == need) {
            snprintf(out, cap, "%s", jp);
            found = 1;
            break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static int install_runtime_for(const char *version_id, int need) {
    JavaArg *ja = (JavaArg *)calloc(1, sizeof(JavaArg));
    if (!ja) return 0;
    ja->major = need;
    mc_java_component(version_id, ja->comp, sizeof(ja->comp));
    HANDLE h = CreateThread(NULL, 0, java_thread, ja, 0, NULL);
    if (h) { WaitForSingleObject(h, 25 * 60 * 1000); CloseHandle(h); }
    char j[MAX_PATH * 3] = {0};
    EnterCriticalSection(&g_lock);
    snprintf(j, sizeof(j), "%s", g.set.java_path);
    LeaveCriticalSection(&g_lock);
    return j[0] != 0;
}

int java_ensure(const char *version_id, char *out, size_t cap) {
    char java[MAX_PATH * 3] = {0};
    EnterCriticalSection(&g_lock);
    snprintf(java, sizeof(java), "%s", g.set.java_path);
    LeaveCriticalSection(&g_lock);

    if (!java[0] || !lz_file_exists(java)) {
        if (!java_detect(java, sizeof(java))) java[0] = 0;
    }
    int need = mc_get_java_major(version_id);

    /* 先找已经下好的、major 正好对得上的运行时；
       不能只判断「太旧」——Java 25 跑 1.19.2 一样会崩 */
    char cand[MAX_PATH * 3];
    if (find_runtime_for(need, cand, sizeof(cand))) {
        snprintf(out, cap, "%s", cand);   /* 只返回，不改用户设置 */
        return 1;
    }

    int have = java[0] ? java_major_of(java) : 0;
    if (have == need) { snprintf(out, cap, "%s", java); return 1; }
    if (have != 0) {
        EnterCriticalSection(&g_lock);
        snprintf(g.status, sizeof(g.status),
                 have ? "需要 Java %d（当前 %d），正在准备…" : "未找到 Java，正在准备 Java %d…", need, have);
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        app_log("[Java] 需要 %d，现有 %d，开始下载运行时", need, have);
        if (!install_runtime_for(version_id, need)) return 0;
        EnterCriticalSection(&g_lock);
        snprintf(java, sizeof(java), "%s", g.set.java_path);
        LeaveCriticalSection(&g_lock);
    }
    if (!java[0]) return 0;
    snprintf(out, cap, "%s", java);
    return 1;
}

/* 启动前清理 0 字节的模组/资源包文件。
   下载中断留下的空 jar 会让 Forge 在读取时直接崩溃，且没有任何提示。 */
static int clean_broken_jars(const char *dir) {
    char pat[MAX_PATH * 4];
    snprintf(pat, sizeof(pat), "%s\\*.jar", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char full[MAX_PATH * 5];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        if (lz_file_size(full) <= 0) {
            if (DeleteFileA(full)) n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

static DWORD WINAPI launch_thread(LPVOID p) {
    LaunchArg *la = (LaunchArg *)p;
    char java[MAX_PATH * 3] = {0};

    /* 清掉上次下载中断留下的空 jar，否则 Forge 会静默崩溃 */
    {
        char md[MAX_PATH * 4];
        if (g.set.version_isolate) {
            char vr[MAX_PATH * 3];
            lz_path_join(vr, sizeof(vr), g.set.game_dir, "versions");
            lz_path_join(md, sizeof(md), vr, la->id);
            lz_path_join(md, sizeof(md), md, "mods");
        } else {
            lz_path_join(md, sizeof(md), g.set.game_dir, "mods");
        }
        int bad = clean_broken_jars(md);
        if (bad > 0) app_toast("已清理 %d 个损坏的模组文件", bad);
    }

    /* 微软令牌只有 24 小时有效期，启动前刷新一次。
       否则过期后游戏里会提示「无效的会话 ID」，联机也用不了。 */
    /* 6 小时内刷新过就跳过，避免每次启动都多等一两秒 */
    int need_refresh = g.auth.refresh[0] &&
        (g.auth.refreshed_at == 0 || (long long)time(NULL) - g.auth.refreshed_at > 6 * 3600);
    if (need_refresh) {
        EnterCriticalSection(&g_lock);
        snprintf(g.status, sizeof(g.status), "正在刷新登录令牌…");
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        char rerr[240] = {0};
        if (auth_refresh_blocking(rerr, sizeof(rerr))) {
            app_log("[启动] 令牌刷新成功");
        } else {
            app_log("[启动] 令牌刷新失败：%s", rerr);
            app_toast("令牌刷新失败：%s（联机可能受影响）", rerr);
        }
    }

    /* 统一走 java_ensure：它会挑 major 正好匹配的运行时。
       以前这里只判断 have < need，Java 25 跑 1.19.2 会被放行，
       Forge 的 Mixin 处理不了 Java 25 字节码，游戏静默崩溃、没有任何报错。 */
    if (!java_ensure(la->id, java, sizeof(java))) {
        app_toast("没有可用的 Java，请到设置里指定");
        EnterCriticalSection(&g_lock);
        g.game.running = 0;
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        free(la);
        return 0;
    }

    if (!java[0]) {
        app_toast("没有可用的 Java，请到设置里指定");
        EnterCriticalSection(&g_lock);
        g.game.running = 0;
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        free(la);
        return 0;
    }

    app_log("[启动] 使用 Java %s (major=%d)", java, java_major_of(java));
    char err[300] = {0};
    HANDLE proc = build_and_run(la->id, java, la->server, la->port, err, sizeof(err));
    if (!proc) {
        app_toast("%s", err);
        app_log("[启动] 失败: %s", err);
        EnterCriticalSection(&g_lock);
        g.game.running = 0;
        LeaveCriticalSection(&g_lock);
        ui_request_redraw();
        free(la);
        return 0;
    }
    WaitForSingleObject(proc, INFINITE);
    CloseHandle(proc);
    EnterCriticalSection(&g_lock);
    g.game.running = 0;
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
    free(la);
    return 0;
}

void mc_launch_async(const char *id, const char *server, int port) {
    if (g.game.running) { app_toast("游戏已在运行"); return; }
    if (!mc_is_installed(id)) { app_toast("请先安装版本 %s", id); return; }
    LaunchArg *la = (LaunchArg *)calloc(1, sizeof(LaunchArg));
    if (!la) return;
    snprintf(la->id, sizeof(la->id), "%s", id);
    if (server) snprintf(la->server, sizeof(la->server), "%s", server);
    la->port = port;
    EnterCriticalSection(&g_lock);
    g.game.running = 1;
    snprintf(g.game.version, sizeof(g.game.version), "%s", id);
    snprintf(g.set.last_version, sizeof(g.set.last_version), "%s", id);
    LeaveCriticalSection(&g_lock);
    CloseHandle(CreateThread(NULL, 0, launch_thread, la, 0, NULL));
    ui_request_redraw();
}
