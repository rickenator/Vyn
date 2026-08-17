// SPDX-License-Identifier: Apache-2.0

// Vyb Type Metadata Runtime Implementation
#include "vyb_type_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Heap-string registry hooks (defined in vyb_runtime.c). Registering a JSON
// String field lets the generated code's release path reclaim the buffer.
extern void __vyb_string_register(void* p);

// Global type registry (simple linear search for now)
#define MAX_TYPES 256
static VybTypeMetadata* g_type_registry[MAX_TYPES];
static size_t g_num_types = 0;

// Register a type in the global registry
void __vyb_register_type(VybTypeMetadata* metadata) {
    if (g_num_types >= MAX_TYPES) {
        fprintf(stderr, "Error: Type registry full\n");
        return;
    }
    g_type_registry[g_num_types++] = metadata;
}

// Lookup type metadata by name
VybTypeMetadata* __vyb_lookup_type(const char* type_name) {
    for (size_t i = 0; i < g_num_types; i++) {
        if (strcmp(g_type_registry[i]->type_name, type_name) == 0) {
            return g_type_registry[i];
        }
    }
    return NULL;
}

// --- Scalable JSON emit ----------------------------------------------------
// A growable output buffer so arbitrarily deep / large structures (long strings,
// wide Vec fields, nested structs) are never truncated at a fixed cap.
typedef struct {
    char* data;
    size_t len;
    size_t cap;
} JsonBuf;

static void jb_reserve(JsonBuf* b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        while (nc < b->len + extra + 1) nc *= 2;
        char* nd = (char*)realloc(b->data, nc);
        if (!nd) { fprintf(stderr, "json: out of memory\n"); exit(1); }
        b->data = nd; b->cap = nc;
    }
}
static void jb_s(JsonBuf* b, const char* s) {
    size_t n = strlen(s);
    jb_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

// Append a JSON string literal (quoted + escaped) for [s, s+len).
static void push_json_string(JsonBuf* b, const char* s, int64_t len) {
    jb_s(b, "\"");
    if (s) {
        for (int64_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)s[i];
            switch (c) {
                case '"':  jb_s(b, "\\\""); break;
                case '\\': jb_s(b, "\\\\"); break;
                case '\n': jb_s(b, "\\n"); break;
                case '\t': jb_s(b, "\\t"); break;
                case '\r': jb_s(b, "\\r"); break;
                case '\b': jb_s(b, "\\b"); break;
                case '\f': jb_s(b, "\\f"); break;
                default:
                    if (c < 0x20) {
                        char t[8];
                        snprintf(t, sizeof t, "\\u%04x", c);
                        jb_s(b, t);
                    } else {
                        char t[2] = { (char)c, '\0' };
                        jb_reserve(b, 1);
                        b->data[b->len++] = t[0];
                        b->data[b->len] = '\0';
                    }
            }
        }
    }
    jb_s(b, "\"");
}

// Inline byte-size of a value of type \p tn (stored in a field slot or a Vec
// element slot). Falls back to 8 so an unknown type still walks plausible slots.
static size_t type_size(const char* tn) {
    if (!tn) return 8;
    if (strcmp(tn, "Int") == 0 || strcmp(tn, "Float") == 0) return 8;
    if (strcmp(tn, "Bool") == 0) return 1;
    if (strcmp(tn, "String") == 0) return 16;             // { char*, int64 }
    if (strncmp(tn, "Vec<", 4) == 0) return 24;           // { void*, int64, int64 }
    VybTypeMetadata* m = __vyb_lookup_type(tn);
    if (m) return m->struct_size;
    return 8;
}

static void push_json_struct(JsonBuf* b, void* inst, VybTypeMetadata* m);

// Serialize the value located at \p p whose type is \p tn (Int/Float/Bool/String
// scalar, a Vec<T> array, or a nested struct) to \p b.
static void push_json_value(JsonBuf* b, void* p, const char* tn) {
    if (!tn) return;
    if (strcmp(tn, "Int") == 0) {
        char t[32]; snprintf(t, sizeof t, "%ld", (long)*(int64_t*)p); jb_s(b, t);
    } else if (strcmp(tn, "Float") == 0) {
        char t[40]; snprintf(t, sizeof t, "%g", *(double*)p); jb_s(b, t);
    } else if (strcmp(tn, "Bool") == 0) {
        jb_s(b, (*(bool*)p) ? "true" : "false");
    } else if (strcmp(tn, "String") == 0) {
        typedef struct { char* data; int64_t length; } StrSlot;
        StrSlot* s = (StrSlot*)p;
        push_json_string(b, s->data, s->length);
    } else if (strncmp(tn, "Vec<", 4) == 0) {
        typedef struct { void* data; int64_t size; int64_t cap; } VecSlot;
        VecSlot* v = (VecSlot*)p;
        // Element type: substring between the angled brackets (handles Vec<Vec<T>>).
        char elem[512] = { 0 };
        const char* last = tn ? strrchr(tn, '>') : NULL;
        const char* s = tn + 4;
        if (last && last > s) {
            size_t n = (size_t)(last - s);
            if (n < sizeof(elem) - 1) { memcpy(elem, s, n); }
        }
        const char* et = *elem ? elem : "Int";
        size_t es = type_size(et);
        jb_s(b, "[");
        for (int64_t i = 0; i < v->size; i++) {
            if (i) jb_s(b, ", ");
            push_json_value(b, (char*)v->data + (size_t)i * es, et);
        }
        jb_s(b, "]");
    } else {
        VybTypeMetadata* m = __vyb_lookup_type(tn);
        if (m) push_json_struct(b, p, m);
        else jb_s(b, "null");
    }
}

static void push_json_struct(JsonBuf* b, void* inst, VybTypeMetadata* m) {
    jb_s(b, "{");
    for (size_t i = 0; i < m->num_fields; i++) {
        if (i) jb_s(b, ", ");
        VybFieldMetadata* f = &m->fields[i];
        push_json_string(b, f->name, (int64_t)strlen(f->name));
        jb_s(b, ": ");
        push_json_value(b, (char*)inst + f->offset, f->type_name);
    }
    jb_s(b, "}");
}

// Serialize a complex type to JSON using its metadata. Returns a fresh heap
// buffer (caller registers/reclaims it through the Vyb string registry).
char* __vyb_complex_to_json_with_metadata(void* instance, VybTypeMetadata* metadata) {
    if (!instance || !metadata) {
        return strdup("null");
    }
    JsonBuf b = { 0 };
    push_json_struct(&b, instance, metadata);
    return b.data ? b.data : strdup("{}");
}

// recursive-descent JSON deserializer (mirrors the growable serializer): parses
// Int/Float/Bool/String scalars, Vec<T> arrays, and nested structs at any depth,
// writing each value into its field slot (or Vec element slot). Registered
// strings and Vec buffers are handed to the caller (the struct value copy) for
// ownership; the caller frees only the top-level instance buffer.
static void json_skip_ws(const char** p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}
static int json_parse_int(const char** p, int64_t* out) { json_skip_ws(p); *out = strtoll(*p, (char**)p, 10); return 1; }
static int json_parse_float(const char** p, double* out) { json_skip_ws(p); *out = strtod(*p, (char**)p); return 1; }
static int json_parse_bool(const char** p, bool* out) {
    json_skip_ws(p);
    if (!strncmp(*p, "true", 4))  { *out = true;  *p += 4; return 1; }
    if (!strncmp(*p, "false", 5)) { *out = false; *p += 5; return 1; }
    return 0;
}
static void json_vec_elem(const char* tn, char* out, size_t outmax) {
    out[0] = '\0';
    if (!tn) return;
    const char* s = strstr(tn, "<");
    const char* last = strrchr(tn, '>');
    if (s && last && last > s) {
        size_t n = (size_t)(last - (s + 1));
        if (n < outmax) { memcpy(out, s + 1, n); out[n] = '\0'; }
    }
}
// Read a quoted JSON string (with \escapes decoded) into a fresh heap buffer.
static char* json_read_string(const char** p) {
    json_skip_ws(p);
    if (**p != '"') return NULL;
    (*p)++;
    size_t cap = 64, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    while (**p && **p != '"') {
        char c = **p;
        if (c == '\\' && (*p)[1]) {
            (*p)++;
            char e = **p, r = '?';
            switch (e) {
                case '"': r = '"'; break; case '\\': r = '\\'; break; case '/': r = '/'; break;
                case 'n': r = '\n'; break; case 't': r = '\t'; break; case 'r': r = '\r'; break;
                case 'b': r = '\b'; break; case 'f': r = '\f'; break;
                case 'u': {
                    int hx = 0, ok = 1;
                    for (int k = 1; k <= 4; k++) {
                        char h = (*p)[k]; int d;
                        if (h >= '0' && h <= '9') d = h - '0';
                        else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                        else { ok = 0; break; }
                        hx = hx * 16 + d;
                    }
                    if (ok) { (*p) += 4; r = (hx < 128) ? (char)hx : '?'; }
                    else r = '?';
                    break;
                }
            }
            (*p)++;
            if (len + 1 >= cap) { cap *= 2; char* nb = (char*)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
            buf[len++] = r;
        } else {
            if (len + 1 >= cap) { cap *= 2; char* nb = (char*)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
            buf[len++] = c; (*p)++;
        }
    }
    if (**p == '"') (*p)++;
    buf[len] = '\0';
    return buf;
}
static void json_fill_value(void* slot, const char* tn, const char** p);

static void json_fill_object(void* inst, VybTypeMetadata* m, const char** p) {
    json_skip_ws(p);
    if (**p == '{') (*p)++; else return;
    json_skip_ws(p);
    while (**p && **p != '}') {
        json_skip_ws(p);
        if (**p == '}') break;
        char* name = json_read_string(p);
        if (!name) break;
        json_skip_ws(p);
        if (**p == ':') (*p)++; else { free(name); break; }
        VybFieldMetadata* f = NULL;
        for (size_t i = 0; i < m->num_fields; i++)
            if (!strcmp(m->fields[i].name, name)) { f = &m->fields[i]; break; }
        free(name);
        if (f) json_fill_value((char*)inst + f->offset, f->type_name, p);
        json_skip_ws(p);
        if (**p == ',') (*p)++;
        else if (**p == '}') { (*p)++; break; }
        else break;
    }
}

static void json_fill_value(void* slot, const char* tn, const char** p) {
    json_skip_ws(p);
    if (!tn) return;
    if (!strcmp(tn, "Int")) { json_parse_int(p, (int64_t*)slot); }
    else if (!strcmp(tn, "Float")) { json_parse_float(p, (double*)slot); }
    else if (!strcmp(tn, "Bool")) { json_parse_bool(p, (bool*)slot); }
    else if (!strcmp(tn, "String")) {
        typedef struct { char* data; int64_t length; } StrSlot;
        StrSlot* s = (StrSlot*)slot;
        char* v = json_read_string(p); if (!v) v = strdup("");
        s->data = v; s->length = (int64_t)strlen(v);
        __vyb_string_register(v);
    } else if (strncmp(tn, "Vec<", 4) == 0) {
        char elem[512] = { 0 }; json_vec_elem(tn, elem, sizeof elem);
        const char* et = *elem ? elem : "Int";
        size_t es = type_size(et);
        typedef struct { void* data; int64_t size; int64_t cap; } VecSlot;
        VecSlot* v = (VecSlot*)slot;
        v->data = NULL; v->size = 0; v->cap = 0;
        json_skip_ws(p);
        if (**p == '[') {
            (*p)++; json_skip_ws(p);
            size_t cap = 4; char* data = (char*)malloc(cap * es);
            if (!data) return;
            while (**p && **p != ']') {
                if ((size_t)v->size == cap) { cap *= 2; char* nb = (char*)realloc(data, cap * es); if (!nb) { free(data); return; } data = nb; }
                json_fill_value(data + (size_t)v->size * es, et, p);
                v->size++;
                json_skip_ws(p);
                if (**p == ',') { (*p)++; json_skip_ws(p); }
                else if (**p == ']') { (*p)++; break; }
                else break;
            }
            v->data = data; v->cap = (int64_t)cap;
        }
    } else {
        VybTypeMetadata* m = __vyb_lookup_type(tn);
        if (m) {
            void* tmp = calloc(1, m->struct_size);
            if (tmp) { json_fill_object(tmp, m, p); memcpy(slot, tmp, m->struct_size); free(tmp); }
        }
    }
}

// Deserialize JSON to a complex type using metadata. First-pass callers free the
// returned instance buffer after copying the value; owned String/Vec payloads
// (registered / malloc'd) transfer to the copied value's scope-exit cleanup.
void* __vyb_complex_from_json_with_metadata(const char* json_str, VybTypeMetadata* metadata) {
    if (!json_str || !metadata) return NULL;
    void* instance = calloc(1, metadata->struct_size);
    if (!instance) return NULL;
    const char* p = json_str;
    json_fill_object(instance, metadata, &p);
    return instance;
}
