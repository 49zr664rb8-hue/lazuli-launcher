/*  ui.h —— 液态玻璃渲染层 + 即时模式控件 */
#ifndef LZ_UI_H
#define LZ_UI_H

#include "common.h"
#include <propidl.h>
#include <gdiplus.h>

typedef struct { float x, y, w, h; } RcF;

#ifndef LZ_ARGB
#define LZ_ARGB(a,r,g,b) ((ARGB)(((DWORD)(a)<<24)|((DWORD)(r)<<16)|((DWORD)(g)<<8)|(DWORD)(b)))
#endif

/* ---------- 配色（深色玻璃 · 翡翠绿） ---------- */
#define C_TEXT      LZ_ARGB(255, 232, 238, 246)
#define C_TEXT2     LZ_ARGB(255, 154, 167, 184)
#define C_TEXT3     LZ_ARGB(255, 104, 116, 133)
#define C_ACCENT    LZ_ARGB(255, 61, 220, 132)
#define C_ACCENT2   LZ_ARGB(255, 110, 231, 168)
#define C_WHITE     LZ_ARGB(255, 255, 255, 255)
#define C_GLASS     LZ_ARGB(18, 255, 255, 255)
#define C_GLASS2    LZ_ARGB(26, 255, 255, 255)
#define C_BORDER    LZ_ARGB(26, 255, 255, 255)
#define C_GREEN     LZ_ARGB(255, 61, 220, 132)
#define C_RED       LZ_ARGB(255, 255, 107, 107)
#define C_ORANGE    LZ_ARGB(255, 255, 176, 32)
#define C_PURPLE    LZ_ARGB(255, 167, 139, 250)

/* 深色主题下的表面与叠加层 */
#define C_HOVER     LZ_ARGB(20, 255, 255, 255)   /* 行悬停 */
#define C_SEL       LZ_ARGB(34, 255, 255, 255)   /* 选中行 */
#define C_SEL_HI    LZ_ARGB(58, 61, 220, 132)    /* 选中高亮（绿） */
#define C_SURF      LZ_ARGB(252, 23, 29, 39)     /* 弹层底 */
#define C_SURF2     LZ_ARGB(252, 17, 22, 30)
#define C_FIELD     LZ_ARGB(255, 28, 35, 46)     /* 输入框底 */
#define C_ON_ACCENT LZ_ARGB(255, 6, 32, 20)      /* 绿底上的字 */

/* ---------- 按钮样式 ---------- */
enum { BTN_PRIMARY = 0, BTN_GHOST, BTN_DANGER, BTN_TEXT, BTN_TINT };

/* ---------- 状态 ---------- */
typedef struct {
    int   hot, press, click;
    float mx, my;
    int   down;
    int   focus_id;
    char *focus_buf;
    int   focus_cap;
    double caret_t;
    float scroll[10];
    float md_scroll;        /* 模组详情弹窗里版本列表的滚动 */
    float md_scroll_max;
    int   md_active;        /* 模组详情弹窗是否开着 */
    float scroll_max[10];
    int   page;
    int   anim;          /* 帧计数 */
    float page_t;        /* 页面切换进度 0..1 */
    DWORD page_tick;
} Ui;

extern Ui ui;
extern float g_ui_scale;

void ui_init(void);
void ui_shutdown(void);

void ui_frame_begin(HDC hdc, int pw, int ph);
void ui_frame_end(HDC hdc);

/* 绘制原语 */
void ui_fill_round(RcF r, float rad, ARGB c);
void ui_fill_round_grad(RcF r, float rad, ARGB top, ARGB bot);
void ui_stroke_round(RcF r, float rad, float w, ARGB c);
void ui_shadow(RcF r, float rad, float spread, int alpha);
void ui_text(RcF r, const char *utf8, float size, int bold, ARGB c, int align);
void ui_text_wrap(RcF r, const char *utf8, float size, int bold, ARGB c, int align);
void ui_text_icon(RcF r, unsigned cp, float size, ARGB c, int align);
void ui_line(float x1, float y1, float x2, float y2, float w, ARGB c);
void ui_circle(float cx, float cy, float rad, ARGB c);
void ui_circle_stroke(float cx, float cy, float rad, float w, ARGB c);
void ui_glass(RcF r, float rad);          /* 玻璃卡片（带投影，贵） */
void ui_panel(RcF r, float rad);          /* 轻量面板（无投影，列表用） */
extern int ui_fast;                       /* 1 = 动画中，跳过投影 */
void ui_glass_light(RcF r, float rad);
void ui_clip_push(RcF r);
void ui_clip_pop(void);
void ui_glow(float cx, float cy, float r, ARGB c);
void ui_cube(float cx, float cy, float s, float alpha_mul);

/* 下拉框状态 */
typedef struct {
    int  open_id;
    int  req_id;
    RcF  req_rect;
    const char **items;
    int  n;
    int *sel;
    float scroll;
} ComboState;
extern ComboState combo;
extern float ui_w, ui_h;

/* 控件 */
void *ui_image_load(const char *path_utf8);
void  ui_image_free(void *img);
void  ui_image_region(void *img, RcF dst, int sx, int sy, int sw, int sh);

int  ui_combo(int id, RcF r, const char **items, int n, int *sel, const char *ph);
void ui_combo_popup(void);
void ui_blend_prev(float alpha, float dy);
int  ui_button(int id, RcF r, const char *label, int style);
int  ui_button_icon(int id, RcF r, unsigned cp, const char *label, int style);
int  ui_hit(int id, RcF r);
int  ui_input(int id, RcF r, char *buf, int cap, const char *ph);
int  ui_chip(int id, RcF r, const char *label, int on);
void ui_progress(RcF r, float frac, ARGB col);
void ui_badge(RcF r, const char *label, ARGB bg, ARGB fg);
void ui_scrollbar(RcF track, float off, float max, float view);

/* 交互 */
void ui_mouse_move(float x, float y);
void ui_mouse_down(float x, float y);
void ui_mouse_up(float x, float y);
void ui_mouse_wheel(float delta);
void ui_release(void);
void ui_key(int vk);
void ui_char(wchar_t c);
void ui_set_page(int page);

float ui_scroll_y(void);
void  ui_scroll_reset(int page);

/* 页面 */
enum { PG_HOME = 0, PG_VER, PG_DL, PG_MP, PG_SKIN, PG_BOX, PG_SET, PG_COUNT };
void pages_frame(float w, float h, int page);
void pages_init(void);
void pages_tick(void);
int  ui_needs_anim(void);

#endif /* LZ_UI_H */
