// SPDX-License-Identifier: Apache-2.0

#include "vyb/runtime/error_handling.hpp"
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

// Platform-specific includes for stack trace
#ifdef __linux__
#include <execinfo.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#else
// macOS
#include <execinfo.h>
#include <unistd.h>
#endif

extern "C" {

// Keep metadata layout in sync with runtime/vyb_type_metadata.h.
typedef struct VybFieldMetadata {
    const char* name;
    const char* type_name;
    size_t offset;
    size_t size;
    bool is_primitive;
    bool is_vec;
    const char* vec_element_type;
} VybFieldMetadata;

typedef struct VybTypeMetadata {
    const char* type_name;
    size_t struct_size;
    size_t num_fields;
    VybFieldMetadata* fields;
    size_t num_aspects;
    void* aspects;
} VybTypeMetadata;

VybTypeMetadata* __vyb_lookup_type(const char* type_name);
char* __vyb_complex_to_json_with_metadata(void* instance, VybTypeMetadata* metadata);

// ===== Type identity registry (id -> name) =====
static std::unordered_map<uint64_t, std::string> g_type_id_names;
static std::mutex g_type_id_names_mutex;

void __vyb_register_typename(uint64_t type_id, const char* type_name) {
    if (!type_name) return;
    std::lock_guard<std::mutex> lock(g_type_id_names_mutex);
    g_type_id_names[type_id] = type_name;
}

const char* __vyb_get_typename(uint64_t type_id) {
    std::lock_guard<std::mutex> lock(g_type_id_names_mutex);
    auto it = g_type_id_names.find(type_id);
    return it != g_type_id_names.end() ? it->second.c_str() : "Unknown";
}

// ===== Global State =====

static std::atomic<VybUntrappedErrorHandler> g_custom_handler{nullptr};

// Vyb-level call stack for source-level stack traces (Phase 6.4)
// Thread-local: the JITed fibers each run on their own OS thread and push/pop as
// they call functions, so a per-thread stack keeps frames un-interleaved and
// lets a crash handler (or error path) read exactly the running fiber's frames.
static thread_local std::vector<VybStackFrame> g_call_stack;
static constexpr size_t MAX_CALL_STACK_DEPTH = 256;

// ===== Helper Functions =====

static uint64_t get_timestamp_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t get_thread_id() {
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

static void format_timestamp(uint64_t timestamp_ns, char* buffer, size_t bufsize) {
    time_t seconds = timestamp_ns / 1000000000ULL;
    long nanoseconds = timestamp_ns % 1000000000ULL;
    struct tm* tm_info = localtime(&seconds);
    snprintf(buffer, bufsize, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             nanoseconds / 1000000);
}

// ===== Stack Trace Implementation =====

VybStackTrace* __vyb_runtime_capture_stack_trace(size_t max_frames) {
    VybStackTrace* trace = new VybStackTrace;
    trace->capacity = max_frames;
    trace->frames = new VybStackFrame[max_frames];
    trace->frame_count = 0;

#if defined(__linux__) || defined(__APPLE__)
    // Use backtrace
    void** addresses = new void*[max_frames];
    int count = backtrace(addresses, max_frames);

    // For now, just store addresses
    // Full symbol resolution would require DWARF parsing
    for (int i = 0; i < count; i++) {
        trace->frames[i].function_name = "<native>";
        trace->frames[i].location.file_path = "<unknown>";
        trace->frames[i].location.line = 0;
        trace->frames[i].location.column = 0;
        trace->frames[i].native_address = addresses[i];
        trace->frame_count++;
    }

    delete[] addresses;
#elif defined(_WIN32)
    // Windows stack trace
    void** addresses = new void*[max_frames];
    USHORT count = CaptureStackBackTrace(0, max_frames, addresses, NULL);

    for (USHORT i = 0; i < count; i++) {
        trace->frames[i].function_name = "<native>";
        trace->frames[i].location.file_path = "<unknown>";
        trace->frames[i].location.line = 0;
        trace->frames[i].location.column = 0;
        trace->frames[i].native_address = addresses[i];
        trace->frame_count++;
    }

    delete[] addresses;
#else
    // No stack trace support
    trace->frames[0].function_name = "<stack trace not available>";
    trace->frames[0].location.file_path = "";
    trace->frames[0].location.line = 0;
    trace->frames[0].location.column = 0;
    trace->frames[0].native_address = nullptr;
    trace->frame_count = 1;
#endif

    return trace;
}

void __vyb_runtime_print_stack_trace(VybStackTrace* trace, FILE* output) {
    fprintf(output, "\nStack Trace:\n");
    if (!trace || trace->frame_count == 0) {
        fprintf(output, "  <no stack trace available>\n");
        return;
    }

    for (size_t i = 0; i < trace->frame_count; i++) {
        const VybStackFrame& frame = trace->frames[i];
        if (frame.location.line > 0) {
            fprintf(output, "  at %s (%s:%u:%u)\n",
                    frame.function_name,
                    frame.location.file_path,
                    frame.location.line,
                    frame.location.column);
        } else {
            fprintf(output, "  at %s (address: %p)\n",
                    frame.function_name,
                    frame.native_address);
        }
    }
}

void __vyb_runtime_free_stack_trace(VybStackTrace* trace) {
    if (!trace) return;
    delete[] trace->frames;
    delete trace;
}

// ===== Vyb-Level Call Stack Management (Phase 6.4) =====

void __vyb_runtime_push_call_frame(const char* function_name, const char* file_path, uint32_t line, uint32_t column) {

    // Prevent stack overflow
    if (g_call_stack.size() >= MAX_CALL_STACK_DEPTH) {
        fprintf(stderr, "Warning: Call stack depth limit reached (%zu frames)\n", MAX_CALL_STACK_DEPTH);
        return;
    }

    VybStackFrame frame;
    frame.function_name = function_name;
    frame.location.file_path = file_path;
    frame.location.line = line;
    frame.location.column = column;
    frame.native_address = nullptr;  // Not needed for Vyb-level traces

    g_call_stack.push_back(frame);
}

void __vyb_runtime_pop_call_frame() {

    if (!g_call_stack.empty()) {
        g_call_stack.pop_back();
    } else {
        fprintf(stderr, "Warning: Attempted to pop empty call stack\n");
    }
}

VybStackTrace* __vyb_runtime_get_current_stack_trace() {

    VybStackTrace* trace = new VybStackTrace;
    trace->capacity = g_call_stack.size();
    trace->frame_count = g_call_stack.size();
    trace->frames = new VybStackFrame[trace->capacity];

    // Copy frames in reverse order (most recent first)
    for (size_t i = 0; i < g_call_stack.size(); i++) {
        size_t idx = g_call_stack.size() - 1 - i;
        trace->frames[i] = g_call_stack[idx];
    }

    return trace;
}

// When running under AddressSanitizer, dump the Vyb-level call stack at death.
// The JIT emits __vyb_runtime_push_call_frame at every function entry, so this
// names the generated-code frame where a UAF / double-free fired (the raw ASAN
// stack only shows "<unknown module>" for JITted code).
#if defined(__SANITIZE_ADDRESS__)
#define VYB_HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define VYB_HAS_ASAN 1
#endif
#endif
#ifdef VYB_HAS_ASAN
extern "C" void __sanitizer_set_death_callback(void (*callback)(void));
static void __vyb_asan_death_callback() {
    fprintf(stderr, "\n===== Vyb call stack at death (most recent first) =====\n");
    size_t shown = 0;
    for (size_t k = 0; k < g_call_stack.size(); ++k) {
        size_t idx = g_call_stack.size() - 1 - k;
        const VybStackFrame& f = g_call_stack[idx];
        fprintf(stderr, "  at %s (%s:%u:%u)\n",
                f.function_name,
                f.location.file_path ? f.location.file_path : "<unknown>",
                f.location.line, f.location.column);
        if (++shown >= 48) break;
    }
    fprintf(stderr, "===== end Vyb call stack =====\n");
}
namespace {
struct VybAsanDeathRegister {
    VybAsanDeathRegister() { __sanitizer_set_death_callback(&__vyb_asan_death_callback); }
};
static VybAsanDeathRegister g_vyb_asan_death_register;
}
#undef VYB_HAS_ASAN
#endif

// ===== Error Management =====

VybError* __vyb_runtime_create_error_ex(
    const char* type_name,
    void* type_id,
    void* data,
    size_t data_size,
    void (*destructor)(void*),
    const char* file,
    uint32_t line,
    uint32_t column
) {
    VybError* error = new VybError;
    error->type_hash = type_name ? std::hash<std::string>{}(std::string(type_name)) : 0ULL;
    error->type_name = type_name;
    error->payload = nullptr;
    error->file = file ? strdup(file) : nullptr;
    error->line = line;
    error->col = column;
    error->type_id = type_id;
    error->data_size = data_size;
    error->destructor = destructor;
    error->location.file_path = error->file;
    error->location.line = line;
    error->location.column = column;
    error->cause = nullptr;
    error->timestamp = get_timestamp_ns();
    error->thread_id = get_thread_id();

    // Copy error data
    if (data && data_size > 0) {
        error->data = malloc(data_size);
        memcpy(error->data, data, data_size);
    } else {
        error->data = nullptr;
    }
    error->payload = error->data;

    // Capture Vyb-level stack trace (Phase 6.4)
    error->stack_trace = __vyb_runtime_get_current_stack_trace();

    return error;
}

VybError* __vyb_runtime_create_error(
    const char* type_name,
    void* type_id,
    void* data,
    size_t data_size,
    void (*destructor)(void*),
    VybSourceLocation location
) {
    return __vyb_runtime_create_error_ex(
        type_name,
        type_id,
        data,
        data_size,
        destructor,
        location.file_path,
        location.line,
        location.column
    );
}

void __vyb_runtime_free_error(VybError* error) {
    if (!error) return;

    // Call custom destructor if provided
    if (error->destructor && error->data) {
        error->destructor(error->data);
    }

    // Free data
    if (error->data) {
        free(error->data);
    }

    // Free stack trace
    __vyb_runtime_free_stack_trace(error->stack_trace);

    if (error->file) {
        free((void*)error->file);
    }

    // Recursively free cause chain
    if (error->cause) {
        __vyb_runtime_free_error(error->cause);
    }

    delete error;
}

VybError* __vyb_runtime_clone_error(VybError* error) {
    if (!error) return nullptr;

    VybError* clone = new VybError(*error);

    // Deep copy data
    if (error->data && error->data_size > 0) {
        clone->data = malloc(error->data_size);
        memcpy(clone->data, error->data, error->data_size);
    }
    clone->payload = clone->data;
    clone->file = error->file ? strdup(error->file) : nullptr;
    clone->location.file_path = clone->file;

    // Clone stack trace
    if (error->stack_trace) {
        clone->stack_trace = new VybStackTrace;
        clone->stack_trace->capacity = error->stack_trace->capacity;
        clone->stack_trace->frame_count = error->stack_trace->frame_count;
        clone->stack_trace->frames = new VybStackFrame[error->stack_trace->capacity];
        memcpy(clone->stack_trace->frames, error->stack_trace->frames,
               error->stack_trace->frame_count * sizeof(VybStackFrame));
    }

    // Clone cause chain
    if (error->cause) {
        clone->cause = __vyb_runtime_clone_error(error->cause);
    }

    return clone;
}

void __vyb_runtime_set_error_cause(VybError* error, VybError* cause) {
    if (!error) return;

    // Free existing cause if present
    if (error->cause) {
        __vyb_runtime_free_error(error->cause);
    }

    error->cause = cause;
}

// ===== Type Dispatch =====

bool __vyb_runtime_error_matches_type(
    VybError* error,
    const char* trap_type_name,
    void* trap_type_id
) {
    if (!error || !trap_type_name) return false;

    // Simple string comparison for now
    // TODO: Add aspect-based matching
    return strcmp(error->type_name, trap_type_name) == 0;
}

void* __vyb_runtime_cast_error(VybError* error, const char* target_type_name) {
    if (!error || !target_type_name) return nullptr;

    if (strcmp(error->type_name, target_type_name) == 0) {
        return error->data;
    }

    return nullptr;
}

// ===== Custom Handler Management =====

void __vyb_runtime_set_untrapped_handler(VybUntrappedErrorHandler handler) {
    g_custom_handler.store(handler);
}

void __vyb_runtime_clear_untrapped_handler() {
    g_custom_handler.store(nullptr);
}

VybUntrappedErrorHandler __vyb_runtime_get_untrapped_handler() {
    return g_custom_handler.load();
}

// ===== Panic Implementation =====

void __vyb_runtime_panic(const char* message) {
    char timestamp_buf[64];
    format_timestamp(get_timestamp_ns(), timestamp_buf, sizeof(timestamp_buf));

    char thread_buf[128];
    snprintf(thread_buf, sizeof(thread_buf), "Thread: 0x%llx", (unsigned long long)get_thread_id());

    char time_buf[128];
    snprintf(time_buf, sizeof(time_buf), "Time: %s", timestamp_buf);

    fprintf(stderr, "\n");
    fprintf(stderr, "┌─ PANIC ──────────────────────────────────────────────────────┐\n");
    fprintf(stderr, "│ %-61s│\n", thread_buf);
    fprintf(stderr, "│ %-61s│\n", time_buf);
    fprintf(stderr, "└──────────────────────────────────────────────────────────────┘\n");
    fprintf(stderr, "\n");

    // Print message (null-terminated C string)
    if (message) {
        fprintf(stderr, "Message: %s\n", message);
    } else {
        fprintf(stderr, "Message: <no message>\n");
    }

    fprintf(stderr, "\nABORTING\n");
    fflush(stderr);

    // A panic in a compiled program should terminate with a non-zero status;
    // not necessarily dump core. (Tests assert exit code 1, not SIGABRT.)
    exit(1);
}

// ===== Untrapped Error Handler =====

void __vyb_runtime_untrapped_error(VybError* error) {
    char timestamp_buf[64];
    format_timestamp(get_timestamp_ns(), timestamp_buf, sizeof(timestamp_buf));

    fprintf(stderr, "\n");
    fprintf(stderr, "┌─ UNTRAPPED FAILURE ──────────────────────────────────────────┐\n");

    char line_buf[128];

    const char* typeName = nullptr;
    VybTypeMetadata* metadata = nullptr;
    if (error && error->type_name) {
        metadata = __vyb_lookup_type(error->type_name);
        if (metadata && metadata->type_name) {
            typeName = metadata->type_name;
        } else {
            typeName = error->type_name;
        }
    }
    std::string typeFallback;
    if (!typeName) {
        uint64_t hash = error ? error->type_hash : 0ULL;
        std::ostringstream oss;
        oss << "0x" << std::hex << std::setw(16) << std::setfill('0') << hash;
        typeFallback = oss.str();
        typeName = typeFallback.c_str();
    }

    snprintf(line_buf, sizeof(line_buf), "Type: %s", typeName);
    fprintf(stderr, "│ %-61s│\n", line_buf);

    snprintf(line_buf, sizeof(line_buf), "Thread: 0x%llx", (unsigned long long)get_thread_id());
    fprintf(stderr, "│ %-61s│\n", line_buf);

    snprintf(line_buf, sizeof(line_buf), "Time: %s", timestamp_buf);
    fprintf(stderr, "│ %-61s│\n", line_buf);

    fprintf(stderr, "└──────────────────────────────────────────────────────────────┘\n");

    if (error && error->payload) {
        char* payloadJson = nullptr;
        if (metadata) {
            payloadJson = __vyb_complex_to_json_with_metadata(error->payload, metadata);
        }
        if (payloadJson) {
            fprintf(stderr, "Payload: %s\n", payloadJson);
            free(payloadJson);
        } else {
            fprintf(stderr, "Payload: <unavailable>\n");
        }
    } else {
        fprintf(stderr, "Payload: null\n");
    }

    const char* file = error ? (error->file ? error->file : error->location.file_path) : nullptr;
    uint32_t line = error ? (error->line ? error->line : error->location.line) : 0;
    uint32_t col = error ? (error->col ? error->col : error->location.column) : 0;
    if (file && *file) {
        fprintf(stderr, "Fail site: %s:%u:%u\n", file, line, col);
    } else {
        fprintf(stderr, "Fail site: <unknown>\n");
    }

    // Phase 6.4: Print Vyb-level call stack
    fprintf(stderr, "\nStack Trace:\n");
    VybStackTrace* stack_trace = __vyb_runtime_get_current_stack_trace();
    if (stack_trace && stack_trace->frame_count > 0) {
        for (size_t i = 0; i < stack_trace->frame_count; i++) {
            const VybStackFrame* frame = &stack_trace->frames[i];
            fprintf(stderr, "  at %s (%s:%u:%u)\n",
                    frame->function_name ? frame->function_name : "<unknown>",
                    frame->location.file_path ? frame->location.file_path : "<unknown>",
                    frame->location.line,
                    frame->location.column);
        }
        __vyb_runtime_free_stack_trace(stack_trace);
    } else {
        fprintf(stderr, "  (no stack trace available)\n");
    }

    // Print stack trace if available
    // NOTE: Currently error is just a heap-allocated error struct (type_id + value)
    // not a full VybError* with stack trace. We'll improve this in Phase 6.3.
    int exitCode = 1;
    if (error && metadata && error->payload) {
        for (size_t i = 0; i < metadata->num_fields; ++i) {
            const VybFieldMetadata& field = metadata->fields[i];
            if (!field.name || !field.type_name) continue;
            if (strcmp(field.name, "exitCode") == 0 && strcmp(field.type_name, "Int") == 0) {
                int64_t payloadExitCode = *(int64_t*)((char*)error->payload + field.offset);
                exitCode = static_cast<int>(payloadExitCode);
                break;
            }
        }
    }
    if (error) {
        __vyb_runtime_free_error(error);
    }

    fprintf(stderr, "\nExit Code: %d\n", exitCode);
    fflush(stderr);

    exit(exitCode);
}

// ===== Defer/Ensure Support (Stubs for now) =====

void __vyb_runtime_register_defer(void (*cleanup_fn)(void*), void* context) {
    // TODO: Implement defer stack
}

void __vyb_runtime_execute_defers() {
    // TODO: Execute defers in LIFO order
}

void __vyb_runtime_push_ensure_block(void (*ensure_fn)()) {
    // TODO: Implement ensure stack
}

void __vyb_runtime_pop_ensure_block() {
    // TODO: Pop ensure stack
}

} // extern "C"
