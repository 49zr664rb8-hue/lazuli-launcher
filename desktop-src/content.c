/*  content.c —— 下载中心：Modrinth / CurseForge / MC百科 + 模组详情与前置解析 */
#include "app.h"

#define MODRINTH_SEARCH "https://api.modrinth.com/v2/search?limit=20&query="
#define MODRINTH_VER    "https://api.modrinth.com/v2/project/%s/version"
#define MODRINTH_PROJ   "https://api.modrinth.com/v2/project/%s"
#define CF_SEARCH       "https://api.curseforge.com/v1/mods/search?gameId=432&pageSize=20&searchFilter="
#define CF_FILES        "https://api.curseforge.com/v1/mods/%s/files?pageSize=1"

DlTask  *g_tasks[MAX_TASKS_DEF];
int      g_ntasks = 0;
ModDetail g_md;
int g_mdtest = 0;

void tasks_add(DlTask *t) {
    if (!t) return;
    EnterCriticalSection(&g_lock);
    if (g_ntasks < MAX_TASKS_DEF) g_tasks[g_ntasks++] = t;
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
}

/* 0 原版 / 1 模组 / 2 光影 / 3 资源包 / 4 整合包 */
static const char *kind_dir(int kind) {
    switch (kind) {
        case 2: return "shaderpacks";
        case 3: return "resourcepacks";
        case 4: return "modpacks";
        default: return "mods";
    }
}

/* 模组要装进「对应版本」的 mods 目录：
   先按模组声明的游戏版本 + 加载器去已安装版本里找，找不到就用最近启动的版本，
   再不行退回全局 mods 目录。 */
static void mod_target_dir(const MdVer *v, int kind, char *out, size_t cap) {
    char base[MAX_PATH * 3];
    lz_path_join(base, sizeof(base), g.set.game_dir, kind_dir(kind));

    if (kind != 0 && kind != 1) {   /* 只有 mods 按版本分流；光影/材质/整合包不分 */
        snprintf(out, cap, "%s", base);
        return;
    }

    const char *want_game = (v && v->game[0]) ? v->game : NULL;
    const char *want_ld   = (v && v->loader[0]) ? v->loader : NULL;
    const char *pick = NULL;

    for (int i = 0; i < g.nversions; i++) {
        McVer *mv = &g.versions[i];
        if (!mv->installed) continue;
        if (want_game && !strstr(mv->id, want_game)) continue;
        if (want_ld) {
            int id_fab = strstr(mv->id, "fabric") != NULL;
            int id_forge = strstr(mv->id, "forge") != NULL;
            int id_neo = strstr(mv->id, "neoforge") != NULL;
            if (strstr(want_ld, "fabric") && !id_fab) continue;
            if (strstr(want_ld, "neoforge") && !id_neo) continue;
            if (strstr(want_ld, "forge") && !id_neo && !id_forge) continue;
        }
        pick = mv->id;
        break;
    }
    if (!pick && g.game.version[0]) pick = g.game.version;
    if (!pick && g.set.last_version[0]) pick = g.set.last_version;

    if (pick && pick[0]) {
        char vdir[MAX_PATH * 4];
        lz_path_join(vdir, sizeof(vdir), g.set.game_dir, "versions");
        lz_path_join(vdir, sizeof(vdir), vdir, pick);
        lz_path_join(out, cap, vdir, kind_dir(kind));
    } else {
        snprintf(out, cap, "%s", base);
    }
}


static int cf_class(int kind) {
    switch (kind) {
        case 2: return 6552;
        case 3: return 12;
        case 4: return 4471;
        default: return 6;
    }
}

static const char *mr_type(int kind) {
    switch (kind) {
        case 2: return "shader";
        case 3: return "resourcepack";
        case 4: return "modpack";
        default: return "mod";
    }
}

static const char *mr_loader(int kind) {
    switch (kind) {
        case LD_FABRIC:   return "fabric";
        case LD_QUILT:    return "quilt";
        case LD_FORGE:    return "forge";
        case LD_NEOFORGE: return "neoforge";
        default:          return NULL;
    }
}


/* ---------------------------------------------------------------- 中文别名
 * Modrinth / CurseForge 只有英文名，这里把常见中文叫法映射过去，
 * 免得用户搜「机械动力」得到一片空白。 */
static const struct { const char *cn; const char *en; } MOD_ALIAS[] = {
    { "钠",         "sodium" },
    { "锂",         "lithium" },
    { "铷",         "rubidium" },
    { "机械动力",   "create" },
    { "匠魂",       "tinkers construct" },
    { "苹果皮",     "apple skin" },
    { "高清修复",   "optifine" },
    { "光影",       "iris" },
    { "光影前置",   "iris" },
    { "星光",       "starlight" },
    { "小地图",     "xaeros minimap" },
    { "地图",       "journeymap" },
    { "合成表",     "jei" },
    { "物品管理器", "jei" },
    { "玉",         "jade" },
    { "血量显示",   "jade" },
    { "传送石碑",   "waystones" },
    { "植物魔法",   "botania" },
    { "工业时代",   "industrial craft" },
    { "通用机械",   "mekanism" },
    { "应用能源",   "applied energistics" },
    { "沉浸工程",   "immersive engineering" },
    { "农夫乐事",   "farmers delight" },
    { "更多箱子",   "iron chests" },
    { "神秘农业",   "mystical agriculture" },
    { "血魔法",     "blood magic" },
    { "虚无世界",   "advent of ascension" },
    { "更好的下界", "better nether" },
    { "拔刀剑",     "slashblade" },
    { "暮色森林",   "twilight forest" },
    { "深渊国度",   "abyssalcraft" },
    { "区块预生成", "chunky" },
    { "优化",       "sodium" },
    { "帧数优化",   "sodium" },
};

static const char *alias_lookup(const char *q) {
    if (!q || !*q) return NULL;
    for (int i = 0; i < LZ_COUNT(MOD_ALIAS); i++) {
        if (!strcmp(q, MOD_ALIAS[i].cn)) return MOD_ALIAS[i].en;
    }
    for (int i = 0; i < LZ_COUNT(MOD_ALIAS); i++) {
        if (strstr(q, MOD_ALIAS[i].cn)) return MOD_ALIAS[i].en;
    }
    return NULL;
}

/* ---------------------------------------------------------------- 搜索 */
typedef struct { char q[160]; int src, kind, nover, note; } SearchArg;

static DWORD WINAPI search_thread(LPVOID p) {
    SearchArg *sa = (SearchArg *)p;
    int st = 0;

    if (sa->src == 2) {
        char *q = lz_url_encode(sa->q);
        char url[600];
        snprintf(url, sizeof(url), "https://search.mcmod.cn/s?key=%s", q ? q : "");
        free(q);
        lz_shell_open(url);
        EnterCriticalSection(&g_lock);
        g.ncontent = 0;
        g.content_state = 2;
        g.content_err[0] = 0;
        LeaveCriticalSection(&g_lock);
        free(sa);
        ui_request_redraw();
        return 0;
    }

    char qbuf[160];
    snprintf(qbuf, sizeof(qbuf), "%s", sa->q);
    lz_trim(qbuf);
    const char *mapped = alias_lookup(qbuf);
    if (mapped) {
        snprintf(qbuf, sizeof(qbuf), "%s", mapped);
        app_toast("「%s」已按英文名搜索：%s", sa->q, mapped);
    }

    char *q = lz_url_encode(qbuf);
    Sb url; sb_init(&url); sb_add(&url, "");
    char hdr[512];
    const char *hs[3]; int hn = 0;
    char sel_mc[40];
    int sel_loader;

    EnterCriticalSection(&g_lock);
    snprintf(sel_mc, sizeof(sel_mc), "%s", g.sel_mc);
    sel_loader = g.sel_loader;
    LeaveCriticalSection(&g_lock);

    if (sa->src == 0) {
        sb_printf(&url, "%s%s&facets=[[\"project_type:%s\"]", MODRINTH_SEARCH, q ? q : "", mr_type(sa->kind));
        if (sel_mc[0] && !sa->nover) sb_printf(&url, ",[\"versions:%s\"]", sel_mc);
        const char *ld = mr_loader(sel_loader);
        if (ld && (sa->kind == 1 || sa->kind == 4)) sb_printf(&url, ",[\"categories:%s\"]", ld);
        sb_add(&url, "]");
    } else {
        sb_printf(&url, "%s%s&classId=%d", CF_SEARCH, q ? q : "", cf_class(sa->kind));
        EnterCriticalSection(&g_lock);
        snprintf(hdr, sizeof(hdr), "x-api-key: %s", g.set.cf_key);
        LeaveCriticalSection(&g_lock);
        hs[hn++] = hdr;
    }
    free(q);

    Buf b = {0};
    if (sa->src == 0) http_get(url.s, &b, &st);
    else http_get_h(url.s, hs, hn, &b, &st);
    sb_free(&url);

    if (!b.data) {
        EnterCriticalSection(&g_lock);
        g.content_state = 3;
        snprintf(g.content_err, sizeof(g.content_err),
                 sa->src == 1 ? "请求失败，请检查 CurseForge API Key" : "请求失败，请检查网络");
        LeaveCriticalSection(&g_lock);
        free(sa);
        ui_request_redraw();
        return 0;
    }

    JVal *root = json_parse(b.data);
    buf_free(&b);
    if (!root) {
        EnterCriticalSection(&g_lock);
        g.content_state = 3;
        snprintf(g.content_err, sizeof(g.content_err), "响应解析失败");
        LeaveCriticalSection(&g_lock);
        free(sa);
        ui_request_redraw();
        return 0;
    }

    int n = 0;
    EnterCriticalSection(&g_lock);
    g.ncontent = 0;
    if (sa->src == 0) {
        JVal *hits = json_get(root, "hits");
        for (int i = 0; i < json_arr_n(hits) && n < MAX_CONTENT; i++) {
            JVal *h = json_arr_at(hits, i);
            ContentItem *ci = &g.content[n++];
            memset(ci, 0, sizeof(*ci));
            snprintf(ci->id, sizeof(ci->id), "%s", json_str(json_get(h, "project_id"), ""));
            snprintf(ci->title, sizeof(ci->title), "%s", json_str(json_get(h, "title"), "未命名"));
            snprintf(ci->desc, sizeof(ci->desc), "%s", json_str(json_get(h, "description"), ""));
            snprintf(ci->author, sizeof(ci->author), "%s", json_str(json_get(h, "author"), ""));
            ci->downloads = (long long)json_num(json_get(h, "downloads"), 0);
            JVal *gv = json_get(h, "versions");
            if (json_arr_n(gv) > 0)
                snprintf(ci->ver, sizeof(ci->ver), "%s",
                         json_str(json_arr_at(gv, json_arr_n(gv) - 1), ""));
        }
    } else {
        JVal *data = json_get(root, "data");
        for (int i = 0; i < json_arr_n(data) && n < MAX_CONTENT; i++) {
            JVal *h = json_arr_at(data, i);
            ContentItem *ci = &g.content[n++];
            memset(ci, 0, sizeof(*ci));
            snprintf(ci->id, sizeof(ci->id), "%.0f", json_num(json_get(h, "id"), 0));
            snprintf(ci->title, sizeof(ci->title), "%s", json_str(json_get(h, "name"), "未命名"));
            snprintf(ci->desc, sizeof(ci->desc), "%s", json_str(json_get(h, "summary"), ""));
            ci->downloads = (long long)json_num(json_get(h, "downloadCount"), 0);
        }
    }
    g.ncontent = n;
    g.content_state = 2;
    g.content_err[0] = 0;
    if (sa->note) snprintf(g.content_note, sizeof(g.content_note),
                           "%s 下没有匹配，已为你显示所有版本", sel_mc);
    else g.content_note[0] = 0;
    int retry = (n == 0 && sel_mc[0] && !sa->nover && sa->src == 0);
    if (retry) g.content_state = 1;   /* 继续保持加载态，准备重试 */
    LeaveCriticalSection(&g_lock);
    json_free(root);

    if (retry) {
        SearchArg *sa2 = (SearchArg *)calloc(1, sizeof(SearchArg));
        *sa2 = *sa;
        sa2->nover = 1;
        sa2->note = 1;
        free(sa);
        CloseHandle(CreateThread(NULL, 0, search_thread, sa2, 0, NULL));
        return 0;
    }

    free(sa);
    ui_request_redraw();
    return 0;
}

void content_search_async(const char *query, int source, int kind) {
    if (g.content_state == 1) return;
    app_log("[搜索] q=\"%s\" src=%d kind=%d mc=%s loader=%d", query ? query : "", source, kind,
            g.sel_mc, g.sel_loader);
    EnterCriticalSection(&g_lock);
    snprintf(g.content_query, sizeof(g.content_query), "%s", query ? query : "");
    g.content_source = source;
    g.content_kind = kind;
    g.content_state = 1;
    g.content_err[0] = 0;
    LeaveCriticalSection(&g_lock);
    SearchArg *sa = (SearchArg *)calloc(1, sizeof(SearchArg));
    snprintf(sa->q, sizeof(sa->q), "%s", query ? query : "");
    sa->src = source;
    sa->kind = kind;
    CloseHandle(CreateThread(NULL, 0, search_thread, sa, 0, NULL));
    ui_request_redraw();
}

/* ==========================================================================
 *  模组详情
 * ========================================================================== */
void md_close(void) {
    EnterCriticalSection(&g_lock);
    g_md.active = 0;
    g_md.state = 0;
    g_md.dep_state = 0;
    g_md.ndeps = 0;
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
}

static void md_fill_versions(JVal *arr) {
    g_md.nvers = 0;
    g_md.sel = 0;
    int n = json_arr_n(arr);
    for (int i = 0; i < n && g_md.nvers < MD_MAX_VERS; i++) {
        JVal *v = json_arr_at(arr, i);
        JVal *files = json_get(v, "files");
        JVal *f = NULL;
        for (int k = 0; k < json_arr_n(files); k++) {
            JVal *cand = json_arr_at(files, k);
            if (json_bool(json_get(cand, "primary"), 0)) { f = cand; break; }
        }
        if (!f && json_arr_n(files) > 0) f = json_arr_at(files, 0);
        if (!f) continue;
        const char *u = json_str(json_get(f, "url"), NULL);
        if (!u) continue;

        MdVer *mv = &g_md.vers[g_md.nvers];
        memset(mv, 0, sizeof(*mv));
        snprintf(mv->id, sizeof(mv->id), "%s", json_str(json_get(v, "id"), ""));
        snprintf(mv->name, sizeof(mv->name), "%s", json_str(json_get(v, "name"), ""));
        snprintf(mv->ver, sizeof(mv->ver), "%s", json_str(json_get(v, "version_number"), ""));
        snprintf(mv->url, sizeof(mv->url), "%s", u);
        snprintf(mv->fname, sizeof(mv->fname), "%s", json_str(json_get(f, "filename"), "mod.jar"));

        JVal *gv = json_get(v, "game_versions");
        if (json_arr_n(gv) > 0)
            snprintf(mv->game, sizeof(mv->game), "%s", json_str(json_arr_at(gv, 0), ""));
        JVal *lds = json_get(v, "loaders");
        if (json_arr_n(lds) > 0)
            snprintf(mv->loader, sizeof(mv->loader), "%s", json_str(json_arr_at(lds, 0), ""));

        JVal *deps = json_get(v, "dependencies");
        for (int k = 0; k < json_arr_n(deps) && mv->nreq < 8; k++) {
            JVal *d = json_arr_at(deps, k);
            const char *type = json_str(json_get(d, "dependency_type"), "");
            const char *pid = json_str(json_get(d, "project_id"), NULL);
            if (!pid || strcmp(type, "required")) continue;
            snprintf(mv->req_id[mv->nreq], 96, "%s", pid);
            mv->nreq++;
        }
        g_md.nvers++;
    }
}

typedef struct { char pid[96]; int idx; } MdArg;

static DWORD WINAPI md_load_thread(LPVOID p) {
    MdArg *a = (MdArg *)p;
    char url[600];
    snprintf(url, sizeof(url), MODRINTH_VER, a->pid);
    char *txt = NULL;
    int ok = http_get_text(url, &txt);

    EnterCriticalSection(&g_lock);
    if (!ok || !txt) {
        g_md.state = 3;
        snprintf(g_md.err, sizeof(g_md.err), "无法获取版本列表");
        LeaveCriticalSection(&g_lock);
        free(a);
        ui_request_redraw();
        return 0;
    }
    JVal *root = json_parse(txt);
    free(txt);
    if (!root) {
        g_md.state = 3;
        snprintf(g_md.err, sizeof(g_md.err), "版本列表解析失败");
        LeaveCriticalSection(&g_lock);
        free(a);
        ui_request_redraw();
        return 0;
    }
    md_fill_versions(root);
    g_md.state = g_md.nvers > 0 ? 2 : 3;
    if (g_md.nvers == 0) snprintf(g_md.err, sizeof(g_md.err), "这个项目没有可下载的文件");
    LeaveCriticalSection(&g_lock);
    json_free(root);
    free(a);
    ui_request_redraw();
    return 0;
}

void md_open(int index) {
    if (index < 0 || index >= g.ncontent) return;
    EnterCriticalSection(&g_lock);
    memset(&g_md, 0, sizeof(g_md));
    g_md.active = 1;
    g_md.index = index;
    g_md.state = 1;
    snprintf(g_md.project_id, sizeof(g_md.project_id), "%s", g.content[index].id);
    snprintf(g_md.title, sizeof(g_md.title), "%s", g.content[index].title);
    snprintf(g_md.desc, sizeof(g_md.desc), "%s", g.content[index].desc);
    snprintf(g_md.author, sizeof(g_md.author), "%s", g.content[index].author);
    g_md.downloads = g.content[index].downloads;
    LeaveCriticalSection(&g_lock);

    MdArg *a = (MdArg *)calloc(1, sizeof(MdArg));
    snprintf(a->pid, sizeof(a->pid), "%s", g.content[index].id);
    CloseHandle(CreateThread(NULL, 0, md_load_thread, a, 0, NULL));
    ui_request_redraw();
}

/* 解析前置：对每个 required 依赖，按当前文件对应的游戏版本+加载器取第一个文件 */
typedef struct { char pid[96]; char game[48]; char loader[28]; int idx; } DepArg;

static DWORD WINAPI dep_thread(LPVOID p) {
    DepArg *a = (DepArg *)p;
    char url[700];
    char gq[160] = {0}, lq[160] = {0};
    if (a->game[0]) snprintf(gq, sizeof(gq), "&game_versions=[\"%s\"]", a->game);
    if (a->loader[0]) snprintf(lq, sizeof(lq), "&loaders=[\"%s\"]", a->loader);

    Sb full; sb_init(&full); sb_add(&full, "");
    snprintf(url, sizeof(url), MODRINTH_VER, a->pid);
    sb_printf(&full, "%s?%s%s", url, gq[0] ? gq + 1 : "", lq[0] ? lq + 1 : "");

    char *txt = NULL;
    char title[160] = {0};
    if (http_get_text(full.s, &txt) && txt) {
        JVal *root = json_parse(txt);
        free(txt);
        if (root) {
            JVal *v = json_arr_at(root, 0);
            JVal *files = v ? json_get(v, "files") : NULL;
            if (json_arr_n(files) > 0) {
                JVal *f = json_arr_at(files, 0);
                EnterCriticalSection(&g_lock);
                if (a->idx < g_md.ndeps) {
                    snprintf(g_md.deps[a->idx].url, sizeof(g_md.deps[a->idx].url), "%s",
                             json_str(json_get(f, "url"), ""));
                    snprintf(g_md.deps[a->idx].fname, sizeof(g_md.deps[a->idx].fname), "%s",
                             json_str(json_get(f, "filename"), "dep.jar"));
                }
                LeaveCriticalSection(&g_lock);
            }
            json_free(root);
        }
    }
    sb_free(&full);

    snprintf(url, sizeof(url), MODRINTH_PROJ, a->pid);
    if (http_get_text(url, &txt) && txt) {
        JVal *root = json_parse(txt);
        free(txt);
        if (root) {
            snprintf(title, sizeof(title), "%s", json_str(json_get(root, "title"), ""));
            json_free(root);
        }
    }
    EnterCriticalSection(&g_lock);
    if (a->idx < g_md.ndeps) {
        snprintf(g_md.deps[a->idx].title, sizeof(g_md.deps[a->idx].title), "%s",
                 title[0] ? title : a->pid);
        snprintf(g_md.deps[a->idx].project, sizeof(g_md.deps[a->idx].project), "%s", a->pid);
    }
    g_md.dep_done++;
    if (g_md.dep_done >= g_md.ndeps) g_md.dep_state = 2;
    LeaveCriticalSection(&g_lock);
    free(a);
    ui_request_redraw();
    return 0;
}

void md_resolve_deps(void) {
    MdVer v;
    int total = 0;

    EnterCriticalSection(&g_lock);
    if (g_md.state != 2 || g_md.sel < 0 || g_md.sel >= g_md.nvers) {
        LeaveCriticalSection(&g_lock);
        return;
    }
    v = g_md.vers[g_md.sel];
    g_md.ndeps = 0;
    g_md.dep_done = 0;
    g_md.dep_state = 1;
    for (int i = 0; i < v.nreq && g_md.ndeps < MD_MAX_DEPS; i++) {
        MdDep *d = &g_md.deps[g_md.ndeps];
        memset(d, 0, sizeof(*d));
        snprintf(d->project, sizeof(d->project), "%s", v.req_id[i]);
        snprintf(d->title, sizeof(d->title), "解析中…");
        g_md.ndeps++;
    }
    total = g_md.ndeps;
    if (total == 0) g_md.dep_state = 2;
    LeaveCriticalSection(&g_lock);

    for (int i = 0; i < total; i++) {
        DepArg *a = (DepArg *)calloc(1, sizeof(DepArg));
        a->idx = i;
        snprintf(a->pid, sizeof(a->pid), "%s", v.req_id[i]);
        snprintf(a->game, sizeof(a->game), "%s", v.game);
        snprintf(a->loader, sizeof(a->loader), "%s", v.loader);
        CloseHandle(CreateThread(NULL, 0, dep_thread, a, 0, NULL));
    }
    if (total == 0) { ui_request_redraw(); return; }

    /* 非阻塞：各依赖线程自己收尾并把 dep_state 置为 2 */
    ui_request_redraw();
}

void md_download_all(void) {
    MdVer v;
    MdDep deps[MD_MAX_DEPS];
    int nd = 0;

    EnterCriticalSection(&g_lock);
    if (g_md.state != 2 || g_md.sel < 0 || g_md.sel >= g_md.nvers) {
        LeaveCriticalSection(&g_lock);
        return;
    }
    v = g_md.vers[g_md.sel];
    nd = g_md.ndeps;
    memcpy(deps, g_md.deps, sizeof(MdDep) * (size_t)nd);
    int kind = g.content_kind;
    LeaveCriticalSection(&g_lock);

    char dir[MAX_PATH * 3], dest[MAX_PATH * 4];
    mod_target_dir(&v, kind, dir, sizeof(dir));
    lz_dir_make(dir);

    lz_sanitize_filename(v.fname);
    lz_path_join(dest, sizeof(dest), dir, v.fname);
    DlTask *t = dl_start(v.url, dest, g.set.dl_threads, ui_request_redraw);
    if (t) tasks_add(t);
    if (kind == 0 || kind == 1) app_toast("下载到：%s", dir);

    int n = 0;
    for (int i = 0; i < nd; i++) {
        if (!deps[i].url[0]) continue;
        lz_sanitize_filename(deps[i].fname);
        lz_path_join(dest, sizeof(dest), dir, deps[i].fname);
        DlTask *d = dl_start(deps[i].url, dest, g.set.dl_threads, ui_request_redraw);
        if (d) { tasks_add(d); n++; }
    }
    app_toast("已加入下载队列：本体 + %d 个前置", n);
    md_close();
}

/* 直接下载最新（整合包等无需前置的场景） */
typedef struct { ContentItem item; int kind; } DlArg;

static DWORD WINAPI content_dl_thread(LPVOID p) {
    DlArg *da = (DlArg *)p;
    int st = 0;
    char url[1024] = {0}, fname[200] = {0};

    if (g.content_source == 0) {
        char api[600];
        snprintf(api, sizeof(api), MODRINTH_VER, da->item.id);
        Buf b = {0};
        if (http_get(api, &b, &st) && b.data) {
            JVal *root = json_parse(b.data);
            buf_free(&b);
            if (root && json_arr_n(root) > 0) {
                JVal *files = json_get(json_arr_at(root, 0), "files");
                if (json_arr_n(files) > 0) {
                    JVal *f = json_arr_at(files, 0);
                    snprintf(url, sizeof(url), "%s", json_str(json_get(f, "url"), ""));
                    snprintf(fname, sizeof(fname), "%s", json_str(json_get(f, "filename"), "download.jar"));
                }
            }
            if (root) json_free(root);
        }
    } else {
        char api[600];
        snprintf(api, sizeof(api), CF_FILES, da->item.id);
        char hdr[512];
        EnterCriticalSection(&g_lock);
        snprintf(hdr, sizeof(hdr), "x-api-key: %s", g.set.cf_key);
        LeaveCriticalSection(&g_lock);
        const char *hs[] = { hdr };
        Buf b = {0};
        if (http_get_h(api, hs, 1, &b, &st) && b.data) {
            JVal *root = json_parse(b.data);
            buf_free(&b);
            if (root && json_arr_n(json_get(root, "data")) > 0) {
                JVal *f = json_arr_at(json_get(root, "data"), 0);
                snprintf(url, sizeof(url), "%s", json_str(json_get(f, "downloadUrl"), ""));
                snprintf(fname, sizeof(fname), "%s", json_str(json_get(f, "fileName"), "download.jar"));
            }
            if (root) json_free(root);
        }
    }

    if (!url[0]) { app_toast("未能获取下载地址"); free(da); return 0; }

    char dest[MAX_PATH * 3], dir[MAX_PATH * 3];
    mod_target_dir(NULL, da->kind, dir, sizeof(dir));
    lz_dir_make(dir);
    lz_sanitize_filename(fname);
    lz_path_join(dest, sizeof(dest), dir, fname);

    DlTask *t = dl_start(url, dest, g.set.dl_threads, ui_request_redraw);
    if (t) { tasks_add(t); app_toast("开始下载：%s", fname); }
    free(da);
    return 0;
}

void content_download(int index) {
    if (index < 0 || index >= g.ncontent) return;
    DlArg *da = (DlArg *)calloc(1, sizeof(DlArg));
    if (!da) return;
    EnterCriticalSection(&g_lock);
    da->item = g.content[index];
    da->kind = g.content_kind;
    LeaveCriticalSection(&g_lock);
    CloseHandle(CreateThread(NULL, 0, content_dl_thread, da, 0, NULL));
    app_toast("正在解析下载地址…");
}
