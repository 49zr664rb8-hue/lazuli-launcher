/*  json.c —— 递归下降 JSON 解析器 */
#include "common.h"

typedef struct { const char *p; int depth; } P;

static JVal *new_val(JType t) {
    JVal *v = (JVal *)calloc(1, sizeof(JVal));
    if (v) v->t = t;
    return v;
}

static void skip_ws(P *s) {
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r') s->p++;
}

static void utf8_put(Sb *b, unsigned cp) {
    char t[4];
    if (cp < 0x80) { t[0] = (char)cp; sb_addn(b, t, 1); }
    else if (cp < 0x800) {
        t[0] = (char)(0xC0 | (cp >> 6));
        t[1] = (char)(0x80 | (cp & 0x3F));
        sb_addn(b, t, 2);
    } else if (cp < 0x10000) {
        t[0] = (char)(0xE0 | (cp >> 12));
        t[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        t[2] = (char)(0x80 | (cp & 0x3F));
        sb_addn(b, t, 3);
    } else {
        t[0] = (char)(0xF0 | (cp >> 18));
        t[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        t[3] = (char)(0x80 | (cp & 0x3F));
        sb_addn(b, t, 4);
    }
}

static int hex4(const char *p) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i]; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

static char *parse_string(P *s) {
    if (*s->p != '"') return NULL;
    s->p++;
    Sb b; sb_init(&b);
    sb_add(&b, "");
    while (*s->p && *s->p != '"') {
        unsigned char c = (unsigned char)*s->p;
        if (c == '\\') {
            s->p++;
            char e = *s->p;
            switch (e) {
                case 'n': sb_addc(&b, '\n'); s->p++; break;
                case 't': sb_addc(&b, '\t'); s->p++; break;
                case 'r': sb_addc(&b, '\r'); s->p++; break;
                case 'b': sb_addc(&b, '\b'); s->p++; break;
                case 'f': sb_addc(&b, '\f'); s->p++; break;
                case '/': sb_addc(&b, '/');  s->p++; break;
                case '\\': sb_addc(&b, '\\'); s->p++; break;
                case '"': sb_addc(&b, '"');  s->p++; break;
                case 'u': {
                    s->p++;
                    int cp = hex4(s->p);
                    if (cp < 0) { s->p--; break; }
                    s->p += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && s->p[0] == '\\' && s->p[1] == 'u') {
                        int lo = hex4(s->p + 2);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            s->p += 6;
                        }
                    }
                    utf8_put(&b, (unsigned)cp);
                    break;
                }
                default: sb_addc(&b, e); s->p++; break;
            }
        } else {
            sb_addc(&b, (char)c);
            s->p++;
        }
    }
    if (*s->p == '"') s->p++;
    return sb_take(&b);
}

static JVal *parse_value(P *s);

static JVal *parse_array(P *s) {
    JVal *v = new_val(J_ARR);
    if (!v) return NULL;
    s->p++; /* [ */
    int cap = 0;
    for (;;) {
        skip_ws(s);
        if (*s->p == ']') { s->p++; break; }
        if (!*s->p) break;
        JVal *it = parse_value(s);
        if (!it) break;
        if (v->u.arr.n >= cap) {
            cap = cap ? cap * 2 : 8;
            v->u.arr.items = (JVal **)realloc(v->u.arr.items, sizeof(JVal *) * (size_t)cap);
        }
        v->u.arr.items[v->u.arr.n++] = it;
        skip_ws(s);
        if (*s->p == ',') { s->p++; continue; }
        if (*s->p == ']') { s->p++; break; }
        break;
    }
    return v;
}

static JVal *parse_object(P *s) {
    JVal *v = new_val(J_OBJ);
    if (!v) return NULL;
    s->p++; /* { */
    int cap = 0;
    for (;;) {
        skip_ws(s);
        if (*s->p == '}') { s->p++; break; }
        if (!*s->p) break;
        char *k = parse_string(s);
        if (!k) break;
        skip_ws(s);
        if (*s->p != ':') { free(k); break; }
        s->p++;
        JVal *it = parse_value(s);
        if (!it) { free(k); break; }
        if (v->u.obj.n >= cap) {
            cap = cap ? cap * 2 : 8;
            v->u.obj.keys = (char **)realloc(v->u.obj.keys, sizeof(char *) * (size_t)cap);
            v->u.obj.vals = (JVal **)realloc(v->u.obj.vals, sizeof(JVal *) * (size_t)cap);
        }
        v->u.obj.keys[v->u.obj.n] = k;
        v->u.obj.vals[v->u.obj.n] = it;
        v->u.obj.n++;
        skip_ws(s);
        if (*s->p == ',') { s->p++; continue; }
        if (*s->p == '}') { s->p++; break; }
        break;
    }
    return v;
}

static JVal *parse_value(P *s) {
    skip_ws(s);
    if (s->depth++ > 200) { s->depth--; return NULL; }
    JVal *r = NULL;
    char c = *s->p;
    if (c == '{') r = parse_object(s);
    else if (c == '[') r = parse_array(s);
    else if (c == '"') {
        char *str = parse_string(s);
        r = new_val(J_STR);
        if (r) r->u.str = str ? str : NULL;
        if (r && !r->u.str) r->u.str = (char *)calloc(1, 1);
    } else if (c == 't' && !strncmp(s->p, "true", 4)) {
        s->p += 4; r = new_val(J_BOOL); if (r) r->u.b = 1;
    } else if (c == 'f' && !strncmp(s->p, "false", 5)) {
        s->p += 5; r = new_val(J_BOOL); if (r) r->u.b = 0;
    } else if (c == 'n' && !strncmp(s->p, "null", 4)) {
        s->p += 4; r = new_val(J_NULL);
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        char *end = NULL;
        double d = strtod(s->p, &end);
        if (end == s->p) { s->depth--; return NULL; }
        s->p = end;
        r = new_val(J_NUM);
        if (r) r->u.num = d;
    } else {
        s->depth--;
        return NULL;
    }
    s->depth--;
    return r;
}

JVal *json_parse(const char *text) {
    if (!text) return NULL;
    P s; s.p = text; s.depth = 0;
    /* 跳过 UTF-8 BOM */
    if ((unsigned char)s.p[0] == 0xEF && (unsigned char)s.p[1] == 0xBB) s.p += 3;
    return parse_value(&s);
}

void json_free(JVal *v) {
    if (!v) return;
    switch (v->t) {
        case J_STR: free(v->u.str); break;
        case J_ARR:
            for (int i = 0; i < v->u.arr.n; i++) json_free(v->u.arr.items[i]);
            free(v->u.arr.items);
            break;
        case J_OBJ:
            for (int i = 0; i < v->u.obj.n; i++) {
                free(v->u.obj.keys[i]);
                json_free(v->u.obj.vals[i]);
            }
            free(v->u.obj.keys);
            free(v->u.obj.vals);
            break;
        default: break;
    }
    free(v);
}

JVal *json_get(JVal *o, const char *key) {
    if (!o || o->t != J_OBJ || !key) return NULL;
    for (int i = 0; i < o->u.obj.n; i++)
        if (!strcmp(o->u.obj.keys[i], key)) return o->u.obj.vals[i];
    return NULL;
}

JVal *json_path(JVal *o, const char *path) {
    if (!o || !path) return NULL;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", path);
    JVal *cur = o;
    char *save = NULL;
    for (char *tok = strtok_r(buf, ".", &save); tok; tok = strtok_r(NULL, ".", &save)) {
        if (cur->t == J_ARR) {
            cur = json_arr_at(cur, atoi(tok));
        } else {
            cur = json_get(cur, tok);
        }
        if (!cur) return NULL;
    }
    return cur;
}

const char *json_str(JVal *v, const char *def) {
    if (!v) return def;
    if (v->t == J_STR) return v->u.str ? v->u.str : def;
    return def;
}

double json_num(JVal *v, double def) {
    if (!v) return def;
    if (v->t == J_NUM) return v->u.num;
    if (v->t == J_STR) return atof(v->u.str);
    if (v->t == J_BOOL) return v->u.b ? 1 : 0;
    return def;
}

int json_bool(JVal *v, int def) {
    if (!v) return def;
    if (v->t == J_BOOL) return v->u.b;
    if (v->t == J_NUM) return v->u.num != 0;
    return def;
}

int json_arr_n(JVal *v) { return (v && v->t == J_ARR) ? v->u.arr.n : 0; }

JVal *json_arr_at(JVal *v, int i) {
    if (!v || v->t != J_ARR || i < 0 || i >= v->u.arr.n) return NULL;
    return v->u.arr.items[i];
}

int json_obj_n(JVal *v) { return (v && v->t == J_OBJ) ? v->u.obj.n : 0; }

const char *json_key_at(JVal *v, int i) {
    if (!v || v->t != J_OBJ || i < 0 || i >= v->u.obj.n) return NULL;
    return v->u.obj.keys[i];
}

JVal *json_val_at(JVal *v, int i) {
    if (!v || v->t != J_OBJ || i < 0 || i >= v->u.obj.n) return NULL;
    return v->u.obj.vals[i];
}
