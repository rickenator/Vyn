// Vyb Runtime Library - Type Conversion Functions
// Comprehensive runtime support for Vyb type conversions

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

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
