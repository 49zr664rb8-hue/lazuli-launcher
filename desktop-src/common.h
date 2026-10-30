/* ==========================================================================
 *  琉璃启动器 Lazuli Launcher
 *  common.h —— 公共类型与基础工具声明
 *  纯 C11 实现，UI 层使用 Win32 + GDI+ 扁平接口
 * ========================================================================== */
#ifndef LZ_COMMON_H
#define LZ_COMMON_H

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* ---- 常用宏 ---- */
#define LZ_MIN(a,b) ((a)<(b)?(a):(b))
#define LZ_MAX(a,b) ((a)>(b)?(a):(b))
#define LZ_CLAMP(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define LZ_COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define LZ_UNUSED(x) ((void)(x))

/* 应用信息 */
#define LZ_NAME      "琉璃启动器"
#define LZ_NAME_EN   "Lazuli Launcher"
#define LZ_VERSION   "1.1.4"

/* ==========================================================================
 *  字符串构建器
 * ========================================================================== */
typedef struct {
    char  *s;
    size_t n;
    size_t cap;
} Sb;

void  sb_init(Sb *b);
void  sb_free(Sb *b);
void  sb_clear(Sb *b);
void  sb_add(Sb *b, const char *s);
void  sb_addn(Sb *b, const char *s, size_t n);
void  sb_addc(Sb *b, char c);
void  sb_printf(Sb *b, const char *fmt, ...);
char *sb_take(Sb *b);                 /* 交出缓冲区所有权并重置 */

/* ==========================================================================
 *  工具函数
 * ========================================================================== */
wchar_t *lz_u8_to_w(const char *s);    /* 需 free */
char    *lz_w_to_u8(const wchar_t *w); /* 需 free */
void     lz_path_join(char *out, size_t cap, const char *a, const char *b);
void     lz_path_slash(char *p);       /* 统一成 '\\' */
int      lz_dir_make(const char *path);/* 递归创建目录 */
int      lz_file_exists(const char *path);
long long lz_file_size(const char *path);
int      lz_file_read(const char *path, char **out, size_t *len);
int      lz_file_write(const char *path, const void *data, size_t len);
void     lz_dir_size(const char *path, long long *bytes, int *files);
int      lz_dir_remove(const char *path);   /* 递归删除 */
void     lz_appdata(char *out, size_t cap);
void     lz_exe_dir(char *out, size_t cap);
void     lz_md5_hex(const void *data, size_t len, char out[33]);
uint64_t lz_now_ms(void);
unsigned lz_rand(unsigned lo, unsigned hi);
void     lz_trim(char *s);
int      lz_stricmp(const char *a, const char *b);
int      lz_icontains(const char *hay, const char *needle);
void     lz_sanitize_filename(char *s);
double   lz_now_sec(void);
int      lz_shell_open(const char *url);
void     lz_clipboard_set(const char *text);
char    *lz_clipboard_get(void);        /* 需 free */

/* ==========================================================================
 *  JSON（极简 DOM）
 * ========================================================================== */
typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
struct JVal {
    JType t;
    union {
        int    b;
        double num;
        char  *str;
        struct { JVal **items; int n; } arr;
        struct { char **keys; JVal **vals; int n; } obj;
    } u;
};

JVal       *json_parse(const char *text);
void        json_free(JVal *v);
JVal       *json_get(JVal *o, const char *key);      /* 对象取值，无则 NULL */
JVal       *json_path(JVal *o, const char *path);    /* "a.b.c" */
const char *json_str(JVal *v, const char *def);
double      json_num(JVal *v, double def);
int         json_bool(JVal *v, int def);
int         json_arr_n(JVal *v);
JVal       *json_arr_at(JVal *v, int i);
int         json_obj_n(JVal *v);
const char *json_key_at(JVal *v, int i);
JVal       *json_val_at(JVal *v, int i);

#endif /* LZ_COMMON_H */
