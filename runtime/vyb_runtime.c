// Vyb Runtime Library - Type Conversion Functions
// Comprehensive runtime support for Vyb type conversions

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#if defined(__GNUC__) || defined(__clang__)
#define VYB_WEAK __attribute__((weak))
#else
#define VYB_WEAK
#endif

// ============================================================================
// CORE RUNTIME SHIMS USED BY NATIVE BUILDS
// ============================================================================
// HEAP-STRING REFERENCE COUNTING
// ============================================================================
// The runtime string producers (to_string / concat) hand back freshly allocated
// char* buffers that generated code shares across variables, parameters, Vec
// elements, and temporaries. Because a String value is a shallow { ptr, len }
// that many holders may point at, every heap buffer is reference counted in a
// registry: each hold calls __vyb_string_retain() when it takes a reference and
// __vyb_string_release() when it drops one (freeing only when the last
// reference goes away). Pointers we never allocated (string literals in
// .rodata) are not in the registry, so retain/release are safe no-ops for them.
// The table has a large fixed capacity — fine for desktop/lifetime-of-program
// use; swap in a dynamic table when scaling memory-heavy workloads later.
#define VYB_STR_REG_CAP 262144
typedef struct { void* p; int64_t refs; } vyb_str_ref;
static vyb_str_ref vyb_str_reg[VYB_STR_REG_CAP] = {0};

static vyb_str_ref* vyb_str_lookup_slot(void* p) {
    if (!p) return NULL;
    size_t h = (size_t)((uintptr_t)p / 16) ^ ((size_t)(uintptr_t)p / 4096);
    h &= VYB_STR_REG_CAP - 1;
    for (size_t i = 0; i < VYB_STR_REG_CAP; ++i) {
        size_t idx = (h + i) & (VYB_STR_REG_CAP - 1);
        if (vyb_str_reg[idx].p == NULL || vyb_str_reg[idx].p == p) return &vyb_str_reg[idx];
    }
    return NULL; // table full — buffer becomes invisible to reference counting
}

// Register a just-created heap buffer with an initial reference count of 1.
VYB_WEAK void __vyb_string_register(void* p) {
    vyb_str_ref* s = vyb_str_lookup_slot(p);
    if (!s) return;
    if (s->p == NULL) { s->p = p; s->refs = 1; }
}

// Take one reference on a buffer (untracked = literal/foreign -> no-op).
VYB_WEAK void* __vyb_string_retain(void* p) {
    vyb_str_ref* s = vyb_str_lookup_slot(p);
    if (!s || s->p == NULL) return p;
    s->refs++;
    return p;
}

// Drop one reference; free the buffer when the last reference is released.
VYB_WEAK void __vyb_string_release(void* p) {
    if (!p) return;
    vyb_str_ref* s = vyb_str_lookup_slot(p);
    if (!s || s->p == NULL) return;
    if (--s->refs <= 0) {
        s->p = NULL;
        s->refs = 0;
        free(p);
    }
}

// Compatibility alias: existing "__vyb_string_free" calls drop one reference
// (the current release semantic).
VYB_WEAK void __vyb_string_free(void* p) { __vyb_string_release(p); }

// The generated code retains each String it places into a Vec<String> (push /
// set) and, when the whole Vec is dropped (clear / scope exit / deep-copy
// teardown), releases every element with these bulk helpers. A Vyb String
// stored as a Vec element is its element-0 data pointer (a single 8-byte slot),
// so a Vec<String> element buffer is a `char**` array. Drop one reference on
// each; string literals (.rodata, not registered) make retain/release a safe
// no-op.
VYB_WEAK void __vyb_string_release_each(void* arr, int64_t n) {
    if (!arr || n <= 0) return;
    char** e = (char**)arr;
    for (int64_t i = 0; i < n; ++i) __vyb_string_release(e[i]);
}

// Take one reference on each of the first `n` String elements in `arr` (used when
// the same elements are shallow-copied into an independent buffer that will own
// its own references).
VYB_WEAK void __vyb_string_retain_each(void* arr, int64_t n) {
    if (!arr || n <= 0) return;
    char** e = (char**)arr;
    for (int64_t i = 0; i < n; ++i) __vyb_string_retain(e[i]);
}

// ============================================================================

VYB_WEAK void __vyb_println(const char* str) {
    fputs(str ? str : "", stdout);
    fputc('\n', stdout);
}

VYB_WEAK void __vyb_print(const char* str) {
    fputs(str ? str : "", stdout);
}

VYB_WEAK void __vyb_println_int(int64_t value) {
    printf("%lld\n", (long long)value);
}

VYB_WEAK void __vyb_print_int(int64_t value) {
    printf("%lld", (long long)value);
}

VYB_WEAK void __vyb_println_bool(int64_t value) {
    puts(value ? "true" : "false");
}

VYB_WEAK void __vyb_print_bool(int64_t value) {
    fputs(value ? "true" : "false", stdout);
}

VYB_WEAK void __vyb_runtime_push_call_frame(const char* function_name, const char* file_path, uint32_t line, uint32_t column) {
    (void)function_name;
    (void)file_path;
    (void)line;
    (void)column;
}

VYB_WEAK void __vyb_runtime_pop_call_frame(void) {}

// ============================================================================
// PRIMITIVE TYPE CONVERSIONS: to_string()
// ============================================================================

// Int to String conversion
char* __vyb_int_to_string(int64_t value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", (long)value);
    char* r = strdup(buffer);
    __vyb_string_register(r);
    return r;
}

// Unsigned integer to string (formats as unsigned, e.g. UInt8 = 250 -> "250").
char* __vyb_uint_to_string(uint64_t value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
    char* r = strdup(buffer);
    __vyb_string_register(r);
    return r;
}

// Float to String conversion
char* __vyb_float_to_string(double value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%g", value);
    char* r = strdup(buffer);
    __vyb_string_register(r);
    return r;
}

// Bool to String conversion
char* __vyb_bool_to_string(bool value) {
    char* r = strdup(value ? "true" : "false");
    __vyb_string_register(r);
    return r;
}

// String to String (identity, but creates a copy)
char* __vyb_string_to_string(const char* str) {
    char* r = strdup(str ? str : "");
    __vyb_string_register(r);
    return r;
}

// ============================================================================
// PRIMITIVE TYPE CONVERSIONS: from_string()
// ============================================================================

// String to Int conversion
int64_t __vyb_int_from_string(const char* str, bool* success) {
    if (!str || !*str) {
        *success = false;
        return 0;
    }

    char* endptr;
    errno = 0;
    long long value = strtoll(str, &endptr, 10);

    if (errno != 0 || *endptr != '\0') {
        *success = false;
        return 0;
    }

    *success = true;
    return (int64_t)value;
}

// String to Float conversion
double __vyb_float_from_string(const char* str, bool* success) {
    if (!str || !*str) {
        *success = false;
        return 0.0;
    }

    char* endptr;
    errno = 0;
    double value = strtod(str, &endptr);

    if (errno != 0 || *endptr != '\0') {
        *success = false;
        return 0.0;
    }

    *success = true;
    return value;
}

// String to Bool conversion
bool __vyb_bool_from_string(const char* str, bool* success) {
    if (!str) {
        *success = false;
        return false;
    }

    if (strcmp(str, "true") == 0 || strcmp(str, "1") == 0) {
        *success = true;
        return true;
    }

    if (strcmp(str, "false") == 0 || strcmp(str, "0") == 0) {
        *success = true;
        return false;
    }

    *success = false;
    return false;
}

// String to String (identity with validation)
char* __vyb_string_from_string(const char* str, bool* success) {
    char* r;
    if (!str) {
        *success = false;
        r = strdup("");
    } else {
        *success = true;
        r = strdup(str);
    }
    // A fresh copy is an owned heap buffer: register it so the generated code's
    // reference-counted release reclaims it like the other __vyb_*_from_string /
    // to_string producers.
    __vyb_string_register(r);
    return r;
}

// ============================================================================
// COMPLEX TYPE CONVERSIONS: JSON serialization using type metadata
// ============================================================================

#include "vyb_type_metadata.h"

// Generic JSON serialization using type metadata
char* __vyb_complex_to_json(void* instance, const char* type_name) {
    VybTypeMetadata* metadata = __vyb_lookup_type(type_name);
    if (!metadata) {
        fprintf(stderr, "Error: Type '%s' not found in registry\n", type_name);
        return strdup("{}");
    }
    return __vyb_complex_to_json_with_metadata(instance, metadata);
}

// Generic JSON deserialization using type metadata
void* __vyb_complex_from_json(const char* json_str, const char* type_name) {
    VybTypeMetadata* metadata = __vyb_lookup_type(type_name);
    if (!metadata) {
        fprintf(stderr, "Error: Type '%s' not found in registry\n", type_name);
        return NULL;
    }
    return __vyb_complex_from_json_with_metadata(json_str, metadata);
}

// ============================================================================
// FILE I/O
// ============================================================================
// The `io` stdlib module wraps these helpers in a Vyb `File` value. The flags
// argument is a stable, portable bitmask (independent of the host <fcntl.h>
// constants): bit 0 read, bit 1 write, bit 2 read+write, bit 7 create,
// bit 9 truncate, bit 10 append. `read_all` returns a heap buffer registered
// with the string registry so a Vyb String built over it is freed when its last
// reference is dropped; the pair { ptr, len } is returned by value.

#define VYB_FILE_O_READ     (1 << 0)
#define VYB_FILE_O_WRITE    (1 << 1)
#define VYB_FILE_O_RDWR     (1 << 2)
#define VYB_FILE_O_CREAT    (1 << 7)
#define VYB_FILE_O_TRUNC    (1 << 9)
#define VYB_FILE_O_APPEND   (1 << 10)

typedef struct { char* ptr; int64_t len; } vyb_file_str;

static int vyb_file_err = 0;

VYB_WEAK int64_t __vyb_file_open(const char* path, int64_t flags) {
    int oflags = 0;
    if (flags & VYB_FILE_O_READ) oflags |= O_RDONLY;
    if (flags & VYB_FILE_O_WRITE) oflags |= O_WRONLY;
    if (flags & VYB_FILE_O_RDWR) oflags |= O_RDWR;
    if (flags & VYB_FILE_O_CREAT) oflags |= O_CREAT;
    if (flags & VYB_FILE_O_TRUNC) oflags |= O_TRUNC;
    if (flags & VYB_FILE_O_APPEND) oflags |= O_APPEND;
    int fd = open(path ? path : "", oflags, 0644);
    vyb_file_err = (fd < 0) ? errno : 0;
    return (int64_t)fd;
}

VYB_WEAK int64_t __vyb_file_close(int64_t fd) {
    int r = close((int)fd);
    vyb_file_err = (r < 0) ? errno : 0;
    return (int64_t)r;
}

// Write `len` bytes from `data` to `fd`, retrying partial writes. Returns the
// total bytes written, or -1 on error (with errno captured).
VYB_WEAK int64_t __vyb_file_write(int64_t fd, const char* data, int64_t len) {
    const char* p = data;
    int64_t left = len;
    while (left > 0) {
        ssize_t n = write((int)fd, p, (size_t)left);
        if (n > 0) { p += n; left -= n; }
        else if (n == 0) break;
        else { vyb_file_err = errno; return -1; }
    }
    vyb_file_err = 0;
    return len - left;
}

// Read the whole file from `fd` into a freshly malloc'd, registry-registered
// buffer. Returns { ptr, len }; ptr is NULL when the read fails.
VYB_WEAK vyb_file_str __vyb_file_read_all(int64_t fd) {
    vyb_file_str r = { NULL, 0 };
    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) { vyb_file_err = errno; return r; }
    for (;;) {
        if (len == cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { vyb_file_err = errno; free(buf); return r; }
            buf = nb;
        }
        ssize_t n = read((int)fd, buf + len, cap - len);
        if (n > 0) {
            len += (size_t)n;
        } else if (n == 0) {
            break;
        } else {
            vyb_file_err = errno;
            free(buf);
            return r;
        }
    }
    // Owned heap buffer: its first holder is the Vyb String built over it, so
    // register it (refcount 1) and let the String's release free it.
    __vyb_string_register(buf);
    r.ptr = buf;
    r.len = (int64_t)len;
    vyb_file_err = 0;
    return r;
}

VYB_WEAK int64_t __vyb_file_error_code(void) {
    return (int64_t)vyb_file_err;
}

// Human-readable message for the last file-op error. Returns an owned heap
// copy (registered with the string registry) so the Vyb String built over it is
// freed when its last reference is dropped.
VYB_WEAK char* __vyb_file_error_message(void) {
    const char* m = (vyb_file_err == 0) ? "no error" : strerror(vyb_file_err);
    char* copy = strdup(m);
    if (copy) __vyb_string_register(copy);
    return copy;
}
