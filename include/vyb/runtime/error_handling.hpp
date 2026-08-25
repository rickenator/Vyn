// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>

// Forward declarations for opaque types in generated code
extern "C" {

// String type used by Vyb (matches generated struct)
struct VybString {
    char* data;
    int64_t length;
};

// Source location information
struct VybSourceLocation {
    const char* file_path;
    uint32_t line;
    uint32_t column;
};

// Stack frame information
struct VybStackFrame {
    const char* function_name;
    VybSourceLocation location;
    void* native_address;  // For native stack trace
};

// Stack trace
struct VybStackTrace {
    VybStackFrame* frames;
    size_t frame_count;
    size_t capacity;
};

// Error value representation
struct VybError {
    uint64_t type_hash;          // Stable hash for runtime matching
    const char* type_name;       // e.g., "DivisionByZero"
    void* payload;               // Error-specific payload bytes
    const char* file;            // Source file of fail site
    uint32_t line;               // Source line of fail site
    uint32_t col;                // Source column of fail site
    // Legacy aliases still used by runtime internals
    void* type_id;               // Unique type identifier (legacy)
    void* data;                  // Alias of payload
    size_t data_size;            // Size of payload
    VybSourceLocation location;  // Alias of file/line/col
    VybStackTrace* stack_trace;  // Captured stack
    VybError* cause;             // Causality chain
    uint64_t timestamp;          // When error occurred
    uint64_t thread_id;          // Which thread
    void (*destructor)(void*);   // Cleanup for data
};

// Handler function type for custom untrapped error handling
typedef void (*VybUntrappedErrorHandler)(VybError* error, VybStackTrace* trace);

// ===== Runtime Function Declarations =====

// Panic - immediate termination (noreturn)
void __vyb_runtime_panic(const char* message) __attribute__((noreturn));

// Untrapped error - cleanup then exit (noreturn)
void __vyb_runtime_untrapped_error(VybError* error) __attribute__((noreturn));

// Error creation
VybError* __vyb_runtime_create_error(
    const char* type_name,
    void* type_id,
    void* data,
    size_t data_size,
    void (*destructor)(void*),
    VybSourceLocation location
);

VybError* __vyb_runtime_create_error_ex(
    const char* type_name,
    void* type_id,
    void* data,
    size_t data_size,
    void (*destructor)(void*),
    const char* file,
    uint32_t line,
    uint32_t column
);

// Error cleanup
void __vyb_runtime_free_error(VybError* error);
VybError* __vyb_runtime_clone_error(VybError* error);
void __vyb_runtime_set_error_cause(VybError* error, VybError* cause);

// Stack trace operations
VybStackTrace* __vyb_runtime_capture_stack_trace(size_t max_frames);
void __vyb_runtime_print_stack_trace(VybStackTrace* trace, FILE* output);
void __vyb_runtime_free_stack_trace(VybStackTrace* trace);

// Call stack management for Vyb-level stack traces (Phase 6.4)
void __vyb_runtime_push_call_frame(const char* function_name, const char* file_path, uint32_t line, uint32_t column);
void __vyb_runtime_pop_call_frame();
VybStackTrace* __vyb_runtime_get_current_stack_trace();

// Custom handler registration
void __vyb_runtime_set_untrapped_handler(VybUntrappedErrorHandler handler);
void __vyb_runtime_clear_untrapped_handler();
VybUntrappedErrorHandler __vyb_runtime_get_untrapped_handler();

// Type identity registry (id -> name), populated at module startup so a runtime
// `Type` value (an opaque uint64 type ID) can be resolved back to its name.
void __vyb_register_typename(uint64_t type_id, const char* type_name);
const char* __vyb_get_typename(uint64_t type_id);

} // extern "C"
