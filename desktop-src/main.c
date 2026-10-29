/*  main.c —— 窗口、消息循环、配置持久化 */
#include "ui.h"
#include "app.h"
#include <dwmapi.h>
#include <commdlg.h>
#include <windowsx.h>

App  g;
HWND g_hwnd = NULL;
CRITICAL_SECTION g_lock;

static char g_toast_buf[300];

/* ---------------------------------------------------------------- 配置 */
static void settings_path(char *out, size_t cap) {
    char ad[MAX_PATH];
    lz_appdata(ad, sizeof(ad));
    char d[MAX_PATH * 2];
    lz_path_join(d, sizeof(d), ad, "Lazuli");
    lz_dir_make(d);
    lz_path_join(out, cap, d, "settings.ini");
}

/* ---------------------------------------------------------------- 账号本 */
static void accounts_path(char *out, size_t cap) {
    char ad[MAX_PATH];
    lz_appdata(ad, sizeof(ad));
    char d[MAX_PATH * 2];
    lz_path_join(d, sizeof(d), ad, "Lazuli");
    lz_dir_make(d);
    lz_path_join(out, cap, d, "accounts.dat");
}

void app_save_accounts(void) {
    char p[MAX_PATH * 2];
    accounts_path(p, sizeof(p));
    Sb b; sb_init(&b); sb_add(&b, "");
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g.book.n; i++) {
        Account *a = &g.book.items[i];
        sb_printf(&b, "%s\t%s\t%s\t%s\t%s\n", a->name, a->uuid, a->xuid, a->token, a->refresh);
    }
    LeaveCriticalSection(&g_lock);
    lz_file_write(p, b.s ? b.s : "", b.n);
    sb_free(&b);
}

void app_load_accounts(void) {
    char p[MAX_PATH * 2];
    accounts_path(p, sizeof(p));
    char *txt = NULL;
    if (!lz_file_read(p, &txt, NULL)) return;
    EnterCriticalSection(&g_lock);
    g.book.n = 0;
    g.book.sel = -1;
    char *line = txt;
    while (line && *line && g.book.n < MAX_ACCOUNTS) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (*line) {
            Account *a = &g.book.items[g.book.n];
            char *f[5] = { line, NULL, NULL, NULL, NULL };
            for (int i = 1; i < 5; i++) {
                f[i] = f[i - 1] ? strchr(f[i - 1], '\t') : NULL;
                if (f[i]) { *f[i] = 0; f[i]++; }
            }
            if (f[0] && f[1] && f[3]) {
                snprintf(a->name, sizeof(a->name), "%s", f[0]);
                snprintf(a->uuid, sizeof(a->uuid), "%s", f[1]);
                snprintf(a->xuid, sizeof(a->xuid), "%s", f[2] ? f[2] : "");
                snprintf(a->token, sizeof(a->token), "%s", f[3]);
                snprintf(a->refresh, sizeof(a->refresh), "%s", f[4] ? f[4] : "");
                g.book.n++;
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    if (g.book.n > 0) {
        g.book.sel = 0;
        Account *a = &g.book.items[0];
        snprintf(g.auth.name, sizeof(g.auth.name), "%s", a->name);
        snprintf(g.auth.uuid, sizeof(g.auth.uuid), "%s", a->uuid);
        snprintf(g.auth.xuid, sizeof(g.auth.xuid), "%s", a->xuid);
        snprintf(g.auth.token, sizeof(g.auth.token), "%s", a->token);
        snprintf(g.auth.refresh, sizeof(g.auth.refresh), "%s", a->refresh);
        g.auth.state = AUTH_OK;
    }
    LeaveCriticalSection(&g_lock);
    free(txt);
}

void app_save_settings(void) {
    char p[MAX_PATH * 2];
    settings_path(p, sizeof(p));
    Sb b; sb_init(&b); sb_add(&b, "");
    sb_printf(&b, "game_dir=%s\n", g.set.game_dir);
    sb_printf(&b, "java_path=%s\n", g.set.java_path);
    sb_printf(&b, "mem_mb=%d\n", g.set.mem_mb);
    sb_printf(&b, "dl_threads=%d\n", g.set.dl_threads);
    sb_printf(&b, "cf_key=%s\n", g.set.cf_key);
    sb_printf(&b, "relay=%s\n", g.set.relay);
    sb_printf(&b, "msa_client_id=%s\n", g.set.msa_client_id);
    sb_printf(&b, "terracotta=%s\n", g.set.terracotta);
    sb_printf(&b, "full_assets=%d\n", g.set.full_assets);
    sb_printf(&b, "version_isolate=%d\n", g.set.version_isolate);
    sb_printf(&b, "last_version=%s\n", g.set.last_version);
    lz_file_write(p, b.s ? b.s : "", b.n);
    sb_free(&b);
}

void app_load_settings(void) {
    char ad[MAX_PATH];
    lz_appdata(ad, sizeof(ad));
    char d[MAX_PATH * 2];
    lz_path_join(d, sizeof(d), ad, "Lazuli");
    lz_path_join(g.set.game_dir, sizeof(g.set.game_dir), d, "minecraft");
    g.set.mem_mb = 4096;
    g.set.dl_threads = 8;
    g.set.full_assets = 1;
    g.set.version_isolate = 1;
    snprintf(g.set.relay, sizeof(g.set.relay), "127.0.0.1:8788");

    char p[MAX_PATH * 2];
    settings_path(p, sizeof(p));
    char *txt = NULL;
    if (!lz_file_read(p, &txt, NULL)) { lz_dir_make(g.set.game_dir); return; }
    char *save = NULL;
    for (char *line = strtok_r(txt, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "game_dir") && *v) snprintf(g.set.game_dir, sizeof(g.set.game_dir), "%s", v);
        else if (!strcmp(k, "java_path")) snprintf(g.set.java_path, sizeof(g.set.java_path), "%s", v);
        else if (!strcmp(k, "mem_mb")) g.set.mem_mb = LZ_CLAMP(atoi(v), 512, 65536);
        else if (!strcmp(k, "dl_threads")) g.set.dl_threads = LZ_CLAMP(atoi(v), 1, 32);
        else if (!strcmp(k, "version_isolate")) g.set.version_isolate = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "last_version")) snprintf(g.set.last_version, sizeof(g.set.last_version), "%s", v);
        else if (!strcmp(k, "cf_key")) snprintf(g.set.cf_key, sizeof(g.set.cf_key), "%s", v);
        else if (!strcmp(k, "relay")) snprintf(g.set.relay, sizeof(g.set.relay), "%s", v);
        else if (!strcmp(k, "msa_client_id")) snprintf(g.set.msa_client_id, sizeof(g.set.msa_client_id), "%s", v);
        else if (!strcmp(k, "terracotta")) snprintf(g.set.terracotta, sizeof(g.set.terracotta), "%s", v);
        else if (!strcmp(k, "full_assets")) g.set.full_assets = atoi(v);
    }
    free(txt);
    lz_dir_make(g.set.game_dir);
}

/* ---------------------------------------------------------------- 虚拟按键持久化 */
static void vpad_path(char *out, size_t cap) {
    char ad[MAX_PATH];
    lz_appdata(ad, sizeof(ad));
    char d[MAX_PATH * 2];
    lz_path_join(d, sizeof(d), ad, "Lazuli");
    lz_dir_make(d);
    lz_path_join(out, cap, d, "vpad.json");
}

void app_save_vpad(void) {
    char p[MAX_PATH * 2];
    vpad_path(p, sizeof(p));
    Sb b; sb_init(&b); sb_add(&b, "[\n");
    for (int i = 0; i < g.nvpad; i++) {
        sb_printf(&b, "  {\"label\":\"%s\",\"key\":%d,\"x\":%.4f,\"y\":%.4f,\"r\":%.4f,\"stick\":%d}%s\n",
                  g.vpad[i].label, g.vpad[i].keycode, g.vpad[i].x, g.vpad[i].y,
                  g.vpad[i].r, g.vpad[i].is_stick, i + 1 < g.nvpad ? "," : "");
    }
    sb_add(&b, "]\n");
    lz_file_write(p, b.s ? b.s : "", b.n);
    sb_free(&b);
}

void app_load_vpad(void) {
    char p[MAX_PATH * 2];
    vpad_path(p, sizeof(p));
    char *txt = NULL;
    if (!lz_file_read(p, &txt, NULL)) return;
    JVal *root = json_parse(txt);
    free(txt);
    if (!root || root->t != J_ARR) { if (root) json_free(root); return; }
    int n = json_arr_n(root);
    if (n > MAX_VPAD) n = MAX_VPAD;
    if (n <= 0) { json_free(root); return; }
    EnterCriticalSection(&g_lock);
    g.nvpad = 0;
    for (int i = 0; i < n; i++) {
        JVal *o = json_arr_at(root, i);
        VButton *vb = &g.vpad[g.nvpad++];
        memset(vb, 0, sizeof(*vb));
        snprintf(vb->label, sizeof(vb->label), "%s", json_str(json_get(o, "label"), "按键"));
        vb->keycode = (int)json_num(json_get(o, "key"), 32);
        vb->x = (float)json_num(json_get(o, "x"), 0.5);
        vb->y = (float)json_num(json_get(o, "y"), 0.5);
        vb->r = (float)json_num(json_get(o, "r"), 0.05);
        vb->is_stick = json_bool(json_get(o, "stick"), 0);
    }
    LeaveCriticalSection(&g_lock);
    json_free(root);
}

/* ---------------------------------------------------------------- 杂项 */
void ui_request_redraw(void) {
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

void app_toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_toast_buf, sizeof(g_toast_buf), fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_lock);
    snprintf(g.toast, sizeof(g.toast), "%s", g_toast_buf);
    g.toast_t = 3.0;
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
}

void app_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.status, sizeof(g.status), fmt, ap);
    va_end(ap);
    ui_request_redraw();
}

void app_log(const char *fmt, ...) {
    char p[MAX_PATH * 2];
    char ad[MAX_PATH];
    lz_appdata(ad, sizeof(ad));
    char d[MAX_PATH * 2];
    lz_path_join(d, sizeof(d), ad, "Lazuli");
    lz_dir_make(d);
    lz_path_join(p, sizeof(p), d, "launcher.log");
    FILE *f = fopen(p, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

void win_minimize(void) { ShowWindow(g_hwnd, SW_MINIMIZE); }
void win_maximize(void) { ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); }
void win_close(void) { PostMessage(g_hwnd, WM_CLOSE, 0, 0); }

/* 选文件对话框，返回 1 表示选好了 */
int win_pick_file(char *out, size_t cap, const char *filter) {
    OPENFILENAMEA ofn;
    char buf[MAX_PATH] = {0};
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = filter ? filter : "所有文件\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return 0;
    snprintf(out, cap, "%s", buf);
    return 1;
}

void win_open_folder(const char *path) {
    if (path && *path) lz_shell_open(path);
}

void win_begin_drag(void) {
    ReleaseCapture();
    SendMessage(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

/* ---------------------------------------------------------------- 窗口 */
static void apply_win_style(HWND hwnd) {
    /* Win11 圆角 */
    typedef HRESULT (WINAPI *PFN_DWM)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    if (dwm) {
        PFN_DWM set = (PFN_DWM)GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (set) {
            int corner = 2; /* DWMWCP_ROUND */
            set(hwnd, 33, &corner, sizeof(corner));
            int dark = 0;
            set(hwnd, 20, &dark, sizeof(dark));
        }
    }
    MARGINS m = { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &m);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp) return 0;
            break;
        case WM_NCACTIVATE:
            return TRUE;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *mm = (MINMAXINFO *)lp;
            mm->ptMinTrackSize.x = (LONG)(980 * g_ui_scale);
            mm->ptMinTrackSize.y = (LONG)(660 * g_ui_scale);
            return 0;
        }
        case WM_NCHITTEST: {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &p);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int b = (int)(7 * g_ui_scale);
            if (!IsZoomed(hwnd)) {
                int L = p.x < b, Rr = p.x > rc.right - b;
                int T = p.y < b, B = p.y > rc.bottom - b;
                if (T && L) return HTTOPLEFT;
                if (T && Rr) return HTTOPRIGHT;
                if (B && L) return HTBOTTOMLEFT;
                if (B && Rr) return HTBOTTOMRIGHT;
                if (L) return HTLEFT;
                if (Rr) return HTRIGHT;
                if (T) return HTTOP;
                if (B) return HTBOTTOM;
            }
            return HTCLIENT;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            DWORD t0 = GetTickCount();
            ui_frame_begin(hdc, rc.right, rc.bottom);
            DWORD t1 = GetTickCount();
            pages_frame((float)rc.right / g_ui_scale, (float)rc.bottom / g_ui_scale, ui.page);
            DWORD t2 = GetTickCount();
            ui_frame_end(hdc);
            EndPaint(hwnd, &ps);
            DWORD t3 = GetTickCount();
            /* 绘制耗时采样（每 200 帧记一次，便于排查性能） */
            static DWORD a0 = 0, a1 = 0, a2 = 0; static int cnt = 0;
            a0 += t1 - t0; a1 += t2 - t1; a2 += t3 - t2; cnt++;
            if (cnt >= 120) {
                app_log("[性能] 背景 %lu ms / 页面 %lu ms / 输出 %lu ms",
                        a0 / (DWORD)cnt, a1 / (DWORD)cnt, a2 / (DWORD)cnt);
                a0 = a1 = a2 = 0; cnt = 0;
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            float x = (float)GET_X_LPARAM(lp) / g_ui_scale;
            float y = (float)GET_Y_LPARAM(lp) / g_ui_scale;
            /* 按键已在窗口外松开时（丢掉了 WM_LBUTTONUP），强制复位，避免"粘住"拖拽 */
            if (ui.down && !(GetKeyState(VK_LBUTTON) & 0x8000)) ui_release();
            ui_mouse_move(x, y);
            /* 限流到 ~30fps：以前每次 mousemove 都整窗重绘，一划鼠标就掉帧 */
            {
                static DWORD last = 0;
                DWORD now = GetTickCount();
                if (now - last >= 30) { last = now; InvalidateRect(hwnd, NULL, FALSE); }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            SetCapture(hwnd);
            ui_mouse_down((float)GET_X_LPARAM(lp) / g_ui_scale,
                          (float)GET_Y_LPARAM(lp) / g_ui_scale);
            if (ui.press == 1200) {
                ui.press = 0;
                win_begin_drag();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_CAPTURECHANGED:
            /* 只有"异常丢失捕获"（按键还按着但捕获没了）才复位；
               正常松开时 ui.down 已经是 0，这里不能动 ui.press，
               否则会把刚刚生成的点击事件抹掉。 */
            if (ui.down) {
                ui_release();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            /* 先结算点击，再释放捕获（ReleaseCapture 会同步发 WM_CAPTURECHANGED） */
            ui_mouse_up((float)GET_X_LPARAM(lp) / g_ui_scale,
                        (float)GET_Y_LPARAM(lp) / g_ui_scale);
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_MOUSEWHEEL: {
            short d = GET_WHEEL_DELTA_WPARAM(wp);
            ui_mouse_wheel((float)d / 120.0f);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_CHAR:
            ui_char((wchar_t)wp);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_KEYDOWN:
            if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                if (ui.focus_buf) {
                    char *c = lz_clipboard_get();
                    if (c) {
                        int n = (int)strlen(ui.focus_buf);
                        int room = ui.focus_cap - n - 1;
                        if (room > 0) {
                            strncat(ui.focus_buf, c, (size_t)room);
                        }
                        free(c);
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            ui_key((int)wp);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_TIMER: {
            pages_tick();
            if (ui_needs_anim()) InvalidateRect(hwnd, NULL, FALSE);
            /* 过渡动画期间提高刷新率，结束后回到 10fps */
            static int cur_ms = 100;
            int want = (ui.page_t < 1.0f) ? 33 : 100;
            if (want != cur_ms) { cur_ms = want; SetTimer(g_hwnd, 1, (UINT)want, NULL); }
            return 0;
        }
        case WM_DWMCOMPOSITIONCHANGED:
            apply_win_style(hwnd);
            return 0;
        case WM_CLOSE:
            if (g.game.running) {
                int r = MessageBoxW(hwnd, L"游戏正在运行，确定退出启动器？", L"琉璃启动器",
                                    MB_YESNO | MB_ICONQUESTION);
                if (r != IDYES) return 0;
            }
            app_save_settings();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------- 入口 */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    LZ_UNUSED(hPrev);

    /* DPI 感知 */
    typedef BOOL (WINAPI *PFN_DPI)(HANDLE);
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (u32) {
        PFN_DPI setdpi = (PFN_DPI)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (setdpi) setdpi((HANDLE)-4); /* PER_MONITOR_AWARE_V2 */
        else SetProcessDPIAware();
    }

    InitializeCriticalSection(&g_lock);
    memset(&g, 0, sizeof(g));
    ui_init();

    HDC screen = GetDC(NULL);
    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    g_ui_scale = (float)dpi / 96.0f;
    if (g_ui_scale < 1.0f) g_ui_scale = 1.0f;

    app_load_settings();
    app_load_accounts();
    http_init();
    auth_load();
    pages_init();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"LazuliLauncherWnd";
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    int ww = (int)(1280 * g_ui_scale);
    int wh = (int)(820 * g_ui_scale);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int aw = wa.right - wa.left, ah = wa.bottom - wa.top;
    if (ww > aw - 40) ww = aw - 40;
    if (wh > ah - 40) wh = ah - 40;
    int wx = wa.left + (aw - ww) / 2;
    int wy = wa.top + (ah - wh) / 2;

    g_hwnd = CreateWindowExW(0, L"LazuliLauncherWnd", L"琉璃启动器",
                             WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                             wx, wy, ww, wh, NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    apply_win_style(g_hwnd);
    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIcon);
    ShowWindow(g_hwnd, show ? show : SW_SHOW);
    UpdateWindow(g_hwnd);

    {
        RECT rr;
        GetWindowRect(g_hwnd, &rr);
        app_log("[启动] scale=%.2f dpi=%d window=%ld,%ld %ldx%ld work=%ldx%ld",
                g_ui_scale, dpi, rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top, aw, ah);
    }

    /* 调试/截图：page=N 直接打开指定页面 */
    if (cmd && cmd[0]) {
        const char *p = strstr(cmd, "page=");
        if (p) {
            ui_set_page(atoi(p + 5));
            InvalidateRect(g_hwnd, NULL, FALSE);
        }
        if (strstr(cmd, "mdtest")) {
            g_mdtest = 1;
            g.content_kind = 1;
            ui_set_page(PG_DL);
            InvalidateRect(g_hwnd, NULL, FALSE);
        }
        /* 自动化：install=<版本号> / launch=<版本号> */
        const char *pi = strstr(cmd, "install=");
        if (pi) {
            char id[64] = {0};
            pi += 8;
            int i = 0;
            while (*pi && *pi != ' ' && i < 63) id[i++] = *pi++;
            app_log("[自动] 开始安装 %s", id);
            mc_install_async(id);
        }
        const char *pl = strstr(cmd, "launch=");
        if (pl) {
            char id[64] = {0};
            pl += 7;
            int i = 0;
            while (*pl && *pl != ' ' && i < 63) id[i++] = *pl++;
            app_log("[自动] 启动 %s", id);
            mc_launch_async(id, NULL, 0);
        }
    }

    SetTimer(g_hwnd, 1, 100, NULL);

    /* 后台任务 */
    mp_scan_start();
    mc_load_manifest_async();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ui_shutdown();
    http_shutdown();
    DeleteCriticalSection(&g_lock);
    return 0;
}
