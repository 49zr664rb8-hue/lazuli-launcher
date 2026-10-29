/*  pages.c —— 全部页面与窗口外壳（液态玻璃风格） */
#include "ui.h"
#include "app.h"
#include <shlobj.h>

#define PAD 18.0f

static struct {
    char sel_ver[64];
    char del_pending[64];   /* 待确认删除的版本 */
    int  ver_filter;          /* 0 全部 1 正式 2 快照 3 远古 */
    char ver_q[80];
    char dl_q[160];
    char mp_code[16];
    char box_url[1200];
    char box_dir[400];
    int  box_threads;
    char set_game[400], set_java[400], set_mem[8], set_cf[200], set_relay[300], set_name[40];
    char set_msa[80];
    int  set_inited;
    int  vpad_drag;
    int  cache_state;         /* 0 未测 1 测量中 2 完成 */
    long long cache_bytes;
    int  cache_files;
    int  hover_never;

    /* 下载页 */
    int  mc_sel;          /* 正式版列表下标 */
    int  ld_sel;          /* 当前加载器 kind */
    int  lv_sel;          /* 加载器版本下标 */
    int  exp_loader;      /* 展开的加载器行，-1 表示没有 */
    int  ld_done;         /* 加载器版本是否已选 */
    char last_mc[40];
    int  last_ld;
    /* 模组详情 */
    int  md_chip, md_for;
    /* 我的版本缓存 */
    int  myv_dirty;
    int  last_page;
    /* 搜索防抖 */
    char last_q[160];
    double q_t;
    int  q_pending;
} ps;

/* 已安装版本缓存 */
static struct { char id[80]; int loader; } ps_myv[160];
static int ps_myv_n = 0;

static RcF Rc(float x, float y, float w, float h) { RcF r = { x, y, w, h }; return r; }
static int acct_modal;

/* 加载器 → Modrinth 用的标识 */
static const char *mr_loader_name(int kind) {
    switch (kind) {
        case LD_FABRIC:   return "fabric";
        case LD_QUILT:    return "quilt";
        case LD_FORGE:    return "forge";
        case LD_NEOFORGE: return "neoforge";
        default:          return "";
    }
}

/* 分类 → 目标文件夹名 */
static const char *kind_dir_name(int kind) {
    switch (kind) {
        case 2: return "shaderpacks";
        case 3: return "resourcepacks";
        case 4: return "modpacks";
        default: return "mods";
    }
}

static const char *NAV_NAME[] = { "首页", "我的版本", "下载", "联机", "皮肤", "百宝箱", "设置" };
static const unsigned NAV_ICON[] = { 0xE80F, 0xE8F1, 0xE896, 0xE774, 0xE790, 0xE71D, 0xE713 };

/* ==========================================================================
 *  通用组件
 * ========================================================================== */
static void card_title(RcF r, const char *t, const char *sub) {
    ui_text(Rc(r.x, r.y, r.w, 22), t, 15.0f, 1, C_TEXT, 0);
    if (sub) ui_text(Rc(r.x, r.y + 22, r.w, 18), sub, 11.5f, 0, C_TEXT3, 0);
}

static void kv_text(RcF r, const char *k, const char *v, ARGB vc) {
    ui_text(Rc(r.x, r.y, r.w * 0.42f, r.h), k, 12.5f, 0, C_TEXT2, 0);
    ui_text(Rc(r.x + r.w * 0.42f, r.y, r.w * 0.58f, r.h), v, 12.5f, 0, vc, 0);
}

static void avatar(RcF r, const char *seed, const char *letter) {
    unsigned h = 0;
    for (const char *p = seed; p && *p; p++) h = h * 31u + (unsigned char)*p;
    ARGB c1 = LZ_ARGB(255, 90 + (h % 90), 140 + (h / 7 % 80), 235 - (h % 50));
    ARGB c2 = LZ_ARGB(255, 40 + (h % 60), 100 + (h / 11 % 70), 200 + (h % 40));
    ui_fill_round_grad(r, r.w * 0.32f, c1, c2);
    ui_stroke_round(r, r.w * 0.32f, 1.0f, C_HOVER);
    char buf[8] = { 0 };
    if (letter && *letter) {
        int i = 0;
        /* 取 UTF-8 首字符 */
        unsigned char c = (unsigned char)letter[0];
        int len = (c < 0x80) ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        for (int k = 0; k < len && k < 7; k++) buf[k] = letter[k];
        i = len;
        buf[i] = 0;
    }
    ui_text(r, buf[0] ? buf : "?", r.w * 0.42f, 1, C_WHITE, 1);
}

static void scroll_area(int page, RcF body, float content_end_y) {
    float content_h = content_end_y - (body.y - ui.scroll[page]);
    ui.scroll_max[page] = LZ_MAX(0.0f, content_h - body.h + 4);
    ui.scroll[page] = LZ_CLAMP(ui.scroll[page], 0.0f, ui.scroll_max[page]);
    ui_scrollbar(Rc(body.x + body.w - 3, body.y + 2, 3.5f, body.h - 4),
                 ui.scroll[page], ui.scroll_max[page], body.h);
}

/* ==========================================================================
 *  窗口外壳
 * ========================================================================== */
static void draw_sidebar(float w, float h) {
    LZ_UNUSED(w);
    RcF sb = Rc(14, 14, 212, h - 28);
    ui_glass(sb, 24);

    /* Logo */
    ui_cube(sb.x + 38, sb.y + 44, 15, 1.0f);
    ui_text(Rc(sb.x + 62, sb.y + 26, 130, 22), LZ_NAME, 15.0f, 1, C_TEXT, 0);
    ui_text(Rc(sb.x + 62, sb.y + 46, 130, 16), LZ_NAME_EN, 10.5f, 0, C_TEXT3, 0);

    /* 导航 */
    float ny = sb.y + 82;
    for (int i = 0; i < PG_COUNT; i++) {
        RcF r = Rc(sb.x + 10, ny, sb.w - 20, 42);
        int on = (ui.page == i);
        int hov = (ui.mx >= r.x && ui.mx <= r.x + r.w && ui.my >= r.y && ui.my <= r.y + r.h);
        if (on) {
            ui_fill_round(r, 13, C_SEL_HI);
            ui_stroke_round(r, 13, 1.0f, LZ_ARGB(70, 61, 220, 132));
            ui_fill_round(Rc(r.x + 1.5f, r.y + 13, 3.0f, 16), 1.5f, C_ACCENT);
        } else if (hov) {
            ui_fill_round(r, 13, C_HOVER);
        }
        ARGB fg = on ? C_ACCENT : C_TEXT2;
        ui_text_icon(Rc(r.x + 15, r.y, 20, r.h), NAV_ICON[i], 15.0f, fg, 0);
        ui_text(Rc(r.x + 44, r.y, 120, r.h), NAV_NAME[i], 13.5f, on ? 1 : 0, fg, 0);
        if (ui_hit(1000 + i, r)) { ui_set_page(i); ui.click = 0; }
        ny += 46;
    }

    /* 账号卡 */
    RcF ac = Rc(sb.x + 10, sb.y + sb.h - 92, sb.w - 20, 78);
    ui_fill_round(ac, 16, LZ_ARGB(16, 255, 255, 255));
    ui_stroke_round(ac, 16, 1.0f, LZ_ARGB(28, 255, 255, 255));
    int logged = (g.auth.state == AUTH_OK);
    avatar(Rc(ac.x + 12, ac.y + 15, 40, 40), logged ? g.auth.name : "guest",
           logged ? g.auth.name : "?");
    ui_text(Rc(ac.x + 60, ac.y + 15, ac.w - 70, 20),
            logged ? g.auth.name : "未登录", 13.0f, 1, C_TEXT, 0);
    char sub[64];
    if (logged) {
        int n = g.book.n;
        if (n > 1) snprintf(sub, sizeof(sub), "正版 · 共 %d 个账号", n);
        else snprintf(sub, sizeof(sub), "正版账号");
    } else {
        snprintf(sub, sizeof(sub), "点这里登录");
    }
    ui_text(Rc(ac.x + 60, ac.y + 35, ac.w - 70, 18), sub, 11.0f, 0,
            logged ? C_GREEN : C_TEXT3, 0);
    if (ui_hit(1100, ac)) { acct_modal = 1; ui.click = 0; }

    ui_text(Rc(sb.x + 14, sb.y + sb.h - 14, sb.w - 28, 14),
            "v" LZ_VERSION "  ·  C / GDI+", 9.5f, 0, C_TEXT3, 1);
}

static void draw_header(float w, int page) {
    static const char *TITLE[] = { "首页", "我的版本", "下载中心", "快捷联机", "皮肤衣柜", "百宝箱", "设置" };
    static const char *SUB[] = {
        "一切就绪，随时开一局",
        "已安装的版本，点一下就能启动",
        "原版游戏、加载器、模组、光影、整合包",
        "陶瓦联机、直连地址、局域网世界",
        "导入皮肤与披风，多账号随时切换",
        "多线程下载、清理缓存、抽签",
        "账号、Java 与游戏目录"
    };
    float x = 238;
    ui_text(Rc(x + 6, 26, 400, 30), TITLE[page], 20.0f, 1, C_TEXT, 0);
    ui_text(Rc(x + 6, 54, 520, 18), SUB[page], 11.5f, 0, C_TEXT3, 0);

    /* 窗口按钮 */
    float bx = w - 14 - 3 * 34 - 6;
    struct { unsigned cp; int id; } btn[3] = { { 0xE921, 1201 }, { 0xE922, 1202 }, { 0xE8BB, 1203 } };
    for (int i = 0; i < 3; i++) {
        RcF r = Rc(bx + i * 34, 22, 30, 30);
        int hov = (ui.mx >= r.x && ui.mx <= r.x + r.w && ui.my >= r.y && ui.my <= r.y + r.h);
        ARGB bg = LZ_ARGB(130, 255, 255, 255);
        ARGB fg = C_TEXT2;
        if (i == 2 && hov) { bg = LZ_ARGB(235, 240, 70, 80); fg = C_WHITE; }
        else if (hov) { bg = LZ_ARGB(48, 255, 255, 255); fg = C_TEXT; }
        ui_fill_round(r, 10, bg);
        ui_stroke_round(r, 10, 1.0f, C_HOVER);
        ui_text_icon(r, btn[i].cp, 10.5f, fg, 1);
        if (ui_hit(btn[i].id, r)) {
            ui.click = 0;
            if (i == 0) win_minimize();
            else if (i == 1) win_maximize();
            else win_close();
        }
    }
    /* 可拖动区域 */
    ui_hit(1200, Rc(x, 0, w - x - 120, 64));
}

/* ==========================================================================
 *  首页
 * ========================================================================== */
static void page_home(RcF body) {
    float x = body.x, w = body.w;
    float y = body.y - ui.scroll[PG_HOME];
    ui_clip_push(body);

    /* 选定版本 */
    if (!ps.sel_ver[0] && g.nversions > 0) {
        for (int i = 0; i < g.nversions; i++)
            if (!strcmp(g.versions[i].type, "release")) {
                snprintf(ps.sel_ver, sizeof(ps.sel_ver), "%s", g.versions[i].id);
                break;
            }
        if (!ps.sel_ver[0]) snprintf(ps.sel_ver, sizeof(ps.sel_ver), "%s", g.versions[0].id);
    }
    int installed = ps.sel_ver[0] ? mc_is_installed(ps.sel_ver) : 0;

    /* Hero */
    RcF hero = Rc(x, y, w, 224);
    ui_glass(hero, 26);
    /* 右侧光晕 */
    ui_glow(hero.x + hero.w - 120, hero.y + 100, 104, LZ_ARGB(64, 120, 185, 255));
    ui_glow(hero.x + hero.w - 120, hero.y + 100, 62, LZ_ARGB(52, 255, 255, 255));
    ui_cube(hero.x + hero.w - 120, hero.y + 96, 50, 1.0f);

    ui_text(Rc(hero.x + 34, hero.y + 34, 400, 18), "MINECRAFT · JAVA EDITION", 10.5f, 1,
            LZ_ARGB(255, 20, 120, 235), 0);
    ui_text(Rc(hero.x + 34, hero.y + 56, 480, 44), "开始你的方块世界", 28.0f, 1, C_TEXT, 0);

    char line[300];
    if (g.game.running) {
        snprintf(line, sizeof(line), "游戏运行中 · %s", g.game.version);
    } else if (installed) {
        snprintf(line, sizeof(line), "版本 %s · 已安装 · 随时可启动", ps.sel_ver);
    } else if (ps.sel_ver[0]) {
        snprintf(line, sizeof(line), "版本 %s · 尚未安装", ps.sel_ver);
    } else {
        snprintf(line, sizeof(line), "正在获取版本清单…");
    }
    ui_text(Rc(hero.x + 34, hero.y + 106, 520, 20), line, 12.5f, 0, C_TEXT2, 0);

    /* 按钮 */
    int logged_home = (g.auth.state == AUTH_OK);
    const char *main_label = g.game.running ? "运行中…"
                          : (!logged_home ? "登录后启动"
                                          : (installed ? "启动游戏" : "下载并启动"));
    if (ui_button(2001, Rc(hero.x + 34, hero.y + 148, 150, 46), main_label, BTN_PRIMARY)) {
        if (!logged_home) {
            auth_start();
        } else if (ps.sel_ver[0]) {
            if (installed) mc_launch_async(ps.sel_ver, NULL, 0);
            else mc_install_async(ps.sel_ver);
        }
    }
    if (ui_button(2002, Rc(hero.x + 196, hero.y + 148, 116, 46), "选择版本", BTN_GHOST))
        ui_set_page(PG_VER);

    /* 版本信息小标签 */
    char tag[80];
    snprintf(tag, sizeof(tag), "%s", ps.sel_ver[0] ? ps.sel_ver : "—");
    RcF pill = Rc(hero.x + hero.w - 190, hero.y + 168, 140, 28);
    ui_fill_round(pill, 14, LZ_ARGB(26, 255, 255, 255));
    ui_stroke_round(pill, 14, 1.0f, LZ_ARGB(40, 255, 255, 255));
    ui_text(pill, tag, 11.5f, 1, C_TEXT2, 1);

    y += 224 + 16;

    /* 三张卡片 */
    float cw = (w - 2 * 14) / 3.0f;
    RcF c1 = Rc(x, y, cw, 132), c2 = Rc(x + cw + 14, y, cw, 132), c3 = Rc(x + (cw + 14) * 2, y, cw, 132);

    /* 账号 */
    ui_glass(c1, 20);
    int logged = (g.auth.state == AUTH_OK);
    avatar(Rc(c1.x + 18, c1.y + 18, 44, 44), logged ? g.auth.name : "guest", logged ? g.auth.name : "?");
    ui_text(Rc(c1.x + 74, c1.y + 22, cw - 90, 20), logged ? g.auth.name : "未登录", 14.0f, 1, C_TEXT, 0);
    ui_text(Rc(c1.x + 74, c1.y + 44, cw - 90, 18),
            logged ? "微软正版账号" : (g.auth.state == AUTH_WAIT ? "等待浏览器确认" : "需要登录才能启动游戏"),
            11.0f, 0, logged ? C_GREEN : C_TEXT3, 0);
    if (logged) {
        if (ui_button(2101, Rc(c1.x + 18, c1.y + 82, 96, 32), "退出登录", BTN_GHOST))
            auth_logout();
    } else if (g.auth.state == AUTH_WAIT) {
        if (ui_button(2101, Rc(c1.x + 18, c1.y + 82, 96, 32), "取消", BTN_GHOST)) auth_cancel();
        if (ui_button(2102, Rc(c1.x + 122, c1.y + 82, 110, 32), "打开浏览器", BTN_TINT))
            lz_shell_open(g.auth.verify_uri);
    } else {
        if (ui_button(2101, Rc(c1.x + 18, c1.y + 82, 110, 32), "微软登录", BTN_PRIMARY))
            auth_start();
    }

    /* 联机 */
    ui_glass(c2, 20);
    ui_text_icon(Rc(c2.x + 18, c2.y + 18, 34, 34), 0xE774, 20.0f, C_ACCENT, 0);
    ui_text(Rc(c2.x + 60, c2.y + 22, cw - 76, 20), "快捷联机", 14.0f, 1, C_TEXT, 0);
    if (g_room.active) {
        char rb[64];
        snprintf(rb, sizeof(rb), "房间码 %s", g_room.code);
        ui_text(Rc(c2.x + 60, c2.y + 44, cw - 76, 18), rb, 11.5f, 0, C_ACCENT, 0);
    } else {
        char rb[64];
        snprintf(rb, sizeof(rb), "%d 个局域网世界", g_nlan);
        ui_text(Rc(c2.x + 60, c2.y + 44, cw - 76, 18), rb, 11.5f, 0, C_TEXT3, 0);
    }
    if (ui_button(2111, Rc(c2.x + 18, c2.y + 82, 110, 32), "进入联机", BTN_GHOST))
        ui_set_page(PG_MP);

    /* 下载 */
    ui_glass(c3, 20);
    ui_text_icon(Rc(c3.x + 18, c3.y + 18, 34, 34), 0xE896, 20.0f, C_PURPLE, 0);
    ui_text(Rc(c3.x + 60, c3.y + 22, cw - 76, 20), "模组与光影", 14.0f, 1, C_TEXT, 0);
    char dl[64];
    if (g_ntasks > 0) snprintf(dl, sizeof(dl), "%d 个下载任务", g_ntasks);
    else snprintf(dl, sizeof(dl), "Modrinth · CurseForge");
    ui_text(Rc(c3.x + 60, c3.y + 44, cw - 76, 18), dl, 11.5f, 0, C_TEXT3, 0);
    if (ui_button(2121, Rc(c3.x + 18, c3.y + 82, 110, 32), "去下载", BTN_GHOST))
        ui_set_page(PG_DL);

    y += 132 + 14;

    /* 版本速览 */
    {
        const char *rid = NULL, *rdt = NULL, *sid = NULL, *sdt = NULL;
        for (int i = 0; i < g.nversions; i++) {
            if (!strcmp(g.versions[i].type, "release") && !rid) { rid = g.versions[i].id; rdt = g.versions[i].rel; }
            if (!strcmp(g.versions[i].type, "snapshot") && !sid) { sid = g.versions[i].id; sdt = g.versions[i].rel; }
            if (rid && sid) break;
        }
        RcF ov = Rc(x, y, w, 52 + 3 * 50);
        ui_glass(ov, 20);
        ui_text(Rc(ov.x + 24, ov.y + 15, 300, 20), "版本速览", 13.0f, 1, C_TEXT, 0);
        ui_text(Rc(ov.x, ov.y + 15, ov.w - 24, 20), "来自 Mojang 官方清单", 10.5f, 0, C_TEXT3, 2);

        const char *labels[3] = { "最新正式版", "最新快照", "当前选择" };
        const char *ids[3] = { rid, sid, ps.sel_ver };
        const char *dts[3] = { rdt, sdt, "" };
        float oy = ov.y + 44;
        for (int i = 0; i < 3; i++) {
            RcF rr = Rc(ov.x + 10, oy, ov.w - 20, 44);
            int hov = (ui.mx >= rr.x && ui.mx <= rr.x + rr.w && ui.my >= rr.y && ui.my <= rr.y + rr.h);
            if (hov) ui_fill_round(rr, 12, LZ_ARGB(90, 255, 255, 255));
            ui_text(Rc(rr.x + 14, rr.y, 110, rr.h), labels[i], 11.5f, 0, C_TEXT2, 0);
            ui_text(Rc(rr.x + 128, rr.y, 300, rr.h), ids[i] ? ids[i] : "—", 12.5f, 1, C_TEXT, 0);
            if (dts[i] && dts[i][0])
                ui_text(Rc(rr.x + 380, rr.y, 140, rr.h), dts[i], 10.5f, 0, C_TEXT3, 0);
            if (ids[i] && ids[i][0]) {
                int inst = mc_is_installed(ids[i]);
                if (ui_button(2300 + i, Rc(rr.x + rr.w - 84, rr.y + 7, 74, 30),
                              inst ? "启动" : "安装", inst ? BTN_TINT : BTN_GHOST)) {
                    snprintf(ps.sel_ver, sizeof(ps.sel_ver), "%s", ids[i]);
                    if (inst) mc_launch_async(ids[i], NULL, 0);
                    else mc_install_async(ids[i]);
                }
            }
            oy += 50;
        }
        y += ov.h + 14;
    }

    /* 安装进度 */
    if (g.inst.active && g.inst.q) {
        RcF pr = Rc(x, y, w, 96);
        ui_glass(pr, 20);
        char st[200];
        int total = 0, done = 0, failed = 0;
        long long bd = 0, bt = 0;
        fq_progress(g.inst.q, &total, &done, &failed, &bd, &bt);
        double el = lz_now_sec() - g.inst.t0;
        char sz1[32], sz2[32];
        lz_fmt_size(sz1, sizeof(sz1), (double)bd);
        lz_fmt_size(sz2, sizeof(sz2), (double)bt);
        snprintf(st, sizeof(st), "%s  ·  %d/%d 文件  ·  %s / %s  ·  %.1f 文件/秒",
                 g.inst.stage, done, total, sz1, sz2, el > 0.5 ? done / el : 0.0);
        ui_text(Rc(pr.x + 22, pr.y + 18, w - 44, 20), st, 12.5f, 0, C_TEXT2, 0);
        ui_progress(Rc(pr.x + 22, pr.y + 50, w - 44, 8),
                    bt > 0 ? (float)((double)bd / (double)bt) : (total > 0 ? (float)done / total : 0),
                    C_ACCENT);
        ui_text(Rc(pr.x + 22, pr.y + 64, w - 44, 18), g.inst.version, 10.5f, 0, C_TEXT3, 0);
        y += 96 + 14;
    }

    /* 最近游玩 */
    if (g.game.log_path[0]) {
        RcF lg = Rc(x, y, w, 64);
        ui_glass(lg, 18);
        ui_text(Rc(lg.x + 22, lg.y, w - 200, lg.h), "最近一次运行日志", 12.5f, 1, C_TEXT, 0);
        if (ui_button(2201, Rc(lg.x + lg.w - 120, lg.y + 16, 100, 32), "打开日志", BTN_GHOST))
            lz_shell_open(g.game.log_path);
        y += 64 + 14;
    }

    ui_clip_pop();
    scroll_area(PG_HOME, body, y);
}

/* ==========================================================================
 *  版本管理
 * ========================================================================== */
/* ==========================================================================
 *  版本列表（原版下载 / 已安装 共用）
 * ========================================================================== */
static void scan_my_versions(void) {
    ps_myv_n = 0;
    char d[MAX_PATH * 2], pat[MAX_PATH * 3];
    lz_path_join(d, sizeof(d), g.set.game_dir, "versions");
    snprintf(pat, sizeof(pat), "%s\\*", d);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { ps.myv_dirty = 0; return; }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        if (ps_myv_n >= 160) break;
        if (!mc_is_installed(fd.cFileName)) continue;
        snprintf(ps_myv[ps_myv_n].id, sizeof(ps_myv[0].id), "%s", fd.cFileName);
        int ld = 0;
        if (strstr(fd.cFileName, "fabric-loader")) ld = LD_FABRIC;
        else if (strstr(fd.cFileName, "quilt-loader")) ld = LD_QUILT;
        else if (strstr(fd.cFileName, "neoforge")) ld = LD_NEOFORGE;
        else if (strstr(fd.cFileName, "-forge-")) ld = LD_FORGE;
        ps_myv[ps_myv_n].loader = ld;
        ps_myv_n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    ps.myv_dirty = 0;
}

static void ver_row(RcF rr, const char *id, const char *type, const char *date,
                    int installed, int loader, int id_btn, int id_row) {
    int hov = (ui.mx >= rr.x && ui.mx <= rr.x + rr.w && ui.my >= rr.y && ui.my <= rr.y + rr.h);
    if (hov) ui_fill_round(rr, 14, C_HOVER);

    ARGB bc, bf;
    const char *bt;
    if (loader == LD_FABRIC)       { bt = "Fab";  bc = LZ_ARGB(64, 255, 190, 60);  bf = LZ_ARGB(255, 170, 120, 0); }
    else if (loader == LD_QUILT)   { bt = "Quilt"; bc = LZ_ARGB(64, 190, 120, 255); bf = LZ_ARGB(255, 120, 70, 210); }
    else if (loader == LD_FORGE)   { bt = "Forge"; bc = LZ_ARGB(64, 120, 150, 200); bf = LZ_ARGB(255, 70, 100, 150); }
    else if (loader == LD_NEOFORGE){ bt = "Neo";  bc = LZ_ARGB(64, 255, 140, 90);  bf = LZ_ARGB(255, 200, 90, 40); }
    else if (type && !strcmp(type, "release")) { bt = "正式"; bc = LZ_ARGB(64, 40, 190, 120); bf = LZ_ARGB(255, 18, 138, 88); }
    else if (type && !strcmp(type, "snapshot")) { bt = "快照"; bc = LZ_ARGB(64, 255, 150, 40); bf = LZ_ARGB(255, 196, 104, 16); }
    else { bt = "远古"; bc = LZ_ARGB(64, 140, 140, 160); bf = C_TEXT3; }
    ui_badge(Rc(rr.x + 16, rr.y + 17, 46, 22), bt, bc, bf);

    ui_text(Rc(rr.x + 74, rr.y, 280, rr.h), id, 13.5f, 1, C_TEXT, 0);
    if (date && date[0]) ui_text(Rc(rr.x + 360, rr.y, 110, rr.h), date, 11.0f, 0, C_TEXT3, 0);
    if (installed)
        ui_text(Rc(rr.x + rr.w - 340, rr.y, 120, rr.h), "已安装", 11.0f, 0, C_GREEN, 2);

    if (installed) {
        int pending = !strcmp(ps.del_pending, id);
        RcF dr = Rc(rr.x + rr.w - 190, rr.y + 11, 84, 30);
        if (ui_button(id_btn + 500, dr, pending ? "确认删除" : "删除",
                      pending ? BTN_PRIMARY : BTN_GHOST)) {
            if (pending) {
                char vp[MAX_PATH * 3], vr[MAX_PATH * 3];
                lz_path_join(vr, sizeof(vr), g.set.game_dir, "versions");
                lz_path_join(vp, sizeof(vp), vr, id);
                if (g.game.running && !strcmp(g.game.version, id)) {
                    app_toast("这个版本正在运行，先关掉游戏");
                } else if (lz_dir_remove(vp)) {
                    app_toast("已删除版本 %s", id);
                    ps.del_pending[0] = 0;
                    ps.myv_dirty = 1;
                    scan_my_versions();
                } else {
                    app_toast("删除失败，可能文件被占用");
                }
            } else {
                snprintf(ps.del_pending, sizeof(ps.del_pending), "%s", id);
            }
        }
    }

    RcF br = Rc(rr.x + rr.w - 96, rr.y + 11, 84, 30);
    if (installed) {
        if (ui_button(id_btn, br, g.game.running ? "运行中" : "启动", BTN_TINT)) {
            snprintf(ps.sel_ver, sizeof(ps.sel_ver), "%s", id);
            mc_launch_async(id, NULL, 0);
        }
    } else {
        int busy = g.inst.active && !strcmp(g.inst.version, id);
        if (ui_button(id_btn, br, busy ? "安装中" : "安装", busy ? BTN_TINT : BTN_GHOST)) {
            if (!g.inst.active) {
                snprintf(ps.sel_ver, sizeof(ps.sel_ver), "%s", id);
                mc_install_async(id);
            }
        }
    }
    if (ui_hit(id_row, rr)) {
        snprintf(ps.sel_ver, sizeof(ps.sel_ver), "%s", id);
        ui.click = 0;
    }
    ui_line(rr.x + 16, rr.y + rr.h, rr.x + rr.w - 16, rr.y + rr.h, 1.0f, LZ_ARGB(26, 120, 140, 170));
}

static void page_ver(RcF body) {
    float x = body.x, w = body.w;
    float y = body.y;

    if (ps.myv_dirty || ps.last_page != PG_VER) scan_my_versions();

    RcF bar = Rc(x, y, w, 60);
    ui_glass(bar, 18);
    ui_input(3001, Rc(bar.x + 14, bar.y + 12, 260, 36), ps.ver_q, sizeof(ps.ver_q), "搜索已安装的版本…");
    char cnt[80];
    snprintf(cnt, sizeof(cnt), "%d 个已安装", ps_myv_n);
    ui_text(Rc(bar.x, bar.y, bar.w - 20, bar.h), cnt, 11.5f, 0, C_TEXT3, 2);
    if (ui_button(3003, Rc(bar.x + 290, bar.y + 14, 120, 32), "去下载更多", BTN_TINT)) {
        g.content_kind = 0;
        ui_set_page(PG_DL);
    }
    y += 60 + 14;

    RcF list = Rc(x, y, w, LZ_MAX(160.0f, body.y + body.h - y));
    ui_glass(list, 20);

    int idx[160], n = 0;
    for (int i = 0; i < ps_myv_n; i++) {
        if (ps.ver_q[0] && !lz_icontains(ps_myv[i].id, ps.ver_q)) continue;
        idx[n++] = i;
    }
    if (ps.myv_dirty || ps.last_page != PG_VER) ps.last_page = PG_VER;

    const float ROW = 56.0f;
    float view_h = list.h - 14;
    ui.scroll_max[PG_VER] = LZ_MAX(0.0f, n * ROW - view_h);
    ui.scroll[PG_VER] = LZ_CLAMP(ui.scroll[PG_VER], 0.0f, ui.scroll_max[PG_VER]);

    if (n == 0) {
        ui_text(Rc(list.x, list.y + list.h / 2 - 30, list.w, 30),
                "还没有安装任何版本", 14.0f, 1, C_TEXT2, 1);
        ui_text(Rc(list.x, list.y + list.h / 2 + 2, list.w, 26),
                "去「下载」页选一个版本和加载器，一键装好", 11.5f, 0, C_TEXT3, 1);
        return;
    }

    ui_clip_push(Rc(list.x + 2, list.y + 4, list.w - 4, list.h - 8));
    float ry = list.y + 7 - ui.scroll[PG_VER];
    for (int k = 0; k < n; k++) {
        if (ry + ROW > list.y - 8 && ry < list.y + list.h + 8) {
            const char *id = ps_myv[idx[k]].id;
            ver_row(Rc(x + 8, ry, w - 16, ROW - 4), id, NULL, "", 1,
                    ps_myv[idx[k]].loader, 3100 + k, 3200 + k);
        }
        ry += ROW;
    }
    ui_clip_pop();
    ui_scrollbar(Rc(list.x + list.w - 9, list.y + 8, 4, list.h - 16),
                 ui.scroll[PG_VER], ui.scroll_max[PG_VER], view_h);
}

/* ==========================================================================
 *  下载中心（照 PCL 的布局：左边分类，右边内容，点了就地展开）
 * ========================================================================== */
static const char *KIND_NAMES[] = { "原版游戏", "模组", "光影", "资源包", "整合包" };
static const char *KIND_ICONS[] = { "原版", "Mod", "光影", "材质", "整合" };

/* 加载器行（OptiFine 暂不支持，直接标出来，不骗人） */
static const struct { int kind; const char *name; int supported; } LD_ROWS[] = {
    { LD_FORGE,    "Forge",    1 },
    { LD_NEOFORGE, "NeoForge", 1 },
    { LD_FABRIC,   "Fabric",   1 },
    { LD_QUILT,    "Quilt",    1 },
    { -1,          "OptiFine", 0 },
};

static const char *ps_mc_items[600];
static int ps_mc_n = 0;

static void build_mc_items(void) {
    ps_mc_n = 0;
    for (int i = 0; i < g.nversions && ps_mc_n < 590; i++) {
        if (strcmp(g.versions[i].type, "release")) continue;
        ps_mc_items[ps_mc_n++] = g.versions[i].id;
    }
    if (ps.mc_sel >= ps_mc_n) ps.mc_sel = 0;
}

/* 版本选择浮层：mode 0 = Minecraft 版本，mode 1 = 加载器版本 */
static int vp_active = 0;
static int vp_mode = 0;
static char vp_q[64];
static int vp_filter = 1;

static void vp_overlay(void) {
    RcF scr = Rc(0, 0, ui_w, ui_h);
    ui_fill_round(scr, 0, LZ_ARGB(90, 20, 26, 40));
    ui_hit(997000, scr);

    float cw = LZ_MIN(680.0f, ui_w - 80);
    float ch = LZ_MIN(600.0f, ui_h - 60);
    RcF c = Rc((ui_w - cw) / 2, (ui_h - ch) / 2, cw, ch);
    ui_shadow(c, 22, 40, 40);
    ui_fill_round_grad(c, 22, C_SURF, C_SURF2);
    ui_stroke_round(c, 22, 1.0f, C_SEL);

    char title[80];
    if (vp_mode == 0) snprintf(title, sizeof(title), "选择 Minecraft 版本");
    else snprintf(title, sizeof(title), "选择 %s 版本", loader_name(ps.ld_sel));
    ui_text(Rc(c.x + 26, c.y + 22, 420, 28), title, 17.0f, 1, C_TEXT, 0);
    if (ui_button(9101, Rc(c.x + c.w - 50, c.y + 20, 30, 30), "✕", BTN_GHOST)) { vp_active = 0; return; }

    ui_input(9201, Rc(c.x + 26, c.y + 60, c.w - 52, 38), vp_q, sizeof(vp_q), "搜索版本号…");

    float listTop = c.y + 110;
    if (vp_mode == 0) {
        const char *fn[] = { "全部", "正式版", "快照", "远古" };
        float fx = c.x + 26;
        for (int i = 0; i < 4; i++) {
            if (ui_chip(9210 + i, Rc(fx, c.y + 108, 62, 30), fn[i], vp_filter == i)) vp_filter = i;
            fx += 68;
        }
        listTop = c.y + 148;
    }

    RcF list = Rc(c.x + 18, listTop, c.w - 36, (c.y + ch - 18) - listTop);
    ui_fill_round(list, 14, LZ_ARGB(14, 255, 255, 255));
    ui_clip_push(list);
    float ry = list.y + 4 - ui.scroll[PG_DL];
    int shown = 0;

    if (vp_mode == 0) {
        for (int i = 0; i < g.nversions; i++) {
            McVer *v = &g.versions[i];
            if (vp_filter == 1 && strcmp(v->type, "release")) continue;
            if (vp_filter == 2 && strcmp(v->type, "snapshot")) continue;
            if (vp_filter == 3 && strcmp(v->type, "old_beta") && strcmp(v->type, "old_alpha")) continue;
            if (vp_q[0] && !lz_icontains(v->id, vp_q)) continue;
            shown++;
            if (ry + 46 > list.y && ry < list.y + list.h) {
                RcF rr = Rc(list.x + 6, ry, list.w - 12, 42);
                int hov = (ui.mx >= rr.x && ui.mx <= rr.x + rr.w && ui.my >= rr.y && ui.my <= rr.y + rr.h);
                if (hov) ui_fill_round(rr, 10, C_SEL);
                int cur = (ps_mc_n > 0 && !strcmp(ps_mc_items[ps.mc_sel], v->id));
                ui_text(Rc(rr.x + 14, rr.y, 300, rr.h), v->id, 13.0f, cur ? 1 : 0,
                        cur ? C_ACCENT : C_TEXT, 0);
                ui_text(Rc(rr.x + 300, rr.y, 110, rr.h), v->rel, 10.5f, 0, C_TEXT3, 0);
                if (cur) ui_text(Rc(rr.x + rr.w - 60, rr.y, 50, rr.h), "当前", 10.0f, 0, C_ACCENT, 2);
                if (ui_hit(9300 + i, rr)) {
                    ui.click = 0;
                    for (int k = 0; k < ps_mc_n; k++)
                        if (!strcmp(ps_mc_items[k], v->id)) { ps.mc_sel = k; break; }
                    vp_active = 0;
                }
            }
            ry += 46;
        }
    } else {
        for (int k = 0; k < g_nlvers; k++) {
            if (vp_q[0] && !lz_icontains(g_lvers[k].ver, vp_q)) continue;
            shown++;
            if (ry + 46 > list.y && ry < list.y + list.h) {
                RcF rr = Rc(list.x + 6, ry, list.w - 12, 42);
                int hov = (ui.mx >= rr.x && ui.mx <= rr.x + rr.w && ui.my >= rr.y && ui.my <= rr.y + rr.h);
                int cur = (ps.ld_done && ps.lv_sel == k);
                if (hov && !cur) ui_fill_round(rr, 10, C_SEL);
                if (cur) ui_fill_round(rr, 10, C_SEL_HI);
                ui_text(Rc(rr.x + 14, rr.y, 400, rr.h), g_lvers[k].label, 13.0f, cur ? 1 : 0,
                        cur ? C_ACCENT : C_TEXT, 0);
                if (cur) ui_text(Rc(rr.x + rr.w - 60, rr.y, 50, rr.h), "当前", 10.0f, 0, C_ACCENT, 2);
                if (ui_hit(9400 + k, rr)) {
                    ui.click = 0;
                    ps.lv_sel = k;
                    ps.ld_done = 1;
                    vp_active = 0;
                }
            }
            ry += 46;
        }
    }

    if (shown == 0)
        ui_text(Rc(list.x, list.y + 20, list.w, 30), "没有匹配的版本", 12.0f, 0, C_TEXT3, 1);
    ui_clip_pop();
    ui.scroll_max[PG_DL] = LZ_MAX(0.0f, (float)shown * 46.0f - list.h + 8);
    ui.scroll[PG_DL] = LZ_CLAMP(ui.scroll[PG_DL], 0.0f, ui.scroll_max[PG_DL]);
}

static char last_sel_mc[40];

static void page_dl(RcF body) {
    float x = body.x, w = body.w;
    float y = body.y;
    build_mc_items();

    /* 顶部选的版本要同步给搜索筛选，变了就重搜 */
    if (ps_mc_n > 0) {
        const char *m = ps_mc_items[ps.mc_sel];
        if (strcmp(m, last_sel_mc)) {
            snprintf(last_sel_mc, sizeof(last_sel_mc), "%s", m);
            EnterCriticalSection(&g_lock);
            snprintf(g.sel_mc, sizeof(g.sel_mc), "%s", m);
            LeaveCriticalSection(&g_lock);
            g.content_state = 0;
            ps.q_pending = 0;
            snprintf(ps.last_q, sizeof(ps.last_q), "%s", ps.dl_q);
        }
    }

    /* ---------- 左侧分类 ---------- */
    float railW = 132.0f;
    RcF rail = Rc(x, y, railW, body.h);
    ui_glass(rail, 20);
    float ry = rail.y + 12;
    for (int i = 0; i < 5; i++) {
        RcF r = Rc(rail.x + 8, ry, rail.w - 16, 44);
        int on = (g.content_kind == i);
        int hov = (ui.mx >= r.x && ui.mx <= r.x + r.w && ui.my >= r.y && ui.my <= r.y + r.h);
        if (on) {
            ui_fill_round(r, 12, C_SEL_HI);
            ui_stroke_round(r, 12, 1.0f, LZ_ARGB(34, 255, 255, 255));
            ui_fill_round(Rc(r.x + 1.5f, r.y + 14, 3.0f, 16), 1.5f, C_ACCENT);
        } else if (hov) {
            ui_fill_round(r, 12, C_HOVER);
        }
        ui_text(Rc(r.x + 16, r.y + 8, r.w - 24, 18), KIND_NAMES[i], 12.5f, on ? 1 : 0,
                on ? C_ACCENT : C_TEXT, 0);
        ui_text(Rc(r.x + 16, r.y + 25, r.w - 24, 14), KIND_ICONS[i], 9.0f, 0, C_TEXT3, 0);
        if (ui_hit(4000 + i, r)) {
            ui.click = 0;
            if (g.content_kind != i) {
                g.content_kind = i;
                g.content_state = 0;
                ui_scroll_reset(PG_DL);
            }
        }
        ry += 48;
    }

    RcF main = Rc(x + railW + 12, y, w - railW - 12, body.h);
    ui_clip_push(main);

    /* ================= 原版游戏 ================= */
    if (g.content_kind == 0) {
        /* 版本行 */
        RcF vr = Rc(main.x, main.y, main.w, 66);
        ui_glass(vr, 18);
        ui_text(Rc(vr.x + 20, vr.y + 12, 200, 16), "Minecraft 版本", 10.5f, 1, C_TEXT3, 0);
        char cur_mc[40] = {0};
        if (ps_mc_n > 0) snprintf(cur_mc, sizeof(cur_mc), "%s", ps_mc_items[ps.mc_sel]);
        ui_text(Rc(vr.x + 20, vr.y + 30, 300, 26), cur_mc[0] ? cur_mc : "点击选择",
                18.0f, 1, C_TEXT, 0);
        ui_text(Rc(vr.x + 132, vr.y + 12, vr.w - 320, 16),
                g.manifest_state == 2 ? "· 点右边按钮可以换任意版本" : "· 加载中…",
                10.5f, 0, C_TEXT3, 0);
        if (ui_button(4011, Rc(vr.x + vr.w - 150, vr.y + 15, 118, 36), "切换版本", BTN_GHOST)) {
            vp_mode = 0;
            vp_active = 1;
            vp_q[0] = 0;
            vp_filter = 0;
            ui.focus_id = 0;
            ui_scroll_reset(PG_DL);
        }
        ui_text_icon(Rc(vr.x + vr.w - 30, vr.y, 24, vr.h), 0xE76C, 13.0f, C_TEXT3, 1);
        if (ui_hit(4010, vr)) {
            ui.click = 0;
            vp_mode = 0;
            vp_active = 1;
            vp_q[0] = 0;
            ui.focus_id = 0;
            ui_scroll_reset(PG_DL);
        }
        float yy = vr.y + vr.h + 12;
        float bottomH = 84.0f;
        float avail = (body.y + body.h) - yy - bottomH - 12;

        /* 加载器列表：撑满中间区域 */
        RcF lc = Rc(main.x, yy, main.w, 0);
        float lh = 12;
        for (int i = 0; i < LZ_COUNT(LD_ROWS); i++) {
            lh += 50;
            if (ps.exp_loader == i) {
                int cnt = (g_lvers_state == 2 && g_lvers_kind == LD_ROWS[i].kind) ? g_nlvers : 0;
                int rows = LZ_MAX(1, LZ_MIN(cnt, 5) + (cnt > 5 ? 1 : 0));
                lh += 24 + rows * 34 + 8;
            }
        }
        lc.h = LZ_MAX(lh, avail);
        ui_glass(lc, 18);
        float ly = lc.y + 6;
        for (int i = 0; i < LZ_COUNT(LD_ROWS); i++) {
            RcF r = Rc(lc.x + 8, ly, lc.w - 16, 46);
            int hov = (ui.mx >= r.x && ui.mx <= r.x + r.w && ui.my >= r.y && ui.my <= r.y + r.h);
            int exp = (ps.exp_loader == i);
            if (exp) ui_fill_round(r, 12, C_SEL);
            else if (hov) ui_fill_round(r, 12, C_HOVER);
            ui_text(Rc(r.x + 16, r.y, 200, r.h), LD_ROWS[i].name, 13.5f, exp ? 1 : 0, C_TEXT, 0);

            char st[64];
            if (!LD_ROWS[i].supported) snprintf(st, sizeof(st), "暂不支持");
            else if (ps.ld_sel == LD_ROWS[i].kind && ps.ld_done) snprintf(st, sizeof(st), "已选择 %s", g_lvers[ps.lv_sel].ver);
            else snprintf(st, sizeof(st), "可以添加");
            ui_text(Rc(r.x + 140, r.y, 220, r.h), st, 11.0f, 0,
                    LD_ROWS[i].supported ? C_TEXT3 : C_ORANGE, 0);
            ui_text_icon(Rc(r.x + r.w - 34, r.y, 24, r.h), exp ? 0xE70D : 0xE76C, 11.0f, C_TEXT3, 1);

            if (ui_hit(4020 + i, r)) {
                ui.click = 0;
                if (LD_ROWS[i].supported) {
                    if (ps.exp_loader == i) {
                        ps.exp_loader = -1;
                    } else {
                        ps.exp_loader = i;
                        ps.ld_sel = LD_ROWS[i].kind;
                        ps.lv_sel = 0;
                        ps.ld_done = 0;
                        loader_list_async(ps.ld_sel, cur_mc);
                    }
                } else {
                    app_toast("OptiFine 需要改写游戏本体，暂未支持");
                }
            }
            ly += 50;

            /* 列表到货后自动选中最新版（用户随时可以改） */
            if (exp && g_lvers_state == 2 && g_lvers_kind == LD_ROWS[i].kind
                && !ps.ld_done && g_nlvers > 0) {
                ps.lv_sel = 0;
                ps.ld_done = 1;
            }

            /* 展开的版本列表 */
            if (exp) {
                if (g_lvers_state == 1) {
                    ui_text(Rc(lc.x + 30, ly, 300, 20), "正在获取版本列表…", 11.0f, 0, C_TEXT3, 0);
                    ly += 20;
                } else if (g_nlvers == 0) {
                    ui_text(Rc(lc.x + 30, ly, 400, 20),
                            g_lvers_err[0] ? g_lvers_err : "没有可用版本", 11.0f, 0, C_ORANGE, 0);
                    ly += 20;
                } else {
                    ly += 6;
                    int show = LZ_MIN(g_nlvers, 5);
                    for (int k = 0; k < show; k++) {
                        RcF ir = Rc(lc.x + 30, ly, lc.w - 60, 32);
                        int ih = (ui.mx >= ir.x && ui.mx <= ir.x + ir.w && ui.my >= ir.y && ui.my <= ir.y + ir.h);
                        int on = (ps.ld_done && ps.lv_sel == k);
                        if (on) ui_fill_round(ir, 9, C_SEL_HI);
                        else if (ih) ui_fill_round(ir, 9, C_HOVER);
                        ui_text(Rc(ir.x + 12, ir.y, ir.w - 24, ir.h), g_lvers[k].label, 11.5f, on ? 1 : 0,
                                on ? C_ACCENT : C_TEXT, 0);
                        if (ui_hit(4100 + k, ir)) {
                            ui.click = 0;
                            ps.lv_sel = k;
                            ps.ld_done = 1;
                        }
                        ly += 34;
                    }
                    if (g_nlvers > show) {
                        RcF mr = Rc(lc.x + 30, ly, lc.w - 60, 30);
                        int mh = (ui.mx >= mr.x && ui.mx <= mr.x + mr.w && ui.my >= mr.y && ui.my <= mr.y + mr.h);
                        if (mh) ui_fill_round(mr, 9, C_HOVER);
                        char mb[96];
                        snprintf(mb, sizeof(mb), "查看全部 %d 个版本，可以自己挑", g_nlvers);
                        ui_text(Rc(mr.x + 12, mr.y, mr.w - 24, mr.h), mb, 11.0f, 0, C_ACCENT, 0);
                        if (ui_hit(4150, mr)) {
                            ui.click = 0;
                            vp_mode = 1;
                            vp_active = 1;
                            vp_q[0] = 0;
                            ui_scroll_reset(PG_DL);
                        }
                        ly += 32;
                    }
                    ly += 8;
                }
            }
        }

        /* 空出来的地方放使用说明 */
        float gy = ly + 10;
        if (gy + 120 < lc.y + lc.h) {
            ui_text(Rc(lc.x + 20, gy, 300, 18), "怎么选", 10.5f, 1, C_TEXT3, 0);
            static const char *tips[] = {
                "只想玩原版：上面选好版本，直接点「开始下载」",
                "要装模组：选 Fabric 或 Forge，加载器版本用最新的那个",
                "光影 / 资源包不需要加载器，左边切到对应分类直接下",
            };
            float ty = gy + 26;
            for (int t = 0; t < 3; t++) {
                ui_circle(lc.x + 26, ty + 9, 3.0f, LZ_ARGB(160, 20, 120, 235));
                ui_text(Rc(lc.x + 38, ty, lc.w - 60, 20), tips[t], 11.0f, 0, C_TEXT2, 0);
                ty += 26;
            }
        }

        /* 目标 + 开始下载 */
        char cur_lver[64] = {0};
        if (ps.ld_sel >= 0 && ps.ld_done && ps.lv_sel < g_nlvers)
            snprintf(cur_lver, sizeof(cur_lver), "%s", g_lvers[ps.lv_sel].ver);
        char vid[96] = {0};
        if (cur_mc[0]) {
            if (ps.exp_loader < 0 || !ps.ld_done) snprintf(vid, sizeof(vid), "%s", cur_mc);
            else if (cur_lver[0]) loader_version_id(ps.ld_sel, cur_mc, cur_lver, vid, sizeof(vid));
        }
        int inst = vid[0] ? mc_is_installed(vid) : 0;

        RcF bb = Rc(main.x, body.y + body.h - bottomH, main.w, bottomH - 6);
        ui_glass(bb, 18);
        char stat[300];
        if (!vid[0]) snprintf(stat, sizeof(stat), "先选一个 Minecraft 版本");
        else if (inst) snprintf(stat, sizeof(stat), "%s 已安装", vid);
        else if (ps.exp_loader < 0 || !ps.ld_done) snprintf(stat, sizeof(stat), "将安装原版 %s", cur_mc);
        else snprintf(stat, sizeof(stat), "将安装 %s + %s %s", cur_mc, LD_ROWS[ps.exp_loader].name, cur_lver);
        ui_text(Rc(bb.x + 20, bb.y + 12, bb.w - 200, 18), stat, 11.0f, 0,
                inst ? C_GREEN : C_TEXT2, 0);
        ui_text(Rc(bb.x + 20, bb.y + 34, bb.w - 200, 30),
                inst ? "已经可以启动，不需要重复下载" : "自动下载游戏文件、依赖库、资源与 Java 运行时",
                10.5f, 0, C_TEXT3, 0);

        RcF btn = Rc(bb.x + bb.w - 170, bb.y + 16, 150, 46);
        if (inst) {
            if (ui_button(4030, btn, g.game.running ? "运行中…" : "启动游戏", BTN_PRIMARY))
                mc_launch_async(vid, NULL, 0);
        } else if (ui_button(4030, btn, g.inst.active ? "安装中…" : "开始下载", BTN_PRIMARY)) {
            if (!g.inst.active && vid[0]) {
                snprintf(ps.sel_ver, sizeof(ps.sel_ver), "%s", vid);
                if (ps.exp_loader < 0 || !ps.ld_done) mc_install_async(cur_mc);
                else loader_install_async(cur_mc, ps.ld_sel, cur_lver);
            }
        }

        ui_clip_pop();
        return;
    }

    /* ================= 模组 / 光影 / 资源包 / 整合包 ================= */
    RcF sbar = Rc(main.x, main.y, main.w, 58);
    ui_glass(sbar, 18);
    ui_input(4020, Rc(sbar.x + 14, sbar.y + 11, main.w - 396, 36), ps.dl_q, sizeof(ps.dl_q),
             "输入关键词搜索，例如 sodium / iris / create…");
    if (ui_button(4021, Rc(sbar.x + main.w - 372, sbar.y + 11, 86, 36), "搜索", BTN_PRIMARY)) {
        ps.q_pending = 0;
        content_search_async(ps.dl_q, g.content_source, g.content_kind);
    }
    const char *srcs[] = { "Modrinth", "CurseForge", "MC百科" };
    float sx = sbar.x + main.w - 278;
    for (int i = 0; i < 3; i++) {
        float bw = i == 2 ? 74 : (i == 1 ? 96 : 88);
        RcF r = Rc(sx, sbar.y + 12, bw, 34);
        if (ui_chip(4030 + i, r, srcs[i], g.content_source == i)) {
            g.content_source = i;
            g.content_state = 0;
            ps.q_pending = 0;
        }
        sx += bw + 6;
    }

    /* 输入即搜（停止输入 0.7 秒后触发） */
    if (strcmp(ps.dl_q, ps.last_q)) {
        snprintf(ps.last_q, sizeof(ps.last_q), "%s", ps.dl_q);
        ps.q_t = lz_now_sec();
        ps.q_pending = 1;
    }
    if (g.content_state == 0) {
        ps.q_pending = 0;
        content_search_async(ps.dl_q, g.content_source, g.content_kind);
    } else if (ps.q_pending && g.content_state != 1 && lz_now_sec() - ps.q_t >= 0.7) {
        ps.q_pending = 0;
        content_search_async(ps.dl_q, g.content_source, g.content_kind);
    }
    float yy = sbar.y + 58 + 12;

    if (g.content_note[0] && g.content_state == 2) {
        ui_text(Rc(main.x, yy, main.w, 22), g.content_note, 10.5f, 0, C_ORANGE, 0);
        yy += 24;
    }
    if (g.content_state == 1) {
        ui_text(Rc(main.x, yy + 20, main.w, 30), "正在搜索…", 13.0f, 0, C_TEXT2, 1);
        yy += 60;
    } else if (g.content_state == 3) {
        ui_text(Rc(main.x, yy + 20, main.w, 30), g.content_err, 12.0f, 0, C_RED, 1);
        yy += 60;
    }

    ui_clip_push(Rc(main.x, yy, main.w, main.y + main.h - yy));
    float ry2 = yy - ui.scroll[PG_DL];
    for (int i = 0; i < g.ncontent; i++) {
        ContentItem *ci = &g.content[i];
        RcF rr = Rc(main.x, ry2, main.w, 68);
        if (ry2 + 68 > yy - 20 && ry2 < main.y + main.h + 20) {
            ui_panel(rr, 16);
            avatar(Rc(rr.x + 14, rr.y + 12, 44, 44), ci->id, ci->title);
            ui_text(Rc(rr.x + 70, rr.y + 10, main.w - 300, 22), ci->title, 13.5f, 1, C_TEXT, 0);
            ui_text(Rc(rr.x + 70, rr.y + 32, main.w - 300, 20), ci->desc, 10.5f, 0, C_TEXT2, 0);
            char dn[64], dl[80];
            lz_fmt_count(dn, sizeof(dn), ci->downloads);
            snprintf(dl, sizeof(dl), "%s 次下载", dn);
            ui_text(Rc(rr.x + main.w - 260, rr.y + 10, 140, 18), dl, 10.0f, 0, C_TEXT3, 2);
            if (ui_button(4200 + i, Rc(rr.x + main.w - 100, rr.y + 18, 84, 32), "详情", BTN_TINT))
                md_open(i);
            if (ui_hit(4300 + i, rr)) { ui.click = 0; md_open(i); }
        }
        ry2 += 76;
    }
    if (g.content_state == 2 && g.ncontent == 0) {
        RcF es = Rc(main.x, yy + 16, main.w, 168);
        ui_glass(es, 18);
        char e1[256];
        if (ps.dl_q[0])
            snprintf(e1, sizeof(e1), "没有找到「%s」", ps.dl_q);
        else
            snprintf(e1, sizeof(e1), "没有找到结果");
        ui_text(Rc(es.x + 22, es.y + 18, es.w - 44, 24), e1, 13.5f, 1, C_TEXT, 0);
        ui_text(Rc(es.x + 22, es.y + 46, es.w - 44, 20),
                g.content_source == 0
                    ? "Modrinth 是英文站，中文名搜不到，换英文关键词试试（例如 sodium / create / jei）"
                    : "换个关键词，或者切到 Modrinth 源再试",
                11.0f, 0, C_TEXT3, 0);
        static const char *sug[] = { "Sodium", "Iris Shaders", "Create", "JEI", "Fabric API" };
        float sx2 = es.x + 22;
        for (int i = 0; i < 5; i++) {
            float bw = 74.0f + (float)strlen(sug[i]) * 3.5f;
            if (sx2 + bw > es.x + es.w - 22) break;
            if (ui_chip(4400 + i, Rc(sx2, es.y + 78, bw, 32), sug[i], 0)) {
                snprintf(ps.dl_q, sizeof(ps.dl_q), "%s", sug[i]);
                snprintf(ps.last_q, sizeof(ps.last_q), "%s", sug[i]);
                ps.q_pending = 0;
                content_search_async(ps.dl_q, g.content_source, g.content_kind);
            }
            sx2 += bw + 8;
        }
        ui_text(Rc(es.x + 22, es.y + 122, es.w - 44, 20),
                "要搜中文模组，切到「MC百科」源，会打开网页搜索", 10.5f, 0, C_TEXT3, 0);
        yy += 168;
    }
    if (g_mdtest && g.content_state == 2 && g.ncontent > 0 && !g_md.active) {
        g_mdtest = 0;
        md_open(0);
    }
    ui.scroll_max[PG_DL] = LZ_MAX(0.0f, (float)g.ncontent * 76.0f - (main.y + main.h - yy));
    ui.scroll[PG_DL] = LZ_CLAMP(ui.scroll[PG_DL], 0.0f, ui.scroll_max[PG_DL]);
    ui_clip_pop();
}

/* ==========================================================================
 *  微软登录浮层（验证码必须看得见）
 * ========================================================================== */
static void auth_overlay(void) {
    if (g.auth.state != AUTH_WAIT) return;

    RcF scr = Rc(0, 0, ui_w, ui_h);
    ui_fill_round(scr, 0, LZ_ARGB(96, 20, 26, 40));
    ui_hit(996000, scr);

    float cw = LZ_MIN(560.0f, ui_w - 80);
    float ch = 348.0f;
    RcF c = Rc((ui_w - cw) / 2, (ui_h - ch) / 2, cw, ch);
    ui_shadow(c, 22, 40, 40);
    ui_fill_round_grad(c, 22, C_SURF, C_SURF2);
    ui_stroke_round(c, 22, 1.0f, C_SEL);

    ui_text(Rc(c.x + 28, c.y + 24, cw - 56, 26), "用微软账号登录", 18.0f, 1, C_TEXT, 0);
    ui_text(Rc(c.x + 28, c.y + 56, cw - 56, 20),
            "浏览器已经打开，把下面的代码填进去：", 11.5f, 0, C_TEXT2, 0);

    RcF code = Rc(c.x + 28, c.y + 88, cw - 56, 74);
    ui_fill_round(code, 16, LZ_ARGB(56, 61, 220, 132));
    ui_stroke_round(code, 16, 1.0f, C_SEL);
    ui_text(code, g.auth.user_code, 34.0f, 1, C_ACCENT, 1);

    ui_text(Rc(c.x + 28, c.y + 172, cw - 56, 20), g.auth.verify_uri, 10.5f, 0, C_TEXT3, 1);

    if (ui_button(9501, Rc(c.x + 28, c.y + 206, 110, 40), "复制代码", BTN_PRIMARY)) {
        lz_clipboard_set(g.auth.user_code);
        app_toast("验证码已复制");
    }
    if (ui_button(9502, Rc(c.x + 148, c.y + 206, 130, 40), "重新打开页面", BTN_GHOST)) {
        if (g.auth.verify_uri[0]) lz_shell_open(g.auth.verify_uri);
    }
    if (ui_button(9503, Rc(c.x + cw - 118, c.y + 206, 90, 40), "取消", BTN_GHOST)) {
        auth_cancel();
        return;
    }

    ui_text(Rc(c.x + 28, c.y + 262, cw - 56, 40),
            "登录完成前请保持这个窗口开着；登录成功后会自动关闭。",
            10.5f, 0, C_TEXT3, 0);
    ui_text(Rc(c.x + 28, c.y + 296, cw - 56, 20),
            g.auth.polling ? "等待浏览器里的确认…" : "已取消", 10.5f, 0,
            g.auth.polling ? C_ACCENT : C_TEXT3, 0);
}

/* ==========================================================================
 *  模组详情（点了直接进，版本用胶囊选，列表里就地下载）
 * ========================================================================== */
static const char *md_chip_items[24];
static int md_chip_n = 0;

static void md_overlay(void) {
    /* 前置解析完成后自动开下 */
    if (g_md.auto_dl && g_md.dep_state == 2) {
        g_md.auto_dl = 0;
        md_download_all();
        return;
    }

    RcF scr = Rc(0, 0, ui_w, ui_h);
    ui_fill_round(scr, 0, LZ_ARGB(96, 20, 26, 40));
    ui_hit(998000, scr);

    float cw = LZ_MIN(780.0f, ui_w - 80);
    float ch = LZ_MIN(620.0f, ui_h - 60);
    RcF c = Rc((ui_w - cw) / 2, (ui_h - ch) / 2, cw, ch);
    ui_shadow(c, 22, 40, 40);
    ui_fill_round_grad(c, 22, C_SURF, C_SURF2);
    ui_stroke_round(c, 22, 1.0f, C_SEL);

    char buf[512];
    avatar(Rc(c.x + 26, c.y + 22, 46, 46), g_md.project_id, g_md.title);
    ui_text(Rc(c.x + 86, c.y + 22, c.w - 140, 26), g_md.title, 18.0f, 1, C_TEXT, 0);
    char dn[64];
    lz_fmt_count(dn, sizeof(dn), g_md.downloads);
    snprintf(buf, sizeof(buf), "%s  ·  %s 次下载", g_md.author[0] ? g_md.author : "未知作者", dn);
    ui_text(Rc(c.x + 86, c.y + 50, c.w - 140, 18), buf, 10.5f, 0, C_TEXT3, 0);
    if (ui_button(9001, Rc(c.x + c.w - 50, c.y + 22, 30, 30), "✕", BTN_GHOST)) { md_close(); return; }

    ui_text_wrap(Rc(c.x + 26, c.y + 78, c.w - 52, 40), g_md.desc, 11.0f, 0, C_TEXT2, 0);

    if (ui_button(9002, Rc(c.x + 26, c.y + 122, 118, 30), "转到 Modrinth", BTN_GHOST)) {
        char u[300];
        snprintf(u, sizeof(u), "https://modrinth.com/project/%s", g_md.project_id);
        lz_shell_open(u);
    }
    if (ui_button(9003, Rc(c.x + 152, c.y + 122, 108, 30), "复制名称", BTN_GHOST))
        lz_clipboard_set(g_md.title);
    if (g_md.state == 2)
        ui_text(Rc(c.x + 270, c.y + 122, c.w - 300, 30),
                g_md.ndeps > 0 ? "前置会自动一起下载" : "", 10.5f, 0, C_TEXT3, 0);

    ui_line(c.x + 26, c.y + 162, c.x + c.w - 26, c.y + 162, 1.0f, LZ_ARGB(30, 120, 140, 170));

    if (g_md.state == 1) {
        ui_text(Rc(c.x, c.y + 200, c.w, 30), "正在获取可用文件…", 13.0f, 0, C_TEXT2, 1);
        return;
    }
    if (g_md.state == 3) {
        ui_text(Rc(c.x, c.y + 200, c.w, 30), g_md.err, 13.0f, 0, C_RED, 1);
        return;
    }
    if (g_md.state != 2) return;

    /* 主版本胶囊：1.21.1 / 1.21 / 26.3 → 归并到大版本 */
    md_chip_n = 0;
    md_chip_items[md_chip_n++] = "全部";
    for (int i = 0; i < g_md.nvers && md_chip_n < 24; i++) {
        char major[24];
        snprintf(major, sizeof(major), "%s", g_md.vers[i].game);
        char *dash = strchr(major, '-');      /* 26.4-snapshot-5 → 26.4 */
        if (dash) *dash = 0;
        int dots = 0;
        for (char *p = major; *p; p++) if (*p == '.') { if (++dots == 2) { *p = 0; break; } }
        int f = 0;
        for (int k = 1; k < md_chip_n; k++) if (!strcmp(md_chip_items[k], major)) f = 1;
        if (!f) {
            /* 需要稳定指针：用静态缓冲 */
            static char chipbuf[24][24];
            snprintf(chipbuf[md_chip_n], sizeof(chipbuf[0]), "%s", major);
            md_chip_items[md_chip_n] = chipbuf[md_chip_n];
            md_chip_n++;
        }
    }
    if (ps.md_chip >= md_chip_n) ps.md_chip = 0;

    float cx2 = c.x + 26;
    for (int i = 0; i < md_chip_n; i++) {
        float bw = 56.0f + (float)strlen(md_chip_items[i]) * 3.0f;
        if (cx2 + bw > c.x + c.w - 26) break;
        if (ui_chip(9100 + i, Rc(cx2, c.y + 176, bw, 30), md_chip_items[i], ps.md_chip == i)) {
            ps.md_chip = i;
        }
        cx2 += bw + 8;
    }

    /* 文件列表 */
    RcF list = Rc(c.x + 18, c.y + 218, c.w - 36, ch - 268);
    ui_fill_round(list, 14, LZ_ARGB(14, 255, 255, 255));
    ui_clip_push(list);
    float ly = list.y + 4 - ui.scroll[PG_DL];
    int rows = 0;
    for (int i = 0; i < g_md.nvers; i++) {
        MdVer *v = &g_md.vers[i];
        if (ps.md_chip > 0) {
            const char *chip = md_chip_items[ps.md_chip];
            if (strncmp(v->game, chip, strlen(chip))) continue;
        }
        rows++;
        if (ly + 56 > list.y && ly < list.y + list.h) {
            RcF rr = Rc(list.x + 6, ly, list.w - 12, 52);
            int hov = (ui.mx >= rr.x && ui.mx <= rr.x + rr.w && ui.my >= rr.y && ui.my <= rr.y + rr.h);
            if (hov) ui_fill_round(rr, 11, C_SEL);

            char lb[96];
            snprintf(lb, sizeof(lb), "%s  %s", v->loader[0] ? v->loader : "通用", v->game);
            ui_text(Rc(rr.x + 14, rr.y + 6, 220, 20), lb, 12.0f, 1, C_TEXT, 0);
            ui_text(Rc(rr.x + 14, rr.y + 26, list.w - 200, 18), v->fname, 10.0f, 0, C_TEXT3, 0);
            if (v->nreq > 0)
                ui_text(Rc(rr.x + 250, rr.y + 6, 100, 20), "含前置", 9.5f, 0, C_ORANGE, 0);

            int busy = (g_md.auto_dl && g_md.sel == i);
            int is_pack = (g.content_kind == 4);
            if (ui_button(9200 + i, Rc(rr.x + rr.w - 92, rr.y + 10, 78, 32),
                          busy ? "准备中" : (is_pack ? "安装" : "下载"),
                          busy ? BTN_GHOST : BTN_PRIMARY)) {
                if (is_pack) {
                    /* 整合包：下完直接装进游戏目录 */
                    modpack_install_async(v->url, v->fname);
                    md_close();
                } else {
                    g_md.sel = i;
                    g_md.auto_dl = 1;
                    md_resolve_deps();
                }
            }
        }
        ly += 56;
    }
    if (rows == 0)
        ui_text(Rc(list.x, list.y + 20, list.w, 30), "这个版本没有对应文件，换个版本试试",
                12.0f, 0, C_TEXT3, 1);
    ui_clip_pop();

    if (g_md.dep_state == 1)
        ui_text(Rc(c.x + 26, c.y + ch - 32, c.w - 52, 20),
                "正在解析前置模组…", 10.5f, 0, C_TEXT3, 0);
    else if (g.content_kind == 4)
        ui_text(Rc(c.x + 26, c.y + ch - 32, c.w - 52, 20),
                "整合包会装到当前游戏目录：下好包 → 装对应版本和加载器 → 解压覆盖配置", 10.5f, 0, C_TEXT3, 0);
    else
        ui_text(Rc(c.x + 26, c.y + ch - 32, c.w - 52, 20),
                "点「下载」会连同前置一起装进 mods 文件夹", 10.5f, 0, C_TEXT3, 0);
}

/* ==========================================================================
 *  快捷联机
 * ========================================================================== */
static void page_mp(RcF body) {
    float x = body.x, w = body.w;
    float y = body.y;

    int tc_ok = g.set.terracotta[0] && lz_file_exists(g.set.terracotta);

    /* ---------- 陶瓦联机 ---------- */
    char tc[400];
    EnterCriticalSection(&g_lock);
    snprintf(tc, sizeof(tc), "%s", g.set.terracotta);
    LeaveCriticalSection(&g_lock);
    int have = (tc[0] && lz_file_exists(tc));

    if (!have) {
        /* 没装：手动引导 */
        RcF nc = Rc(x, y, w, 236);
        ui_glass(nc, 20);
        ui_text(Rc(nc.x + 24, nc.y + 22, 400, 24), "还没有陶瓦联机", 15.0f, 1, C_TEXT, 0);
        ui_text(Rc(nc.x + 24, nc.y + 50, w - 48, 18),
                "陶瓦联机（Terracotta）是开源的联机工具，它会建一个虚拟局域网，"
                "游戏里的「对局域网开放」对方就能直接看到。",
                11.0f, 0, C_TEXT2, 0);
        ui_text(Rc(nc.x + 24, nc.y + 74, w - 48, 18),
                "用法：点「打开下载页」下载 Windows 包 → 解压 → 点「选择本地程序」挑里面的 terracotta.exe。",
                11.0f, 0, C_TEXT3, 0);
        ui_text(Rc(nc.x + 24, nc.y + 98, w - 48, 18),
                "只需要设置一次，之后启动器会记住。", 10.5f, 0, C_TEXT3, 0);

        if (ui_button(5320, Rc(nc.x + 24, nc.y + 132, 150, 42), "打开下载页", BTN_PRIMARY))
            lz_shell_open("https://github.com/burningtnt/Terracotta/releases");
        if (ui_button(5321, Rc(nc.x + 186, nc.y + 132, 190, 42), "选择本地 terracotta.exe", BTN_GHOST)) {
            char p2[MAX_PATH] = {0};
            if (win_pick_file(p2, sizeof(p2), "可执行文件 (*.exe)\0*.exe\0所有文件\0*.*\0")) {
                EnterCriticalSection(&g_lock);
                snprintf(g.set.terracotta, sizeof(g.set.terracotta), "%s", p2);
                LeaveCriticalSection(&g_lock);
                app_save_settings();
                app_toast("已设置陶瓦联机");
            }
        }
        ui_text(Rc(nc.x + 24, nc.y + 188, w - 48, 18),
                "不想装也行：下面「直接连接」填对方 IP:端口，或者同一个局域网里直接进。",
                10.5f, 0, C_TEXT3, 0);
        y += 236 + 14;
        goto direct;
    }

    RcF tv = Rc(x, y, w, 208);
    ui_glass(tv, 20);
    ui_text(Rc(tv.x + 24, tv.y + 20, 300, 22), "陶瓦联机", 14.5f, 1, C_TEXT, 0);
    ui_text(Rc(tv.x + 24, tv.y + 44, w - 48, 18),
            "点「启动」拉起陶瓦，按它自己的界面建房 / 进房，对方进同一房间后就能在游戏里互相看到。",
            11.0f, 0, C_TEXT3, 0);

    ui_text(Rc(tv.x + 24, tv.y + 74, 120, 18), "程序位置", 11.0f, 0, C_TEXT2, 0);
    RcF path = Rc(tv.x + 100, tv.y + 68, w - 340, 34);
    ui_fill_round(path, 12, C_FIELD);
    ui_stroke_round(path, 12, 1.0f, LZ_ARGB(30, 255, 255, 255));
    ui_text(Rc(path.x + 12, path.y, path.w - 24, path.h),
            have ? tc : "还没有选择陶瓦联机的 exe", 11.0f, 0, have ? C_TEXT : C_TEXT3, 0);

    if (ui_button(5300, Rc(tv.x + w - 228, tv.y + 66, 100, 36), "选择", BTN_GHOST)) {
        char p2[MAX_PATH] = {0};
        if (win_pick_file(p2, sizeof(p2), "可执行文件 (*.exe)\0*.exe\0所有文件\0*.*\0")) {
            EnterCriticalSection(&g_lock);
            snprintf(g.set.terracotta, sizeof(g.set.terracotta), "%s", p2);
            LeaveCriticalSection(&g_lock);
            app_save_settings();
        }
    }
    if (ui_button(5301, Rc(tv.x + w - 120, tv.y + 66, 96, 36), "启动", BTN_PRIMARY)) {
        if (have) {
            lz_shell_open(tc);
            app_toast("已启动陶瓦联机");
        } else {
            app_toast("先选一下陶瓦联机的 exe");
        }
    }

    ui_text(Rc(tv.x + 24, tv.y + 116, w - 48, 18),
            "没装的话去 terracotta.lan 下载，装好选一下 exe 就行；启动后按它的界面建房 / 进房。",
            10.5f, 0, C_TEXT3, 0);

    if (ui_button(5302, Rc(tv.x + 24, tv.y + 146, 150, 40), "重新选择程序", BTN_TINT)) {
        char p2[MAX_PATH] = {0};
        if (win_pick_file(p2, sizeof(p2), "可执行文件 (*.exe)\0*.exe\0所有文件\0*.*\0")) {
            EnterCriticalSection(&g_lock);
            snprintf(g.set.terracotta, sizeof(g.set.terracotta), "%s", p2);
            LeaveCriticalSection(&g_lock);
            app_save_settings();
        }
    }

    /* ---------- 直接连接 ---------- */
    y += 208 + 14;
direct:
    RcF dc = Rc(x, y, w, 150);
    ui_glass(dc, 20);
    ui_text(Rc(dc.x + 24, dc.y + 20, 300, 22), "直接连接", 14.5f, 1, C_TEXT, 0);
    ui_text(Rc(dc.x + 24, dc.y + 44, w - 48, 18),
            "同一个局域网、或者对方有公网 IP 时，填地址就能直接进，不需要任何中转。",
            11.0f, 0, C_TEXT3, 0);
    ui_input(5310, Rc(dc.x + 24, dc.y + 74, 300, 40), ps.mp_code, sizeof(ps.mp_code),
             "192.168.1.10:25565");
    if (ui_button(5311, Rc(dc.x + 336, dc.y + 74, 140, 40), "连接并启动", BTN_PRIMARY)) {
        char host[80] = {0};
        int port = 25565;
        snprintf(host, sizeof(host), "%s", ps.mp_code);
        char *colon = strrchr(host, ':');
        if (colon) { *colon = 0; port = atoi(colon + 1); if (port <= 0) port = 25565; }
        if (!host[0]) app_toast("先填服务器地址");
        else if (!ps.sel_ver[0]) app_toast("请先在「下载」里装一个版本");
        else { app_toast("直连 %s:%d", host, port); mc_launch_async(ps.sel_ver, host, port); }
    }
    ui_text(Rc(dc.x + 24, dc.y + 122, w - 48, 18),
            "需要先登录微软账号；离线模式已经移除。", 10.5f, 0, C_TEXT3, 0);

    /* ---------- 局域网世界 ---------- */
    y += 150 + 14;
    RcF lan = Rc(x, y, w, 96 + LZ_MAX(1, g_nlan) * 54);
    ui_glass(lan, 20);
    ui_text(Rc(lan.x + 24, lan.y + 20, 400, 22), "局域网世界", 14.5f, 1, C_TEXT, 0);
    ui_text(Rc(lan.x + 24, lan.y + 44, w - 48, 18),
            "游戏里「对局域网开放」之后，这里会自动出现，点一下就能进。", 11.0f, 0, C_TEXT3, 0);
    float ly = lan.y + 74;
    if (g_nlan == 0) {
        ui_text(Rc(lan.x + 24, ly, w - 48, 40), "暂未发现局域网世界", 12.0f, 0, C_TEXT3, 0);
    } else {
        for (int i = 0; i < g_nlan; i++) {
            RcF rr = Rc(lan.x + 14, ly, w - 28, 48);
            ui_fill_round(rr, 12, C_HOVER);
            ui_text(Rc(rr.x + 14, rr.y, w - 220, rr.h), g_lan[i].motd, 12.5f, 1, C_TEXT, 0);
            char ad[80];
            snprintf(ad, sizeof(ad), "%s:%d", g_lan[i].addr, g_lan[i].port);
            ui_text(Rc(rr.x + w - 320, rr.y, 160, rr.h), ad, 11.0f, 0, C_TEXT3, 2);
            if (ui_button(5310 + i, Rc(rr.x + rr.w - 116, rr.y + 9, 104, 30), "进服", BTN_PRIMARY)) {
                if (ps.sel_ver[0]) mc_launch_async(ps.sel_ver, g_lan[i].addr, g_lan[i].port);
                else app_toast("请先在「下载」里装一个版本");
            }
            ly += 54;
        }
    }
    y += lan.h + 14;

    ui_clip_pop();
    scroll_area(PG_MP, body, y);
}

/* ==========================================================================
 *  虚拟按键
 * ========================================================================== */
static const struct { const char *name; int code; } KEYMAP[] = {
    { "空格", 32 }, { "Shift", 340 }, { "Ctrl", 341 }, { "Alt", 342 },
    { "E", 69 }, { "Q", 81 }, { "R", 82 }, { "F", 70 }, { "T", 84 },
    { "Tab", 258 }, { "Esc", 256 }, { "Enter", 257 },
    { "F1", 290 }, { "F3", 292 }, { "F5", 294 }, { "数字1", 49 }, { "数字2", 50 }
};

static const char *key_name(int code) {
    if (code == 87) return "W";
    if (code == 65) return "A";
    if (code == 83) return "S";
    if (code == 68) return "D";
    if (code == -1) return "左键";
    if (code == -2) return "右键";
    if (code == -3) return "摇杆";
    for (int i = 0; i < LZ_COUNT(KEYMAP); i++) if (KEYMAP[i].code == code) return KEYMAP[i].name;
    return "?";
}

static void vpad_defaults(void) {
    g.nvpad = 0;
    VButton *b;
#define ADDV(L, C, X, Y, R, ST) do { b = &g.vpad[g.nvpad++]; snprintf(b->label, 16, "%s", L); \
    b->keycode = C; b->x = X; b->y = Y; b->r = R; b->is_stick = ST; } while (0)
    ADDV("摇杆", -3, 0.145f, 0.70f, 0.105f, 1);
    ADDV("跳跃", 32, 0.855f, 0.74f, 0.058f, 0);
    ADDV("潜行", 340, 0.700f, 0.82f, 0.048f, 0);
    ADDV("攻击", -1, 0.945f, 0.545f, 0.050f, 0);
    ADDV("使用", -2, 0.810f, 0.575f, 0.045f, 0);
    ADDV("背包", 69, 0.655f, 0.660f, 0.042f, 0);
    ADDV("丢弃", 81, 0.575f, 0.800f, 0.038f, 0);
#undef ADDV
}

/* ==========================================================================
 *  皮肤衣柜
 * ========================================================================== */
static int ps_skin_sel = -1;

/* 画一个纸娃娃：从 64x64 皮肤里抠出各部位 */
static void draw_doll(void *img, RcF area, int slim) {
    if (!img) return;
    float s = LZ_MIN(area.w / 17.0f, area.h / 33.0f);
    float cx = area.x + area.w * 0.5f;
    float top = area.y + (area.h - 32 * s) * 0.5f;
    int aw = slim ? 3 : 4;
    /* 头 */
    ui_image_region(img, Rc(cx - 4 * s, top, 8 * s, 8 * s), 8, 8, 8, 8);
    /* 身体 */
    ui_image_region(img, Rc(cx - 4 * s, top + 8 * s, 8 * s, 12 * s), 20, 20, 8, 12);
    /* 右臂 */
    ui_image_region(img, Rc(cx - 4 * s - aw * s, top + 8 * s, aw * s, 12 * s), 44, 20, aw, 12);
    /* 左臂 */
    ui_image_region(img, Rc(cx + 4 * s, top + 8 * s, aw * s, 12 * s), 36, 52, aw, 12);
    /* 右腿 */
    ui_image_region(img, Rc(cx - 4 * s, top + 20 * s, 4 * s, 12 * s), 4, 20, 4, 12);
    /* 左腿 */
    ui_image_region(img, Rc(cx, top + 20 * s, 4 * s, 12 * s), 20, 52, 4, 12);
}

static void page_skin(RcF body) {
    float x = body.x, w = body.w;
    int logged = (g.auth.state == AUTH_OK);

    if (logged && g.skin.state == 0) skin_refresh_async();

    float lw = 340.0f;
    RcF pv = Rc(x, body.y, lw, body.h - 8);
    ui_glass(pv, 20);
    ui_text(Rc(pv.x + 22, pv.y + 18, lw - 44, 20), "当前形象", 13.5f, 1, C_TEXT, 0);
    ui_text(Rc(pv.x + 22, pv.y + 40, lw - 44, 18),
            logged ? g.auth.name : "未登录", 11.0f, 0, logged ? C_GREEN : C_TEXT3, 0);

    RcF stage = Rc(pv.x + 18, pv.y + 70, lw - 36, pv.h - 190);
    ui_fill_round(stage, 16, LZ_ARGB(26, 255, 255, 255));

    /* 优先预览衣架里选中的本地皮肤，没有才用账号皮肤 */
    const char *src = NULL;
    int local = 0;
    if (ps_skin_sel >= 0 && ps_skin_sel < skin_wardrobe_count()) {
        src = skin_wardrobe_item(ps_skin_sel);
        local = 1;
    } else if (logged) {
        src = g.skin.png;
    }
    void *img = src ? ui_image_load(src) : NULL;
    if (img) {
        draw_doll(img, Rc(stage.x, stage.y + 10, stage.w, stage.h - 20), g.skin.model);
    } else {
        ui_text(Rc(stage.x, stage.y + stage.h / 2 - 14, stage.w, 28),
                logged ? (g.skin.state == 1 ? "正在获取皮肤…" : "还没有皮肤")
                       : "导入一套皮肤就能预览",
                12.0f, 0, C_TEXT3, 1);
    }
    if (img) ui_image_free(img);
    if (local)
        ui_text(Rc(pv.x + 22, pv.y + 62, lw - 44, 16), "本地预览（未上传）", 10.0f, 0, C_TEXT3, 0);

    /* 模型切换 + 导入 */
    if (ui_chip(7001, Rc(pv.x + 20, pv.y + pv.h - 106, 88, 32), "经典", g.skin.model == 0))
        g.skin.model = 0;
    if (ui_chip(7002, Rc(pv.x + 114, pv.y + pv.h - 106, 88, 32), "纤细", g.skin.model == 1))
        g.skin.model = 1;

    if (ui_button(7003, Rc(pv.x + 20, pv.y + pv.h - 64, 140, 40), "导入皮肤文件", BTN_PRIMARY)) {
        {
            char path[MAX_PATH] = {0};
            if (win_pick_file(path, sizeof(path), "皮肤图片 (*.png)\0*.png\0所有文件\0*.*\0")) {
                char dir[MAX_PATH * 2], dst[MAX_PATH * 3];
                skin_dir(dir, sizeof(dir));
                const char *nm = strrchr(path, '\\');
                nm = nm ? nm + 1 : path;
                lz_path_join(dst, sizeof(dst), dir, nm);
                char *data = NULL;
                size_t n = 0;
                FILE *f = fopen(path, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
                    if (sz > 0 && sz < 1024 * 1024) {
                        data = (char *)malloc((size_t)sz);
                        n = fread(data, 1, (size_t)sz, f);
                    }
                    fclose(f);
                }
                if (data && n > 0) {
                    lz_file_write(dst, data, n);
                    free(data);
                    skin_scan();
                    ps_skin_sel = -1;
                    for (int i = 0; i < skin_wardrobe_count(); i++)
                        if (!strcmp(skin_wardrobe_item(i), dst)) ps_skin_sel = i;
                    if (logged) skin_upload_async(dst, g.skin.model);
                    else app_toast("已导入，可以本地预览；登录后可穿到账号");
                } else {
                    app_toast("读不到这个文件");
                }
            }
        }
    }
    if (ui_button(7004, Rc(pv.x + 168, pv.y + pv.h - 64, 120, 40), "刷新", BTN_GHOST))
        skin_refresh_async();

    /* 衣架 */
    RcF wd = Rc(x + lw + 14, body.y, w - lw - 14, body.h - 8);
    ui_glass(wd, 20);
    ui_text(Rc(wd.x + 22, wd.y + 18, 240, 20), "衣架", 13.5f, 1, C_TEXT, 0);
    char cnt[64];
    snprintf(cnt, sizeof(cnt), "%d 套", skin_wardrobe_count());
    ui_text(Rc(wd.x + 22, wd.y + 40, 240, 18), cnt, 11.0f, 0, C_TEXT3, 0);

    float cell = 92.0f, gap = 12.0f;
    int cols = (int)((wd.w - 44 + gap) / (cell + gap));
    if (cols < 1) cols = 1;
    float gx = wd.x + 22, gy = wd.y + 70;
    for (int i = 0; i < skin_wardrobe_count(); i++) {
        int cx = i % cols, cy = i / cols;
        RcF cellR = Rc(gx + cx * (cell + gap), gy + cy * (cell + gap), cell, cell);
        if (cellR.y + cell > wd.y + wd.h - 90) break;
        int on = (ps_skin_sel == i);
        ui_fill_round(cellR, 14, on ? LZ_ARGB(200, 208, 232, 255) : LZ_ARGB(22, 255, 255, 255));
        if (on) ui_stroke_round(cellR, 14, 2.0f, C_ACCENT);
        void *im = ui_image_load(skin_wardrobe_item(i));
        if (im) {
            ui_image_region(im, Rc(cellR.x + 16, cellR.y + 14, cell - 32, cell - 32), 8, 8, 8, 8);
            ui_image_free(im);
        }
        if (ui_hit(7100 + i, cellR)) {
            ui.click = 0;
            ps_skin_sel = i;
        }
        if (on) ui_text(Rc(cellR.x, cellR.y + cell - 22, cell, 18), "已选中", 9.5f, 0, C_ACCENT, 1);
    }
    if (skin_wardrobe_count() == 0) {
        ui_text(Rc(wd.x + 22, gy + 10, wd.w - 44, 24),
                "衣架还是空的，点左边「导入皮肤文件」放一套进来", 11.5f, 0, C_TEXT3, 0);
    }

    /* 底部：穿上 + 披风 */
    if (ui_button(7200, Rc(wd.x + 22, wd.y + wd.h - 66, 160, 42),
                  ps_skin_sel >= 0 ? "穿到账号上" : "先选一套皮肤",
                  ps_skin_sel >= 0 ? BTN_PRIMARY : BTN_GHOST)) {
        if (ps_skin_sel >= 0) skin_apply_local(skin_wardrobe_item(ps_skin_sel), g.skin.model);
    }
    if (ui_button(7201, Rc(wd.x + 192, wd.y + wd.h - 66, 120, 42), "打开文件夹", BTN_GHOST)) {
        char d[MAX_PATH * 2];
        skin_dir(d, sizeof(d));
        win_open_folder(d);
    }

    /* 披风 */
    float cxp = wd.x + 340;
    if (cxp < wd.x + wd.w - 200) {
        ui_text(Rc(cxp, wd.y + wd.h - 58, 120, 18), "披风", 11.5f, 1, C_TEXT3, 0);
        if (g.skin.ncapes == 0) {
            ui_text(Rc(cxp + 46, wd.y + wd.h - 58, 300, 18),
                    logged ? "这个账号没有披风" : "登录后显示", 10.5f, 0, C_TEXT3, 0);
        }
        float bx2 = cxp + 46;
        for (int i = 0; i < g.skin.ncapes; i++) {
            RcF br = Rc(bx2, wd.y + wd.h - 64, 108, 38);
            if (bx2 + 108 > wd.x + wd.w - 20) break;
            if (ui_button(7300 + i, br, g.skin.capes[i].active ? "使用中" : "换上",
                          g.skin.capes[i].active ? BTN_TINT : BTN_GHOST))
                skin_cape_async(g.skin.capes[i].id);
            ui_text(Rc(br.x, br.y - 18, 108, 16), g.skin.capes[i].alias, 9.0f, 0, C_TEXT3, 0);
            bx2 += 116;
        }
    }
}

/* ==========================================================================
 *  账号切换浮层
 * ========================================================================== */
static int acct_modal = 0;

static void acct_overlay(void) {
    if (!acct_modal) return;
    RcF scr = Rc(0, 0, ui_w, ui_h);
    ui_fill_round(scr, 0, LZ_ARGB(96, 20, 26, 40));
    ui_hit(995000, scr);

    float cw = LZ_MIN(460.0f, ui_w - 80);
    float ch = 180.0f + g.book.n * 62.0f;
    if (ch > ui_h - 60) ch = ui_h - 60;
    RcF c = Rc((ui_w - cw) / 2, (ui_h - ch) / 2, cw, ch);
    ui_shadow(c, 20, 36, 40);
    ui_fill_round_grad(c, 20, C_SURF, C_SURF2);
    ui_stroke_round(c, 20, 1.0f, C_SEL);

    ui_text(Rc(c.x + 24, c.y + 20, cw - 70, 24), "账号", 16.0f, 1, C_TEXT, 0);
    if (ui_button(9401, Rc(c.x + cw - 46, c.y + 18, 28, 28), "✕", BTN_GHOST)) { acct_modal = 0; return; }

    float y = c.y + 60;
    for (int i = 0; i < g.book.n; i++) {
        Account *a = &g.book.items[i];
        RcF r = Rc(c.x + 16, y, cw - 32, 54);
        int on = (g.book.sel == i);
        ui_fill_round(r, 12, on ? LZ_ARGB(200, 208, 232, 255) : LZ_ARGB(22, 255, 255, 255));
        avatar(Rc(r.x + 10, r.y + 8, 38, 38), a->name, a->name);
        ui_text(Rc(r.x + 58, r.y + 8, cw - 200, 20), a->name, 13.0f, 1, C_TEXT, 0);
        ui_text(Rc(r.x + 58, r.y + 28, cw - 200, 18), on ? "当前使用" : "点一下切换",
                10.0f, 0, on ? C_GREEN : C_TEXT3, 0);
        if (ui_button(9410 + i, Rc(r.x + r.w - 78, r.y + 11, 66, 32), on ? "使用中" : "切换",
                      on ? BTN_TINT : BTN_GHOST)) {
            if (!on) {
                EnterCriticalSection(&g_lock);
                g.book.sel = i;
                snprintf(g.auth.name, sizeof(g.auth.name), "%s", a->name);
                snprintf(g.auth.uuid, sizeof(g.auth.uuid), "%s", a->uuid);
                snprintf(g.auth.xuid, sizeof(g.auth.xuid), "%s", a->xuid);
                snprintf(g.auth.token, sizeof(g.auth.token), "%s", a->token);
                snprintf(g.auth.refresh, sizeof(g.auth.refresh), "%s", a->refresh);
                g.auth.state = AUTH_OK;
                LeaveCriticalSection(&g_lock);
                app_save_accounts();
                g.skin.state = 0;
                app_toast("已切换到 %s", a->name);
            }
        }
        y += 62;
    }
    if (g.book.n == 0)
        ui_text(Rc(c.x + 24, y + 6, cw - 48, 24), "还没有登录过账号", 12.0f, 0, C_TEXT3, 0);

    if (ui_button(9400, Rc(c.x + 24, c.y + ch - 56, 150, 40), "添加账号", BTN_PRIMARY)) {
        acct_modal = 0;
        auth_start();
    }
    if (g.book.n > 0 && ui_button(9402, Rc(c.x + 186, c.y + ch - 56, 110, 40), "退出登录", BTN_GHOST)) {
        auth_logout();
        acct_modal = 0;
    }
}

/* 抽签文案 */
static const char *fortune_text(int n) {
    static const char *good[] = {
        "今天开箱必出好东西", "苦力怕会绕着你走", "钻石就在脚底下",
        "附魔一次就出经验修补", "末影龙今晚有点紧张", "你挖的每一格都是矿脉",
    };
    static const char *mid[] = {
        "平平淡淡才是真", "今天适合种田", "别往岩浆里跳就行",
        "箱子里的东西刚刚好", "慢慢来，方块不会跑",
    };
    static const char *bad[] = {
        "小心脚下的岩浆", "今晚别下矿", "苦力怕正在靠近",
        "背包可能会满", "别相信那个 TNT",
    };
    if (n >= 85) return good[n % 6];
    if (n >= 50) return mid[n % 5];
    return bad[n % 5];
}

static void page_box(RcF body) {
    float x = body.x, w = body.w;
    float y = body.y - ui.scroll[PG_BOX];
    ui_clip_push(body);

    /* 多线程下载 */
    RcF dl = Rc(x, y, w, 208);
    ui_glass(dl, 22);
    card_title(Rc(dl.x + 24, dl.y + 20, w - 48, 40), "多线程下载",
               "粘贴任意直链，自动分块并行下载");
    ui_input(7001, Rc(dl.x + 24, dl.y + 68, w - 48, 40), ps.box_url, sizeof(ps.box_url),
             "https://example.com/file.jar");

    ui_text(Rc(dl.x + 24, dl.y + 122, 60, 20), "线程", 11.5f, 0, C_TEXT2, 0);
    int ths[] = { 1, 4, 8, 16, 32 };
    float tx = dl.x + 68;
    for (int i = 0; i < 5; i++) {
        char lb[8];
        snprintf(lb, sizeof(lb), "%d", ths[i]);
        if (ui_chip(7010 + i, Rc(tx, dl.y + 118, 48, 28), lb, ps.box_threads == ths[i]))
            ps.box_threads = ths[i];
        tx += 54;
    }
    ui_text(Rc(tx + 10, dl.y + 118, 40, 28), "保存到", 11.5f, 0, C_TEXT2, 0);
    ui_text(Rc(tx + 60, dl.y + 118, w - (tx - dl.x) - 300, 28), ps.box_dir, 11.0f, 0, C_TEXT3, 0);

    if (ui_button(7020, Rc(dl.x + 24, dl.y + 158, 118, 36), "开始下载", BTN_PRIMARY)) {
        if (strlen(ps.box_url) > 8) {
            char name[200] = "download.bin";
            const char *slash = strrchr(ps.box_url, '/');
            if (slash && slash[1]) {
                snprintf(name, sizeof(name), "%s", slash + 1);
                char *qm = strchr(name, '?');
                if (qm) *qm = 0;
                lz_sanitize_filename(name);
            }
            char dest[MAX_PATH * 3];
            lz_path_join(dest, sizeof(dest), ps.box_dir, name);
            DlTask *t = dl_start(ps.box_url, dest, ps.box_threads, ui_request_redraw);
            if (t) {
                tasks_add(t);
                ps.box_url[0] = 0;
                ui.focus_id = 0;
                app_toast("已加入下载队列");
            }
        } else {
            app_toast("请先粘贴下载链接");
        }
    }
    if (ui_button(7021, Rc(dl.x + 150, dl.y + 158, 118, 36), "粘贴剪贴板", BTN_GHOST)) {
        char *c = lz_clipboard_get();
        if (c) {
            snprintf(ps.box_url, sizeof(ps.box_url), "%s", c);
            free(c);
            app_toast("已粘贴");
        }
    }
    if (ui_button(7022, Rc(dl.x + 276, dl.y + 158, 118, 36), "打开目录", BTN_GHOST))
        win_open_folder(ps.box_dir);
    y += 208 + 14;

    /* 任务列表 */
    if (g_ntasks > 0) {
        float hh = 52 + g_ntasks * 62;
        RcF tk = Rc(x, y, w, hh);
        ui_glass(tk, 22);
        ui_text(Rc(tk.x + 24, tk.y + 16, w - 48, 20), "下载任务", 13.0f, 1, C_TEXT, 0);
        float ty = tk.y + 46;
        for (int i = 0; i < g_ntasks; i++) {
            DlTask *t = g_tasks[i];
            RcF rr = Rc(tk.x + 14, ty, w - 28, 56);
            ui_fill_round(rr, 12, LZ_ARGB(80, 255, 255, 255));
            char sz1[32], sz2[32], sp[48];
            lz_fmt_size(sz1, sizeof(sz1), (double)dl_got(t));
            lz_fmt_size(sz2, sizeof(sz2), (double)dl_total(t));
            lz_fmt_size(sp, sizeof(sp), dl_speed(t));
            int stt = dl_state(t);
            const char *stn = stt == 0 ? "排队" : stt == 1 ? "下载中" : stt == 2 ? "完成" :
                              stt == 3 ? "失败" : "已取消";
            char line[300];
            if (stt == 1)
                snprintf(line, sizeof(line), "%s / %s  ·  %s/s", sz1, sz2, sp);
            else if (stt == 2)
                snprintf(line, sizeof(line), "%s  ·  已完成", sz2);
            else
                snprintf(line, sizeof(line), "%s", stt == 3 ? dl_error(t) : stn);
            ui_text(Rc(rr.x + 14, rr.y + 8, w - 220, 20), dl_name(t), 11.5f, 1, C_TEXT, 0);
            ui_text(Rc(rr.x + 14, rr.y + 28, w - 220, 18), line, 10.5f, 0,
                    stt == 2 ? C_GREEN : (stt == 3 ? C_RED : C_TEXT3), 0);
            long long tot = dl_total(t);
            float frac = tot > 0 ? (float)((double)dl_got(t) / (double)tot) : (stt == 2 ? 1.0f : 0.0f);
            ui_progress(Rc(rr.x + w - 300, rr.y + 26, 190, 6), frac,
                        stt == 3 ? C_RED : (stt == 2 ? C_GREEN : C_ACCENT));
            if (stt == 1 || stt == 0) {
                if (ui_button(7100 + i, Rc(rr.x + rr.w - 96, rr.y + 14, 80, 28), "取消", BTN_GHOST))
                    dl_cancel(t);
            } else {
                if (ui_button(7100 + i, Rc(rr.x + rr.w - 96, rr.y + 14, 80, 28), "移除", BTN_GHOST)) {
                    dl_free(t);
                    for (int k = i; k < g_ntasks - 1; k++) g_tasks[k] = g_tasks[k + 1];
                    g_ntasks--;
                    break;
                }
            }
            ty += 62;
        }
        y += hh + 14;
    }

    /* 清理缓存 */
    RcF cc = Rc(x, y, w, 116);
    ui_glass(cc, 22);
    ui_text(Rc(cc.x + 24, cc.y + 20, 300, 20), "清理启动器缓存", 13.5f, 1, C_TEXT, 0);
    char cbuf[160];
    if (ps.cache_state == 2) {
        char s1[32];
        lz_fmt_size(s1, sizeof(s1), (double)ps.cache_bytes);
        snprintf(cbuf, sizeof(cbuf), "日志与崩溃报告占用 %s（%d 个文件）", s1, ps.cache_files);
    } else if (ps.cache_state == 1) {
        snprintf(cbuf, sizeof(cbuf), "正在统计…");
    } else {
        snprintf(cbuf, sizeof(cbuf), "清理 logs / crash-reports，不会动存档与资源");
    }
    ui_text(Rc(cc.x + 24, cc.y + 48, w - 200, 20), cbuf, 11.5f, 0, C_TEXT3, 0);
    if (ui_button(7201, Rc(cc.x + 24, cc.y + 74, 110, 32), "统计占用", BTN_GHOST)) {
        ps.cache_state = 1;
        ui_request_redraw();
    }
    if (ui_button(7202, Rc(cc.x + 144, cc.y + 74, 110, 32), "清理缓存", BTN_TINT)) {
        char p[MAX_PATH * 3];
        lz_path_join(p, sizeof(p), g.set.game_dir, "logs");         lz_dir_remove(p);
        lz_path_join(p, sizeof(p), g.set.game_dir, "crash-reports"); lz_dir_remove(p);
        ps.cache_state = 0;
        app_toast("缓存已清理");
    }
    y += 116 + 14;

    /* 抽签 */
    RcF ft = Rc(x, y, w, 150);
    ui_glass(ft, 22);
    ui_text(Rc(ft.x + 24, ft.y + 20, 300, 20), "今日抽签", 13.5f, 1, C_TEXT, 0);
    ui_text(Rc(ft.x + 24, ft.y + 46, w - 260, 18), "1 - 100，数字越大运气越好",
            11.0f, 0, C_TEXT3, 0);
    if (g.fortune > 0) {
        char nb[16];
        snprintf(nb, sizeof(nb), "%d", g.fortune);
        ARGB fc = g.fortune >= 76 ? C_GREEN : g.fortune >= 45 ? C_ACCENT : C_ORANGE;
        ui_text(Rc(ft.x + 24, ft.y + 68, 90, 56), nb, 42.0f, 1, fc, 0);
        ui_text(Rc(ft.x + 118, ft.y + 78, w - 300, 24), fortune_text(g.fortune), 12.5f, 0, C_TEXT, 0);
    } else {
        ui_text(Rc(ft.x + 24, ft.y + 78, w - 300, 24), "还没抽，试试今天的手气", 12.0f, 0, C_TEXT3, 0);
    }
    if (ui_button(7301, Rc(ft.x + w - 140, ft.y + 78, 116, 40), "抽一签", BTN_PRIMARY)) {
        g.fortune = (int)lz_rand(1, 100);
        app_toast("签已抽出");
    }
    y += 150 + 14;

    /* 千万别点 */
    RcF nv = Rc(x, y, w, 108);
    ui_glass(nv, 22);
    ui_text(Rc(nv.x + 24, nv.y + 20, w - 200, 20), "千万别点", 13.5f, 1, LZ_ARGB(255, 220, 50, 60), 0);
    ui_text(Rc(nv.x + 24, nv.y + 46, w - 200, 20),
            "这个按钮什么都不会发生。真的。相信我。", 11.0f, 0, C_TEXT3, 0);
    if (ui_button(7401, Rc(nv.x + 24, nv.y + 70, 150, 30), "点一下试试", BTN_DANGER)) {
        lz_shell_open("https://www.bilibili.com/video/BV1GJ411x7h7/?spm_id_from=333.337.search-card.all.click");
    }
    y += 108 + 14;

    ui_clip_pop();
    scroll_area(PG_BOX, body, y);
}

/* ==========================================================================
 *  设置
 * ========================================================================== */
static void settings_sync_from_app(void) {
    if (ps.set_inited) return;
    snprintf(ps.set_game, sizeof(ps.set_game), "%s", g.set.game_dir);
    snprintf(ps.set_java, sizeof(ps.set_java), "%s", g.set.java_path);
    snprintf(ps.set_mem, sizeof(ps.set_mem), "%d", g.set.mem_mb);
    snprintf(ps.set_cf, sizeof(ps.set_cf), "%s", g.set.cf_key);
    snprintf(ps.set_relay, sizeof(ps.set_relay), "%s", g.set.relay);
    snprintf(ps.set_msa, sizeof(ps.set_msa), "%s", g.set.msa_client_id);
    ps.set_inited = 1;
}

static void page_set(RcF body) {
    float x = body.x, w = body.w;
    float y = body.y - ui.scroll[PG_SET];
    ui_clip_push(body);
    settings_sync_from_app();

    /* 账号 */
    RcF ac = Rc(x, y, w, 132);
    ui_glass(ac, 22);
    ui_text(Rc(ac.x + 24, ac.y + 18, 300, 20), "账号", 13.5f, 1, C_TEXT, 0);
    int logged = (g.auth.state == AUTH_OK);
    avatar(Rc(ac.x + 24, ac.y + 48, 48, 48), logged ? g.auth.name : "guest", logged ? g.auth.name : "?");
    if (logged) {
        ui_text(Rc(ac.x + 84, ac.y + 52, w - 200, 22), g.auth.name, 14.0f, 1, C_TEXT, 0);
        ui_text(Rc(ac.x + 84, ac.y + 74, w - 200, 18), "微软账号已登录", 11.0f, 0, C_GREEN, 0);
        if (ui_button(8001, Rc(ac.x + w - 130, ac.y + 56, 106, 32), "退出登录", BTN_GHOST))
            auth_logout();
    } else if (g.auth.state == AUTH_WAIT) {
        char cd[128];
        snprintf(cd, sizeof(cd), "在浏览器输入代码：%s", g.auth.user_code);
        ui_text(Rc(ac.x + 84, ac.y + 52, w - 200, 22), cd, 13.5f, 1, C_ACCENT, 0);
        ui_text(Rc(ac.x + 84, ac.y + 74, w - 200, 18), g.auth.verify_uri, 10.5f, 0, C_TEXT3, 0);
        if (ui_button(8002, Rc(ac.x + w - 130, ac.y + 56, 106, 32), "取消", BTN_GHOST)) auth_cancel();
        if (ui_button(8003, Rc(ac.x + w - 246, ac.y + 56, 106, 32), "打开页面", BTN_TINT))
            lz_shell_open(g.auth.verify_uri);
    } else {
        ui_text(Rc(ac.x + 84, ac.y + 52, w - 200, 22), "未登录", 13.5f, 1, C_TEXT, 0);
        ui_text(Rc(ac.x + 84, ac.y + 74, w - 200, 18),
                g.auth.err[0] ? g.auth.err : "登录后可进入正版服务器", 10.5f, 0,
                g.auth.err[0] ? C_RED : C_TEXT3, 0);
        if (ui_button(8004, Rc(ac.x + w - 130, ac.y + 56, 106, 32), "微软登录", BTN_PRIMARY))
            auth_start();
    }
    y += 132 + 14;

    /* 游戏 */
    RcF gs = Rc(x, y, w, 250);
    ui_glass(gs, 22);
    ui_text(Rc(gs.x + 24, gs.y + 18, 300, 20), "游戏", 13.5f, 1, C_TEXT, 0);

    ui_text(Rc(gs.x + 24, gs.y + 52, 90, 18), "游戏目录", 11.5f, 0, C_TEXT2, 0);
    ui_input(8101, Rc(gs.x + 120, gs.y + 44, w - 260, 36), ps.set_game, sizeof(ps.set_game), "");
    if (ui_button(8102, Rc(gs.x + w - 128, gs.y + 44, 100, 36), "浏览", BTN_GHOST))
        win_open_folder(g.set.game_dir);

    ui_text(Rc(gs.x + 24, gs.y + 100, 90, 18), "内存", 11.5f, 0, C_TEXT2, 0);
    ui_input(8104, Rc(gs.x + 120, gs.y + 92, 90, 36), ps.set_mem, sizeof(ps.set_mem), "4096");
    ui_text(Rc(gs.x + 220, gs.y + 100, 40, 18), "MB", 11.5f, 0, C_TEXT3, 0);
    ui_text(Rc(gs.x + 280, gs.y + 100, 220, 18), "需要微软正版账号才能启动游戏", 10.5f, 0, C_TEXT3, 0);

    ui_text(Rc(gs.x + 24, gs.y + 148, 90, 18), "下载线程", 11.5f, 0, C_TEXT2, 0);
    int ths[] = { 4, 8, 16, 32 };
    float tx = gs.x + 120;
    for (int i = 0; i < 4; i++) {
        char lb[8];
        snprintf(lb, sizeof(lb), "%d", ths[i]);
        if (ui_chip(8110 + i, Rc(tx, gs.y + 142, 50, 28), lb, g.set.dl_threads == ths[i]))
            g.set.dl_threads = ths[i];
        tx += 56;
    }
    ui_text(Rc(gs.x + 360, gs.y + 148, 200, 18), "资源文件", 11.5f, 0, C_TEXT2, 0);
    if (ui_chip(8115, Rc(gs.x + 420, gs.y + 142, 80, 28), "完整下载", g.set.full_assets))
        g.set.full_assets = !g.set.full_assets;
    ui_text(Rc(gs.x + 510, gs.y + 148, w - 760, 18),
            g.set.full_assets ? "含声音/语言" : "仅游戏核心", 10.5f, 0, C_TEXT3, 0);
    if (w > 760 && ui_chip(8116, Rc(gs.x + w - 200, gs.y + 142, 140, 28), "版本隔离", g.set.version_isolate)) {
        g.set.version_isolate = !g.set.version_isolate;
        app_toast(g.set.version_isolate ? "每个版本独立 mods / 存档" : "所有版本共用 mods / 存档");
    }

    ui_text(Rc(gs.x + 24, gs.y + 196, 90, 18), "Java", 11.5f, 0, C_TEXT2, 0);
    ui_input(8120, Rc(gs.x + 120, gs.y + 188, w - 380, 36), ps.set_java, sizeof(ps.set_java), "自动检测");
    if (ui_button(8121, Rc(gs.x + w - 248, gs.y + 188, 96, 36), "自动检测", BTN_GHOST)) {
        char j[MAX_PATH * 3];
        if (java_detect(j, sizeof(j))) {
            snprintf(ps.set_java, sizeof(ps.set_java), "%s", j);
            snprintf(g.set.java_path, sizeof(g.set.java_path), "%s", j);
            app_toast("已找到 Java");
        } else {
            app_toast("未找到 Java，可下载运行时");
        }
    }
    if (ui_button(8122, Rc(gs.x + w - 144, gs.y + 188, 116, 36), "下载运行时", BTN_TINT))
        java_install_async(21);
    y += 250 + 14;

    /* 网络 */
    RcF nt = Rc(x, y, w, 176);
    ui_glass(nt, 22);
    ui_text(Rc(nt.x + 24, nt.y + 18, 300, 20), "网络", 13.5f, 1, C_TEXT, 0);
    ui_text(Rc(nt.x + 24, nt.y + 52, 120, 18), "CurseForge Key", 11.5f, 0, C_TEXT2, 0);
    ui_input(8201, Rc(nt.x + 150, nt.y + 44, w - 180, 36), ps.set_cf, sizeof(ps.set_cf),
             "粘贴 API Key（可留空）");
    ui_text(Rc(nt.x + 24, nt.y + 100, 130, 18), "微软登录 ID", 11.5f, 0, C_TEXT2, 0);
    ui_input(8203, Rc(nt.x + 150, nt.y + 92, w - 180, 36), ps.set_msa, sizeof(ps.set_msa),
             "留空则用内置的公开客户端 ID");
    ui_text(Rc(nt.x + 24, nt.y + 140, w - 48, 18),
            "登录报 AADSTS700016 说明客户端 ID 被微软吊销了，在这里换一个即可。",
            10.5f, 0, C_TEXT3, 0);
    y += 176 + 14;

    /* 保存 */
    RcF sv = Rc(x, y, w, 84);
    ui_glass(sv, 22);
    if (ui_button(8301, Rc(sv.x + 24, sv.y + 22, 140, 40), "保存设置", BTN_PRIMARY)) {
        EnterCriticalSection(&g_lock);
        snprintf(g.set.game_dir, sizeof(g.set.game_dir), "%s", ps.set_game);
        snprintf(g.set.java_path, sizeof(g.set.java_path), "%s", ps.set_java);
        snprintf(g.set.cf_key, sizeof(g.set.cf_key), "%s", ps.set_cf);
        snprintf(g.set.relay, sizeof(g.set.relay), "%s", ps.set_relay);
        snprintf(g.set.msa_client_id, sizeof(g.set.msa_client_id), "%s", ps.set_msa);
        g.set.mem_mb = LZ_CLAMP(atoi(ps.set_mem), 512, 65536);
        LeaveCriticalSection(&g_lock);
        app_save_settings();
        app_toast("设置已保存");
    }
    ui_text(Rc(sv.x + 180, sv.y + 22, w - 200, 40),
            "配置保存在 %APPDATA%\\Lazuli\\settings.ini", 10.5f, 0, C_TEXT3, 0);
    y += 84 + 14;

    ui_clip_pop();
    scroll_area(PG_SET, body, y);
}

/* ==========================================================================
 *  帧
 * ========================================================================== */
void pages_frame(float w, float h, int page) {
    draw_sidebar(w, h);
    draw_header(w, page);

    RcF body = Rc(238, 78, w - 238 - 22, h - 78 - 16);
    float pt = ui.page_t;
    int animating = (pt < 1.0f);
    ui_fast = animating;
    float ease = 1.0f - (1.0f - pt) * (1.0f - pt) * (1.0f - pt);   /* ease-out cubic */
    if (animating) body.y += (1.0f - ease) * 26.0f;
    switch (page) {
        case PG_HOME: page_home(body); break;
        case PG_VER:  page_ver(body);  break;
        case PG_DL:   page_dl(body);   break;
        case PG_MP:   page_mp(body);   break;
        case PG_SKIN: page_skin(body); break;
        case PG_BOX:  page_box(body);  break;
        case PG_SET:  page_set(body);  break;
        default: break;
    }

    ui_fast = 0;
    if (animating) {
        /* 旧页整屏叠在上面淡出，新页自然浮现；同时轻微上移制造推进感 */
        ui_blend_prev(1.0f - ease, -(ease * 26.0f));
    }
    if (g_md.active) md_overlay();
    if (vp_active) vp_overlay();
    auth_overlay();
    acct_overlay();
    ui_combo_popup();

    /* 提示条 */
    if (g.toast_t > 0) {
        float a = (float)LZ_CLAMP(g.toast_t, 0.0, 1.0);
        float tw = LZ_MIN(w - 120, 520.0f);
        RcF tr = Rc((w - tw) / 2, h - 62, tw, 42);
        ui_fill_round(tr, 21, LZ_ARGB((BYTE)(200 * a), 26, 32, 48));
        ui_stroke_round(tr, 21, 1.0f, LZ_ARGB((BYTE)(70 * a), 255, 255, 255));
        ui_text(tr, g.toast, 12.0f, 0, LZ_ARGB((BYTE)(255 * a), 255, 255, 255), 1);
    }
}

void pages_init(void) {
    memset(&ps, 0, sizeof(ps));
    ps.box_threads = 8;
    ps.ver_filter = 0;
    ps.mc_sel = 0;
    ps.ld_sel = -1;
    ps.lv_sel = 0;
    ps.exp_loader = -1;
    ps.ld_done = 0;
    ps.md_chip = 0;
    ps.last_page = -1;
    ps.md_for = -1;
    ps.myv_dirty = 1;
    combo.open_id = 0;
    snprintf(ps.box_dir, sizeof(ps.box_dir), "%s", g.set.game_dir);
    vpad_defaults();
    app_load_vpad();
    g.content_source = 0;
    g.content_kind = 0;
}

int ui_needs_anim(void) {
    if (ui.page_t < 1.0f) return 1;
    static int force = -1;
    if (force < 0) { char v[8]; force = GetEnvironmentVariableA("LZ_FORCE_ANIM", v, sizeof(v)) ? 1 : 0; }
    if (force) return 1;
    if (ui.focus_id) return 1;
    if (g.toast_t > 0) return 1;
    if (g.inst.active) return 1;
    if (g.content_state == 1 || g.manifest_state == 1) return 1;
    if (ps.q_pending) return 1;
    if (g.auth.state == AUTH_WAIT) return 1;
    if (g_room.active) return 1;
    if (g.game.running) return 1;
    for (int i = 0; i < g_ntasks; i++) {
        int st = dl_state(g_tasks[i]);
        if (st == 0 || st == 1) return 1;
    }
    return 0;
}

void pages_tick(void) {
    /* 页面切换动画推进 */
    if (ui.page_t < 1.0f) {
        DWORD now = GetTickCount();
        DWORD dt = now - ui.page_tick;
        ui.page_tick = now;
        ui.page_t += (float)dt / 200.0f;
        if (ui.page_t > 1.0f) ui.page_t = 1.0f;
    }
    /* 安装/运行时任务收尾：回收队列 */
    if (g.inst.finished) {
        EnterCriticalSection(&g_lock);
        FileQueue *q = g.inst.q;
        g.inst.q = NULL;
        g.inst.finished = 0;
        g.inst.active = 0;
        LeaveCriticalSection(&g_lock);
        if (q) fq_destroy(q);
        ps.myv_dirty = 1;
    }
    /* 缓存统计 */
    if (ps.cache_state == 1) {
        long long b = 0; int n = 0;
        char p[MAX_PATH * 3];
        lz_path_join(p, sizeof(p), g.set.game_dir, "logs");
        lz_dir_size(p, &b, &n);
        char p2[MAX_PATH * 3];
        lz_path_join(p2, sizeof(p2), g.set.game_dir, "crash-reports");
        int n2 = 0;
        lz_dir_size(p2, &b, &n2);
        ps.cache_bytes = b;
        ps.cache_files = n + n2;
        ps.cache_state = 2;
    }
    if (g.toast_t > 0) g.toast_t -= 0.1;
    ui.caret_t += 0.1;
    if (ps.box_dir[0] == 0) snprintf(ps.box_dir, sizeof(ps.box_dir), "%s", g.set.game_dir);
}
