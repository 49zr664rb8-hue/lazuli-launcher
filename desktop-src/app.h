/*  app.h —— 全局应用状态 */
#ifndef LZ_APP_H
#define LZ_APP_H

#include "common.h"
#include "net.h"

#define MAX_VERSIONS 2048
#define MAX_CONTENT  40

/* 版本条目 */
typedef struct {
    char id[64];
    char type[24];
    char url[400];
    char rel[24];
    int  installed;
} McVer;

/* 登录 */
typedef enum { AUTH_NONE = 0, AUTH_WAIT, AUTH_OK, AUTH_FAIL } AuthState;

typedef struct {
    AuthState state;
    char name[64];
    char uuid[40];
    char token[4096];
    char refresh[2048];
    char xuid[64];
    char device_code[1200];
    char user_code[32];
    char verify_uri[300];
    int  interval;
    int  expires;
    volatile int polling;
    char err[300];
} Auth;

/* 设置 */
typedef struct {
    char game_dir[400];
    char java_path[400];
    int  mem_mb;
    int  dl_threads;
    char cf_key[160];
    char relay[300];
    char terracotta[400];
    char msa_client_id[80];
    int  full_assets;
    int  vpad_scale;
    int  version_isolate;   /* 版本隔离：每个版本独立的 mods/saves/config */
    char last_version[64];  /* 上次启动的版本，用于模组下载定位 */
} Settings;

/* 账号本 */
#define MAX_ACCOUNTS 6
typedef struct {
    char name[64];
    char uuid[40];
    char xuid[64];
    char token[4096];
    char refresh[2048];
} Account;

typedef struct {
    Account items[MAX_ACCOUNTS];
    int     n;
    int     sel;      /* -1 表示没有选中 */
} AccountBook;

/* 皮肤 */
#define MAX_CAPES 8
typedef struct {
    volatile int state;      /* 0 空 1 加载中 2 就绪 3 失败 */
    char err[200];
    char png[400];           /* 本地皮肤文件 */
    int  model;              /* 0 classic 1 slim */
    int  ncapes;
    struct { char id[80]; char alias[64]; int active; } capes[MAX_CAPES];
} SkinState;

/* 下载中心条目 */
typedef struct {
    char id[96];
    char title[220];
    char desc[700];
    char author[96];
    char ver[80];
    long long downloads;
    char url[1024];
    char filename[200];
} ContentItem;

/* 安装进度 */
typedef struct {
    volatile int active;
    volatile int finished;
    FileQueue   *q;
    char version[64];
    char stage[128];
    char err[240];
    double t0;
} InstallState;

/* 游戏进程 */
typedef struct {
    volatile int running;
    char version[64];
    char log_path[400];
} GameState;

/* 虚拟按键 */
typedef struct {
    char label[16];
    int  keycode;      /* GLFW 键码 */
    float x, y;        /* 0..1 相对坐标 */
    float r;           /* 半径 0..1（相对屏幕短边） */
    int  is_stick;
} VButton;

#define MAX_VPAD 24

/* 日志行 */
#define MAX_LOG 200

typedef struct {
    McVer versions[MAX_VERSIONS];
    int   nversions;
    volatile int manifest_state;   /* 0 未开始 1 加载中 2 完成 3 失败 */
    char  manifest_err[240];

    Auth auth;
    AccountBook book;
    SkinState skin;
    Settings set;
    InstallState inst;
    GameState game;

    ContentItem content[MAX_CONTENT];
    int   ncontent;
    volatile int content_state;    /* 0 空闲 1 加载中 2 完成 3 失败 */
    char  content_err[240];
    char  content_query[160];
    char  content_note[200];
    int   content_source;          /* 0 Modrinth 1 CurseForge 2 MC百科 */
    int   content_kind;            /* 0 模组 1 光影 2 资源包 3 整合包 */

    char  toast[300];
    double toast_t;

    int   fortune;                 /* 0 未抽 */
    double fortune_t;

    VButton vpad[MAX_VPAD];
    int   nvpad;
    int   vpad_sel;

    char  sel_mc[40];              /* 下载页选中的 MC 版本 */
    int   sel_loader;              /* 选中的加载器 */
    char  sel_lver[64];            /* 选中的加载器版本 */

    char  status[300];
    int   online;                  /* 网络可用 */
} App;

extern App g;
extern HWND g_hwnd;
extern CRITICAL_SECTION g_lock;

/* ---- launcher.c ---- */
void mc_load_manifest_async(void);
void mc_refresh_installed(void);
int  mc_is_installed(const char *id);
void mc_install_async(const char *id);
void mc_launch_async(const char *id, const char *server, int port);
int  java_detect(char *out, size_t cap);
int  java_probe(const char *path, char *ver_out, size_t cap);
void java_install_async(int major);
int  mc_get_java_major(const char *id);
int  install_impl_public(const char *id, char *err, size_t errcap);
int  java_ensure(const char *version_id, char *out, size_t cap);

/* ---- auth.c ---- */
void auth_start(void);
void auth_cancel(void);
void auth_logout(void);
void auth_save(void);
void auth_load(void);

/* ---- mp.c ---- */
#define MAX_LAN 12
typedef struct {
    char motd[160];
    char addr[64];
    int  port;
    double t;
} LanWorld;

extern LanWorld g_lan[MAX_LAN];
extern int      g_nlan;

typedef struct {
    int  active;
    char code[16];
    char host[128];
    int  port;
    int  local_port;
    int  lan_port;
    volatile int tunnel_running;
    volatile int tunnel_stop;
    char msg[200];
    char err[200];
} Room;
extern Room g_room;

void mp_init(void);
void mp_scan_start(void);
void mp_create_room_async(int lan_port, const char *motd);
void mp_join_room_async(const char *code);
void mp_leave_room(void);

/* ---- skin.c ---- */
void skin_dir(char *out, size_t cap);
void skin_scan(void);
int  skin_wardrobe_count(void);
const char *skin_wardrobe_item(int i);
void skin_refresh_async(void);
void skin_upload_async(const char *path, int slim);
void skin_cape_async(const char *cape_id);
void skin_apply_local(const char *path, int slim);

/* ---- loader.c ---- */
#define MAX_LVERS 128
typedef struct { char ver[64]; char label[96]; } LoaderVer;

enum { LD_VANILLA = 0, LD_FABRIC, LD_QUILT, LD_FORGE, LD_NEOFORGE, LD_COUNT };

extern LoaderVer     g_lvers[MAX_LVERS];
extern int           g_nlvers;
extern volatile int  g_lvers_state;      /* 0 空闲 1 加载中 2 完成 3 失败 */
extern char          g_lvers_err[220];
extern volatile int  g_lvers_kind;
extern char          g_lvers_mc[40];

void        loader_list_async(int kind, const char *mcver);
void        loader_install_async(const char *mcver, int kind, const char *lver);
void        modpack_install_async(const char *url, const char *name);
int         loader_install_sync(const char *mc, int kind, const char *lver, char *err, size_t errcap);
const char *loader_name(int kind);
const char *loader_pretty(int kind, const char *mcver, const char *lver, char *out, size_t cap);
int         loader_version_id(int kind, const char *mcver, const char *lver, char *out, size_t cap);

/* ---- content.c ---- */
#define MAX_TASKS_DEF 48

/* 模组详情 */
#define MD_MAX_VERS 48
#define MD_MAX_DEPS 12

typedef struct {
    char id[64], name[90], ver[48], game[48], loader[28];
    char url[1024], fname[200];
    int  nreq;
    char req_id[8][96];
} MdVer;

typedef struct {
    char title[160], fname[200], url[1024], project[96];
    int  added;
} MdDep;

typedef struct {
    volatile int active;
    int  index;
    char project_id[96], title[220], desc[760], author[96];
    long long downloads;
    volatile int state;       /* 0 空 1 加载中 2 就绪 3 失败 */
    char err[220];
    MdVer vers[MD_MAX_VERS];
    int  nvers, sel;
    volatile int dep_state;   /* 0 无 1 解析中 2 就绪 3 失败 */
    MdDep deps[MD_MAX_DEPS];
    int  ndeps;
    volatile int dep_done;
    volatile int auto_dl;
} ModDetail;

extern ModDetail g_md;

extern int g_mdtest;
void md_open(int content_index);
void md_close(void);
void md_resolve_deps(void);
void md_download_all(void);
extern DlTask *g_tasks[MAX_TASKS_DEF];
extern int     g_ntasks;
void tasks_add(DlTask *t);

void content_search_async(const char *query, int source, int kind);
void content_download(int index);

/* ---- util 扩展 ---- */
void lz_fmt_size(char *out, size_t cap, double bytes);
void lz_fmt_count(char *out, size_t cap, long long n);
void ui_request_redraw(void);
void win_minimize(void);
void win_maximize(void);
void win_close(void);
void win_begin_drag(void);
void win_open_folder(const char *path);
int  win_pick_file(char *out, size_t cap, const char *filter);
void app_toast(const char *fmt, ...);
void app_status(const char *fmt, ...);
void app_log(const char *fmt, ...);
void app_save_accounts(void);
void app_load_accounts(void);
void app_save_settings(void);
void app_load_settings(void);
void app_save_vpad(void);
void app_load_vpad(void);

#endif /* LZ_APP_H */
