// SPDX-License-Identifier: Apache-2.0

// Vyb Runtime Library - Type Conversion Functions
// Comprehensive runtime support for Vyb type conversions

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
typedef struct { _Atomic(void*) p; _Atomic(int64_t) refs; } vyb_str_ref;
static vyb_str_ref vyb_str_reg[VYB_STR_REG_CAP] = {0};

// Serializes slot claiming in register() and the slot reset after the last
// release. retain()/release() of a live, already-registered buffer are lock-free
// (atomic RMW on `refs`); the mutex only guards publishing/retiring a slot.
static pthread_mutex_t vyb_str_reg_lock = PTHREAD_MUTEX_INITIALIZER;

static vyb_str_ref* vyb_str_lookup_slot(void* p) {
    if (!p) return NULL;
    size_t h = (size_t)((uintptr_t)p / 16) ^ ((size_t)(uintptr_t)p / 4096);
    h &= VYB_STR_REG_CAP - 1;
    for (size_t i = 0; i < VYB_STR_REG_CAP; ++i) {
        size_t idx = (h + i) & (VYB_STR_REG_CAP - 1);
        void* slotP = atomic_load_explicit(&vyb_str_reg[idx].p, memory_order_acquire);
        if (slotP == NULL || slotP == p) return &vyb_str_reg[idx];
    }
    return NULL; // table full — buffer becomes invisible to reference counting
}

// Register a just-created heap buffer with an initial reference count of 1.
VYB_WEAK void __vyb_string_register(void* p) {
    if (!p) return;
    pthread_mutex_lock(&vyb_str_reg_lock);
    vyb_str_ref* s = vyb_str_lookup_slot(p);
    if (s && atomic_load_explicit(&s->p, memory_order_relaxed) == NULL) {
        // Publish refs(1) before the pointer so a concurrent retain that sees the
        // pointer also sees the initialized refcount (release/acquire pairing).
        atomic_store_explicit(&s->refs, 1, memory_order_relaxed);
        atomic_store_explicit(&s->p, p, memory_order_release);
    }
    pthread_mutex_unlock(&vyb_str_reg_lock);
}

// Take one reference on a buffer (untracked = literal/foreign -> no-op).
VYB_WEAK void* __vyb_string_retain(void* p) {
    vyb_str_ref* s = vyb_str_lookup_slot(p);
    if (!s || atomic_load_explicit(&s->p, memory_order_acquire) == NULL) return p;
    atomic_fetch_add_explicit(&s->refs, 1, memory_order_relaxed);
    return p;
}

// Drop one reference; free the buffer when the last reference is released.
VYB_WEAK void __vyb_string_release(void* p) {
    if (!p) return;
    vyb_str_ref* s = vyb_str_lookup_slot(p);
    if (!s || atomic_load_explicit(&s->p, memory_order_acquire) == NULL) return;
    int64_t old = atomic_fetch_sub_explicit(&s->refs, 1, memory_order_acq_rel);
    if (old == 1) {
        // Last reference: retire the slot under the register lock so a concurrent
        // register can't claim a slot that reset() is about to wipe.
        pthread_mutex_lock(&vyb_str_reg_lock);
        free(p);
        atomic_store_explicit(&s->refs, 0, memory_order_relaxed);
        atomic_store_explicit(&s->p, NULL, memory_order_relaxed);
        pthread_mutex_unlock(&vyb_str_reg_lock);
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

// ============================================================================
// NETWORK I/O (network stdlib module) - thin facades over BSD sockets.
// IP addresses cross as strings ("127.0.0.1"); the runtime handles address and
// port byte order internally, so the Vyb surface stays allocation/pointer-free.
// ============================================================================

static int vyb_net_err = 0;

static int vyb_net_fill_addr(const char* ip, int64_t port, struct sockaddr_in* out) {
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    if (!ip || !*ip) {
        out->sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, ip, &out->sin_addr) != 1) {
        return -1;
    }
    out->sin_port = htons((uint16_t)port);
    return 0;
}

VYB_WEAK int64_t __vyb_net_open(int64_t domain, int64_t t, int64_t protocol) {
    int fd = socket((int)domain, (int)t, (int)protocol);
    vyb_net_err = (fd < 0) ? errno : 0;
    return (int64_t)fd;
}

VYB_WEAK int64_t __vyb_net_close(int64_t fd) {
    int r = close((int)fd);
    vyb_net_err = (r < 0) ? errno : 0;
    return (int64_t)r;
}

VYB_WEAK int64_t __vyb_net_bind(int64_t fd, const char* ip, int64_t port) {
    struct sockaddr_in addr;
    if (vyb_net_fill_addr(ip, port, &addr) != 0) { vyb_net_err = EINVAL; return -1; }
    int r = bind((int)fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr));
    vyb_net_err = (r < 0) ? errno : 0;
    return (int64_t)r;
}

VYB_WEAK int64_t __vyb_net_listen(int64_t fd, int64_t backlog) {
    int r = listen((int)fd, (int)backlog);
    vyb_net_err = (r < 0) ? errno : 0;
    return (int64_t)r;
}

VYB_WEAK int64_t __vyb_net_accept(int64_t fd) {
    struct sockaddr_in addr;
    socklen_t len = (socklen_t)sizeof(addr);
    int c = accept((int)fd, (struct sockaddr*)&addr, &len);
    vyb_net_err = (c < 0) ? errno : 0;
    return (int64_t)c;
}

VYB_WEAK int64_t __vyb_net_connect(int64_t fd, const char* ip, int64_t port) {
    struct sockaddr_in addr;
    if (vyb_net_fill_addr(ip, port, &addr) != 0) { vyb_net_err = EINVAL; return -1; }
    int r = connect((int)fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr));
    vyb_net_err = (r < 0) ? errno : 0;
    return (int64_t)r;
}

VYB_WEAK int64_t __vyb_net_send(int64_t fd, const char* data, int64_t len) {
    ssize_t n = send((int)fd, data, (size_t)len, 0);
    vyb_net_err = (n < 0) ? errno : 0;
    return (int64_t)n;
}

// Receive up to `maxlen` bytes into a fresh, registry-registered buffer. Returns
// { ptr, len }; ptr is NULL (len 0) on error or EOF.
VYB_WEAK vyb_file_str __vyb_net_recv(int64_t fd, int64_t maxlen) {
    vyb_file_str r = { NULL, 0 };
    if (maxlen <= 0) maxlen = 4096;
    char* buf = (char*)malloc((size_t)maxlen);
    if (!buf) { vyb_net_err = errno; return r; }
    ssize_t n = recv((int)fd, buf, (size_t)maxlen, 0);
    if (n < 0) { vyb_net_err = errno; free(buf); return r; }
    // Owned heap buffer: its first holder is the Vyb String built over it, so
    // register it (refcount 1) and let the String's release free it.
    __vyb_string_register(buf);
    r.ptr = buf;
    r.len = (int64_t)n;
    vyb_net_err = 0;
    return r;
}

// The port a listening socket is actually bound to (0 passed to bind ->
// ephemeral). Returns -1 on error.
VYB_WEAK int64_t __vyb_net_local_port(int64_t fd) {
    struct sockaddr_in addr;
    socklen_t len = (socklen_t)sizeof(addr);
    if (getsockname((int)fd, (struct sockaddr*)&addr, &len) != 0) {
        vyb_net_err = errno;
        return -1;
    }
    vyb_net_err = 0;
    return (int64_t)ntohs(addr.sin_port);
}

VYB_WEAK int64_t __vyb_net_error_code(void) {
    return (int64_t)vyb_net_err;
}

// Human-readable message for the last network-op error. Returns an owned heap
// copy (registered with the string registry).
VYB_WEAK char* __vyb_net_error_message(void) {
    const char* m = (vyb_net_err == 0) ? "no error" : strerror(vyb_net_err);
    char* copy = strdup(m);
    if (copy) __vyb_string_register(copy);
    return copy;
}

// ============================================================================
// TIME module (stdlib/network is separate) - epoch/monotonic clocks and sleep.
// ============================================================================

VYB_WEAK int64_t __vyb_time_epoch_secs(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec;
}

VYB_WEAK int64_t __vyb_time_epoch_millis(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

VYB_WEAK int64_t __vyb_time_nanos(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

// A monotonic source (unaffected by wall-clock changes) ideal for measuring
// intervals.
VYB_WEAK int64_t __vyb_time_mono_millis(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

// Sleep for `millis` milliseconds, retrying on EINTR. Always returns 0.
VYB_WEAK int64_t __vyb_time_sleep_ms(int64_t millis) {
    if (millis < 0) millis = 0;
    struct timespec req;
    req.tv_sec = millis / 1000;
    req.tv_nsec = (millis % 1000) * 1000000L;
    struct timespec rem;
    while (nanosleep(&req, &rem) != 0 && errno == EINTR) req = rem;
    return 0;
}

// ============================================================================
// THREADS + MUTEX RUNTIME (threads stdlib module)
// ============================================================================
// Minimal pthread-backed threading for Vyb. A Vyb `fn() -> Int` closure is a
// uniform `{ void* env; void* fn }`; spawn stores it in a fixed slot and hands a
// trampoline the slot. The trampoline calls the closure (`int64_t (*)(void*)`)
// with its hidden environment parameter and parks the result for join().
//
// The slot table is fixed-capacity with a simple used flag: a spawned thread is
// reclaimed only when its handle is joined (joinable), so nothing is leaked for
// joined threads. Detached/fire-and-forget threads are a later follow-on (they
// need a reaper that frees the slot once the thread body completes). A never
// joined thread keeps its slot occupied.
#define VYB_THREAD_CAP 256

// Closure environments are reference-counted by the compiler's ownership pass
// (`{ refcount; cap_dtor; ...captures }`). A closure handed to a thread is owned
// by that thread: spawn takes a reference so the env outlives the caller, and
// the trampoline drops it once the body returns. Without this, a per-argument
// release in a wrapper function (e.g. the `threads` module's `thread_spawn`)
// frees the env while the worker still needs it, so concurrent workers would
// all observe the last capture (a use-after-reuse corruption).
extern void* __vyb_closure_retain(void* env);
extern void  __vyb_closure_release(void* env);

typedef struct {
    pthread_t tid;
    void* fn;      // the lambda's function pointer
    void* env;     // the lambda's capture environment (hidden first param)
    int64_t result;
    int used;      // slot in use (set on spawn, cleared on join/reap)
    int done;      // thread body has returned (so detaching a finished thread reaps it)
    int detach;    // detached: the slot self-reaps when the body returns
} vyb_thread;
static vyb_thread vyb_threads[VYB_THREAD_CAP];
static pthread_mutex_t vyb_threads_lock = PTHREAD_MUTEX_INITIALIZER;

static void* vyb_thread_trampoline(void* arg) {
    vyb_thread* t = (vyb_thread*)arg;
    void* env = t->env;  // the closure's capture environment (may be null)
    t->result = ((int64_t (*)(void*))t->fn)(env);
    if (env) __vyb_closure_release(env);  // drop the thread's reference
    pthread_mutex_lock(&vyb_threads_lock);
    if (t->detach) t->used = 0;  // fire-and-forget: reclaim the slot now
    t->done = 1;
    pthread_mutex_unlock(&vyb_threads_lock);
    return NULL;
}

// Start `fn` (with its capture `env`) on a new thread. Returns a positive
// handle (>=1, for thread_join) or -1 on failure (thread-table full / create
// failed).
VYB_WEAK int64_t __vyb_thread_spawn(void* env, void* fn) {
    if (!fn) return -1;
    pthread_mutex_lock(&vyb_threads_lock);
    int idx = -1;
    for (int i = 0; i < VYB_THREAD_CAP; ++i) {
        if (!vyb_threads[i].used) { idx = i; break; }
    }
    if (idx < 0) {
        pthread_mutex_unlock(&vyb_threads_lock);
        return -1; // table full
    }
    vyb_thread* t = &vyb_threads[idx];
    t->fn = fn;
    t->env = env;
    t->result = 0;
    t->used = 1;
    t->done = 0;
    t->detach = 0;
    // The thread owns the closure for its whole lifetime (retained before the
    // worker can run, so it can never be reaped before spawn returns).
    if (t->env) __vyb_closure_retain(t->env);
    int rc = pthread_create(&t->tid, NULL, vyb_thread_trampoline, t);
    if (rc != 0) {
        if (t->env) __vyb_closure_release(t->env);
        t->used = 0;
        pthread_mutex_unlock(&vyb_threads_lock);
        return -1;
    }
    pthread_mutex_unlock(&vyb_threads_lock);
    return (int64_t)(idx + 1);
}

// Block until the thread identified by `handle` finishes, then return its
// closure result. Returns -2 for an unknown/already-joined handle.
VYB_WEAK int64_t __vyb_thread_join(int64_t handle) {
    int idx = (int)handle - 1;
    if (idx < 0 || idx >= VYB_THREAD_CAP) return -2;
    pthread_mutex_lock(&vyb_threads_lock);
    vyb_thread* t = &vyb_threads[idx];
    if (!t->used) {
        pthread_mutex_unlock(&vyb_threads_lock);
        return -2;
    }
    if (t->detach) {
        pthread_mutex_unlock(&vyb_threads_lock);
        return -2;  // detached threads are not joinable
    }
    pthread_mutex_unlock(&vyb_threads_lock);

    pthread_join(t->tid, NULL); // blocks until the thread completes

    pthread_mutex_lock(&vyb_threads_lock);
    int64_t result = t->result;
    t->used = 0; // reclaim the slot
    pthread_mutex_unlock(&vyb_threads_lock);
    return result;
}

// Detach a spawned thread: it becomes non-joinable and its slot is reclaimed
// automatically when its body returns (a reaper), so fire-and-forget threads
// (e.g. per-request HTTP handlers) can't exhaust the fixed table. Returns 0 on
// success, -2 for an unknown/already-reaped handle, or -1 if already detached.
VYB_WEAK int64_t __vyb_thread_detach(int64_t handle) {
    int idx = (int)handle - 1;
    if (idx < 0 || idx >= VYB_THREAD_CAP) return -2;
    pthread_mutex_lock(&vyb_threads_lock);
    vyb_thread* t = &vyb_threads[idx];
    if (!t->used) {
        pthread_mutex_unlock(&vyb_threads_lock);
        return -2;
    }
    if (t->detach) {
        pthread_mutex_unlock(&vyb_threads_lock);
        return -1;  // already detached
    }
    t->detach = 1;
    if (t->done) t->used = 0;  // already finished: reclaim immediately
    pthread_mutex_unlock(&vyb_threads_lock);
    return 0;
}

// A heap-allocated pthread mutex; the Vyb handle is the mutex pointer itself.
VYB_WEAK int64_t __vyb_mutex_new(void) {
    pthread_mutex_t* m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (!m) return 0;
    if (pthread_mutex_init(m, NULL) != 0) { free(m); return 0; }
    return (int64_t)(intptr_t)m;
}

VYB_WEAK int64_t __vyb_mutex_lock(int64_t mh) {
    if (!mh) return -1;
    return pthread_mutex_lock((pthread_mutex_t*)(intptr_t)mh) == 0 ? 0 : -1;
}

VYB_WEAK int64_t __vyb_mutex_unlock(int64_t mh) {
    if (!mh) return -1;
    return pthread_mutex_unlock((pthread_mutex_t*)(intptr_t)mh) == 0 ? 0 : -1;
}

VYB_WEAK int64_t __vyb_mutex_free(int64_t mh) {
    if (!mh) return -1;
    pthread_mutex_t* m = (pthread_mutex_t*)(intptr_t)mh;
    pthread_mutex_destroy(m);
    free(m);
    return 0;
}

// A heap-allocated condition variable; the Vyb handle is the pointer itself.
// As with the mutex, pthread_cond_wait needs the caller's mutex handle passed
// explicitly (cond_wait releases it while sleeping and reacquires on wake), so
// a condition variable composes 1:1 with the existing Mutex ergonomics.
typedef struct { pthread_cond_t cond; } vyb_cond;
VYB_WEAK int64_t __vyb_cond_new(void) {
    vyb_cond* c = (vyb_cond*)malloc(sizeof(vyb_cond));
    if (!c) return 0;
    if (pthread_cond_init(&c->cond, NULL) != 0) { free(c); return 0; }
    return (int64_t)(intptr_t)c;
}
VYB_WEAK int64_t __vyb_cond_wait(int64_t cv, int64_t mh) {
    if (!cv || !mh) return -1;
    return pthread_cond_wait(&((vyb_cond*)(intptr_t)cv)->cond,
                             (pthread_mutex_t*)(intptr_t)mh) == 0 ? 0 : -1;
}
VYB_WEAK int64_t __vyb_cond_signal(int64_t cv) {
    if (!cv) return -1;
    return pthread_cond_signal(&((vyb_cond*)(intptr_t)cv)->cond) == 0 ? 0 : -1;
}
VYB_WEAK int64_t __vyb_cond_broadcast(int64_t cv) {
    if (!cv) return -1;
    return pthread_cond_broadcast(&((vyb_cond*)(intptr_t)cv)->cond) == 0 ? 0 : -1;
}
VYB_WEAK int64_t __vyb_cond_free(int64_t cv) {
    if (!cv) return -1;
    vyb_cond* c = (vyb_cond*)(intptr_t)cv;
    pthread_cond_destroy(&c->cond);
    free(c);
    return 0;
}

// A heap-allocated lock-free atomic int; the Vyb handle is the pointer itself.
// All operations are seq_cst. atomic_add returns the *new* value (result after
// the addition); atomic_cas returns 1 if the swap happened, else 0.
typedef struct { _Atomic int64_t v; } vyb_atomic;
VYB_WEAK int64_t __vyb_atomic_new(int64_t init) {
    vyb_atomic* a = (vyb_atomic*)malloc(sizeof(vyb_atomic));
    if (!a) return 0;
    atomic_init(&a->v, init);
    return (int64_t)(intptr_t)a;
}
VYB_WEAK int64_t __vyb_atomic_load(int64_t ah) {
    if (!ah) return 0;
    return atomic_load_explicit(&((vyb_atomic*)(intptr_t)ah)->v, memory_order_seq_cst);
}
VYB_WEAK int64_t __vyb_atomic_store(int64_t ah, int64_t v) {
    if (!ah) return -1;
    atomic_store_explicit(&((vyb_atomic*)(intptr_t)ah)->v, v, memory_order_seq_cst);
    return 0;
}
VYB_WEAK int64_t __vyb_atomic_add(int64_t ah, int64_t v) {
    if (!ah) return 0;
    return atomic_fetch_add_explicit(&((vyb_atomic*)(intptr_t)ah)->v, v,
                                     memory_order_seq_cst) + v;
}
VYB_WEAK int64_t __vyb_atomic_cas(int64_t ah, int64_t expected, int64_t desired) {
    if (!ah) return 0;
    int64_t exp = expected;
    return atomic_compare_exchange_strong_explicit(
        &((vyb_atomic*)(intptr_t)ah)->v, &exp, desired,
        memory_order_seq_cst, memory_order_seq_cst) ? 1 : 0;
}
VYB_WEAK int64_t __vyb_atomic_free(int64_t ah) {
    if (!ah) return -1;
    free((void*)(intptr_t)ah);
    return 0;
}

// A thread-safe typed channel (Int payloads) with a Mutex + CondVar. The Vyb
// handle is a pointer to a heap `vyb_chan`. A default channel (`capacity <= 0`)
// is unbounded: send always succeeds (growing the ring buffer as needed). A
// bounded channel keeps a fixed-capacity ring; a full bounded send returns 0
// immediately (non-blocking), and `chan_try` offers a non-blocking recv so callers
// can poll/select without risking a block. `chan_recv` blocks (releasing the
// CondVar) until a value is available or the channel is closed.
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    int64_t* buf;
    size_t head;  // index of the oldest element
    size_t size;  // live element count
    size_t cap;   // allocated ring capacity
    int bounded;  // 1 = fixed capacity, 0 = grow on demand
    int closed;
} vyb_chan;

VYB_WEAK int64_t __vyb_chan_new(int64_t capacity) {
    vyb_chan* c = (vyb_chan*)calloc(1, sizeof(vyb_chan));
    if (!c) return 0;
    if (pthread_mutex_init(&c->mutex, NULL) != 0) { free(c); return 0; }
    if (pthread_cond_init(&c->not_empty, NULL) != 0) { pthread_mutex_destroy(&c->mutex); free(c); return 0; }
    c->bounded = capacity > 0;
    size_t initial = c->bounded ? (size_t)capacity : 8;
    c->buf = (int64_t*)malloc(sizeof(int64_t) * initial);
    if (!c->buf) { pthread_cond_destroy(&c->not_empty); pthread_mutex_destroy(&c->mutex); free(c); return 0; }
    c->cap = initial;
    c->head = 0; c->size = 0; c->closed = 0;
    return (int64_t)(intptr_t)c;
}

// Grow the ring buffer (caller holds the mutex). Returns 0 on success.
static int vyb_chan_grow(vyb_chan* c) {
    size_t ncap = (c->cap > 0) ? c->cap * 2 : 8;
    int64_t* nb = (int64_t*)malloc(sizeof(int64_t) * ncap);
    if (!nb) return -1;
    for (size_t i = 0; i < c->size; ++i) {
        nb[i] = c->buf[(c->head + i) % c->cap];
    }
    free(c->buf);
    c->buf = nb; c->cap = ncap; c->head = 0;
    return 0;
}

// Non-blocking enqueue: returns 1 on success, 0 if the channel is closed or a
// bounded channel is full.
VYB_WEAK int64_t __vyb_chan_send(int64_t ch, int64_t v) {
    if (!ch) return 0;
    vyb_chan* c = (vyb_chan*)(intptr_t)ch;
    pthread_mutex_lock(&c->mutex);
    if (c->closed) { pthread_mutex_unlock(&c->mutex); return 0; }
    if (c->bounded && c->size >= c->cap) { pthread_mutex_unlock(&c->mutex); return 0; }
    if (!c->bounded && c->size == c->cap && vyb_chan_grow(c) != 0) {
        pthread_mutex_unlock(&c->mutex); return 0;
    }
    c->buf[(c->head + c->size) % c->cap] = v;
    c->size++;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->mutex);
    return 1;
}

// Blocking dequeue: waits until a value is available or the channel is closed.
// Returns the value, or -1 if closed and empty.
VYB_WEAK int64_t __vyb_chan_recv(int64_t ch) {
    if (!ch) return -1;
    vyb_chan* c = (vyb_chan*)(intptr_t)ch;
    pthread_mutex_lock(&c->mutex);
    while (c->size == 0 && !c->closed) {
        pthread_cond_wait(&c->not_empty, &c->mutex);
    }
    if (c->size == 0) { pthread_mutex_unlock(&c->mutex); return -1; }  // closed & empty
    int64_t v = c->buf[c->head];
    c->head = (c->head + 1) % c->cap;
    c->size--;
    pthread_mutex_unlock(&c->mutex);
    return v;
}

// Non-blocking dequeue: returns the value, or -1 if empty/closed.
VYB_WEAK int64_t __vyb_chan_try(int64_t ch) {
    if (!ch) return -1;
    vyb_chan* c = (vyb_chan*)(intptr_t)ch;
    pthread_mutex_lock(&c->mutex);
    if (c->size == 0) { pthread_mutex_unlock(&c->mutex); return -1; }
    int64_t v = c->buf[c->head];
    c->head = (c->head + 1) % c->cap;
    c->size--;
    pthread_mutex_unlock(&c->mutex);
    return v;
}

// Number of buffered values.
VYB_WEAK int64_t __vyb_chan_len(int64_t ch) {
    if (!ch) return -1;
    vyb_chan* c = (vyb_chan*)(intptr_t)ch;
    pthread_mutex_lock(&c->mutex);
    int64_t n = (int64_t)c->size;
    pthread_mutex_unlock(&c->mutex);
    return n;
}

// Destroy and free `ch`; returns 0 or -1.
VYB_WEAK int64_t __vyb_chan_free(int64_t ch) {
    if (!ch) return -1;
    vyb_chan* c = (vyb_chan*)(intptr_t)ch;
    pthread_cond_destroy(&c->not_empty);
    pthread_mutex_destroy(&c->mutex);
    free(c->buf);
    free(c);
    return 0;
}

// A "task" is a fire-and-forget unit of work that delivers one Int result to a
// Future-style channel. __vyb_task_spawn runs a Vyb `fn() -> Int` closure (as
// { env, fn }) on a detached pthread; when the closure returns, its result is
// pushed into a private bounded channel (capacity 1). The task handle IS that
// channel, so:
//   - __vyb_task_await == a blocking recv (waits for the result),
//   - __vyb_task_poll == a non-blocking try (-1 until the result is ready),
//   - __vyb_task_free reclaims the handle (call after await/poll so the worker
//     has already left the channel alone).
typedef struct {
    void* env;
    void* fn;
    int64_t chan;
} vyb_task;

static void* vyb_task_trampoline(void* arg) {
    vyb_task* t = (vyb_task*)arg;
    int64_t (*f)(void*) = (int64_t (*)(void*))t->fn;
    int64_t r = f(t->env);
    __vyb_chan_send(t->chan, r);
    if (t->env) __vyb_closure_release(t->env);
    free(t);
    return NULL;
}

VYB_WEAK int64_t __vyb_task_spawn(void* env, void* fn) {
    if (!fn) return 0;
    int64_t chan = __vyb_chan_new(1);
    if (!chan) return 0;
    vyb_task* t = (vyb_task*)malloc(sizeof(vyb_task));
    if (!t) { __vyb_chan_free(chan); return 0; }
    t->env = env; t->fn = fn; t->chan = chan;
    if (env) __vyb_closure_retain(env);
    pthread_t tid;
    if (pthread_create(&tid, NULL, vyb_task_trampoline, t) != 0) {
        if (env) __vyb_closure_release(env);
        free(t);
        __vyb_chan_free(chan);
        return 0;
    }
    pthread_detach(tid);
    return chan;
}

// Block until the task's result is delivered and return it (-1 on a bad handle).
VYB_WEAK int64_t __vyb_task_await(int64_t task) {
    if (!task) return -1;
    return __vyb_chan_recv(task);
}

// Non-blocking: return the result if ready, or -1 while the task is still
// running (indistinguishable from a genuine -1 result).
VYB_WEAK int64_t __vyb_task_poll(int64_t task) {
    if (!task) return -1;
    return __vyb_chan_try(task);
}

// Reclaim the task handle (its result channel). Call only after the worker has
// delivered its result (await/poll), otherwise the worker may still touch it.
VYB_WEAK int64_t __vyb_task_free(int64_t task) {
    if (!task) return -1;
    return __vyb_chan_free(task);
}

// Select over channels. __vyb_chan_select blocks until at least one of the `n`
// channel handles in `handles` has a value (or is closed), then returns the
// index of the first such channel (best-effort: it does NOT consume the value —
// the caller follows up with __vyb_chan_recv/handles[i]). A closed channel
// counts as ready so the caller can observe closure via a -1 recv. This is a
// polling first-cut (a ~1ms retry loop) rather than a wakeup-integrated select,
// so wakeup latency is on the order of a millisecond; returns -1 on a bad/empty
// handle array.
VYB_WEAK int64_t __vyb_chan_select(int64_t* handles, int64_t n) {
    if (!handles || n <= 0) return -1;
    struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 1000000; /* 1 ms */
    for (;;) {
        for (int64_t i = 0; i < n; ++i) {
            vyb_chan* c = (vyb_chan*)(intptr_t)handles[i];
            if (!c) continue;
            int ready = 0;
            pthread_mutex_lock(&c->mutex);
            if (c->size > 0 || c->closed) ready = 1;
            pthread_mutex_unlock(&c->mutex);
            if (ready) return i;
        }
        nanosleep(&ts, NULL);
    }
}

// String channels. A distinct pthread/Mutex+CondVar ring buffer whose slots are
// Vyb String values `{ ptr, len }`. On send, the data pointer is RETAINED so the
// channel owns a reference that survives the sender's own release; on recv/try
// that reference is transferred to the caller, who drops it (normal String
// teardown) once done. A String channel must not be mixed with the Int-channel
// functions, and __vyb_strchan_free should be called after the channel is
// drained so no buffered reference leaks.
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    vyb_file_str* buf;   // each slot holds a retained { ptr, len }
    size_t head;
    size_t size;
    size_t cap;
    int bounded;
    int closed;
} vyb_strchan;

VYB_WEAK int64_t __vyb_strchan_new(int64_t capacity) {
    vyb_strchan* c = (vyb_strchan*)calloc(1, sizeof(vyb_strchan));
    if (!c) return 0;
    if (pthread_mutex_init(&c->mutex, NULL) != 0) { free(c); return 0; }
    if (pthread_cond_init(&c->not_empty, NULL) != 0) { pthread_mutex_destroy(&c->mutex); free(c); return 0; }
    c->bounded = capacity > 0;
    size_t initial = c->bounded ? (size_t)capacity : 8;
    c->buf = (vyb_file_str*)malloc(sizeof(vyb_file_str) * initial);
    if (!c->buf) { pthread_cond_destroy(&c->not_empty); pthread_mutex_destroy(&c->mutex); free(c); return 0; }
    c->cap = initial; c->head = 0; c->size = 0; c->closed = 0;
    return (int64_t)(intptr_t)c;
}

static int vyb_strchan_grow(vyb_strchan* c) {
    size_t ncap = (c->cap > 0) ? c->cap * 2 : 8;
    vyb_file_str* nb = (vyb_file_str*)malloc(sizeof(vyb_file_str) * ncap);
    if (!nb) return -1;
    for (size_t i = 0; i < c->size; ++i) nb[i] = c->buf[(c->head + i) % c->cap];
    free(c->buf);
    c->buf = nb; c->cap = ncap; c->head = 0;
    return 0;
}

// Fixed pending to drop buffered references inside strchan_free. Must be callable
// while holding the mutex is NOT safe (release may touch the registry), so free
// first drains the references.
VYB_WEAK int64_t __vyb_strchan_send(int64_t ch, char* ptr, int64_t len) {
    if (!ch) return 0;
    vyb_strchan* c = (vyb_strchan*)(intptr_t)ch;
    if (!ptr) return 0;
    __vyb_string_retain(ptr);   // channel owns a reference
    pthread_mutex_lock(&c->mutex);
    if (c->closed || (c->bounded && c->size >= c->cap)) {
        pthread_mutex_unlock(&c->mutex);
        __vyb_string_release(ptr);
        return 0;
    }
    if (!c->bounded && c->size == c->cap && vyb_strchan_grow(c) != 0) {
        pthread_mutex_unlock(&c->mutex);
        __vyb_string_release(ptr);
        return 0;
    }
    c->buf[(c->head + c->size) % c->cap] = (vyb_file_str){ ptr, len };
    c->size++;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->mutex);
    return 1;
}

// Blocking dequeue; transfers the channel's retained reference to the caller.
VYB_WEAK vyb_file_str __vyb_strchan_recv(int64_t ch) {
    vyb_file_str r = { NULL, 0 };
    if (!ch) return r;
    vyb_strchan* c = (vyb_strchan*)(intptr_t)ch;
    pthread_mutex_lock(&c->mutex);
    while (c->size == 0 && !c->closed) pthread_cond_wait(&c->not_empty, &c->mutex);
    if (c->size == 0) { pthread_mutex_unlock(&c->mutex); return r; }
    r = c->buf[c->head];
    c->head = (c->head + 1) % c->cap;
    c->size--;
    pthread_mutex_unlock(&c->mutex);
    return r;
}

// Non-blocking dequeue; returns an empty (NULL,0) string when nothing is ready.
VYB_WEAK vyb_file_str __vyb_strchan_try(int64_t ch) {
    vyb_file_str r = { NULL, 0 };
    if (!ch) return r;
    vyb_strchan* c = (vyb_strchan*)(intptr_t)ch;
    pthread_mutex_lock(&c->mutex);
    if (c->size == 0) { pthread_mutex_unlock(&c->mutex); return r; }
    r = c->buf[c->head];
    c->head = (c->head + 1) % c->cap;
    c->size--;
    pthread_mutex_unlock(&c->mutex);
    return r;
}

VYB_WEAK int64_t __vyb_strchan_len(int64_t ch) {
    if (!ch) return -1;
    vyb_strchan* c = (vyb_strchan*)(intptr_t)ch;
    pthread_mutex_lock(&c->mutex);
    int64_t n = (int64_t)c->size;
    pthread_mutex_unlock(&c->mutex);
    return n;
}

// Reclaim the channel, dropping any references still buffered (call after
// draining so no live reference is released here unnecessarily).
VYB_WEAK int64_t __vyb_strchan_free(int64_t ch) {
    if (!ch) return -1;
    vyb_strchan* c = (vyb_strchan*)(intptr_t)ch;
    pthread_mutex_lock(&c->mutex);
    for (size_t i = 0; i < c->size; ++i) __vyb_string_release(c->buf[(c->head + i) % c->cap].ptr);
    pthread_mutex_unlock(&c->mutex);
    pthread_cond_destroy(&c->not_empty);
    pthread_mutex_destroy(&c->mutex);
    free(c->buf);
    free(c);
    return 0;
}

// ============================================================================
// Cooperative async tasks (stackful fibers) on a multi-threaded thread pool.
//
// Each spawned async task gets a private stack (a ucontext fiber) pinned to a
// worker thread: once a fiber has run on a worker it must suspend and resume on
// that same worker (a ucontext is not safe to migrate across OS threads), so
// every task remembers its home worker and is always re-queued there. A fresh
// task's context is built lazily by its worker at first run, so no context is
// ever fabricated on one thread and launched on another.
//
// A small pool of worker threads (lazily spawned on first use, sized to the
// CPU count) each run their own scheduler loop: fire due timers, drain their
// FIFO ready queue, and idle on a condition variable whose timeout is the next
// pending deadline. Spawn assigns tasks round-robin across the workers to load
// balance CPU-bound work; `__thread` state keeps each worker's ucontext and
// current fiber isolated to its OS thread. Mutable scheduler state is guarded
// by `g_async_lock`; workers signal each other's condition variables when they
// enqueue work, and the main thread parks on a condition variable while a
// main-thread await is outstanding.
//
// Suspension is cooperative: __vyb_async_yield() reschedules round-robin via
// the worker's ready queue, __vyb_async_sleep_ms(m) parks on the global timer
// heap (honoured by whichever worker runs it), and __vyb_async_await(t)
// registers the caller as a waiter on `t` and suspends until `t` completes.
// Because the fibers are stackful, a Vyb function can suspend mid-body simply
// by calling a suspendable intrinsic -- no compiler state-machine transform is
// needed; the whole Vyb call stack lives on the fiber's stack.
//
// Lifecycle: tasks stay valid while loops are running. When every worker is
// idle with no queued work and no pending timer, __vyb_async_run_all() returns
// after reclaiming every task (stacks + structs + closure-env retains); handles
// must not be used afterwards. An atexit hook stops + joins the worker threads
// and reclaims anything a forgotten run_all left behind.
#include <ucontext.h>
#define VYB_ASYNC_STACK_SIZE (1u << 20)   // 1 MiB per fiber
#define VYB_WORKER_MAX 64

typedef struct vyb_async_task vyb_async_task;
typedef struct vyb_worker vyb_worker;

typedef struct vyb_async_task {
    ucontext_t ctx;
    int context_made;          // ctx built by this task's worker (see pinning)
    int64_t (*fn)(void*);
    void* env;
    int state;                 // 0 = READY, 1 = BLOCKED, 2 = DONE
    int64_t result;
    int64_t wake_ms;           // abs mono-ms when on the timer heap, else -1
    void* stack;
    vyb_worker* home;          // worker this fiber is pinned to
    struct vyb_async_task* next_ready;
    struct vyb_async_task* next_timer;
    struct vyb_async_task* next_waiter;   // I am waiting on a task
    struct vyb_async_task* waiters;       // tasks waiting on me
    int64_t awaited_result;               // stashed result delivered to a waiter
    struct vyb_async_task* next_all;      // lifecycle list
} vyb_async_task;

typedef struct vyb_worker {
    int id;
    pthread_t thread;
    int started;
    int busy;                  // currently executing a fiber
    vyb_async_task* ready;     // this worker's FIFO ready queue
    vyb_async_task* ready_tail;
    pthread_cond_t cv;         // signaled when work arrives / at shutdown
    ucontext_t sched_ctx;      // this worker's scheduler context
} vyb_worker;

static pthread_mutex_t g_async_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_main_cv = PTHREAD_COND_INITIALIZER;   // wake a main-thread await
static pthread_cond_t g_drain_cv = PTHREAD_COND_INITIALIZER;  // wake run_all once quiescent
static vyb_async_task* g_timers = NULL;       // ascending by wake_ms
static vyb_async_task* g_all = NULL;          // every live task
static vyb_async_task* g_main_wait = NULL;    // task a main-thread await is waiting on
static vyb_worker g_workers[VYB_WORKER_MAX];
static int g_nworkers = 0;       // worker threads actually spawned
static int g_spin = 0;           // spawn round-robin cursor
static int g_nready = 0;         // tasks queued across all workers
static int g_ntimers = 0;        // timers pending on the heap
static int g_shutdown = 0;       // set at atexit: stop + join workers
static __thread vyb_async_task* tls_cur = NULL;   // fiber currently running on this thread

// Enqueue `t` on its home worker's ready queue and wake that worker. Lock held.
static void async_ready_push(vyb_async_task* t) {
    t->state = 0;
    t->next_ready = NULL;
    vyb_worker* w = t->home;
    if (w->ready_tail) w->ready_tail->next_ready = t;
    else w->ready = t;
    w->ready_tail = t;
    g_nready++;
    pthread_cond_signal(&w->cv);
}

// Insert `t` into the ascending timer heap. Lock held.
static void async_timer_add(vyb_async_task* t) {
    t->next_timer = NULL;
    g_ntimers++;
    if (!g_timers || t->wake_ms < g_timers->wake_ms) { t->next_timer = g_timers; g_timers = t; return; }
    vyb_async_task* p = g_timers;
    while (p->next_timer && p->next_timer->wake_ms <= t->wake_ms) p = p->next_timer;
    t->next_timer = p->next_timer;
    p->next_timer = t;
}

// Requeue every timer that is due at or before `now`. Lock held.
static void async_fire_due(int64_t now) {
    while (g_timers && g_timers->wake_ms <= now) {
        vyb_async_task* t = g_timers;
        g_timers = t->next_timer;
        t->next_timer = NULL;
        t->wake_ms = -1;
        g_ntimers--;
        async_ready_push(t);
    }
}

// True when no worker is executing a fiber and no work (ready/timer) remains,
// i.e. run_all may safely reclaim every task. Lock held.
static int async_quiescent_locked(void) {
    if (g_nready != 0 || g_ntimers != 0) return 0;
    for (int i = 0; i < g_nworkers; i++)
        if (g_workers[i].busy) return 0;
    return 1;
}

static void vyb_async_tramp(void) {
    vyb_async_task* t = tls_cur;
    int64_t r = t->fn ? t->fn(t->env) : 0;
    pthread_mutex_lock(&g_async_lock);
    t->result = r;
    t->state = 2;
    if (g_main_wait == t) pthread_cond_signal(&g_main_cv);
    vyb_async_task* w = t->waiters;
    t->waiters = NULL;
    while (w) {
        vyb_async_task* nxt = w->next_waiter;
        w->awaited_result = r;
        async_ready_push(w);
        w = nxt;
    }
    pthread_mutex_unlock(&g_async_lock);
    swapcontext(&t->ctx, &t->home->sched_ctx);
}

// Forward decl (defined after the worker pool helpers below).
static void* async_worker_main(void* arg);

// Build the fiber context on the worker thread that will run it (portability:
// a ucontext is only launched on the thread that fabricates it). Returns 0 on
// success; the worker is free to run the fiber only after this succeeds.
static int async_make_context(vyb_async_task* t) {
    if (getcontext(&t->ctx) != 0) return -1;
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = VYB_ASYNC_STACK_SIZE;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, (void(*)(void))vyb_async_tramp, 0);
    t->context_made = 1;
    return 0;
}

// Lazily spawn the worker pool (once), sized to the CPU count. Must hold lock.
static void async_ensure_workers(void) {
    if (g_nworkers > 0) return;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    int n = (ncpu > 0) ? (int)ncpu : 4;
    if (n < 1) n = 1;
    if (n > VYB_WORKER_MAX) n = VYB_WORKER_MAX;
    for (int i = 0; i < n; i++) {
        g_workers[i].id = i;
        pthread_cond_init(&g_workers[i].cv, NULL);
    }
    for (int i = 0; i < n; i++) {
        if (pthread_create(&g_workers[i].thread, NULL, async_worker_main, &g_workers[i]) == 0) {
            g_workers[i].started = 1;
            g_nworkers++;
        }
    }
}

// Per-worker scheduler loop. Only this worker touches its own sched_ctx and
// ready queue, so the fiber-pinning invariant is kept. Exits on shutdown.
static void* async_worker_main(void* arg) {
    vyb_worker* w = (vyb_worker*)arg;
    pthread_mutex_lock(&g_async_lock);
    for (;;) {
        async_fire_due(__vyb_time_mono_millis());
        vyb_async_task* t = w->ready;
        if (t) {
            w->ready = t->next_ready;
            if (!w->ready) w->ready_tail = NULL;
            g_nready--;
            w->busy = 1;
        } else {
            if (g_shutdown) { pthread_mutex_unlock(&g_async_lock); break; }
            if (async_quiescent_locked()) pthread_cond_broadcast(&g_drain_cv);
            if (g_timers) {
                int64_t diff = g_timers->wake_ms - __vyb_time_mono_millis();
                if (diff < 1) diff = 1;
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += diff / 1000;
                ts.tv_nsec += (long)(diff % 1000) * 1000000L;
                if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
                pthread_cond_timedwait(&w->cv, &g_async_lock, &ts);
            } else {
                pthread_cond_wait(&w->cv, &g_async_lock);
            }
            continue;
        }
        pthread_mutex_unlock(&g_async_lock);

        if (!t->context_made && async_make_context(t) != 0) {
            // Extremely unlikely (getcontext failure): leave the task DONE in
            // g_all so the lifecycle cleanup reclaims its stack/env.
            pthread_mutex_lock(&g_async_lock);
            t->result = -1;
            t->state = 2;
            pthread_mutex_unlock(&g_async_lock);
        } else {
            tls_cur = t;
            swapcontext(&w->sched_ctx, &t->ctx);
            tls_cur = NULL;
        }

        pthread_mutex_lock(&g_async_lock);
        w->busy = 0;
    }
    return NULL;
}

static void async_cleanup_all(void) {
    vyb_async_task* t;
    pthread_mutex_lock(&g_async_lock);
    t = g_all; g_all = NULL;
    g_timers = NULL;
    g_ntimers = 0;
    g_main_wait = NULL;
    for (int i = 0; i < g_nworkers; i++) g_workers[i].ready = g_workers[i].ready_tail = NULL;
    g_nready = 0;
    pthread_mutex_unlock(&g_async_lock);
    while (t) {
        vyb_async_task* nxt = t->next_all;
        if (t->env) __vyb_closure_release(t->env);
        free(t->stack);
        free(t);
        t = nxt;
    }
}

// Spawn a `fn() -> Int` closure as an async task on the thread pool. Returns a
// task handle (>= 1) or 0 on failure. The task is queued to a worker until the
// pool is driven (async_run_all / a main-thread async_await / from within
// another task).
VYB_WEAK int64_t __vyb_async_spawn(void* env, void* fn) {
    if (!fn) return 0;
    vyb_async_task* t = (vyb_async_task*)calloc(1, sizeof(vyb_async_task));
    if (!t) return 0;
    t->stack = malloc(VYB_ASYNC_STACK_SIZE);
    if (!t->stack) { free(t); return 0; }
    t->fn = (int64_t (*)(void*))fn;
    t->wake_ms = -1;
    if (env) { __vyb_closure_retain(env); t->env = env; }
    pthread_mutex_lock(&g_async_lock);
    async_ensure_workers();
    if (g_nworkers < 1) {
        if (t->env) __vyb_closure_release(t->env);
        free(t->stack); free(t);
        pthread_mutex_unlock(&g_async_lock);
        return 0;
    }
    t->home = &g_workers[g_spin++ % g_nworkers];
    async_ready_push(t);
    t->next_all = g_all;
    g_all = t;
    pthread_mutex_unlock(&g_async_lock);
    return (int64_t)(intptr_t)t;
}

// Run the pool until every worker is idle with no queued work or pending timer,
// then reclaim all tasks. Returns 0.
VYB_WEAK int64_t __vyb_async_run_all(void) {
    pthread_mutex_lock(&g_async_lock);
    async_ensure_workers();
    if (g_nworkers > 0)
        for (int i = 0; i < g_nworkers; i++) pthread_cond_signal(&g_workers[i].cv);
    while (!async_quiescent_locked())
        pthread_cond_wait(&g_drain_cv, &g_async_lock);
    pthread_mutex_unlock(&g_async_lock);
    if (g_all) async_cleanup_all();
    return 0;
}

// Awaits the result of `task` (blocking). From the main thread this parks on a
// condition variable until the worker completes it; from inside a fiber it
// suspends the fiber until the task completes (the pool keeps running other
// work).
VYB_WEAK int64_t __vyb_async_await(int64_t task) {
    if (!task) return -1;
    vyb_async_task* t = (vyb_async_task*)(intptr_t)task;
    if (tls_cur) {
        // Fiber path: suspend this fiber until `t` completes.
        vyb_async_task* self = tls_cur;
        pthread_mutex_lock(&g_async_lock);
        if (t->state == 2) { int64_t v = t->result; pthread_mutex_unlock(&g_async_lock); return v; }
        self->next_waiter = t->waiters;
        t->waiters = self;
        self->state = 1;                       // BLOCKED
        pthread_mutex_unlock(&g_async_lock);
        swapcontext(&self->ctx, &self->home->sched_ctx);
        return self->awaited_result;
    }
    // Main-thread path: park until the workers deliver the result.
    pthread_mutex_lock(&g_async_lock);
    if (t->state == 2) { int64_t v = t->result; pthread_mutex_unlock(&g_async_lock); return v; }
    g_main_wait = t;
    async_ensure_workers();
    if (g_nworkers > 0)
        for (int i = 0; i < g_nworkers; i++) pthread_cond_signal(&g_workers[i].cv);
    while (t->state != 2 && g_nworkers > 0)
        pthread_cond_wait(&g_main_cv, &g_async_lock);
    int64_t v = (t->state == 2) ? t->result : 0;
    g_main_wait = NULL;
    pthread_mutex_unlock(&g_async_lock);
    return v;
}

// Non-blocking: returns the task's result if it has finished, else -1.
VYB_WEAK int64_t __vyb_async_poll(int64_t task) {
    if (!task) return -1;
    vyb_async_task* t = (vyb_async_task*)(intptr_t)task;
    pthread_mutex_lock(&g_async_lock);
    int64_t v = (t->state == 2) ? t->result : -1;
    pthread_mutex_unlock(&g_async_lock);
    return v;
}

// Cooperative yield: suspend the current fiber and reschedule it (no-op if not
// running inside a fiber).
VYB_WEAK int64_t __vyb_async_yield(void) {
    if (!tls_cur) return 0;
    vyb_async_task* self = tls_cur;
    pthread_mutex_lock(&g_async_lock);
    async_ready_push(self);
    pthread_mutex_unlock(&g_async_lock);
    swapcontext(&self->ctx, &self->home->sched_ctx);
    return 0;
}

// Cooperative sleep: suspend the current fiber via the timer heap so other pool
// work proceeds. Outside a fiber this just sleeps the calling thread.
VYB_WEAK int64_t __vyb_async_sleep_ms(int64_t ms) {
    if (!tls_cur) { __vyb_time_sleep_ms(ms); return 0; }
    vyb_async_task* self = tls_cur;
    pthread_mutex_lock(&g_async_lock);
    self->wake_ms = __vyb_time_mono_millis() + ms;
    async_timer_add(self);
    self->state = 1;                        // BLOCKED
    pthread_mutex_unlock(&g_async_lock);
    swapcontext(&self->ctx, &self->home->sched_ctx);
    return 0;
}
// Stop + join the worker pool at process exit, then reclaim any leftover tasks
// so a forgotten async_run_all never leaks (runs once, before main()).
static void vyb_async_atexit(void) {
    pthread_mutex_lock(&g_async_lock);
    g_shutdown = 1;
    for (int i = 0; i < g_nworkers; i++) pthread_cond_broadcast(&g_workers[i].cv);
    pthread_mutex_unlock(&g_async_lock);
    for (int i = 0; i < g_nworkers; i++)
        if (g_workers[i].started) pthread_join(g_workers[i].thread, NULL);
    async_cleanup_all();
}
static void vyb_async_atexit_reg(void) __attribute__((constructor));
static void vyb_async_atexit_reg(void) { atexit(vyb_async_atexit); }

