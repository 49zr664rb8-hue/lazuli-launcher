/*  net.h —— HTTP 客户端、文件队列、多线程分块下载器 */
#ifndef LZ_NET_H
#define LZ_NET_H

#include "common.h"

typedef struct { char *data; size_t len; } Buf;

/* ---- 基础 HTTP ---- */
int  http_init(void);
void http_shutdown(void);

int http_get(const char *url, Buf *out, int *status);
int http_get_h(const char *url, const char **hdrs, int n, Buf *out, int *status);
int http_post(const char *url, const char *body, const char *ctype,
              const char **hdrs, int n, Buf *out, int *status);
int http_get_text(const char *url, char **out);           /* 需 free */
int http_verb_len(const char *method, const char *url, const char *body, size_t blen,
                  const char *ctype, const char **hdrs, int n, Buf *out, int *status);
int http_head_info(const char *url, long long *size, int *ranges, char *final_url, size_t cap);

char *lz_url_encode(const char *s);                       /* 需 free */
void  buf_free(Buf *b);

/* ---- 文件队列（批量小文件，多线程） ---- */
typedef struct FileQueue FileQueue;

FileQueue *fq_create(int nthreads, void (*on_change)(void));
void       fq_add(FileQueue *q, const char *url, const char *path);
void       fq_add_size(FileQueue *q, const char *url, const char *path, long long size);
int        fq_run(FileQueue *q);          /* 阻塞直到全部完成，返回失败数 */
void       fq_cancel(FileQueue *q);
const char *fq_last_error(FileQueue *q);
void       fq_progress(FileQueue *q, int *total, int *done, int *failed,
                       long long *bytes_done, long long *bytes_total);
void       fq_destroy(FileQueue *q);

/* ---- 多线程分块下载任务（百宝箱） ---- */
typedef struct DlTask DlTask;

DlTask    *dl_start(const char *url, const char *path, int nthreads, void (*on_change)(void));
void       dl_cancel(DlTask *t);
void       dl_free(DlTask *t);
int        dl_state(DlTask *t);            /* 0排队 1下载中 2完成 3失败 4已取消 */
long long  dl_total(DlTask *t);
long long  dl_got(DlTask *t);
double     dl_speed(DlTask *t);            /* 字节/秒 */
const char *dl_error(DlTask *t);
const char *dl_path(DlTask *t);
const char *dl_name(DlTask *t);

#endif /* LZ_NET_H */
