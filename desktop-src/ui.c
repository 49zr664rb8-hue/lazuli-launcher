/*  ui.c —— 液态玻璃渲染层 */
#include "ui.h"
#include <math.h>

Ui    ui;
float g_ui_scale = 1.0f;
float ui_w = 1280.0f, ui_h = 820.0f;
int   ui_fast = 0;
ComboState combo;

static RcF Rc(float x, float y, float w, float h) { RcF r = { x, y, w, h }; return r; }

static ULONG_PTR        g_token = 0;
static GpGraphics      *g_g = NULL;
static HDC              g_memdc = NULL;
static HBITMAP          g_dib = NULL;
static void            *g_bits = NULL;
static int              g_bw = 0, g_bh = 0;
static HBITMAP          g_prev_dib = NULL;
static HDC              g_prev_dc = NULL;
static void            *g_prev_bits = NULL;
static int              g_prev_w = 0, g_prev_h = 0;
static HBITMAP          g_bg_dib = NULL;
static HDC              g_bg_dc = NULL;
static void            *g_bg_bits = NULL;
static int              g_bg_w = 0, g_bg_h = 0;

/* ---------------- 命中区 ---------------- */
typedef struct { int id; RcF r; } Hit;
static Hit g_hits[768];
static int g_nhits = 0;

/* ---------------- 字体缓存 ---------------- */
#define MAX_FONTS 48
typedef struct { GpFont *f; float size; int bold; } FontEnt;
static FontEnt g_fonts[MAX_FONTS];
static int     g_nfonts = 0;
static GpFontFamily *g_fam = NULL;
static GpFontFamily *g_fam_icon = NULL;

static GpStringFormat *g_fmt_sl[3];   /* 单行 */
static GpStringFormat *g_fmt_ml[3];   /* 多行 */

/* ==========================================================================
 *  初始化
 * ========================================================================== */
void ui_init(void) {
    GdiplusStartupInput in;
    in.GdiplusVersion = 1;
    in.DebugEventCallback = NULL;
    in.SuppressBackgroundThread = FALSE;
    in.SuppressExternalCodecs = FALSE;
    GdiplusStartup(&g_token, &in, NULL);

    /* 字体族：优先中文界面字体 */
    const wchar_t *cands[] = { L"Microsoft YaHei UI", L"Microsoft YaHei", L"Segoe UI" };
    for (int i = 0; i < 3 && !g_fam; i++) {
        GpFontFamily *f = NULL;
        if (GdipCreateFontFamilyFromName(cands[i], NULL, &f) == Ok && f) g_fam = f;
    }
    const wchar_t *ic[] = { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" };
    for (int i = 0; i < 2 && !g_fam_icon; i++) {
        GpFontFamily *f = NULL;
        if (GdipCreateFontFamilyFromName(ic[i], NULL, &f) == Ok && f) g_fam_icon = f;
    }

    for (int a = 0; a < 3; a++) {
        GdipCreateStringFormat(0, LANG_NEUTRAL, &g_fmt_sl[a]);
        GdipSetStringFormatAlign(g_fmt_sl[a],
            a == 0 ? StringAlignmentNear : (a == 1 ? StringAlignmentCenter : StringAlignmentFar));
        GdipSetStringFormatLineAlign(g_fmt_sl[a], StringAlignmentCenter);
        GdipSetStringFormatFlags(g_fmt_sl[a], StringFormatFlagsNoWrap);
        GdipSetStringFormatTrimming(g_fmt_sl[a], StringTrimmingEllipsisCharacter);

        GdipCreateStringFormat(0, LANG_NEUTRAL, &g_fmt_ml[a]);
        GdipSetStringFormatAlign(g_fmt_ml[a],
            a == 0 ? StringAlignmentNear : (a == 1 ? StringAlignmentCenter : StringAlignmentFar));
        GdipSetStringFormatLineAlign(g_fmt_ml[a], StringAlignmentNear);
        GdipSetStringFormatTrimming(g_fmt_ml[a], StringTrimmingEllipsisCharacter);
    }
    memset(&ui, 0, sizeof(ui));
    ui.focus_id = 0;
}

void ui_shutdown(void) {
    for (int i = 0; i < g_nfonts; i++) if (g_fonts[i].f) GdipDeleteFont(g_fonts[i].f);
    g_nfonts = 0;
    if (g_fam) GdipDeleteFontFamily(g_fam);
    if (g_fam_icon) GdipDeleteFontFamily(g_fam_icon);
    for (int a = 0; a < 3; a++) {
        if (g_fmt_sl[a]) GdipDeleteStringFormat(g_fmt_sl[a]);
        if (g_fmt_ml[a]) GdipDeleteStringFormat(g_fmt_ml[a]);
    }
    if (g_prev_dib) DeleteObject(g_prev_dib);
    if (g_prev_dc) DeleteDC(g_prev_dc);
    if (g_bg_dib) DeleteObject(g_bg_dib);
    if (g_bg_dc) DeleteDC(g_bg_dc);
    if (g_dib) DeleteObject(g_dib);
    if (g_memdc) DeleteDC(g_memdc);
    GdiplusShutdown(g_token);
}

static GpFont *font_get(float size, int bold) {
    for (int i = 0; i < g_nfonts; i++)
        if (g_fonts[i].size == size && g_fonts[i].bold == bold) return g_fonts[i].f;
    if (g_nfonts >= MAX_FONTS) return g_fonts[0].f;
    GpFont *f = NULL;
    GdipCreateFont(g_fam, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel, &f);
    g_fonts[g_nfonts].f = f;
    g_fonts[g_nfonts].size = size;
    g_fonts[g_nfonts].bold = bold;
    g_nfonts++;
    return f;
}

/* ==========================================================================
 *  背景（彩色光斑，缓存为位图）
 * ========================================================================== */
static void add_ellipse(GpPath *p, float cx, float cy, float rx, float ry) {
    GdipAddPathEllipse(p, cx - rx, cy - ry, rx * 2, ry * 2);
}

static void blob(GpGraphics *g, float cx, float cy, float r, ARGB c) {
    GpPath *p = NULL;
    GdipCreatePath(FillModeAlternate, &p);
    add_ellipse(p, cx, cy, r, r * 0.92f);
    GpPathGradient *br = NULL;
    if (GdipCreatePathGradientFromPath(p, &br) != Ok) { GdipDeletePath(p); return; }
    /* 边缘完全透明，中心为原色 —— 形成柔和的径向光斑 */
    ARGB sur = LZ_ARGB(0, (BYTE)(c >> 16), (BYTE)(c >> 8), (BYTE)c);
    int cnt = 1;
    GdipSetPathGradientSurroundColorsWithCount(br, &sur, &cnt);
    GdipSetPathGradientCenterColor(br, c);
    GpPointF center = { cx, cy };
    GdipSetPathGradientCenterPoint(br, &center);
    GdipFillPath(g, (GpBrush *)br, p);
    GdipDeleteBrush((GpBrush *)br);
    GdipDeletePath(p);
}

static void bg_rebuild(int w, int h) {
    if (g_bg_dib && g_bg_w == w && g_bg_h == h) return;
    if (g_bg_dc) { DeleteDC(g_bg_dc); g_bg_dc = NULL; }
    if (g_bg_dib) { DeleteObject(g_bg_dib); g_bg_dib = NULL; }
    HDC screen = GetDC(NULL);
    g_bg_dc = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_bg_dib = CreateDIBSection(g_bg_dc, &bi, DIB_RGB_COLORS, &g_bg_bits, NULL, 0);
    SelectObject(g_bg_dc, g_bg_dib);
    ReleaseDC(NULL, screen);
    g_bg_w = w; g_bg_h = h;
    if (!g_bg_dib) return;

    GpGraphics *g = NULL;
    GdipCreateFromHDC(g_bg_dc, &g);
    if (!g) return;
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(g, PixelOffsetModeHighQuality);

    /* 底色渐变 */
    GpRectF full = { 0, 0, (float)w, (float)h };
    GpLineGradient *lg = NULL;
    GdipCreateLineBrushFromRect(&full, LZ_ARGB(255, 11, 16, 23), LZ_ARGB(255, 21, 28, 40),
                                LinearGradientModeVertical, WrapModeTile, &lg);
    GpSolidFill *sf = NULL;
    GdipCreateSolidFill(LZ_ARGB(255, 255, 255, 255), &sf);
    GdipFillRectangle(g, (GpBrush *)lg, 0, 0, (float)w, (float)h);
    GdipDeleteBrush((GpBrush *)lg);
    GdipDeleteBrush((GpBrush *)sf);

    float s = (float)h / 820.0f;
    blob(g, w * 0.02f, h * 0.04f, 700 * s, LZ_ARGB(86, 24, 190, 118));
    blob(g, w * 0.98f, h * 0.02f, 640 * s, LZ_ARGB(64, 40, 96, 210));
    blob(g, w * 0.88f, h * 1.02f, 700 * s, LZ_ARGB(52, 30, 120, 190));

    GdipDeleteGraphics(g);
}

/* ==========================================================================
 *  帧
 * ========================================================================== */
static void ensure_buffer(int w, int h) {
    if (g_dib && g_bw == w && g_bh == h) return;
    if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
    if (g_memdc) { DeleteDC(g_memdc); g_memdc = NULL; }
    HDC screen = GetDC(NULL);
    g_memdc = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_dib = CreateDIBSection(g_memdc, &bi, DIB_RGB_COLORS, &g_bits, NULL, 0);
    SelectObject(g_memdc, g_dib);
    ReleaseDC(NULL, screen);
    g_bw = w; g_bh = h;
}

void ui_frame_begin(HDC hdc, int pw, int ph) {
    LZ_UNUSED(hdc);
    ui_w = (float)pw / g_ui_scale;
    ui_h = (float)ph / g_ui_scale;
    ensure_buffer(pw, ph);
    g_nhits = 0;
    GdipCreateFromHDC(g_memdc, &g_g);
    GdipSetSmoothingMode(g_g, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(g_g, PixelOffsetModeDefault);
    GdipSetInterpolationMode(g_g, InterpolationModeDefault);
    GdipSetCompositingQuality(g_g, CompositingQualityDefault);
    GdipSetTextRenderingHint(g_g, TextRenderingHintAntiAlias);

    bg_rebuild(pw, ph);
    if (g_bg_dc) BitBlt(g_memdc, 0, 0, pw, ph, g_bg_dc, 0, 0, SRCCOPY);
    GdipScaleWorldTransform(g_g, g_ui_scale, g_ui_scale, MatrixOrderPrepend);
}

void ui_frame_end(HDC hdc) {
    if (g_g) { GdipDeleteGraphics(g_g); g_g = NULL; }
    BitBlt(hdc, 0, 0, g_bw, g_bh, g_memdc, 0, 0, SRCCOPY);

    /* 保存这一帧，供下次切页做交叉淡入 */
    if (!g_prev_dib || g_prev_w != g_bw || g_prev_h != g_bh) {
        if (g_prev_dc) { DeleteDC(g_prev_dc); g_prev_dc = NULL; }
        if (g_prev_dib) { DeleteObject(g_prev_dib); g_prev_dib = NULL; }
        HDC scr = GetDC(NULL);
        g_prev_dc = CreateCompatibleDC(scr);
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = g_bw;
        bi.bmiHeader.biHeight = -g_bh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        g_prev_dib = CreateDIBSection(g_prev_dc, &bi, DIB_RGB_COLORS, &g_prev_bits, NULL, 0);
        SelectObject(g_prev_dc, g_prev_dib);
        ReleaseDC(NULL, scr);
        g_prev_w = g_bw;
        g_prev_h = g_bh;
    }
    if (g_prev_dc) BitBlt(g_prev_dc, 0, 0, g_bw, g_bh, g_memdc, 0, 0, SRCCOPY);
}

/* 把上一帧整屏叠上来（alpha 0..1），用来做交叉淡入 */
void ui_blend_prev(float alpha, float dy) {
    if (!g_prev_dc || !g_bw) return;
    int a = (int)(alpha * 255.0f);
    if (a <= 0) return;
    if (a > 255) a = 255;
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = (BYTE)a;
    bf.AlphaFormat = 0;
    AlphaBlend(g_memdc, 0, (int)dy, g_bw, g_bh, g_prev_dc, 0, 0, g_bw, g_bh, bf);
}

/* ==========================================================================
 *  绘制原语
 * ========================================================================== */
static void add_round(GpPath *p, RcF r, float rad) {
    float d = rad * 2.0f;
    if (d > r.w) d = r.w;
    if (d > r.h) d = r.h;
    if (d <= 0.5f) {
        GdipAddPathRectangle(p, r.x, r.y, r.w, r.h);
        return;
    }
    GdipAddPathArc(p, r.x, r.y, d, d, 180, 90);
    GdipAddPathArc(p, r.x + r.w - d, r.y, d, d, 270, 90);
    GdipAddPathArc(p, r.x + r.w - d, r.y + r.h - d, d, d, 0, 90);
    GdipAddPathArc(p, r.x, r.y + r.h - d, d, d, 90, 90);
    GdipClosePathFigure(p);
}

void ui_fill_round(RcF r, float rad, ARGB c) {
    if (r.w <= 0 || r.h <= 0) return;
    GpPath *p = NULL;
    GdipCreatePath(FillModeAlternate, &p);
    add_round(p, r, rad);
    GpSolidFill *b = NULL;
    GdipCreateSolidFill(c, &b);
    GdipFillPath(g_g, (GpBrush *)b, p);
    GdipDeleteBrush((GpBrush *)b);
    GdipDeletePath(p);
}

void ui_fill_round_grad(RcF r, float rad, ARGB top, ARGB bot) {
    if (r.w <= 0 || r.h <= 0) return;
    GpPath *p = NULL;
    GdipCreatePath(FillModeAlternate, &p);
    add_round(p, r, rad);
    GpRectF rr = { r.x, r.y, r.w, r.h };
    GpLineGradient *b = NULL;
    GdipCreateLineBrushFromRect(&rr, top, bot, LinearGradientModeVertical, WrapModeTile, &b);
    GdipFillPath(g_g, (GpBrush *)b, p);
    GdipDeleteBrush((GpBrush *)b);
    GdipDeletePath(p);
}

void ui_stroke_round(RcF r, float rad, float w, ARGB c) {
    GpPath *p = NULL;
    GdipCreatePath(FillModeAlternate, &p);
    RcF rr = { r.x + w * 0.5f, r.y + w * 0.5f, r.w - w, r.h - w };
    add_round(p, rr, rad);
    GpPen *pen = NULL;
    GdipCreatePen1(c, w, UnitPixel, &pen);
    GdipDrawPath(g_g, pen, p);
    GdipDeletePen(pen);
    GdipDeletePath(p);
}

void ui_shadow(RcF r, float rad, float spread, int alpha) {
    /* 多层叠加：卡片本体覆盖中心，只露出外圈的柔和投影。
       投影本身很淡，关掉抗锯齿可省掉大量逐像素混合开销。 */
    if (ui_fast) return;
    GdipSetSmoothingMode(g_g, SmoothingModeNone);
    for (int i = 2; i >= 1; i--) {
        float t = (float)i / 2.0f;
        RcF s = { r.x - spread * t, r.y - spread * t * 0.5f + 2.0f,
                  r.w + spread * t * 2, r.h + spread * t * 2 };
        ui_fill_round(s, rad + spread * t * 0.35f, LZ_ARGB(alpha, 0, 0, 0));
    }
    GdipSetSmoothingMode(g_g, SmoothingModeAntiAlias);
}

int g_no_text = -1;
static int no_text(void) {
    if (g_no_text < 0) { char v[8]; g_no_text = GetEnvironmentVariableA("LZ_NO_TEXT", v, sizeof(v)) ? 1 : 0; }
    return g_no_text;
}

void ui_text(RcF r, const char *utf8, float size, int bold, ARGB c, int align) {
    if (no_text()) return;
    if (!utf8 || !*utf8) return;
    wchar_t *w = lz_u8_to_w(utf8);
    if (!w) return;
    GpFont *f = font_get(size, bold);
    GpSolidFill *b = NULL;
    GdipCreateSolidFill(c, &b);
    GpRectF rr = { r.x, r.y, r.w, r.h };
    GdipDrawString(g_g, w, -1, f, &rr, g_fmt_sl[LZ_CLAMP(align, 0, 2)], (GpBrush *)b);
    GdipDeleteBrush((GpBrush *)b);
    free(w);
}

void ui_text_wrap(RcF r, const char *utf8, float size, int bold, ARGB c, int align) {
    if (no_text()) return;
    if (!utf8 || !*utf8) return;
    wchar_t *w = lz_u8_to_w(utf8);
    if (!w) return;
    GpFont *f = font_get(size, bold);
    GpSolidFill *b = NULL;
    GdipCreateSolidFill(c, &b);
    GpRectF rr = { r.x, r.y, r.w, r.h };
    GdipDrawString(g_g, w, -1, f, &rr, g_fmt_ml[LZ_CLAMP(align, 0, 2)], (GpBrush *)b);
    GdipDeleteBrush((GpBrush *)b);
    free(w);
}

static GpFont *icon_font_get(float size) {
    static GpFont *cache[8];
    static float sz[8];
    static int n = 0;
    for (int i = 0; i < n; i++) if (sz[i] == size) return cache[i];
    if (n >= 8) return cache[0];
    GpFont *f = NULL;
    GdipCreateFont(g_fam_icon ? g_fam_icon : g_fam, size, FontStyleRegular, UnitPixel, &f);
    cache[n] = f; sz[n] = size; n++;
    return f;
}

void ui_text_icon(RcF r, unsigned cp, float size, ARGB c, int align) {
    if (no_text()) return;
    wchar_t w[2] = { (wchar_t)cp, 0 };
    GpFont *f = icon_font_get(size);
    GpSolidFill *b = NULL;
    GdipCreateSolidFill(c, &b);
    GpRectF rr = { r.x, r.y, r.w, r.h };
    GdipDrawString(g_g, w, -1, f, &rr, g_fmt_sl[LZ_CLAMP(align, 0, 2)], (GpBrush *)b);
    GdipDeleteBrush((GpBrush *)b);
}

void ui_line(float x1, float y1, float x2, float y2, float w, ARGB c) {
    GpPen *pen = NULL;
    GdipCreatePen1(c, w, UnitPixel, &pen);
    GdipDrawLine(g_g, pen, x1, y1, x2, y2);
    GdipDeletePen(pen);
}

void ui_circle(float cx, float cy, float rad, ARGB c) {
    GpSolidFill *b = NULL;
    GdipCreateSolidFill(c, &b);
    GdipFillEllipse(g_g, (GpBrush *)b, cx - rad, cy - rad, rad * 2, rad * 2);
    GdipDeleteBrush((GpBrush *)b);
}

void ui_circle_stroke(float cx, float cy, float rad, float w, ARGB c) {
    GpPen *pen = NULL;
    GdipCreatePen1(c, w, UnitPixel, &pen);
    GdipDrawEllipse(g_g, pen, cx - rad, cy - rad, rad * 2, rad * 2);
    GdipDeletePen(pen);
}

void ui_glass(RcF r, float rad) {
    ui_shadow(r, rad, 26.0f, 60);
    ui_fill_round_grad(r, rad, LZ_ARGB(30, 255, 255, 255), LZ_ARGB(14, 255, 255, 255));
    ui_stroke_round(r, rad, 1.0f, LZ_ARGB(30, 255, 255, 255));
    /* 顶部高光 */
    ui_line(r.x + rad * 0.7f, r.y + 1.2f, r.x + r.w - rad * 0.7f, r.y + 1.2f,
            1.0f, LZ_ARGB(230, 255, 255, 255));
    /* 底部内阴影 */
    ui_line(r.x + rad * 0.7f, r.y + r.h - 1.2f, r.x + r.w - rad * 0.7f, r.y + r.h - 1.2f,
            1.0f, LZ_ARGB(26, 90, 110, 160));
}

void ui_panel(RcF r, float rad) {
    ui_fill_round_grad(r, rad, LZ_ARGB(26, 255, 255, 255), LZ_ARGB(12, 255, 255, 255));
    ui_stroke_round(r, rad, 1.0f, LZ_ARGB(26, 255, 255, 255));
}

void ui_glass_light(RcF r, float rad) {
    ui_fill_round(r, rad, LZ_ARGB(96, 255, 255, 255));
    ui_stroke_round(r, rad, 1.0f, LZ_ARGB(150, 255, 255, 255));
}

/* ==========================================================================
 *  命中
 * ========================================================================== */
int ui_hit(int id, RcF r) {
    if (g_nhits < 768) { g_hits[g_nhits].id = id; g_hits[g_nhits].r = r; g_nhits++; }
    return ui.click == id;
}

static int hit_test(float x, float y) {
    for (int i = g_nhits - 1; i >= 0; i--) {
        RcF r = g_hits[i].r;
        if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) return g_hits[i].id;
    }
    return 0;
}

static int is_hover(RcF r) {
    return ui.mx >= r.x && ui.mx <= r.x + r.w && ui.my >= r.y && ui.my <= r.y + r.h;
}

/* ==========================================================================
 *  控件
 * ========================================================================== */
int ui_button(int id, RcF r, const char *label, int style) {
    int hov = is_hover(r);
    int pre = (ui.press == id) && hov;
    float dy = pre ? 0.6f : 0.0f;

    if (style == BTN_PRIMARY) {
        ui_shadow(r, r.h * 0.42f, 10.0f, 80);
        ui_fill_round_grad((RcF){ r.x, r.y + dy, r.w, r.h },
                           r.h * 0.42f,
                           hov ? LZ_ARGB(255, 96, 238, 160) : LZ_ARGB(255, 74, 228, 146),
                           hov ? LZ_ARGB(255, 40, 198, 122) : LZ_ARGB(255, 28, 178, 108));
        ui_stroke_round((RcF){ r.x, r.y + dy, r.w, r.h }, r.h * 0.42f, 1.0f, LZ_ARGB(70, 255, 255, 255));
        ui_text((RcF){ r.x, r.y + dy, r.w, r.h }, label, 14.0f, 1, C_ON_ACCENT, 1);
    } else if (style == BTN_DANGER) {
        ui_shadow(r, r.h * 0.42f, 8.0f, 60);
        ui_fill_round_grad((RcF){ r.x, r.y + dy, r.w, r.h }, r.h * 0.42f,
                           hov ? LZ_ARGB(255, 255, 122, 122) : LZ_ARGB(255, 240, 90, 96),
                           hov ? LZ_ARGB(255, 226, 70, 80) : LZ_ARGB(255, 206, 58, 70));
        ui_text((RcF){ r.x, r.y + dy, r.w, r.h }, label, 14.0f, 1, LZ_ARGB(255, 255, 255, 255), 1);
    } else if (style == BTN_GHOST) {
        ui_fill_round((RcF){ r.x, r.y + dy, r.w, r.h }, r.h * 0.42f,
                      hov ? LZ_ARGB(40, 255, 255, 255) : LZ_ARGB(20, 255, 255, 255));
        ui_stroke_round((RcF){ r.x, r.y + dy, r.w, r.h }, r.h * 0.42f, 1.0f,
                        hov ? LZ_ARGB(46, 255, 255, 255) : LZ_ARGB(30, 255, 255, 255));
        ui_text((RcF){ r.x, r.y + dy, r.w, r.h }, label, 13.5f, 0, C_TEXT, 1);
    } else if (style == BTN_TINT) {
        ui_fill_round((RcF){ r.x, r.y + dy, r.w, r.h }, r.h * 0.42f,
                      hov ? LZ_ARGB(70, 61, 220, 132) : LZ_ARGB(44, 61, 220, 132));
        ui_stroke_round((RcF){ r.x, r.y + dy, r.w, r.h }, r.h * 0.42f, 1.0f,
                        LZ_ARGB(90, 61, 220, 132));
        ui_text((RcF){ r.x, r.y + dy, r.w, r.h }, label, 13.5f, 1, C_ACCENT, 1);
    } else { /* BTN_TEXT */
        if (hov) ui_fill_round((RcF){ r.x, r.y, r.w, r.h }, r.h * 0.35f, LZ_ARGB(46, 255, 255, 255));
        ui_text(r, label, 13.0f, 0, C_ACCENT, 1);
    }

    int clicked = ui_hit(id, r);
    if (clicked) ui.click = 0;
    return clicked;
}

int ui_button_icon(int id, RcF r, unsigned cp, const char *label, int style) {
    int hov = is_hover(r);
    int pre = (ui.press == id) && hov;
    float dy = pre ? 0.6f : 0.0f;
    ARGB bg = (style == BTN_PRIMARY) ? LZ_ARGB(255, 10, 118, 245) : LZ_ARGB(150, 255, 255, 255);
    ui_fill_round((RcF){ r.x, r.y + dy, r.w, r.h }, r.h * 0.4f,
                  hov ? (style == BTN_PRIMARY ? LZ_ARGB(255, 30, 140, 255) : LZ_ARGB(230, 255, 255, 255)) : bg);
    ARGB fg = (style == BTN_PRIMARY) ? LZ_ARGB(255, 255, 255, 255) : C_TEXT;
    if (label && *label) {
        ui_text_icon((RcF){ r.x + 14, r.y + dy, 20, r.h }, cp, 15.0f, fg, 0);
        ui_text((RcF){ r.x + 36, r.y + dy, r.w - 48, r.h }, label, 13.5f, 0, fg, 0);
    } else {
        ui_text_icon((RcF){ r.x, r.y + dy, r.w, r.h }, cp, 15.0f, fg, 1);
    }
    int clicked = ui_hit(id, r);
    if (clicked) ui.click = 0;
    return clicked;
}

int ui_input(int id, RcF r, char *buf, int cap, const char *ph) {
    int focused = (ui.focus_id == id);
    ui_fill_round(r, r.h * 0.34f, focused ? LZ_ARGB(255, 32, 41, 54) : C_FIELD);
    ui_stroke_round(r, r.h * 0.34f, focused ? 1.6f : 1.0f,
                    focused ? C_ACCENT : LZ_ARGB(34, 255, 255, 255));

    RcF tr = { r.x + 14, r.y, r.w - 28, r.h };
    if (buf[0]) {
        ui_text(tr, buf, 13.5f, 0, C_TEXT, 0);
    } else if (ph) {
        ui_text(tr, ph, 13.5f, 0, C_TEXT3, 0);
    }
    if (focused) {
        /* 光标 */
        wchar_t *ww = lz_u8_to_w(buf);
        GpFont *f = font_get(13.5f, 0);
        GpStringFormat *fmt = NULL;
        GdipCreateStringFormat(0, LANG_NEUTRAL, &fmt);
        GdipSetStringFormatFlags(fmt, StringFormatFlagsNoWrap);
        float wsum = 0;
        if (ww && ww[0]) {
            GpRectF one = { 0, 0, 4000, r.h };
            GpRectF out = { 0, 0, 0, 0 };
            GdipMeasureString(g_g, ww, -1, f, &one, fmt, &out, NULL, NULL);
            wsum = out.Width;
        }
        GdipDeleteStringFormat(fmt);
        free(ww);
        if (fmod(ui.caret_t * 2.0, 2.0) < 1.15) {
            float cx = tr.x + LZ_MIN(wsum, r.w - 34);
            ui_line(cx + 1, r.y + r.h * 0.24f, cx + 1, r.y + r.h * 0.76f, 1.4f,
                    LZ_ARGB(200, 30, 120, 255));
        }
    }

    if (ui.click == id) {
        ui.focus_id = id;
        ui.focus_buf = buf;
        ui.focus_cap = cap;
        ui.caret_t = 0;
        ui.click = 0;
    }
    ui_hit(id, r);
    return focused;
}

int ui_combo(int id, RcF r, const char **items, int n, int *sel, const char *ph) {
    int open = (combo.open_id == id);
    int hov = is_hover(r);
    ui_fill_round(r, r.h * 0.34f, open ? LZ_ARGB(255, 34, 42, 56) : (hov ? LZ_ARGB(255, 30, 37, 49) : LZ_ARGB(255, 25, 31, 42)));
    ui_stroke_round(r, r.h * 0.34f, open ? 1.6f : 1.0f,
                    open ? LZ_ARGB(255, 61, 220, 132) : LZ_ARGB(40, 255, 255, 255));

    const char *txt = NULL;
    if (items && n > 0 && *sel >= 0 && *sel < n) txt = items[*sel];
    ui_text(Rc(r.x + 12, r.y, r.w - 34, r.h), txt ? txt : (ph ? ph : "请选择"),
            12.5f, txt ? 0 : 0, txt ? C_TEXT : C_TEXT3, 0);

    float cx = r.x + r.w - 15, cy = r.y + r.h * 0.5f;
    ARGB ac = open ? LZ_ARGB(255, 20, 120, 235) : LZ_ARGB(255, 110, 120, 140);
    ui_line(cx - 4, cy - 2, cx, cy + 2.4f, 1.6f, ac);
    ui_line(cx, cy + 2.4f, cx + 4, cy - 2, 1.6f, ac);

    if (ui_hit(id, r)) {
        ui.click = 0;
        if (open) {
            combo.open_id = 0;
        } else {
            combo.open_id = id;
            combo.scroll = 0;
            /* 让当前选中项落在可见区域 */
            if (*sel > 3) combo.scroll = LZ_MIN((float)(*sel - 3) * 32.0f,
                                                LZ_MAX(0.0f, (float)n * 32.0f - 268.0f));
        }
    }
    /* 用"点击之后"的状态判断，保证同一帧就能把弹层画出来 */
    if (combo.open_id == id) {
        combo.req_id = id;
        combo.req_rect = r;
        combo.items = items;
        combo.n = n;
        combo.sel = sel;
    }
    return 0;
}

void ui_combo_popup(void) {
    if (!combo.open_id || combo.req_id != combo.open_id) return;
    RcF a = combo.req_rect;
    int n = combo.n;
    if (n <= 0) { combo.open_id = 0; return; }

    const float itemH = 32.0f;
    float listH = (float)n * itemH;
    float boxH = LZ_MIN(listH + 12.0f, 280.0f);
    float y = a.y + a.h + 6.0f;
    if (y + boxH > ui_h - 12.0f) y = LZ_MAX(10.0f, a.y - boxH - 6.0f);
    RcF p = Rc(a.x, y, LZ_MAX(a.w, 180.0f), boxH);

    /* 先铺一层全屏拦截，点空白处收起 */
    ui_hit(999999, Rc(0, 0, ui_w, ui_h));

    ui_shadow(p, 14, 22, 26);
    ui_fill_round(p, 14, C_SURF);
    ui_stroke_round(p, 14, 1.0f, LZ_ARGB(34, 255, 255, 255));

    float maxScroll = LZ_MAX(0.0f, listH - (boxH - 12.0f));
    combo.scroll = LZ_CLAMP(combo.scroll, 0.0f, maxScroll);

    ui_clip_push(Rc(p.x + 2, p.y + 4, p.w - 4, p.h - 8));
    float iy = p.y + 6.0f - combo.scroll;
    for (int i = 0; i < n; i++) {
        RcF ir = Rc(p.x + 5, iy, p.w - 10, itemH - 2);
        if (iy + itemH > p.y && iy < p.y + p.h) {
            int hov = is_hover(ir);
            int on = (combo.sel && *combo.sel == i);
            if (on) ui_fill_round(ir, 9, C_SEL_HI);
            else if (hov) ui_fill_round(ir, 9, C_HOVER);
            ui_text(Rc(ir.x + 12, ir.y, ir.w - 24, ir.h), combo.items[i], 12.0f,
                    on ? 1 : 0, on ? C_ACCENT : C_TEXT, 0);
            if (ui_hit(970000 + i, ir)) {
                ui.click = 0;
                if (combo.sel) *combo.sel = i;
                combo.open_id = 0;
            }
        }
        iy += itemH;
    }
    ui_clip_pop();
    if (maxScroll > 0)
        ui_scrollbar(Rc(p.x + p.w - 7, p.y + 6, 3.5f, p.h - 12),
                     combo.scroll, maxScroll, boxH - 12);

    if (ui.click == 999999) { ui.click = 0; combo.open_id = 0; }
}

int ui_chip(int id, RcF r, const char *label, int on) {
    int hov = is_hover(r);
    if (on) {
        ui_fill_round_grad(r, r.h * 0.5f, LZ_ARGB(255, 74, 228, 146), LZ_ARGB(255, 28, 178, 108));
        ui_text(r, label, 12.5f, 1, C_ON_ACCENT, 1);
    } else {
        ui_fill_round(r, r.h * 0.5f, hov ? LZ_ARGB(38, 255, 255, 255) : LZ_ARGB(18, 255, 255, 255));
        ui_stroke_round(r, r.h * 0.5f, 1.0f, LZ_ARGB(34, 255, 255, 255));
        ui_text(r, label, 12.5f, 0, C_TEXT2, 1);
    }
    int clicked = ui_hit(id, r);
    if (clicked) ui.click = 0;
    return clicked;
}

void ui_progress(RcF r, float frac, ARGB col) {
    frac = LZ_CLAMP(frac, 0.0f, 1.0f);
    ui_fill_round(r, r.h * 0.5f, LZ_ARGB(70, 120, 140, 180));
    if (frac > 0.001f) {
        RcF f = { r.x, r.y, LZ_MAX(r.h, r.w * frac), r.h };
        ui_fill_round_grad(f, r.h * 0.5f, col, LZ_ARGB((BYTE)(col >> 24),
                           (BYTE)LZ_MAX(0, (int)(BYTE)(col >> 16) - 30),
                           (BYTE)LZ_MAX(0, (int)(BYTE)(col >> 8) - 10),
                           (BYTE)LZ_MAX(0, (int)(BYTE)col - 10)));
    }
}

void ui_badge(RcF r, const char *label, ARGB bg, ARGB fg) {
    ui_fill_round(r, r.h * 0.5f, bg);
    ui_text(r, label, 11.0f, 1, fg, 1);
}

void ui_scrollbar(RcF track, float off, float max, float view) {
    if (max <= view || max <= 0) return;
    ui_fill_round(track, track.w * 0.5f, LZ_ARGB(30, 90, 110, 150));
    float frac = LZ_CLAMP(view / max, 0.08f, 1.0f);
    float h = track.h * frac;
    float pos = (max - view > 0) ? (off / (max - view)) : 0;
    pos = LZ_CLAMP(pos, 0.0f, 1.0f);
    RcF th = { track.x, track.y + (track.h - h) * pos, track.w, h };
    ui_fill_round(th, th.w * 0.5f, LZ_ARGB(110, 120, 140, 175));
}

/* ==========================================================================
 *  交互
 * ========================================================================== */
void ui_mouse_move(float x, float y) {
    ui.mx = x; ui.my = y;
}

void ui_mouse_down(float x, float y) {
    ui.mx = x; ui.my = y; ui.down = 1;
    int id = hit_test(x, y);
    ui.press = id;
    if (id == 0) { ui.focus_id = 0; ui.focus_buf = NULL; }
}

void ui_mouse_up(float x, float y) {
    ui.mx = x; ui.my = y; ui.down = 0;
    int id = hit_test(x, y);
    if (id && id == ui.press) ui.click = id;
    ui.press = 0;
}

void ui_release(void) {
    ui.down = 0;
    ui.press = 0;
}

void ui_mouse_wheel(float delta) {
    if (combo.open_id) {
        combo.scroll -= delta * 42.0f;
        float maxs = LZ_MAX(0.0f, (float)combo.n * 32.0f - 268.0f);
        combo.scroll = LZ_CLAMP(combo.scroll, 0.0f, maxs);
        return;
    }
    /* 模组详情弹窗开着时，滚轮只滚弹窗里的版本列表 */
    if (ui.md_active) {
        ui.md_scroll -= delta * 60.0f;
        ui.md_scroll = LZ_CLAMP(ui.md_scroll, 0.0f, LZ_MAX(0.0f, ui.md_scroll_max));
        return;
    }

    int p = ui.page;
    ui.scroll[p] -= delta * 60.0f;
    float maxs = LZ_MAX(0.0f, ui.scroll_max[p]);
    ui.scroll[p] = LZ_CLAMP(ui.scroll[p], 0.0f, maxs);
}

void ui_key(int vk) {
    if (!ui.focus_buf) return;
    int n = (int)strlen(ui.focus_buf);
    if (vk == VK_BACK && n > 0) {
        /* 删除一个 UTF-8 字符 */
        int i = n - 1;
        while (i > 0 && ((unsigned char)ui.focus_buf[i] & 0xC0) == 0x80) i--;
        ui.focus_buf[i] = 0;
        ui.caret_t = 0;
    } else if (vk == VK_RETURN) {
        ui.focus_id = 0;
    }
}

void ui_char(wchar_t c) {
    if (!ui.focus_buf || !ui.focus_id) return;
    if (c < 32) return;
    int n = (int)strlen(ui.focus_buf);
    if (n + 4 >= ui.focus_cap) return;
    char tmp[8];
    int len = WideCharToMultiByte(CP_UTF8, 0, &c, 1, tmp, sizeof(tmp), NULL, NULL);
    if (len > 0) {
        memcpy(ui.focus_buf + n, tmp, (size_t)len);
        ui.focus_buf[n + len] = 0;
        ui.caret_t = 0;
    }
}

/* ---------------- 裁剪 ---------------- */
void ui_glow(float cx, float cy, float r, ARGB c) {
    blob(g_g, cx, cy, r, c);
}

void ui_clip_push(RcF r) {
    GdipSetClipRect(g_g, r.x, r.y, r.w, r.h, CombineModeIntersect);
}

void ui_clip_pop(void) { GdipResetClip(g_g); }

/* ---------------- 装饰：等距方块 ---------------- */
void ui_cube(float cx, float cy, float s, float alpha_mul) {
    GpPointF top[4] = {
        { cx, cy - s }, { cx + s * 0.866f, cy - s * 0.5f },
        { cx, cy }, { cx - s * 0.866f, cy - s * 0.5f }
    };
    GpPointF left[4] = {
        { cx - s * 0.866f, cy - s * 0.5f }, { cx, cy },
        { cx, cy + s }, { cx - s * 0.866f, cy + s * 0.5f }
    };
    GpPointF right[4] = {
        { cx + s * 0.866f, cy - s * 0.5f }, { cx, cy },
        { cx, cy + s }, { cx + s * 0.866f, cy + s * 0.5f }
    };
    int am = (int)(alpha_mul * 255);
    ARGB ct = LZ_ARGB((BYTE)LZ_MIN(255, am), 158, 216, 255);
    ARGB cl = LZ_ARGB((BYTE)LZ_MIN(255, am), 86, 160, 240);
    ARGB cr = LZ_ARGB((BYTE)LZ_MIN(255, am), 42, 118, 214);

    GpSolidFill *b = NULL;
    GdipCreateSolidFill(ct, &b); GdipFillPolygon(g_g, (GpBrush *)b, top, 4, FillModeWinding);
    GdipDeleteBrush((GpBrush *)b);
    GdipCreateSolidFill(cl, &b); GdipFillPolygon(g_g, (GpBrush *)b, left, 4, FillModeWinding);
    GdipDeleteBrush((GpBrush *)b);
    GdipCreateSolidFill(cr, &b); GdipFillPolygon(g_g, (GpBrush *)b, right, 4, FillModeWinding);
    GdipDeleteBrush((GpBrush *)b);

    /* 描边 */
    GpPen *pen = NULL;
    GdipCreatePen1(LZ_ARGB(120, 255, 255, 255), 1.4f, UnitPixel, &pen);
    GdipDrawPolygon(g_g, pen, top, 4);
    GdipDrawPolygon(g_g, pen, left, 4);
    GdipDrawPolygon(g_g, pen, right, 4);
    GdipDeletePen(pen);
    /* 顶部高光 */
    GpPen *p2 = NULL;
    GdipCreatePen1(LZ_ARGB(160, 255, 255, 255), 2.0f, UnitPixel, &p2);
    GdipDrawLine(g_g, p2, cx - s * 0.72f, cy - s * 0.48f, cx - s * 0.06f, cy - s * 0.86f);
    GdipDeletePen(p2);
}

void ui_set_page(int page) {
    int p = LZ_CLAMP(page, 0, PG_COUNT - 1);
    if (p != ui.page) {
        ui.page = p;
        ui.page_t = 0.0f;
        ui.page_tick = GetTickCount();
    }
    ui.focus_id = 0;
    ui.focus_buf = NULL;
}

float ui_scroll_y(void) { return ui.scroll[ui.page]; }

void ui_scroll_reset(int page) {
    if (page >= 0 && page < 10) ui.scroll[page] = 0;
}

/* ---------------- 图片（皮肤 PNG） ---------------- */
void *ui_image_load(const char *path_utf8) {
    if (!path_utf8 || !*path_utf8) return NULL;
    wchar_t wp[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wp, MAX_PATH * 2)) return NULL;
    GpImage *img = NULL;
    if (GdipLoadImageFromFile(wp, &img) != Ok) return NULL;
    return img;
}

void ui_image_free(void *img) {
    if (img) GdipDisposeImage((GpImage *)img);
}

void ui_image_region(void *img, RcF dst, int sx, int sy, int sw, int sh) {
    if (!img || sw <= 0 || sh <= 0) return;
    GpInterpolationMode old;
    GdipGetInterpolationMode(g_g, &old);
    /* 像素画必须最近邻，否则糊成一团 */
    GdipSetInterpolationMode(g_g, InterpolationModeNearestNeighbor);
    GdipDrawImageRectRectI(g_g, (GpImage *)img, (int)dst.x, (int)dst.y, (int)dst.w, (int)dst.h,
                           sx, sy, sw, sh, UnitPixel, NULL, NULL, NULL);
    GdipSetInterpolationMode(g_g, old);
}

