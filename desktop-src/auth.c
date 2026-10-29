/*  auth.c —— 微软账号登录（设备代码流 OAuth2 → Xbox Live → XSTS → Minecraft Services） */
#include "app.h"

/* 旧的 00000000402b5328 已被微软下架（AADSTS700016），
   这里用当前可用的公开客户端 ID，用户也可以在设置里换。 */
#define MS_CLIENT_ID_DEFAULT "c36a9fb6-4f2a-41ff-90bd-ae7cc92031eb"
#define MS_SCOPE "XboxLive.SignIn XboxLive.offline_access"

static const char *ms_client_id(void) {
    return g.set.msa_client_id[0] ? g.set.msa_client_id : MS_CLIENT_ID_DEFAULT;
}
#define URL_DEVICE   "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode"
#define URL_TOKEN    "https://login.microsoftonline.com/consumers/oauth2/v2.0/token"
#define URL_XBL      "https://user.auth.xboxlive.com/user/authenticate"
#define URL_XSTS     "https://xsts.auth.xboxlive.com/xsts/authorize"
#define URL_MCLOGIN  "https://api.minecraftservices.com/authentication/login_with_xbox"
#define URL_PROFILE  "https://api.minecraftservices.com/minecraft/profile"

static char *post_form(const char *url, const char *body, int *status) {
    Buf b = {0};
    const char *h[] = { "Accept: application/json" };
    http_post(url, body, "application/x-www-form-urlencoded", h, 1, &b, status);
    if (!b.data) return NULL;
    return b.data;
}

static char *post_json(const char *url, const char *body, const char **extra, int n, int *status) {
    Buf b = {0};
    const char *h[8]; int hn = 0;
    h[hn++] = "Accept: application/json";
    for (int i = 0; i < n && hn < 7; i++) h[hn++] = extra[i];
    http_post(url, body, "application/json", h, hn, &b, status);
    if (!b.data) return NULL;
    return b.data;
}

static char *get_json(const char *url, const char **extra, int n, int *status) {
    Buf b = {0};
    const char *h[8]; int hn = 0;
    h[hn++] = "Accept: application/json";
    for (int i = 0; i < n && hn < 7; i++) h[hn++] = extra[i];
    http_get_h(url, h, hn, &b, status);
    if (!b.data) return NULL;
    return b.data;
}

static void auth_fail(const char *msg) {
    EnterCriticalSection(&g_lock);
    g.auth.state = AUTH_FAIL;
    snprintf(g.auth.err, sizeof(g.auth.err), "%s", msg ? msg : "登录失败");
    g.auth.polling = 0;
    LeaveCriticalSection(&g_lock);
    app_toast("%s", msg ? msg : "登录失败");
    ui_request_redraw();
}

static void auth_succeed(const char *name, const char *uuid, const char *token, const char *xuid) {
    EnterCriticalSection(&g_lock);
    g.auth.state = AUTH_OK;
    snprintf(g.auth.name, sizeof(g.auth.name), "%s", name);
    snprintf(g.auth.uuid, sizeof(g.auth.uuid), "%s", uuid);
    snprintf(g.auth.token, sizeof(g.auth.token), "%s", token);
    snprintf(g.auth.xuid, sizeof(g.auth.xuid), "%s", xuid ? xuid : "0");
    g.auth.polling = 0;
    g.auth.err[0] = 0;
    LeaveCriticalSection(&g_lock);
    auth_save();

    /* 写进账号本，支持多账号随时切换 */
    EnterCriticalSection(&g_lock);
    int idx = -1;
    for (int i = 0; i < g.book.n; i++)
        if (!strcmp(g.book.items[i].uuid, g.auth.uuid)) { idx = i; break; }
    if (idx < 0 && g.book.n < MAX_ACCOUNTS) idx = g.book.n++;
    if (idx >= 0) {
        Account *a = &g.book.items[idx];
        memset(a, 0, sizeof(*a));
        snprintf(a->name, sizeof(a->name), "%s", g.auth.name);
        snprintf(a->uuid, sizeof(a->uuid), "%s", g.auth.uuid);
        snprintf(a->xuid, sizeof(a->xuid), "%s", g.auth.xuid);
        snprintf(a->token, sizeof(a->token), "%s", g.auth.token);
        snprintf(a->refresh, sizeof(a->refresh), "%s", g.auth.refresh);
        g.book.sel = idx;
    }
    LeaveCriticalSection(&g_lock);
    app_save_accounts();

    app_toast("登录成功，欢迎 %s", name);
    ui_request_redraw();
}

/* 设备代码 → 轮询令牌 → 换取 MC 档案 */
static DWORD WINAPI auth_thread(LPVOID param) {
    LZ_UNUSED(param);
    char body[1024];
    snprintf(body, sizeof(body), "client_id=%s&scope=XboxLive.SignIn%%20XboxLive.offline_access",
             ms_client_id());

    int st = 0;
    char *r = post_form(URL_DEVICE, body, &st);
    if (!r) { auth_fail("无法连接微软登录服务"); return 0; }
    JVal *j = json_parse(r);
    free(r);
    if (!j) { auth_fail("设备代码响应解析失败"); return 0; }

    const char *dc = json_str(json_get(j, "device_code"), NULL);
    const char *uc = json_str(json_get(j, "user_code"), NULL);
    const char *vu = json_str(json_get(j, "verification_uri"), NULL);
    int interval = (int)json_num(json_get(j, "interval"), 5);
    int expires = (int)json_num(json_get(j, "expires_in"), 900);

    if (!dc || !uc) {
        const char *ed = json_str(json_get(j, "error_description"), "无法获取设备代码");
        char msg[300];
        if (ed && strstr(ed, "AADSTS700016"))
            snprintf(msg, sizeof(msg), "登录客户端 ID 已失效，请到「设置 → 微软登录客户端 ID」换一个");
        else if (ed && strstr(ed, "AADSTS"))
            snprintf(msg, sizeof(msg), "微软拒绝了登录请求：%.180s", ed);
        else
            snprintf(msg, sizeof(msg), "%s", ed ? ed : "无法获取设备代码");
        app_log("[登录] 设备码失败: %s", msg);
        auth_fail(msg);
        json_free(j);
        return 0;
    }

    EnterCriticalSection(&g_lock);
    g.auth.state = AUTH_WAIT;
    snprintf(g.auth.device_code, sizeof(g.auth.device_code), "%s", dc);
    snprintf(g.auth.user_code, sizeof(g.auth.user_code), "%s", uc);
    snprintf(g.auth.verify_uri, sizeof(g.auth.verify_uri), "%s",
             vu ? vu : "https://microsoft.com/link");
    g.auth.interval = interval;
    g.auth.expires = expires;
    g.auth.polling = 1;
    LeaveCriticalSection(&g_lock);

    /* 自动打开登录页，并把验证码放进剪贴板 */
    lz_clipboard_set(uc);
    if (g.auth.verify_uri[0]) lz_shell_open(g.auth.verify_uri);
    app_log("[登录] 验证码 %s，已打开 %s", uc, g.auth.verify_uri);
    ui_request_redraw();

    /* 轮询 */
    char *ms_token = NULL;
    int waited = 0;
    while (waited < expires) {
        for (int i = 0; i < interval; i++) {
            if (!g.auth.polling) { json_free(j); return 0; }
            Sleep(1000);
            waited++;
        }
        char tb[2048];
        snprintf(tb, sizeof(tb),
                 "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code"
                 "&client_id=%s&device_code=%s", ms_client_id(), dc);
        int s2 = 0;
        char *tr = post_form(URL_TOKEN, tb, &s2);
        if (!tr) continue;
        JVal *tj = json_parse(tr);
        free(tr);
        if (!tj) continue;
        const char *err = json_str(json_get(tj, "error"), NULL);
        if (err) {
            if (!strcmp(err, "authorization_pending")) { json_free(tj); continue; }
            if (!strcmp(err, "slow_down")) { interval += 5; json_free(tj); continue; }
            const char *ed = json_str(json_get(tj, "error_description"), "登录被拒绝或已过期");
            char msg[300];
            snprintf(msg, sizeof(msg), "%s", ed);
            json_free(tj);
            json_free(j);
            auth_fail(msg);
            return 0;
        }
        const char *at = json_str(json_get(tj, "access_token"), NULL);
        const char *rt = json_str(json_get(tj, "refresh_token"), NULL);
        if (at) { ms_token = (char *)calloc(1, strlen(at) + 1); strcpy(ms_token, at); }
        if (rt) {
            /* 存下来，之后 token 过期可以免登录刷新 */
            EnterCriticalSection(&g_lock);
            snprintf(g.auth.refresh, sizeof(g.auth.refresh), "%s", rt);
            LeaveCriticalSection(&g_lock);
            app_log("[登录] 已保存 refresh_token（%d 字节）", (int)strlen(rt));
        }
        json_free(tj);
        break;
    }
    json_free(j);

    if (!ms_token) { auth_fail("登录超时，请重试"); return 0; }

    /* ---- Xbox Live ---- */
    char xblbody[4096];
    snprintf(xblbody, sizeof(xblbody),
             "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\","
             "\"RpsTicket\":\"d=%s\"},\"RelyingParty\":\"http://auth.xboxlive.com\","
             "\"TokenType\":\"JWT\"}", ms_token);
    free(ms_token);

    int s3 = 0;
    char *xr = post_json(URL_XBL, xblbody, NULL, 0, &s3);
    if (!xr) { auth_fail("Xbox Live 认证失败"); return 0; }
    JVal *xj = json_parse(xr);
    free(xr);
    const char *xbl = xj ? json_str(json_get(xj, "Token"), NULL) : NULL;
    const char *uhs = xj ? json_str(json_path(xj, "DisplayClaims.xui.0.uhs"), NULL) : NULL;
    if (!xbl || !uhs) {
        if (xj) json_free(xj);
        auth_fail("Xbox Live 返回数据异常");
        return 0;
    }
    char xbl_tok[4096], xbl_uhs[64];
    snprintf(xbl_tok, sizeof(xbl_tok), "%s", xbl);
    snprintf(xbl_uhs, sizeof(xbl_uhs), "%s", uhs);
    json_free(xj);

    /* ---- XSTS ---- */
    char xsbody[4300];
    snprintf(xsbody, sizeof(xsbody),
             "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"%s\"]},"
             "\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}", xbl_tok);
    int s4 = 0;
    char *xr2 = post_json(URL_XSTS, xsbody, NULL, 0, &s4);
    if (!xr2) { auth_fail("XSTS 认证失败"); return 0; }
    JVal *x2 = json_parse(xr2);
    free(xr2);
    if (!x2) { auth_fail("XSTS 响应解析失败"); return 0; }
    const char *xsts = json_str(json_get(x2, "Token"), NULL);
    const char *xuid = json_str(json_path(x2, "DisplayClaims.xui.0.xid"), "0");
    if (!xsts) {
        const char *xerr = json_str(json_get(x2, "XErr"), NULL);
        char msg[300] = "XSTS 认证失败";
        if (xerr) {
            long long e = atoll(xerr);
            if (e == 2148916233LL) snprintf(msg, sizeof(msg), "该微软账号尚未创建 Xbox 档案");
            else if (e == 2148916235LL) snprintf(msg, sizeof(msg), "该账号所在地区不支持 Xbox Live");
            else if (e == 2148916238LL) snprintf(msg, sizeof(msg), "未成年账号需加入家庭组");
        }
        json_free(x2);
        auth_fail(msg);
        return 0;
    }
    char xsts_tok[4096], xuid_s[64];
    snprintf(xsts_tok, sizeof(xsts_tok), "%s", xsts);
    snprintf(xuid_s, sizeof(xuid_s), "%s", xuid);
    json_free(x2);

    /* ---- Minecraft Services ---- */
    char mcbody[4300];
    snprintf(mcbody, sizeof(mcbody),
             "{\"identityToken\":\"XBL3.0 x=%s;%s\"}", xbl_uhs, xsts_tok);
    int s5 = 0;
    char *mr = post_json(URL_MCLOGIN, mcbody, NULL, 0, &s5);
    if (!mr) { auth_fail("Minecraft 服务认证失败"); return 0; }
    JVal *mj = json_parse(mr);
    free(mr);
    if (!mj) { auth_fail("Minecraft 响应解析失败"); return 0; }
    const char *mtok = json_str(json_get(mj, "access_token"), NULL);
    if (!mtok) {
        json_free(mj);
        auth_fail("未能获取 Minecraft 访问令牌");
        return 0;
    }
    char mc_tok[4096];
    snprintf(mc_tok, sizeof(mc_tok), "%s", mtok);
    json_free(mj);

    /* ---- 档案 ---- */
    char hdr[4300];
    snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s", mc_tok);
    const char *hs[] = { hdr };
    int s6 = 0;
    char *pr = get_json(URL_PROFILE, hs, 1, &s6);
    if (!pr) {
        auth_fail("该账号未拥有 Minecraft Java 版");
        return 0;
    }
    JVal *pj = json_parse(pr);
    free(pr);
    if (!pj) { auth_fail("档案解析失败"); return 0; }
    const char *nm = json_str(json_get(pj, "name"), NULL);
    const char *uid = json_str(json_get(pj, "id"), NULL);
    if (!nm || !uid) {
        json_free(pj);
        auth_fail("该账号未拥有 Minecraft Java 版");
        return 0;
    }
    char name[64], uuid[40];
    snprintf(name, sizeof(name), "%s", nm);
    snprintf(uuid, sizeof(uuid), "%s", uid);
    json_free(pj);

    auth_succeed(name, uuid, mc_tok, xuid_s);
    return 0;
}

/* 用 refresh_token 换新的 Minecraft 令牌（同步，启动前调用）。
   微软的 access_token 只有 24 小时，过期后游戏会提示「无效的会话 ID」。 */
int auth_refresh_blocking(char *err, size_t errcap) {
    char rt[2048];
    EnterCriticalSection(&g_lock);
    snprintf(rt, sizeof(rt), "%s", g.auth.refresh);
    LeaveCriticalSection(&g_lock);

    if (!rt[0]) {
        if (err) snprintf(err, errcap, "没有 refresh_token，请重新登录");
        return 0;
    }

    char *enc = lz_url_encode(rt);
    char body[8192];
    snprintf(body, sizeof(body),
             "grant_type=refresh_token&client_id=%s&refresh_token=%s"
             "&scope=XboxLive.SignIn%%20XboxLive.offline_access",
             ms_client_id(), enc ? enc : rt);
    free(enc);

    int st = 0;
    char *r = post_form(URL_TOKEN, body, &st);
    if (!r) { if (err) snprintf(err, errcap, "无法连接微软服务"); return 0; }
    JVal *j = json_parse(r);
    free(r);
    if (!j) { if (err) snprintf(err, errcap, "刷新响应解析失败"); return 0; }

    const char *at = json_str(json_get(j, "access_token"), NULL);
    const char *nrt = json_str(json_get(j, "refresh_token"), NULL);
    const char *ed = json_str(json_get(j, "error_description"), NULL);
    if (!at) {
        if (err) snprintf(err, errcap, "%s", ed ? ed : "刷新令牌被拒绝");
        app_log("[登录] 刷新失败: %s", ed ? ed : "无 access_token");
        json_free(j);
        return 0;
    }
    char ms_token[4096];
    snprintf(ms_token, sizeof(ms_token), "%s", at);
    if (nrt && nrt[0]) {
        EnterCriticalSection(&g_lock);
        snprintf(g.auth.refresh, sizeof(g.auth.refresh), "%s", nrt);
        LeaveCriticalSection(&g_lock);
    }
    json_free(j);

    /* ---- Xbox Live ---- */
    char xblbody[4600];
    snprintf(xblbody, sizeof(xblbody),
             "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\","
             "\"RpsTicket\":\"d=%s\"},\"RelyingParty\":\"http://auth.xboxlive.com\","
             "\"TokenType\":\"JWT\"}", ms_token);
    int s3 = 0;
    char *xr = post_json(URL_XBL, xblbody, NULL, 0, &s3);
    if (!xr) { if (err) snprintf(err, errcap, "Xbox Live 认证失败"); return 0; }
    JVal *xj = json_parse(xr);
    free(xr);
    const char *xbl = xj ? json_str(json_get(xj, "Token"), NULL) : NULL;
    const char *uhs = xj ? json_str(json_path(xj, "DisplayClaims.xui.0.uhs"), NULL) : NULL;
    if (!xbl || !uhs) {
        if (xj) json_free(xj);
        if (err) snprintf(err, errcap, "Xbox Live 返回数据异常");
        return 0;
    }
    char xbl_tok[4096], xbl_uhs[64];
    snprintf(xbl_tok, sizeof(xbl_tok), "%s", xbl);
    snprintf(xbl_uhs, sizeof(xbl_uhs), "%s", uhs);
    json_free(xj);

    /* ---- XSTS ---- */
    char xsbody[4300];
    snprintf(xsbody, sizeof(xsbody),
             "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"%s\"]},"
             "\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}", xbl_tok);
    int s4 = 0;
    char *xr2 = post_json(URL_XSTS, xsbody, NULL, 0, &s4);
    if (!xr2) { if (err) snprintf(err, errcap, "XSTS 认证失败"); return 0; }
    JVal *x2 = json_parse(xr2);
    free(xr2);
    if (!x2) { if (err) snprintf(err, errcap, "XSTS 响应解析失败"); return 0; }
    const char *xsts = json_str(json_get(x2, "Token"), NULL);
    const char *xuid = json_str(json_path(x2, "DisplayClaims.xui.0.xid"), "0");
    if (!xsts) {
        json_free(x2);
        if (err) snprintf(err, errcap, "XSTS 认证失败");
        return 0;
    }
    char xsts_tok[4096], xuid_s[64];
    snprintf(xsts_tok, sizeof(xsts_tok), "%s", xsts);
    snprintf(xuid_s, sizeof(xuid_s), "%s", xuid);
    json_free(x2);

    /* ---- Minecraft Services ---- */
    char mcbody[4300];
    snprintf(mcbody, sizeof(mcbody),
             "{\"identityToken\":\"XBL3.0 x=%s;%s\"}", xbl_uhs, xsts_tok);
    int s5 = 0;
    char *mr = post_json(URL_MCLOGIN, mcbody, NULL, 0, &s5);
    if (!mr) { if (err) snprintf(err, errcap, "Minecraft 服务认证失败"); return 0; }
    JVal *mj = json_parse(mr);
    free(mr);
    if (!mj) { if (err) snprintf(err, errcap, "Minecraft 响应解析失败"); return 0; }
    const char *mtok = json_str(json_get(mj, "access_token"), NULL);
    if (!mtok) {
        json_free(mj);
        if (err) snprintf(err, errcap, "未能获取 Minecraft 令牌");
        return 0;
    }
    char mc_tok[4096];
    snprintf(mc_tok, sizeof(mc_tok), "%s", mtok);
    json_free(mj);

    /* ---- 档案 ---- */
    char hdr[4300];
    snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s", mc_tok);
    const char *hs[] = { hdr };
    int s6 = 0;
    char *pr = get_json(URL_PROFILE, hs, 1, &s6);
    if (!pr) { if (err) snprintf(err, errcap, "该账号未拥有 Minecraft Java 版"); return 0; }
    JVal *pj = json_parse(pr);
    free(pr);
    if (!pj) { if (err) snprintf(err, errcap, "档案解析失败"); return 0; }
    const char *nm = json_str(json_get(pj, "name"), NULL);
    const char *uid = json_str(json_get(pj, "id"), NULL);
    if (!nm || !uid) {
        json_free(pj);
        if (err) snprintf(err, errcap, "该账号未拥有 Minecraft Java 版");
        return 0;
    }
    char name[64], uuid[40];
    snprintf(name, sizeof(name), "%s", nm);
    snprintf(uuid, sizeof(uuid), "%s", uid);
    json_free(pj);

    auth_succeed(name, uuid, mc_tok, xuid_s);
    app_log("[登录] 令牌已刷新：%s", name);
    return 1;
}

void auth_start(void) {
    if (g.auth.polling) return;
    if (g.auth.state == AUTH_WAIT) return;
    EnterCriticalSection(&g_lock);
    g.auth.state = AUTH_WAIT;
    g.auth.err[0] = 0;
    g.auth.polling = 1;
    LeaveCriticalSection(&g_lock);
    CloseHandle(CreateThread(NULL, 0, auth_thread, NULL, 0, NULL));
    ui_request_redraw();
}

void auth_cancel(void) {
    EnterCriticalSection(&g_lock);
    g.auth.polling = 0;
    if (g.auth.state == AUTH_WAIT) g.auth.state = AUTH_NONE;
    LeaveCriticalSection(&g_lock);
    ui_request_redraw();
}

void auth_logout(void) {
    EnterCriticalSection(&g_lock);
    g.auth.state = AUTH_NONE;
    g.auth.name[0] = g.auth.uuid[0] = g.auth.token[0] = g.auth.xuid[0] = 0;
    g.auth.polling = 0;
    LeaveCriticalSection(&g_lock);
    auth_save();
    ui_request_redraw();
}

/* ---------------- 持久化 ---------------- */
static void acct_path(char *out, size_t cap) {
    char ad[MAX_PATH];
    lz_appdata(ad, sizeof(ad));
    char d[MAX_PATH * 2];
    lz_path_join(d, sizeof(d), ad, "Lazuli");
    lz_dir_make(d);
    lz_path_join(out, cap, d, "account.dat");
}

void auth_save(void) {
    char p[MAX_PATH * 2];
    acct_path(p, sizeof(p));
    Sb b; sb_init(&b); sb_add(&b, "");
    sb_printf(&b, "name=%s\n", g.auth.name);
    sb_printf(&b, "uuid=%s\n", g.auth.uuid);
    sb_printf(&b, "token=%s\n", g.auth.token);
    sb_printf(&b, "xuid=%s\n", g.auth.xuid);
    sb_printf(&b, "refresh=%s\n", g.auth.refresh);
    lz_file_write(p, b.s ? b.s : "", b.n);
    sb_free(&b);
}

void auth_load(void) {
    char p[MAX_PATH * 2];
    acct_path(p, sizeof(p));
    char *txt = NULL;
    if (!lz_file_read(p, &txt, NULL)) return;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "name")) snprintf(g.auth.name, sizeof(g.auth.name), "%s", v);
        else if (!strcmp(k, "uuid")) snprintf(g.auth.uuid, sizeof(g.auth.uuid), "%s", v);
        else if (!strcmp(k, "token")) snprintf(g.auth.token, sizeof(g.auth.token), "%s", v);
        else if (!strcmp(k, "refresh")) snprintf(g.auth.refresh, sizeof(g.auth.refresh), "%s", v);
        else if (!strcmp(k, "xuid")) snprintf(g.auth.xuid, sizeof(g.auth.xuid), "%s", v);
    }
    free(txt);
    if (g.auth.name[0] && g.auth.token[0]) g.auth.state = AUTH_OK;
}
