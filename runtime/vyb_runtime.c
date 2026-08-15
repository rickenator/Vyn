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
// HEAP-STRING REGISTRY
// ============================================================================
// The runtime string producers (to_string / concat / serialization) hand back
// freshly allocated char* buffers that Vyb's generated code may or may not free.
// We keep a registry of those live buffers so codegen can call
// __vyb_string_free() unconditionally on a serialization/string value: freeing
// is a safe no-op for pointers we did not allocate (e.g. literals in .rodata),
// and a second free of an already-freed buffer is also a no-op (no double free).
#define VYB_STR_REG_CAP 262144
static void* vyb_str_reg[VYB_STR_REG_CAP] = {0};
static void vyb_str_registry_insert(void* p) {
    if (!p) return;
    size_t h = (size_t)((uintptr_t)p / 16) ^ ((size_t)(uintptr_t)p / 4096);
    h &= VYB_STR_REG_CAP - 1;
    for (size_t i = 0; i < VYB_STR_REG_CAP; ++i) {
        size_t idx = (h + i) & (VYB_STR_REG_CAP - 1);
        if (vyb_str_reg[idx] == NULL) { vyb_str_reg[idx] = p; return; }
        if (vyb_str_reg[idx] == p) return;
    }
}
VYB_WEAK void __vyb_string_register(void* p) { vyb_str_registry_insert(p); }

VYB_WEAK void __vyb_string_free(void* p) {
    if (!p) return;
    size_t h = (size_t)((uintptr_t)p / 16) ^ ((size_t)(uintptr_t)p / 4096);
    h &= VYB_STR_REG_CAP - 1;
    for (size_t i = 0; i < VYB_STR_REG_CAP; ++i) {
        size_t idx = (h + i) & (VYB_STR_REG_CAP - 1);
        if (vyb_str_reg[idx] == p) { vyb_str_reg[idx] = NULL; free(p); return; }
        if (vyb_str_reg[idx] == NULL) return;   // not allocated by us
    }
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
    if (!str) {
        *success = false;
        return strdup("");
    }

    *success = true;
    return strdup(str);
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
