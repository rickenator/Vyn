// SPDX-License-Identifier: Apache-2.0

#include "vyb/vyb.hpp"
#include "vyb/parser/lexer.hpp"   // For Lexer
#include "vyb/parser/parser.hpp"  // For vyb::Parser
#include "vyb/semantic.hpp"       // For vyb::SemanticAnalyzer
#include "vyb/module_registry.hpp"
#include "vyb/manifest.hpp"
#include "vyb/vre/llvm/codegen.hpp" // For vyb::LLVMCodegen
#include "vyb/bindgen.hpp"      // For vyb::bindgen::generateBindings
#include <catch2/catch_session.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <set> // For test and parser verbosity specifiers
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <algorithm> // For std::find
#include <cctype>
#include <optional>
#include <utility>
#include <cstdio> // For printf and fflush
#include <cstdlib> // For malloc/free
#include <cstring> // For memset
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#endif

// Declare the intrinsic functions from intrinsics.cpp
extern "C" {
    // This is just a declaration - implementation is in intrinsics.cpp
    void __vyb_println(const char* str);
    void __vyb_print(const char* str);
    void __vyb_println_int(int64_t val);
    void __vyb_print_int(int64_t val);
    void __vyb_println_bool(int64_t val);
    void __vyb_print_bool(int64_t val);
    char* __vyb_serialize_to_json(void* obj, const char* type_name);
    char* __vyb_convert_lit_string(const char* str);
    char* __vyb_string_concat(const char* left, const char* right);

    // String concatenation intrinsic function
    char* __vyb_string_concat(const char* left, const char* right);

    // String replace runtime helper
    char* __vyb_string_replace(const char* src, int64_t src_len,
                               const char* old_s, const char* new_s,
                               int64_t* out_len);

    // String format runtime helper
    char* __vyb_string_format(const char* fmt, int64_t fmt_len,
                              int64_t count, char** args,
                              int64_t* out_len);

    // Closure capture-environment reference counting helpers
    void* __vyb_closure_retain(void* env);
    void  __vyb_closure_release(void* env);

    // ToString intrinsic functions for automatic string conversion - all basic types
    char* __vyb_toString_int(int64_t value);
    char* __vyb_toString_int8(int8_t value);
    char* __vyb_toString_int16(int16_t value);
    char* __vyb_toString_int32(int32_t value);
    char* __vyb_toString_int64(int64_t value);
    char* __vyb_toString_uint8(uint8_t value);
    char* __vyb_toString_uint16(uint16_t value);
    char* __vyb_toString_uint32(uint32_t value);
    char* __vyb_toString_uint64(uint64_t value);
    char* __vyb_toString_float(double value);
    char* __vyb_toString_float32(float value);
    char* __vyb_toString_bool(bool value);
    char* __vyb_toString_string(const char* value);
    char* __vyb_toString_char(uint8_t value);
    char* __vyb_toString_rune(uint32_t value);
    char* __vyb_toString_byte(uint8_t value);

    // New type conversion functions (primitive to_string/from_string)
    char* __vyb_int_to_string(int64_t value);
    char* __vyb_uint_to_string(uint64_t value);
    char* __vyb_float_to_string(double value);
    char* __vyb_bool_to_string(bool value);
    char* __vyb_string_to_string(const char* str);
    void __vyb_string_register(void* ptr);
    void __vyb_string_free(void* ptr);
    void* __vyb_string_retain(void* ptr);
    void __vyb_string_release_each(void* arr, int64_t n);
    void __vyb_string_retain_each(void* arr, int64_t n);

    int64_t __vyb_int_from_string(const char* str, bool* success);
    double __vyb_float_from_string(const char* str, bool* success);
    bool __vyb_bool_from_string(const char* str, bool* success);
    char* __vyb_string_from_string(const char* str, bool* success);

    // File I/O runtime helpers (io stdlib module)
    struct vyb_file_str { char* ptr; int64_t len; };
    int64_t __vyb_file_open(const char* path, int64_t flags);
    int64_t __vyb_file_close(int64_t fd);
    int64_t __vyb_file_write(int64_t fd, const char* data, int64_t len);
    vyb_file_str __vyb_file_read_all(int64_t fd);
    int64_t __vyb_file_error_code(void);
    const char* __vyb_file_error_message(void);

    // Network I/O runtime helpers (network stdlib module)
    int64_t __vyb_net_open(int64_t domain, int64_t t, int64_t protocol);
    int64_t __vyb_net_close(int64_t fd);
    int64_t __vyb_net_bind(int64_t fd, const char* ip, int64_t port);
    int64_t __vyb_net_listen(int64_t fd, int64_t backlog);
    int64_t __vyb_net_accept(int64_t fd);
    int64_t __vyb_net_connect(int64_t fd, const char* ip, int64_t port);
    int64_t __vyb_net_send(int64_t fd, const char* data, int64_t len);
    vyb_file_str __vyb_net_recv(int64_t fd, int64_t maxlen);
    int64_t __vyb_net_local_port(int64_t fd);
    int64_t __vyb_net_error_code(void);
    const char* __vyb_net_error_message(void);
    vyb_file_str __vyb_net_resolve(const char* host);
    // UDP helpers (network stdlib module): sendto/recvfrom + last-peer probes.
    int64_t __vyb_net_sendto(int64_t fd, const char* data, int64_t len,
                             const char* ip, int64_t port);
    vyb_file_str __vyb_net_recvfrom(int64_t fd, int64_t maxlen);
    const char* __vyb_net_last_peer_ip(void);
    int64_t __vyb_net_last_peer_port(void);
    int64_t __vyb_net_set_timeout(int64_t fd, int64_t ms);

    // UTF-8 codepoint helpers (utf8 stdlib module)
    int64_t __vyb_utf8_len(const char* s, int64_t len);
    int64_t __vyb_utf8_index(const char* s, int64_t len, int64_t cp_index);
    int64_t __vyb_utf8_at(const char* s, int64_t len, int64_t byte_off);
    int64_t __vyb_utf8_valid(const char* s, int64_t len);

    // Environment helpers (env stdlib module)
    vyb_file_str __vyb_env_get(const char* name);
    int64_t __vyb_env_set(const char* name, const char* value);
    int64_t __vyb_env_unset(const char* name);

    // Pseudo-random helpers (rand stdlib module)
    int64_t __vyb_rand(void);
    int64_t __vyb_rand_range(int64_t lo, int64_t hi);
    void    __vyb_rand_seed(int64_t seed);

    // Process helpers (process stdlib module)
    int64_t __vyb_exec_run(const char* cmd);
    vyb_file_str __vyb_exec_output(const char* cmd);
    int64_t __vyb_exec_status(void);

    // Regex helpers (regex stdlib module)
    int64_t __vyb_regex_match(const char* pat, int64_t plen, const char* s, int64_t slen);
    int64_t __vyb_regex_find(const char* pat, int64_t plen, const char* s, int64_t slen);
    vyb_file_str __vyb_regex_capture_match(const char* pat, int64_t plen, const char* s, int64_t slen);
    vyb_file_str __vyb_regex_capture(const char* pat, int64_t plen, const char* s, int64_t slen);
    vyb_file_str __vyb_regex_replace(const char* pat, int64_t plen, const char* s, int64_t slen,
                                     const char* repl, int64_t rlen);
    vyb_file_str __vyb_regex_replace_all(const char* pat, int64_t plen, const char* s, int64_t slen,
                                         const char* repl, int64_t rlen);

    // Terminal + stdin runtime helpers (term stdlib module)
    vyb_file_str __vyb_stdin_read(int64_t maxlen);
    vyb_file_str __vyb_stdin_read_line(void);
    int64_t __vyb_stdin_isatty(void);
    int64_t __vyb_stdin_raw_enable(void);
    int64_t __vyb_stdin_raw_disable(void);
    int64_t __vyb_eprint(const char* s, int64_t len);
    int64_t __vyb_eprintln(const char* s, int64_t len);
    int64_t __vyb_stdout_flush(void);
    int64_t __vyb_stderr_flush(void);
    int64_t __vyb_term_cols(void);
    int64_t __vyb_term_rows(void);
    int64_t __vyb_term_clear(void);
    int64_t __vyb_term_move_cursor(int64_t row, int64_t col);
    int64_t __vyb_term_hide_cursor(void);
    int64_t __vyb_term_show_cursor(void);

#ifdef VYB_HAVE_NCURSES
    // Curses TUI runtime helpers (curses stdlib module) over ncursesw. The
    // whole screen is owned by the runtime; Vyb sees Int keycodes/attrs only.
    int64_t __vyb_curses_init(void);
    int64_t __vyb_curses_close(void);
    int64_t __vyb_curses_ok(void);
    int64_t __vyb_curses_rows(void);
    int64_t __vyb_curses_cols(void);
    int64_t __vyb_curses_refresh(void);
    int64_t __vyb_curses_clear(void);
    int64_t __vyb_curses_move(int64_t y, int64_t x);
    int64_t __vyb_curses_addstr(const char* s, int64_t len);
    int64_t __vyb_curses_move_addstr(int64_t y, int64_t x, const char* s, int64_t len);
    int64_t __vyb_curses_has_color(void);
    int64_t __vyb_curses_start_color(void);
    int64_t __vyb_curses_color_pair(int64_t n);
    int64_t __vyb_curses_init_pair(int64_t pair, int64_t fg, int64_t bg);
    int64_t __vyb_curses_attr_on(int64_t attr);
    int64_t __vyb_curses_attr_off(int64_t attr);
    int64_t __vyb_curses_attr_normal(void);
    int64_t __vyb_curses_attr_bold(void);
    int64_t __vyb_curses_attr_underline(void);
    int64_t __vyb_curses_attr_reverse(void);
    int64_t __vyb_curses_attr_blink(void);
    int64_t __vyb_curses_getch(void);
    int64_t __vyb_curses_nodelay(int64_t flag);
    int64_t __vyb_curses_timeout(int64_t ms);
    int64_t __vyb_curses_keypad(int64_t flag);
    int64_t __vyb_curses_show_cursor(void);
    int64_t __vyb_curses_hide_cursor(void);
#endif

    // Qt5 native GUI runtime helpers (qt stdlib module) over the C++ bridge in
    // runtime/vyb_qt_bridge.cpp, or the fallback stub shims in
    // runtime/vyb_qt_stub.cpp when Qt5 is absent (both export the same
    // __vyb_qt_* extern "C" symbols, so the JIT registrations are
    // unconditional). Handles are opaque Int (qintptr-sized); the title/label/
    // button/edit-text getters return an owned, registry-registered
    // { ptr, len } buffer that a Vyb String adopts.
        // gen_qt[main_decl]: begin
    int64_t __vyb_qt_web_create(int64_t parent);
    int64_t __vyb_qt_web_load(int64_t web, const char* url, int64_t len);
    vyb_file_str __vyb_qt_web_url(int64_t web);
    vyb_file_str __vyb_qt_web_title(int64_t web);
    int64_t __vyb_qt_web_loading(int64_t web);
    int64_t __vyb_qt_web_back(int64_t web);
    int64_t __vyb_qt_web_forward(int64_t web);
    int64_t __vyb_qt_web_reload(int64_t web);
    int64_t __vyb_qt_init();
    int64_t __vyb_qt_quit();
    int64_t __vyb_qt_active();
    int64_t __vyb_qt_process_events();
    int64_t __vyb_qt_set_timer(int64_t ms);
    int64_t __vyb_qt_timer_fired();
    int64_t __vyb_qt_window_create();
    int64_t __vyb_qt_window_close(int64_t w);
    int64_t __vyb_qt_window_set_title(int64_t w, const char* title, int64_t len);
    vyb_file_str __vyb_qt_window_title(int64_t w);
    int64_t __vyb_qt_window_resize(int64_t w, int64_t width, int64_t height);
    int64_t __vyb_qt_window_width(int64_t w);
    int64_t __vyb_qt_window_height(int64_t w);
    int64_t __vyb_qt_window_show(int64_t w);
    int64_t __vyb_qt_window_hide(int64_t w);
    int64_t __vyb_qt_window_visible(int64_t w);
    int64_t __vyb_qt_label_create(int64_t parent, const char* text, int64_t len);
    int64_t __vyb_qt_label_set_text(int64_t label, const char* text, int64_t len);
    vyb_file_str __vyb_qt_label_text(int64_t label);
    int64_t __vyb_qt_button_create(int64_t parent, const char* text, int64_t len);
    int64_t __vyb_qt_button_set_text(int64_t button, const char* text, int64_t len);
    vyb_file_str __vyb_qt_button_text(int64_t button);
    int64_t __vyb_qt_button_set_enabled(int64_t button, int64_t enabled);
    int64_t __vyb_qt_edit_create(int64_t parent, const char* text, int64_t len);
    vyb_file_str __vyb_qt_edit_text(int64_t edit);
    int64_t __vyb_qt_edit_set_text(int64_t edit, const char* text, int64_t len);
    int64_t __vyb_qt_edit_set_placeholder(int64_t edit, const char* text, int64_t len);
    int64_t __vyb_qt_checkbox_create(int64_t parent, const char* text, int64_t len);
    int64_t __vyb_qt_checkbox_checked(int64_t box);
    int64_t __vyb_qt_checkbox_set_checked(int64_t box, int64_t on);
    int64_t __vyb_qt_progress_create(int64_t parent, int64_t max);
    int64_t __vyb_qt_progress_set_value(int64_t bar, int64_t value);
    int64_t __vyb_qt_vbox(int64_t parent);
    int64_t __vyb_qt_hbox(int64_t parent);
    int64_t __vyb_qt_layout_add(int64_t layout, int64_t child);
    int64_t __vyb_qt_kind(int64_t h);
    int64_t __vyb_qt_event_count();
    int64_t __vyb_qt_event_handle();
    int64_t __vyb_qt_event_kind();
    int64_t __vyb_qt_event_pop();
    int64_t __vyb_qt_wait_event(int64_t timeout);
    int64_t __vyb_qt_run();
    int64_t __vyb_qt_run_stop();
    int64_t __vyb_qt_on_event(void* env, void* fn);
    int64_t __vyb_qt_post_event(int64_t h, int64_t kind);
    int64_t __vyb_qt_combo_create(int64_t parent);
    int64_t __vyb_qt_combo_add_item(int64_t combo, const char* text, int64_t len);
    int64_t __vyb_qt_combo_count(int64_t combo);
    int64_t __vyb_qt_combo_current_index(int64_t combo);
    int64_t __vyb_qt_combo_set_current_index(int64_t combo, int64_t idx);
    vyb_file_str __vyb_qt_combo_item_text(int64_t combo, int64_t idx);
    int64_t __vyb_qt_spin_create(int64_t parent, int64_t min, int64_t max);
    int64_t __vyb_qt_spin_value(int64_t spin);
    int64_t __vyb_qt_spin_set_value(int64_t spin, int64_t value);
    int64_t __vyb_qt_slider_create(int64_t parent, int64_t min, int64_t max);
    int64_t __vyb_qt_slider_value(int64_t slider);
    int64_t __vyb_qt_slider_set_value(int64_t slider, int64_t value);
    int64_t __vyb_qt_dial_create(int64_t parent, int64_t min, int64_t max);
    int64_t __vyb_qt_dial_value(int64_t dial);
    int64_t __vyb_qt_dial_set_value(int64_t dial, int64_t value);
    int64_t __vyb_qt_group_create(int64_t parent, const char* title, int64_t len);
    int64_t __vyb_qt_text_edit_create(int64_t parent);
    vyb_file_str __vyb_qt_text_edit_text(int64_t ed);
    int64_t __vyb_qt_text_edit_set_text(int64_t ed, const char* text, int64_t len);
    int64_t __vyb_qt_radio_create(int64_t parent, const char* text, int64_t len);
    int64_t __vyb_qt_radio_checked(int64_t radio);
    int64_t __vyb_qt_radio_set_checked(int64_t radio, int64_t on);
    int64_t __vyb_qt_widget_set_enabled(int64_t h, int64_t on);
    int64_t __vyb_qt_widget_enabled(int64_t h);
    int64_t __vyb_qt_grid(int64_t parent);
    int64_t __vyb_qt_grid_add(int64_t layout, int64_t child, int64_t row, int64_t col);
// gen_qt[main_decl]: end

#ifdef VYB_HAVE_OPENSSL
    // TLS runtime helpers (tls stdlib module) over OpenSSL. SSL/SSL_CTX are
    // opaque pointers carried across as Int; certs are in-line PEM strings.
    int64_t __vyb_tls_client_context(void);
    int64_t __vyb_tls_client_context_verified(const char* ca_pem);
    int64_t __vyb_tls_server_context(const char* cert_pem, const char* key_pem);
    void    __vyb_tls_ctx_free(int64_t ctxp);
    int64_t __vyb_tls_stream(int64_t ctxp, int64_t fd, const char* host);
    int64_t __vyb_tls_connect(int64_t sslp);
    int64_t __vyb_tls_accept(int64_t sslp);
    int64_t __vyb_tls_write(int64_t sslp, const char* data, int64_t len);
    vyb_file_str __vyb_tls_read(int64_t sslp, int64_t maxlen);
    int64_t __vyb_tls_close(int64_t sslp, int64_t fd);
    int64_t __vyb_tls_error_code(void);
    const char* __vyb_tls_error_message(void);
#endif

    // Time runtime helpers (time stdlib module)
    int64_t __vyb_time_epoch_secs(void);
    int64_t __vyb_time_epoch_millis(void);
    int64_t __vyb_time_nanos(void);
    int64_t __vyb_time_mono_millis(void);
    int64_t __vyb_time_sleep_ms(int64_t millis);

    // Threads runtime helpers (threads stdlib module). A `fn() -> Int` closure
    // arrives as { env, fn }; spawn runs it on a pthread.
    int64_t __vyb_thread_spawn(void* env, void* fn);
    int64_t __vyb_thread_join(int64_t handle);
    int64_t __vyb_thread_detach(int64_t handle);
    int64_t __vyb_agent_start(void* env, void* fn, int64_t failable, int64_t cap);
    int64_t __vyb_agent_start_bool(void* env, void* fn, int64_t failable, int64_t cap);
    int64_t __vyb_agent_start_float(void* env, void* fn, int64_t failable, int64_t cap);
    int64_t __vyb_agent_start_string(void* env, void* fn, int64_t failable, int64_t cap);
    int64_t __vyb_agent_send(int64_t handle, int64_t v);
    int64_t __vyb_agent_send_bool(int64_t handle, int64_t b);
    int64_t __vyb_agent_send_float(int64_t handle, int64_t bits);
    int64_t __vyb_agent_send_string(int64_t handle, char* ptr, int64_t len);
    int64_t __vyb_agent_len(int64_t handle);
    int64_t __vyb_agent_alive(int64_t handle);
    int64_t __vyb_agent_close(int64_t handle);
    int64_t __vyb_agent_free(int64_t handle);
    int64_t __vyb_agent_mailbox(int64_t handle);
    int64_t __vyb_agent_status(int64_t handle);
    int64_t __vyb_agent_error_code(int64_t handle);
    char* __vyb_agent_error(int64_t handle);
    int64_t __vyb_agent_set_dead_letter(int64_t handle, int64_t ch);
    int64_t __vyb_mutex_new(void);
    int64_t __vyb_mutex_lock(int64_t mh);
    int64_t __vyb_mutex_unlock(int64_t mh);
    int64_t __vyb_mutex_free(int64_t mh);
    // CondVar (composes with a Mutex handle for pthread_cond_wait).
    int64_t __vyb_cond_new(void);
    int64_t __vyb_cond_wait(int64_t cv, int64_t mh);
    int64_t __vyb_cond_signal(int64_t cv);
    int64_t __vyb_cond_broadcast(int64_t cv);
    int64_t __vyb_cond_free(int64_t cv);
    // AtomicInt (lock-free seq_cst).
    int64_t __vyb_atomic_new(int64_t init);
    int64_t __vyb_atomic_load(int64_t ah);
    int64_t __vyb_atomic_store(int64_t ah, int64_t v);
    int64_t __vyb_atomic_add(int64_t ah, int64_t v);
    int64_t __vyb_atomic_cas(int64_t ah, int64_t expected, int64_t desired);
    int64_t __vyb_atomic_free(int64_t ah);
    // Typed channels (Int payloads): a heap ring-buffer under a Mutex + CondVar.
    int64_t __vyb_chan_new(int64_t capacity);
    int64_t __vyb_chan_send(int64_t ch, int64_t v);
    int64_t __vyb_chan_recv(int64_t ch);
    int64_t __vyb_chan_try(int64_t ch);
    int64_t __vyb_chan_poll(int64_t ch, int64_t* out);
    int64_t __vyb_chan_recv_opt(int64_t ch, int64_t* out);
    int64_t __vyb_chan_len(int64_t ch);
    int64_t __vyb_chan_close(int64_t ch);
    int64_t __vyb_chan_free(int64_t ch);
    int64_t __vyb_chan_select(int64_t* handles, int64_t n);
    // String channels (channels stdlib module): a pthread ring buffer carrying
    // Vyb String payloads; recv/try transfer a retained reference to the caller.
    int64_t __vyb_strchan_new(int64_t capacity);
    int64_t __vyb_strchan_send(int64_t ch, const char* ptr, int64_t len);
    vyb_file_str __vyb_strchan_recv(int64_t ch);
    vyb_file_str __vyb_strchan_try(int64_t ch);
    int64_t __vyb_strchan_recv_opt(int64_t ch, vyb_file_str* out);
    int64_t __vyb_strchan_len(int64_t ch);
    int64_t __vyb_strchan_close(int64_t ch);
    int64_t __vyb_strchan_free(int64_t ch);
    // Tasks (tasks stdlib module): a detached pthread running a closure whose
    // result is delivered to a private capacity-1 channel. handle == chan.
    int64_t __vyb_task_spawn(void* env, void* fn);
    int64_t __vyb_task_await(int64_t task);
    int64_t __vyb_task_poll(int64_t task);
    int64_t __vyb_task_free(int64_t task);
    // Async event loop (async stdlib module): a cooperative, stackful-fiber
    // executor on this thread. spawn enqueues a closure as a fiber; await/poll
    // wait on its result; yield/sleep_ms suspend cooperatively without blocking
    // the loop.
    int64_t __vyb_async_spawn(void* env, void* fn);
    int64_t __vyb_async_run_all(void);
    int64_t __vyb_async_await(int64_t task);
    int64_t __vyb_async_poll(int64_t task);
    int64_t __vyb_async_set_error(int64_t task, void* err);
    int64_t __vyb_async_take_error(int64_t task);
    int64_t __vyb_async_detach(int64_t task);
    int64_t __vyb_async_yield(void);
    int64_t __vyb_async_sleep_ms(int64_t ms);
    // Async I/O (async stdlib module): suspendable non-blocking socket ops.
    int64_t __vyb_async_io_wait(int64_t fd, int64_t write);
    int64_t __vyb_async_accept(int64_t fd);
    vyb_file_str __vyb_async_recv(int64_t fd, int64_t maxlen);
    int64_t __vyb_async_send(int64_t fd, const char* data, int64_t len);
    int64_t __vyb_async_connect(int64_t fd, const char* ip, int64_t port);
    int64_t __vyb_async_sendto(int64_t fd, const char* data, int64_t len,
                               const char* ip, int64_t port);
    vyb_file_str __vyb_async_recvfrom(int64_t fd, int64_t maxlen);

    // JSON serialization for complex types
    char* __vyb_complex_to_json(void* instance, const char* type_name);
    void* __vyb_complex_from_json(const char* json_str, const char* type_name);

    // Type metadata registration
    void __vyb_register_type(void* metadata);

    // Type identity registry (id -> name)
    void __vyb_register_typename(uint64_t type_id, const char* type_name);
    const char* __vyb_get_typename(uint64_t type_id);

    // Error handling runtime functions (from error_handling.cpp)
    void __vyb_runtime_panic(const char* message) __attribute__((noreturn));
    void __vyb_runtime_untrapped_error(void* error) __attribute__((noreturn));
    void* __vyb_runtime_create_error_ex(const char* type_name, void* type_id, void* data, uint64_t data_size, void (*destructor)(void*), const char* file, uint32_t line, uint32_t column);
    void __vyb_runtime_free_error(void* error);

    // Stack trace runtime functions (Phase 6.4 - from error_handling.cpp)
    void __vyb_runtime_push_call_frame(const char* function_name, const char* file_path, uint32_t line, uint32_t column);
    void __vyb_runtime_pop_call_frame();
    void* __vyb_runtime_get_current_stack_trace();  // Returns VybStackTrace*

    // TODO: Future toString functions for compound types:
    // char* __vyb_toString_vec(void* vec_ptr, const char* element_type);
    // char* __vyb_toString_tuple(void* tuple_ptr, const char* type_spec);
}

// LLVM includes for ORC JIT compilation
#include <dlfcn.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Error.h>

// LLVM includes for object file emission
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

// LLVM includes for IR optimization passes (new pass manager)
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>

// System includes for linking
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>  // For chmod

// Globals for test verbose control
std::set<std::string> g_verbose_test_specifiers;
bool g_make_all_tests_verbose = false;
bool g_suppress_all_debug_output = false;

// Globals for parser verbose control
namespace vyb {
    std::set<std::string> g_verbose_parser_test_specifiers;
    bool g_make_all_parser_verbose = false;
    bool g_suppress_all_parser_debug_output = false;
    // Codegen debug output: off by default; enable with --debug-codegen
    bool g_debug_codegen = false;
}

// Concrete implementation of SemanticAnalyzer


// Function to optimize LLVM IR module based on optimization level
void optimize_module(llvm::Module* module, llvm::TargetMachine* targetMachine, int optLevel) {
    if (optLevel == 0) {
        if (vyb::g_debug_codegen) std::cout << "Skipping IR optimization (-O0)" << std::endl;
        return;  // No optimization at -O0
    }

    if (vyb::g_debug_codegen) std::cout << "Applying IR optimization passes (-O" << optLevel << ")..." << std::endl;

    // Create analysis managers
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    // Create pass builder
    llvm::PassBuilder PB(targetMachine);

    // Register all analysis passes
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Create module pass manager based on optimization level
    llvm::ModulePassManager MPM;

    switch (optLevel) {
        case 1: {
            if (vyb::g_debug_codegen) std::cout << "  Using O1 optimization pipeline (basic)" << std::endl;
            MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O1);
            break;
        }
        case 2: {
            if (vyb::g_debug_codegen) std::cout << "  Using O2 optimization pipeline (default)" << std::endl;
            MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
            break;
        }
        case 3: {
            if (vyb::g_debug_codegen) std::cout << "  Using O3 optimization pipeline (aggressive)" << std::endl;
            MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
            break;
        }
        default: {
            if (vyb::g_debug_codegen) std::cout << "  Using O2 optimization pipeline (default)" << std::endl;
            MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
            break;
        }
    }

    // Run the optimization pipeline
    MPM.run(*module, MAM);
    if (vyb::g_debug_codegen) std::cout << "  IR optimization completed" << std::endl;
}

namespace {
namespace fs = std::filesystem;

struct ModuleParseOptions {
    std::vector<fs::path> cliModulePaths;
    fs::path executablePath;
    bool skipImportResolution = false;
};

ModuleParseOptions g_module_parse_options;

struct ParsedModule {
    std::unique_ptr<vyb::ast::Module> ast;
    // Namespace-scope data computed during import resolution: which module owns
    // each top-level symbol and which names each module's own code may resolve.
    // Copied here so the analyzer can hide a consumer from another module's
    // private dependencies (see vyb::SemanticAnalyzer::setModuleScoping).
    std::unordered_map<std::string, std::string> ownerByName;
    std::unordered_map<std::string, std::unordered_set<std::string>> effectiveScope;
};

ParsedModule parse_vyb_module(const std::string& source, const std::string& fileName) {
    vyb::ModuleRegistryOptions options;
    options.cliModulePaths = g_module_parse_options.cliModulePaths;
    options.executablePath = g_module_parse_options.executablePath;
    options.skipImportResolution = g_module_parse_options.skipImportResolution;
    vyb::ModuleRegistry registry(std::move(options));
    ParsedModule parsed;
    parsed.ast = registry.resolveRoot(source, fileName);
    parsed.ownerByName = registry.moduleKeyByName();
    parsed.effectiveScope = registry.effectiveScope();
    return parsed;
}
} // namespace



// Function to compile Vyb code to object file
int compile_vyb_to_object(const std::string& source, const std::string& fileName,
                          const std::string& outputFile, int optLevel = 2) {
    std::cout << "Compiling " << fileName << " to object file..." << std::endl;

    // Initialize LLVM targets
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    try {
        std::cout << "Creating driver instance..." << std::endl;
        vyb::Driver driver;

        std::cout << "Parsing source and resolving imports..." << std::endl;
        auto parsed = parse_vyb_module(source, fileName);
        std::cout << "AST created successfully" << std::endl;

        std::cout << "Running semantic analysis..." << std::endl;
        vyb::SemanticAnalyzer semanticAnalyzer(driver);
        driver.setSemanticAnalyzer(&semanticAnalyzer);
        semanticAnalyzer.setModuleScoping(parsed.ownerByName, parsed.effectiveScope);
        semanticAnalyzer.analyze(parsed.ast.get());

        const auto& semanticErrors = semanticAnalyzer.getErrors();
        if (!semanticErrors.empty()) {
            std::cerr << "\nSemantic Errors:" << std::endl;
            for (const auto& error : semanticErrors) {
                std::cerr << "  " << error << std::endl;
            }
            throw std::runtime_error("Semantic analysis failed with " +
                std::to_string(semanticErrors.size()) + " error(s)");
        }
        std::cout << "Semantic analysis completed" << std::endl;

        std::cout << "Generating LLVM IR code..." << std::endl;
        vyb::LLVMCodegen codegen(driver);
        codegen.generate(parsed.ast.get(), fileName + ".ll");
        std::cout << "LLVM IR generation completed" << std::endl;

        // Get the LLVM module
        llvm::Module* module = codegen.getModule();

        // Verify the module
        std::cout << "Verifying module..." << std::endl;
        std::string verifyErrors;
        llvm::raw_string_ostream verifyStream(verifyErrors);
        if (llvm::verifyModule(*module, &verifyStream)) {
            verifyStream.flush();
            std::cerr << "Module verification failed:\n" << verifyErrors << std::endl;
            throw std::runtime_error("Module verification failed: " + verifyErrors);
        }
        std::cout << "Module verified successfully" << std::endl;

        // Setup target machine
        auto targetTriple = llvm::sys::getDefaultTargetTriple();
        std::cout << "Target triple: " << targetTriple << std::endl;
        module->setTargetTriple(targetTriple);

        std::string error;
        auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
        if (!target) {
            throw std::runtime_error("Failed to lookup target: " + error);
        }

        auto CPU = "generic";
        auto features = "";

        llvm::TargetOptions opt;
        auto relocModel = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
        auto targetMachine = target->createTargetMachine(targetTriple, CPU, features,
                                                         opt, relocModel);

        module->setDataLayout(targetMachine->createDataLayout());

        // Apply IR optimization passes before code generation
        optimize_module(module, targetMachine, optLevel);

        // Open output file
        std::error_code EC;
        llvm::raw_fd_ostream dest(outputFile, EC, llvm::sys::fs::OF_None);
        if (EC) {
            throw std::runtime_error("Could not open file: " + EC.message());
        }

        // Set optimization level
        llvm::CodeGenOptLevel cgOptLevel;
        switch (optLevel) {
            case 0: cgOptLevel = llvm::CodeGenOptLevel::None; break;
            case 1: cgOptLevel = llvm::CodeGenOptLevel::Less; break;
            case 3: cgOptLevel = llvm::CodeGenOptLevel::Aggressive; break;
            default: cgOptLevel = llvm::CodeGenOptLevel::Default; break;
        }

        // Emit object file
        llvm::legacy::PassManager pass;
        auto fileType = llvm::CodeGenFileType::ObjectFile;

        if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
            throw std::runtime_error("TargetMachine can't emit a file of this type");
        }

        std::cout << "Emitting object file with optimization level -O" << optLevel << "..." << std::endl;
        pass.run(*module);
        dest.flush();

        std::cout << "Successfully compiled to: " << outputFile << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error compiling to object file: " << e.what() << std::endl;
        return 1;
    }
}

// Function to link object files into executable
int link_vyb_executable(const std::vector<std::string>& objectFiles,
                        const std::string& outputExecutable,
                        const std::vector<std::string>& userLinkArgs = {},
                        bool staticLink = false) {
    std::cout << "Linking executable: " << outputExecutable << std::endl;

    // Locate the runtime source directory: prefer the CWD `runtime/` (the legacy
    // single-file `--build` from the source tree), else derive it from the vyb
    // executable location so `vyb build` works from any project directory.
    fs::path runtimeDir;
    {
        fs::path exeDir = fs::path(g_module_parse_options.executablePath).parent_path();
        std::vector<fs::path> candidates = {
            fs::path("runtime"),
            exeDir / ".." / "runtime",
            exeDir / "runtime"
        };
        for (const auto& c : candidates) {
            if (fs::exists(c / "vyb_runtime.c")) { runtimeDir = c; break; }
        }
    }
    if (runtimeDir.empty()) {
        std::cerr << "Error: cannot locate the Vyb runtime (runtime/vyb_runtime.c). "
                     "Run vyb from its installation directory or set the install layout "
                     "so the runtime sits next to the compiler." << std::endl;
        return 1;
    }

    // Compile the runtime support objects next to the output executable. The C
    // units live in runtimeDir; the type/metadata helpers are in the compiler's
    // C++ runtime source (self-contained, no LLVM dependency) and are compiled
    // as a separate C++ object so codegen-emitted `__vyb_*` symbols resolve.
    fs::path exeOutDir = fs::path(outputExecutable).parent_path();
    fs::path repoRoot = fs::path(g_module_parse_options.executablePath).parent_path() / "..";

    struct RuntimeUnit { std::string src, obj, compiler; };
    std::vector<RuntimeUnit> runtimeUnits{
        { (runtimeDir / "vyb_runtime.c").string(), "vyb_runtime.o", "cc" },
        { (runtimeDir / "vyb_type_metadata.c").string(), "vyb_type_metadata.o", "cc" }
    };
    fs::path cppRuntime = repoRoot / "src/runtime/error_handling.cpp";
    if (!fs::exists(cppRuntime)) {
        fs::path cwdCpp = fs::current_path() / "src/runtime/error_handling.cpp";
        if (fs::exists(cwdCpp)) cppRuntime = cwdCpp;
    }
    if (fs::exists(cppRuntime)) {
        runtimeUnits.push_back({ cppRuntime.string(), "error_handling.o", "c++" });
    }
    // The compiler's intrinsic library also lives in C++ source (self-contained,
    // extern "C"). It defines __vyb_closure_retain/release and other codegen-emitted
    // `__vyb_*` helpers, so native apps must link it too.
    fs::path intrinsicsSrc = repoRoot / "src/vre/intrinsics.cpp";
    if (!fs::exists(intrinsicsSrc)) {
        fs::path cwdIntr = fs::current_path() / "src/vre/intrinsics.cpp";
        if (fs::exists(cwdIntr)) intrinsicsSrc = cwdIntr;
    }
    if (fs::exists(intrinsicsSrc)) {
        runtimeUnits.push_back({ intrinsicsSrc.string(), "intrinsics.o", "c++" });
    }

    // Some runtime atoms (OpenSSL TLS shims, ncurses TUI shims) are compiled out
    // when their -D feature flag is absent. Detect, from the program's own
    // compiled object, whether it references those intrinsics, and only then
    // enable the matching runtime feature + link library. Plain standalone
    // binaries without TLS/`curses` keep a light footprint and build even on
    // systems without OpenSSL/ncurses dev packages.
    bool needTls = false;
    bool needCurses = false;
    for (const auto& objFile : objectFiles) {
        if (!needTls && system(("grep -aq __vyb_tls_ " + objFile).c_str()) == 0) {
            needTls = true;
        }
        if (!needCurses && system(("grep -aq __vyb_curses_ " + objFile).c_str()) == 0) {
            needCurses = true;
        }
    }

    std::vector<std::string> runtimeObjects;
    for (const auto& unit : runtimeUnits) {
        fs::path runtimeObject = exeOutDir / unit.obj;
        std::cout << "Compiling Vyb runtime library (" << unit.src << ")..." << std::endl;
        std::string compileCmd = unit.compiler + " -c -O2 -fPIC " + unit.src +
                                 " -o " + runtimeObject.string();
        if (unit.compiler == "cc") {
            compileCmd += " -D_GNU_SOURCE";
            if (needTls) compileCmd += " -DVYB_HAVE_OPENSSL";
            if (needCurses) compileCmd += " -DVYB_HAVE_NCURSES";
        }
        if (unit.compiler == "c++") {
            compileCmd += " -D_GNU_SOURCE";
            compileCmd += " -std=c++17 -I" + (repoRoot / "include").string();
        }
        if (system(compileCmd.c_str()) != 0) {
            std::cerr << "Failed to compile runtime library" << std::endl;
            return 1;
        }
        runtimeObjects.push_back(runtimeObject.string());
    }

    // Prefer the C++ compiler driver for the final link: it pulls in CRT
    // startup objects, libstdc++ and libgcc automatically (which raw ld cannot),
    // which is required once the C++ runtime atom (error_handling.o) is linked.
    auto findCompilerDriver = []() -> std::string {
        if (const char* cxx = std::getenv("CXX")) {
            if (*cxx) return std::string(cxx);
        }
        for (const char* cand : {"c++", "g++", "clang++"}) {
            if (std::system((std::string("which ") + cand + " >/dev/null 2>&1").c_str()) == 0)
                return std::string(cand);
        }
        return "";
    };
    std::string linker = findCompilerDriver();
    std::vector<std::string> linkerArgs;
    bool useCompilerDriver = !linker.empty();

    if (useCompilerDriver) {
        linkerArgs.push_back("-o");
        linkerArgs.push_back(outputExecutable);
        if (staticLink) linkerArgs.push_back("-static");
        linkerArgs.push_back("-Wl,--no-as-needed");
    } else {
#ifdef __APPLE__
        linker = "ld";
        linkerArgs.push_back("-macosx_version_min");
        linkerArgs.push_back("10.15");
        linkerArgs.push_back("-arch");
        linkerArgs.push_back("x86_64");
        linkerArgs.push_back("-dynamic");
        linkerArgs.push_back("-dylib");
        linkerArgs.push_back("-L/usr/lib");
        linkerArgs.push_back("-L/usr/local/lib");
        linkerArgs.push_back("-o");
        linkerArgs.push_back(outputExecutable);
#else
        if (std::system("which lld >/dev/null 2>&1") == 0) linker = "lld";
        else if (std::system("which ld.lld >/dev/null 2>&1") == 0) linker = "ld.lld";
        else linker = "ld";
        linkerArgs.push_back("-dynamic-linker");
        linkerArgs.push_back("/lib64/ld-linux-x86-64.so.2");
        linkerArgs.push_back("-o");
        linkerArgs.push_back(outputExecutable);

        std::vector<std::string> crtPaths = {
            "/usr/lib/x86_64-linux-gnu/", "/usr/lib64/", "/usr/lib/",
            "/lib/x86_64-linux-gnu/", "/lib64/", "/lib/"
        };
        auto findCrtFile = [&](const std::string& filename) -> std::string {
            for (const auto& path : crtPaths)
                if (access((path + filename).c_str(), F_OK) == 0) return path + filename;
            return "";
        };
        std::string crt1 = findCrtFile("crt1.o");
        std::string crti = findCrtFile("crti.o");
        if (!crt1.empty()) linkerArgs.push_back(crt1);
        if (!crti.empty()) linkerArgs.push_back(crti);
#endif
    }

    // Add all object files, then the compiled runtime atoms.
    for (const auto& objFile : objectFiles) linkerArgs.push_back(objFile);
    for (const auto& runtimeObject : runtimeObjects)
        if (access(runtimeObject.c_str(), F_OK) == 0) linkerArgs.push_back(runtimeObject);

    if (!useCompilerDriver) {
#ifndef __APPLE__
        linkerArgs.push_back("-L/usr/lib/x86_64-linux-gnu");
        linkerArgs.push_back("-L/usr/lib64");
        linkerArgs.push_back("-L/usr/lib");
#endif
    }

    auto normalizeLinkArg = [](const std::string& linkArg) -> std::string {
        auto endsWith = [](const std::string& value, const std::string& suffix) -> bool {
            return value.size() >= suffix.size() &&
                   value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (linkArg.empty() || linkArg[0] == '-' || linkArg.find('/') != std::string::npos ||
            endsWith(linkArg, ".a") || endsWith(linkArg, ".so") ||
            endsWith(linkArg, ".dylib") || endsWith(linkArg, ".o")) {
            return linkArg;
        }
        return "-l" + linkArg;
    };
    for (const auto& linkArg : userLinkArgs) linkerArgs.push_back(normalizeLinkArg(linkArg));

    // Link the optional runtime libraries the program's intrinsics require.
    if (needTls) {
        linkerArgs.push_back("-lssl");
        linkerArgs.push_back("-lcrypto");
    }
    if (needCurses) {
        linkerArgs.push_back("-lncursesw");
#ifndef __APPLE__
        linkerArgs.push_back("-ltinfo");
#endif
    }

    // Link against the C standard library and math library.
    if (staticLink) {
        if (!useCompilerDriver) linkerArgs.push_back("-lc");
        linkerArgs.push_back("-lm");
    } else {
        linkerArgs.push_back("-lm");
    }
    // Build command for display
    std::string command = linker;
    for (const auto& arg : linkerArgs) {
        command += " " + arg;
    }
    std::cout << "Linker command: " << command << std::endl;

    // Execute linker using fork/exec for better control
    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Failed to fork process" << std::endl;
        return 1;
    }

    if (pid == 0) {
        // Child process - execute linker
        std::vector<char*> args;
        args.push_back(const_cast<char*>(linker.c_str()));
        for (const auto& arg : linkerArgs) {
            args.push_back(const_cast<char*>(arg.c_str()));
        }
        args.push_back(nullptr);

        execvp(linker.c_str(), args.data());

        // If execvp returns, it failed
        std::cerr << "Failed to execute linker: " << linker << std::endl;
        exit(1);
    } else {
        // Parent process - wait for linker to complete
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            int exitCode = WEXITSTATUS(status);
            if (exitCode == 0) {
                std::cout << "Successfully linked executable: " << outputExecutable << std::endl;

                // Make executable
                chmod(outputExecutable.c_str(), 0755);

                return 0;
            } else {
                std::cerr << "Linker failed with exit code: " << exitCode << std::endl;
                return exitCode;
            }
        } else {
            std::cerr << "Linker process terminated abnormally" << std::endl;
            return 1;
        }
    }
}

// ===========================================================================
// Project system: `vyb build` and `vyb new`
// ===========================================================================
namespace {

std::string read_source_file(const std::string& filename) {
    std::ifstream file(filename);
    std::string s((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return s;
}

// Module search paths for a project: the project root + src/, then each local
// path dependency (the dependency dir and its src subdir). Local imports in the
// project's own src tree and in dependency trees resolve through these paths.
std::vector<fs::path> project_module_paths(const vyb::Manifest& m) {
    std::vector<fs::path> paths;
    paths.push_back(m.rootDir);
    auto srcDir = m.rootDir / "src";
    if (fs::is_directory(srcDir)) paths.push_back(srcDir);
    for (const auto& d : m.dependencies) {
        if (d.source != "path") continue;
        fs::path p = d.path.empty() ? fs::path(d.name) : fs::path(d.path);
        if (p.is_relative()) p = m.rootDir / p;
        p = fs::absolute(p).lexically_normal();
        paths.push_back(p);
        auto depSrc = p / "src";
        if (fs::is_directory(depSrc)) paths.push_back(depSrc);
    }
    return paths;
}

// Record the resolved dependency set to vyb.lock.
void write_lockfile(const vyb::Manifest& m) {
    fs::path lock = m.rootDir / "vyb.lock";
    std::ofstream out(lock);
    out << "# vyb.lock - generated by `vyb build`. Do not edit by hand.\n";
    out << "version = 1\n\n";
    for (const auto& d : m.dependencies) {
        if (d.source == "path") {
            fs::path p = d.path.empty() ? fs::path(d.name) : fs::path(d.path);
            if (p.is_relative()) p = m.rootDir / p;
            out << "[[" << d.name << "]]\n";
            out << "source = \"path\"\n";
            out << "resolved = \"" << fs::absolute(p).lexically_normal().string() << "\"\n\n";
        } else {
            out << "[[" << d.name << "]]\n";
            out << "source = \"" << d.source << "\"\n";
            out << "resolved = \"UNRESOLVED - dependency backend not implemented\"\n\n";
        }
    }
}

int run_build_command(int argc, char** argv, const std::string& exeArg) {
    std::string dir = ".";
    std::vector<std::string> linkArgs;
    bool staticLink = false;
    int opt = 2;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--link" && i + 1 < argc) {
            linkArgs.push_back(argv[++i]);
        } else if (a == "--static") {
            staticLink = true;
        } else if (a.compare(0, 2, "-O") == 0 && a.length() >= 3) {
            int lvl = a[2] - '0';
            opt = (lvl >= 0 && lvl <= 3) ? lvl : 2;
        } else if (a == "-C" && i + 1 < argc) {
            dir = argv[++i];
        } else if (!a.empty() && a[0] != '-') {
            dir = a;
        } else {
            std::cerr << "Error: unknown argument '" << a << "' for vyb build" << std::endl;
            return 1;
        }
    }

    std::error_code dirErr;
    fs::path root = fs::absolute(dir, dirErr);
    if (dirErr || !fs::is_directory(root)) {
        std::cerr << "Error: project directory not found: " << dir << std::endl;
        return 1;
    }

    std::string merr;
    auto manifest = vyb::load_manifest(root, &merr);
    if (!manifest) {
        std::cerr << "Error: " << merr << std::endl;
        std::cerr << "Run `vyb new <name>` to scaffold a project, or create a vyb.toml." << std::endl;
        return 1;
    }

    for (const auto& d : manifest->dependencies) {
        if (d.source != "path") {
            std::cerr << "Error: dependency '" << d.name << "' uses the '" << d.source
                      << "' source, which is not implemented yet; use a local path dependency."
                      << std::endl;
            return 1;
        }
    }

    g_module_parse_options.cliModulePaths = project_module_paths(*manifest);
    // The registry derives the stdlib search path from the executable location;
    // set it here because the normal arg loop that does this is bypassed.
    std::error_code exePathError;
    g_module_parse_options.executablePath = fs::absolute(exeArg, exePathError);
    if (exePathError) g_module_parse_options.executablePath = exeArg;

    fs::path targetDir = root / "target";
    fs::create_directories(targetDir);

    std::cout << "Building project '" << manifest->name << "' v" << manifest->version
              << " in " << root.string() << std::endl;

    int failures = 0;
    for (const auto& bin : manifest->bins) {
        fs::path src = root / bin.path;
        if (!fs::exists(src)) {
            std::cerr << "Error: bin source not found: " << src.string() << std::endl;
            ++failures;
            continue;
        }
        std::cout << "\n== bin '" << bin.name << "' ==\n";
        fs::path obj = targetDir / (bin.name + ".o");
        std::string source = read_source_file(src.string());
        int r = compile_vyb_to_object(source, src.string(), obj.string(), opt);
        if (r != 0) {
            ++failures;
            continue;
        }
        fs::path exe = targetDir / bin.name;
        int lr = link_vyb_executable({ obj.string() }, exe.string(), linkArgs, staticLink);
        if (lr != 0) {
            ++failures;
            continue;
        }
        std::cout << "Built " << exe.string() << std::endl;
    }

    write_lockfile(*manifest);

    if (failures == 0) {
        std::cout << "\nBuild successful. Binaries in " << targetDir.string() << std::endl;
        return 0;
    }
    std::cerr << "\nBuild finished with " << failures << " failed target(s)." << std::endl;
    return 1;
}

int run_new_command(int argc, char** argv) {
    std::string name;
    std::string version = "0.1.0";
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--version" && i + 1 < argc) {
            version = argv[++i];
        } else if (!a.empty() && a[0] != '-') {
            name = a;
        } else {
            std::cerr << "Error: unknown argument '" << a << "' for vyb new" << std::endl;
            return 1;
        }
    }
    if (name.empty()) {
        std::cerr << "Usage: vyb new <name> [--version X.Y.Z]" << std::endl;
        return 1;
    }

    fs::path root = fs::current_path() / name;
    fs::path srcDir = root / "src";
    if (fs::exists(root)) {
        std::cerr << "Error: '" << root.string() << "' already exists." << std::endl;
        return 1;
    }
    fs::create_directories(srcDir);

    std::ofstream manifestFile(root / "vyb.toml");
    manifestFile << vyb::default_manifest_toml(name, version);

    std::ofstream mainFile(srcDir / "main.vyb");
    mainFile << "// " << name << " — scaffolded by `vyb new`.\n\n"
             << "main()<Int> -> {\n"
             << "    println(\"Hello from " << name << "!\")\n"
             << "    return 0\n"
             << "}\n";

    std::cout << "Created project '" << name << "' in " << root.string() << std::endl;
    std::cout << "  cd " << name << " && vyb build" << std::endl;
    return 0;
}

} // namespace

// Function to execute Vyb code using LLVM JIT
int run_vyb_code(const std::string& source, const std::string& fileName, bool generateLLVMIR) {
    VYB_CDBG << "Starting run_vyb_code for file: " << fileName << std::endl;

    // Initialize LLVM targets for JIT
    VYB_CDBG << "Initializing LLVM targets..." << std::endl;
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // Setup for ORC JIT execution
    VYB_CDBG << "Setting up ORC JIT options..." << std::endl;
    VYB_CDBG << "LLVM components ready for ORC JIT." << std::endl;

    try {
        VYB_CDBG << "Creating driver instance..." << std::endl;
        vyb::Driver driver;

        VYB_CDBG << "Parsing source and resolving imports..." << std::endl;
        auto parsed = parse_vyb_module(source, fileName);
        VYB_CDBG << "AST created successfully" << std::endl;

        VYB_CDBG << "Running semantic analysis..." << std::endl;
        vyb::SemanticAnalyzer semanticAnalyzer(driver);
        driver.setSemanticAnalyzer(&semanticAnalyzer);  // Make semantic data available to codegen
        semanticAnalyzer.setModuleScoping(parsed.ownerByName, parsed.effectiveScope);
        semanticAnalyzer.analyze(parsed.ast.get());

        // Check for semantic errors and fail if any exist
        const auto& semanticErrors = semanticAnalyzer.getErrors();
        if (!semanticErrors.empty()) {
            std::cerr << "\nSemantic Errors:" << std::endl;
            for (const auto& error : semanticErrors) {
                std::cerr << "  " << error << std::endl;
            }
            throw std::runtime_error("Semantic analysis failed with " +
                std::to_string(semanticErrors.size()) + " error(s)");
        }
        VYB_CDBG << "Semantic analysis completed" << std::endl;

        VYB_CDBG << "Generating LLVM IR code..." << std::endl;
        vyb::LLVMCodegen codegen(driver);
        codegen.generate(parsed.ast.get(), fileName + ".ll");
        VYB_CDBG << "LLVM IR generation completed" << std::endl;

        if (generateLLVMIR) {
            // Generate LLVM IR to a file if requested
            std::string irFilename = fileName + ".ll";
            std::error_code EC;
            llvm::raw_fd_ostream irFile(irFilename, EC);
            if (EC) {
                throw std::runtime_error("Failed to open file for IR output: " + EC.message());
            }
            codegen.getModule()->print(irFile, nullptr);
            irFile.flush();
            std::cout << "Generated LLVM IR to " << irFilename << std::endl;
        }

        // Get the LLVM module and context from the code generator
        std::unique_ptr<llvm::Module> module = codegen.releaseModule();
        std::unique_ptr<llvm::LLVMContext> context = codegen.releaseContext();

        VYB_CDBG << "Setting up execution engine..." << std::endl;

        // Retrieve intrinsic functions before moving module into JIT
        llvm::Function* printlnFunc = module->getFunction("__vyb_println");
        llvm::Function* serializeFunc = module->getFunction("__vyb_serialize_to_json");
        llvm::Function* litConvertFunc = module->getFunction("__vyb_convert_lit_string");
        llvm::Function* stringConcatFunc = module->getFunction("__vyb_string_concat");

        // Retrieve toString functions
        llvm::Function* toStringIntFunc = module->getFunction("__vyb_toString_int");
        llvm::Function* toStringInt8Func = module->getFunction("__vyb_toString_int8");
        llvm::Function* toStringInt16Func = module->getFunction("__vyb_toString_int16");
        llvm::Function* toStringInt32Func = module->getFunction("__vyb_toString_int32");
        llvm::Function* toStringInt64Func = module->getFunction("__vyb_toString_int64");
        llvm::Function* toStringUInt8Func = module->getFunction("__vyb_toString_uint8");
        llvm::Function* toStringUInt16Func = module->getFunction("__vyb_toString_uint16");
        llvm::Function* toStringUInt32Func = module->getFunction("__vyb_toString_uint32");
        llvm::Function* toStringUInt64Func = module->getFunction("__vyb_toString_uint64");
        llvm::Function* toStringFloatFunc = module->getFunction("__vyb_toString_float");
        llvm::Function* toStringFloat32Func = module->getFunction("__vyb_toString_float32");
        llvm::Function* toStringBoolFunc = module->getFunction("__vyb_toString_bool");
        llvm::Function* toStringStringFunc = module->getFunction("__vyb_toString_string");
        llvm::Function* toStringCharFunc = module->getFunction("__vyb_toString_char");
        llvm::Function* toStringRuneFunc = module->getFunction("__vyb_toString_rune");
        llvm::Function* toStringByteFunc = module->getFunction("__vyb_toString_byte");

        if (!printlnFunc || !serializeFunc) {
            throw std::runtime_error("Missing required intrinsic functions in module");
        }

        // Verify the module before JIT compilation
        VYB_CDBG << "Verifying module..." << std::endl;
        std::string verifyErrors;
        llvm::raw_string_ostream verifyStream(verifyErrors);
        if (llvm::verifyModule(*module, &verifyStream)) {
            verifyStream.flush();
            std::cerr << "Module verification failed:\n" << verifyErrors << std::endl;
            throw std::runtime_error("Module verification failed: " + verifyErrors);
        }
        VYB_CDBG << "Module verified successfully" << std::endl;


        // Force libssl/libcrypto into the global symbol scope so the ORC JIT's
        // process-symbol generator can resolve OpenSSL symbols for the stdlib
        // `tls` module. They are linked into the binary, but under the default
        // linker's --as-needed the objects may be dropped (nothing native
        // references them), so dlopen(RTLD_GLOBAL) guarantees reachability.
        // Best-effort: ignore failures so builds without OpenSSL still work.
        for (const char* lib : {"libssl.so", "libssl.so.3", "libcrypto.so", "libcrypto.so.3"}) {
            if (void* h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL)) {
                if (dlsym(h, "SSL_CTX_new")) { VYB_CDBG << "OpenSSL symbols loaded from " << lib << std::endl; }
            }
        }

        // Create the ORC JIT execution engine
        auto jitOrErr = llvm::orc::LLJITBuilder().create();
        if (!jitOrErr) {
            std::string errorMsg;
            llvm::raw_string_ostream stream(errorMsg);
            stream << jitOrErr.takeError();
            throw std::runtime_error("Failed to create LLJIT: " + errorMsg);
        }
        auto& jit = *jitOrErr;

        // Register runtime functions with the JIT before adding module
        auto& mainDylib = jit->getMainJITDylib();
        llvm::orc::MangleAndInterner mangle(jit->getExecutionSession(), jit->getDataLayout());

        auto processSymbols = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            jit->getDataLayout().getGlobalPrefix());
        if (!processSymbols) {
            std::string errorMsg;
            llvm::raw_string_ostream stream(errorMsg);
            stream << processSymbols.takeError();
            throw std::runtime_error("Failed to expose process symbols to JIT: " + errorMsg);
        }
        mainDylib.addGenerator(std::move(*processSymbols));

        // Define symbol mappings for our runtime functions
        llvm::orc::SymbolMap runtimeSymbols;
        runtimeSymbols[mangle("__vyb_println")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_println), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_print")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_print), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_println_int")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_println_int), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_print_int")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_print_int), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_println_bool")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_println_bool), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_print_bool")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_print_bool), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_serialize_to_json")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_serialize_to_json), llvm::JITSymbolFlags::Exported);

        // Register error handling runtime functions
        runtimeSymbols[mangle("__vyb_runtime_panic")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_panic), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_untrapped_error")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_untrapped_error), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_create_error_ex")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_create_error_ex), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_free_error")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_free_error), llvm::JITSymbolFlags::Exported);

        // Register stack trace runtime functions (Phase 6.4)
        runtimeSymbols[mangle("__vyb_runtime_push_call_frame")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_push_call_frame), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_pop_call_frame")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_pop_call_frame), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_get_current_stack_trace")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_get_current_stack_trace), llvm::JITSymbolFlags::Exported);

        // Register standard library functions
        // Register malloc/free/memset/memcpy variants with numeric suffixes
        // LLVM may create renamed variants (malloc.1, malloc.2, etc.) when the same
        // function type is declared multiple times in the module.
        {
            auto mallocPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&malloc);
            auto freePtr = llvm::orc::ExecutorAddr::fromPtr((void*)&free);
            auto memsetPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&memset);
            auto memcpyPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&memcpy);
            auto memmovePtr = llvm::orc::ExecutorAddr::fromPtr((void*)&memmove);
            auto strlenPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&strlen);
            auto strcpyPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&strcpy);
            auto strdupPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&strdup);

            // Register base names
            runtimeSymbols[mangle("malloc")] = llvm::orc::ExecutorSymbolDef(mallocPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("free")] = llvm::orc::ExecutorSymbolDef(freePtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("memset")] = llvm::orc::ExecutorSymbolDef(memsetPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("memcpy")] = llvm::orc::ExecutorSymbolDef(memcpyPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("memmove")] = llvm::orc::ExecutorSymbolDef(memmovePtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("strlen")] = llvm::orc::ExecutorSymbolDef(strlenPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("strcpy")] = llvm::orc::ExecutorSymbolDef(strcpyPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("strdup")] = llvm::orc::ExecutorSymbolDef(strdupPtr, llvm::JITSymbolFlags::Exported);

            // Register numbered variants (LLVM auto-renames when same function declared multiple times
            // in the module; e.g. malloc.1, malloc.2, ... up to MAX_LIBC_SYMBOL_VARIANTS)
            static constexpr int MAX_LIBC_SYMBOL_VARIANTS = 20;
            for (int i = 1; i <= MAX_LIBC_SYMBOL_VARIANTS; ++i) {
                std::string suffix = "." + std::to_string(i);
                runtimeSymbols[mangle("malloc" + suffix)] = llvm::orc::ExecutorSymbolDef(mallocPtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("free" + suffix)] = llvm::orc::ExecutorSymbolDef(freePtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("memset" + suffix)] = llvm::orc::ExecutorSymbolDef(memsetPtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("memcpy" + suffix)] = llvm::orc::ExecutorSymbolDef(memcpyPtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("memmove" + suffix)] = llvm::orc::ExecutorSymbolDef(memmovePtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("strlen" + suffix)] = llvm::orc::ExecutorSymbolDef(strlenPtr, llvm::JITSymbolFlags::Exported);
            }
        }

        if (litConvertFunc) {
            runtimeSymbols[mangle("__vyb_convert_lit_string")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_convert_lit_string), llvm::JITSymbolFlags::Exported);
        }
        if (stringConcatFunc) {
            runtimeSymbols[mangle("__vyb_string_concat")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_concat), llvm::JITSymbolFlags::Exported);
        }

        // Register string replace helper (always export — codegen may emit the symbol)
        runtimeSymbols[mangle("__vyb_string_replace")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_replace), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_format")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_format), llvm::JITSymbolFlags::Exported);

        // Register closure reference-count helpers (always export — codegen may emit the symbols)
        runtimeSymbols[mangle("__vyb_closure_retain")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_closure_retain), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_closure_release")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_closure_release), llvm::JITSymbolFlags::Exported);

        // The detached-thread reaper (always export — the `threads` module may emit it).
        runtimeSymbols[mangle("__vyb_thread_detach")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_thread_detach), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_start")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_start), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_start_bool")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_start_bool), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_start_float")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_start_float), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_start_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_start_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_send")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_send), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_send_bool")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_send_bool), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_send_float")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_send_float), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_send_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_send_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_len")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_len), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_alive")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_alive), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_close")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_close), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_mailbox")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_mailbox), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_status")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_status), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_error_code")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_error_code), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_error")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_error), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_agent_set_dead_letter")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_agent_set_dead_letter), llvm::JITSymbolFlags::Exported);

        // Register toString functions
        if (toStringIntFunc) {
            runtimeSymbols[mangle("__vyb_toString_int")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int), llvm::JITSymbolFlags::Exported);
        }
        if (toStringInt8Func) {
            runtimeSymbols[mangle("__vyb_toString_int8")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int8), llvm::JITSymbolFlags::Exported);
        }
        if (toStringInt16Func) {
            runtimeSymbols[mangle("__vyb_toString_int16")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int16), llvm::JITSymbolFlags::Exported);
        }
        if (toStringInt32Func) {
            runtimeSymbols[mangle("__vyb_toString_int32")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int32), llvm::JITSymbolFlags::Exported);
        }
        if (toStringInt64Func) {
            runtimeSymbols[mangle("__vyb_toString_int64")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int64), llvm::JITSymbolFlags::Exported);
        }
        if (toStringUInt8Func) {
            runtimeSymbols[mangle("__vyb_toString_uint8")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_uint8), llvm::JITSymbolFlags::Exported);
        }
        if (toStringUInt16Func) {
            runtimeSymbols[mangle("__vyb_toString_uint16")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_uint16), llvm::JITSymbolFlags::Exported);
        }
        if (toStringUInt32Func) {
            runtimeSymbols[mangle("__vyb_toString_uint32")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_uint32), llvm::JITSymbolFlags::Exported);
        }
        if (toStringUInt64Func) {
            runtimeSymbols[mangle("__vyb_toString_uint64")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_uint64), llvm::JITSymbolFlags::Exported);
        }
        if (toStringFloatFunc) {
            runtimeSymbols[mangle("__vyb_toString_float")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_float), llvm::JITSymbolFlags::Exported);
        }
        if (toStringFloat32Func) {
            runtimeSymbols[mangle("__vyb_toString_float32")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_float32), llvm::JITSymbolFlags::Exported);
        }
        if (toStringBoolFunc) {
            runtimeSymbols[mangle("__vyb_toString_bool")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_bool), llvm::JITSymbolFlags::Exported);
        }
        if (toStringStringFunc) {
            runtimeSymbols[mangle("__vyb_toString_string")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_string), llvm::JITSymbolFlags::Exported);
        }
        if (toStringCharFunc) {
            runtimeSymbols[mangle("__vyb_toString_char")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_char), llvm::JITSymbolFlags::Exported);
        }
        if (toStringRuneFunc) {
            runtimeSymbols[mangle("__vyb_toString_rune")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_rune), llvm::JITSymbolFlags::Exported);
        }
        if (toStringByteFunc) {
            runtimeSymbols[mangle("__vyb_toString_byte")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_byte), llvm::JITSymbolFlags::Exported);
        }

        // Register new type conversion functions (to_string/from_string)
        runtimeSymbols[mangle("__vyb_int_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_int_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_uint_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_uint_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_float_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_float_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_bool_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_bool_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_register")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_register), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_retain")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_retain), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_release_each")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_release_each), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_retain_each")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_retain_each), llvm::JITSymbolFlags::Exported);

        runtimeSymbols[mangle("__vyb_int_from_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_int_from_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_float_from_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_float_from_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_bool_from_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_bool_from_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_from_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_from_string), llvm::JITSymbolFlags::Exported);

        // Register File I/O runtime helpers
        runtimeSymbols[mangle("__vyb_file_open")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_file_open), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_file_close")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_file_close), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_file_write")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_file_write), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_file_read_all")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_file_read_all), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_file_error_code")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_file_error_code), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_file_error_message")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_file_error_message), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_stdin_read")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_stdin_read), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_stdin_read_line")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_stdin_read_line), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_stdin_isatty")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_stdin_isatty), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_stdin_raw_enable")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_stdin_raw_enable), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_stdin_raw_disable")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_stdin_raw_disable), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_eprint")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_eprint), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_eprintln")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_eprintln), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_stdout_flush")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_stdout_flush), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_stderr_flush")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_stderr_flush), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_term_cols")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_term_cols), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_term_rows")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_term_rows), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_term_clear")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_term_clear), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_term_move_cursor")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_term_move_cursor), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_term_hide_cursor")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_term_hide_cursor), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_term_show_cursor")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_term_show_cursor), llvm::JITSymbolFlags::Exported);
#ifdef VYB_HAVE_NCURSES
        runtimeSymbols[mangle("__vyb_curses_init")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_init), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_close")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_close), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_ok")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_ok), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_rows")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_rows), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_cols")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_cols), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_refresh")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_refresh), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_clear")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_clear), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_move")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_move), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_addstr")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_addstr), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_move_addstr")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_move_addstr), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_has_color")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_has_color), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_start_color")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_start_color), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_color_pair")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_color_pair), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_init_pair")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_init_pair), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_attr_on")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_attr_on), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_attr_off")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_attr_off), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_attr_normal")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_attr_normal), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_attr_bold")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_attr_bold), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_attr_underline")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_attr_underline), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_attr_reverse")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_attr_reverse), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_attr_blink")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_attr_blink), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_getch")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_getch), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_nodelay")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_nodelay), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_timeout")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_timeout), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_keypad")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_keypad), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_show_cursor")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_show_cursor), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_curses_hide_cursor")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_curses_hide_cursor), llvm::JITSymbolFlags::Exported);
#endif
        // gen_qt[main_reg]: begin
        runtimeSymbols[mangle("__vyb_qt_web_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_web_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_web_load")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_web_load), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_web_url")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_web_url), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_web_title")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_web_title), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_web_loading")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_web_loading), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_web_back")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_web_back), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_web_forward")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_web_forward), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_web_reload")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_web_reload), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_init")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_init), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_quit")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_quit), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_active")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_active), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_process_events")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_process_events), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_set_timer")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_set_timer), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_timer_fired")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_timer_fired), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_close")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_close), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_set_title")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_set_title), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_title")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_title), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_resize")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_resize), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_width")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_width), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_height")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_height), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_show")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_show), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_hide")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_hide), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_window_visible")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_window_visible), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_label_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_label_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_label_set_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_label_set_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_label_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_label_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_button_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_button_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_button_set_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_button_set_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_button_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_button_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_button_set_enabled")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_button_set_enabled), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_edit_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_edit_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_edit_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_edit_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_edit_set_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_edit_set_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_edit_set_placeholder")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_edit_set_placeholder), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_checkbox_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_checkbox_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_checkbox_checked")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_checkbox_checked), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_checkbox_set_checked")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_checkbox_set_checked), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_progress_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_progress_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_progress_set_value")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_progress_set_value), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_vbox")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_vbox), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_hbox")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_hbox), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_layout_add")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_layout_add), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_kind")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_kind), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_event_count")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_event_count), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_event_handle")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_event_handle), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_event_kind")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_event_kind), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_event_pop")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_event_pop), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_wait_event")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_wait_event), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_run")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_run), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_run_stop")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_run_stop), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_on_event")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_on_event), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_post_event")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_post_event), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_combo_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_combo_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_combo_add_item")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_combo_add_item), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_combo_count")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_combo_count), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_combo_current_index")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_combo_current_index), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_combo_set_current_index")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_combo_set_current_index), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_combo_item_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_combo_item_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_spin_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_spin_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_spin_value")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_spin_value), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_spin_set_value")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_spin_set_value), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_slider_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_slider_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_slider_value")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_slider_value), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_slider_set_value")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_slider_set_value), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_dial_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_dial_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_dial_value")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_dial_value), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_dial_set_value")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_dial_set_value), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_group_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_group_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_text_edit_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_text_edit_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_text_edit_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_text_edit_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_text_edit_set_text")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_text_edit_set_text), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_radio_create")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_radio_create), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_radio_checked")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_radio_checked), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_radio_set_checked")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_radio_set_checked), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_widget_set_enabled")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_widget_set_enabled), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_widget_enabled")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_widget_enabled), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_grid")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_grid), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_qt_grid_add")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_qt_grid_add), llvm::JITSymbolFlags::Exported);
// gen_qt[main_reg]: end
        runtimeSymbols[mangle("__vyb_net_open")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_open), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_close")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_close), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_bind")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_bind), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_listen")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_listen), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_accept")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_accept), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_connect")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_connect), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_send")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_send), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_recv")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_recv), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_local_port")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_local_port), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_error_code")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_error_code), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_error_message")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_error_message), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_resolve")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_resolve), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_sendto")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_sendto), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_recvfrom")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_recvfrom), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_last_peer_ip")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_last_peer_ip), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_last_peer_port")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_last_peer_port), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_net_set_timeout")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_net_set_timeout), llvm::JITSymbolFlags::Exported);

        // UTF-8 codepoint helpers (utf8 stdlib module)
        runtimeSymbols[mangle("__vyb_utf8_len")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_utf8_len), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_utf8_index")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_utf8_index), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_utf8_at")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_utf8_at), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_utf8_valid")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_utf8_valid), llvm::JITSymbolFlags::Exported);

        // Environment helpers (env stdlib module)
        runtimeSymbols[mangle("__vyb_env_get")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_env_get), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_env_set")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_env_set), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_env_unset")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_env_unset), llvm::JITSymbolFlags::Exported);

        // Pseudo-random helpers (rand stdlib module)
        runtimeSymbols[mangle("__vyb_rand")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_rand), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_rand_range")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_rand_range), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_rand_seed")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_rand_seed), llvm::JITSymbolFlags::Exported);

        // Process helpers (process stdlib module)
        runtimeSymbols[mangle("__vyb_exec_run")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_exec_run), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_exec_output")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_exec_output), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_exec_status")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_exec_status), llvm::JITSymbolFlags::Exported);

        // Regex helpers (regex stdlib module)
        runtimeSymbols[mangle("__vyb_regex_match")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_regex_match), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_regex_find")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_regex_find), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_regex_capture_match")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_regex_capture_match), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_regex_capture")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_regex_capture), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_regex_replace")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_regex_replace), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_regex_replace_all")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_regex_replace_all), llvm::JITSymbolFlags::Exported);

#ifdef VYB_HAVE_OPENSSL
        // TLS runtime shims (tls stdlib module). Only present in OpenSSL builds;
        // a no-OpenSSL build simply has no tls module symbols to register.
        runtimeSymbols[mangle("__vyb_tls_client_context")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_client_context), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_client_context_verified")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_client_context_verified), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_server_context")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_server_context), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_ctx_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_ctx_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_stream")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_stream), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_connect")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_connect), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_accept")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_accept), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_write")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_write), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_read")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_read), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_close")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_close), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_error_code")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_error_code), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_tls_error_message")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_tls_error_message), llvm::JITSymbolFlags::Exported);
#endif
        runtimeSymbols[mangle("__vyb_time_epoch_secs")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_time_epoch_secs), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_time_epoch_millis")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_time_epoch_millis), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_time_nanos")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_time_nanos), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_time_mono_millis")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_time_mono_millis), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_time_sleep_ms")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_time_sleep_ms), llvm::JITSymbolFlags::Exported);

        runtimeSymbols[mangle("__vyb_thread_spawn")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_thread_spawn), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_thread_join")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_thread_join), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_mutex_new")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_mutex_new), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_mutex_lock")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_mutex_lock), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_mutex_unlock")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_mutex_unlock), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_mutex_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_mutex_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_cond_new")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_cond_new), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_cond_wait")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_cond_wait), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_cond_signal")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_cond_signal), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_cond_broadcast")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_cond_broadcast), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_cond_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_cond_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_atomic_new")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_atomic_new), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_atomic_load")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_atomic_load), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_atomic_store")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_atomic_store), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_atomic_add")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_atomic_add), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_atomic_cas")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_atomic_cas), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_atomic_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_atomic_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_new")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_new), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_send")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_send), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_recv")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_recv), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_try")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_try), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_poll")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_poll), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_recv_opt")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_recv_opt), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_len")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_len), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_close")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_close), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_chan_select")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_chan_select), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_strchan_new")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_strchan_new), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_strchan_send")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_strchan_send), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_strchan_recv")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_strchan_recv), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_strchan_recv_opt")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_strchan_recv_opt), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_strchan_try")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_strchan_try), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_strchan_len")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_strchan_len), llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("__vyb_strchan_close")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_strchan_close), llvm::JITSymbolFlags::Exported);
runtimeSymbols[mangle("__vyb_strchan_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_strchan_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_task_spawn")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_task_spawn), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_task_await")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_task_await), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_task_poll")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_task_poll), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_spawn")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_spawn), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_run_all")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_run_all), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_await")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_await), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_poll")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_poll), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_detach")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_detach), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_set_error")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_set_error), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_take_error")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_take_error), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_yield")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_yield), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_sleep_ms")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_sleep_ms), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_io_wait")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_io_wait), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_accept")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_accept), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_recv")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_recv), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_send")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_send), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_connect")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_connect), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_sendto")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_sendto), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_async_recvfrom")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_async_recvfrom), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_task_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_task_free), llvm::JITSymbolFlags::Exported);

        // Register JSON serialization functions
        runtimeSymbols[mangle("__vyb_complex_to_json")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_complex_to_json), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_complex_from_json")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_complex_from_json), llvm::JITSymbolFlags::Exported);

        // Register type metadata functions
        runtimeSymbols[mangle("__vyb_register_type")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_register_type), llvm::JITSymbolFlags::Exported);

        // Register the type identity registry
        runtimeSymbols[mangle("__vyb_register_typename")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_register_typename), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_get_typename")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_get_typename), llvm::JITSymbolFlags::Exported);

        // Add all the runtime symbols to the main dylib
        auto defineErr = mainDylib.define(llvm::orc::absoluteSymbols(runtimeSymbols));
        if (defineErr) {
            std::string errorMsg;
            llvm::raw_string_ostream stream(errorMsg);
            stream << defineErr;
            throw std::runtime_error("Failed to define runtime symbols: " + errorMsg);
        }

        // Check main function's return type before moving module
        llvm::Function* mainFuncForTypeCheck = module->getFunction("main");
        bool mainReturnsStruct = false;
        bool mainReturnsString = false;    // { ptr, i64 } Vyb string struct
        bool mainReturnsFailableStruct = false; // { T, i8* }
        bool mainFailablePayloadIsInt = false;
        bool mainFailablePayloadIsVoidDummy = false;

        if (mainFuncForTypeCheck) {
            llvm::Type* returnType = mainFuncForTypeCheck->getReturnType();
            mainReturnsStruct = returnType->isStructTy();
            // Detect if this is a Vyb string struct: { ptr, i64 } with 2 elements
            if (mainReturnsStruct) {
                llvm::StructType* st = llvm::cast<llvm::StructType>(returnType);
                if (st->getNumElements() == 2) {
                    if (st->getElementType(1)->isPointerTy()) {
                        // Failable return ABI { payload, error_ptr }.
                        mainReturnsFailableStruct = true;
                        mainFailablePayloadIsInt = st->getElementType(0)->isIntegerTy(64);
                        mainFailablePayloadIsVoidDummy = st->getElementType(0)->isIntegerTy(1);
                    } else if (st->getElementType(0)->isPointerTy() &&
                               st->getElementType(1)->isIntegerTy(64)) {
                        mainReturnsString = true;
                    }
                }
            }
        }

        // Apply IR optimizations before JIT execution (default -O2 for JIT)
        // Note: We need a target machine for optimization, but JIT uses default target
        std::string targetTriple = llvm::sys::getDefaultTargetTriple();
        std::string error;
        auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
        if (!target) {
            std::cerr << "Warning: Could not create target for optimization: " << error << std::endl;
        } else {
            llvm::TargetOptions opt;
            auto targetMachine = target->createTargetMachine(targetTriple, "generic", "",
                                                             opt, std::optional<llvm::Reloc::Model>());
            if (targetMachine) {
                module->setDataLayout(targetMachine->createDataLayout());
                optimize_module(module.get(), targetMachine, 2);  // Use O2 for JIT by default
                delete targetMachine;
            }
        }

        // Add the module to the JIT
        auto tsm = llvm::orc::ThreadSafeModule(std::move(module), std::move(context));
        auto addErr = jit->addIRModule(std::move(tsm));
        if (addErr) {
            std::string errorMsg;
            llvm::raw_string_ostream stream(errorMsg);
            stream << addErr;
            throw std::runtime_error("Failed to add module to JIT: " + errorMsg);
        }
        VYB_CDBG << "ORC JIT execution engine created successfully" << std::endl;

        // Call type registration function before main (simulates global constructors)
        auto registerTypesResult = jit->lookup("__vyb_register_all_types");
        if (registerTypesResult) {
            typedef void (*RegisterTypesFuncType)();
            RegisterTypesFuncType registerFunc = reinterpret_cast<RegisterTypesFuncType>(
                static_cast<void*>(registerTypesResult->toPtr<void*>()));
            registerFunc();
            VYB_CDBG << "Type metadata registered successfully" << std::endl;
        } else {
            VYB_CDBG << "No type registration function found (no custom types in program)" << std::endl;
        }

        // Look up the main function symbol
        auto symbolResult = jit->lookup("main");
        if (!symbolResult) {
            std::cerr << "Error: Could not find main function" << std::endl;
            return 1;
        }

        auto executorAddr = *symbolResult;

        // Check if return type is a struct (for String returns handled specially)
        if (mainReturnsStruct) {
            if (mainReturnsFailableStruct) {
                if (mainFailablePayloadIsVoidDummy) {
                    struct FailableVoidMainResult { bool ok_dummy; void* error; };
                    typedef FailableVoidMainResult (*FailableVoidMainFuncType)();
                    FailableVoidMainFuncType fMain = reinterpret_cast<FailableVoidMainFuncType>(
                        static_cast<void*>(executorAddr.toPtr<void*>()));
                    FailableVoidMainResult result = fMain();
                    if (result.error != nullptr) {
                        __vyb_runtime_untrapped_error(result.error);
                    }
                    return 0;
                }
                if (mainFailablePayloadIsInt) {
                    struct FailableIntMainResult { int64_t value; void* error; };
                    typedef FailableIntMainResult (*FailableIntMainFuncType)();
                    FailableIntMainFuncType fMain = reinterpret_cast<FailableIntMainFuncType>(
                        static_cast<void*>(executorAddr.toPtr<void*>()));
                    FailableIntMainResult result = fMain();
                    if (result.error != nullptr) {
                        __vyb_runtime_untrapped_error(result.error);
                    }
                    return 0;
                }
            }
            if (mainReturnsString) {
                // Single String return: call as struct { char*, int64_t } returning function
                // On x86_64 SysV ABI, { ptr, i64 } is returned in registers (rax + rdx)
                struct VybStringResult { const char* ptr; int64_t len; };
                typedef VybStringResult (*StringMainFuncType)();
                StringMainFuncType strMainFunc = reinterpret_cast<StringMainFuncType>(
                    static_cast<void*>(executorAddr.toPtr<void*>()));
                VybStringResult result = strMainFunc();
                if (result.ptr) {
                    // Output as JSON-encoded string with quotes
                    std::cout << "\"" << result.ptr << "\"" << std::endl;
                } else {
                    std::cout << "null" << std::endl;
                }
                return 0;
            }
            // Non-String struct: fall through to void call (codegen changed return type to void)
        }
        // Void return (or any non-String type, which codegen has lowered to void with
        // inline serialization): call as void and return 0.
        {
            typedef void (*VoidMainFuncType)();
            VoidMainFuncType voidMainFunc = reinterpret_cast<VoidMainFuncType>(
                static_cast<void*>(executorAddr.toPtr<void*>()));
            voidMainFunc();
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error running Vyb code: " << e.what() << std::endl;
        throw; // Re-throw the exception to allow calling code to handle errors
    }
}

namespace {
// Resolves the libclang helper binary sitting next to the running executable.
std::string helperBinPath(const char* argv0) {
#if defined(__unix__) || defined(__APPLE__)
    std::error_code ec;
    std::string self = std::filesystem::canonical(argv0, ec);
    if (!ec) {
        std::filesystem::path exe(self);
        if (exe.has_parent_path()) return (exe.parent_path() / "vyb-libclang").string();
    }
#endif
    return "vyb-libclang";
}

// Runs the libclang helper, capturing its stdout (the generated bindings).
// The helper writes diagnostics to its stderr, which is inherited by us.
#if defined(__unix__) || defined(__APPLE__)
bool runHelper(const std::string& helper, const std::vector<std::string>& args,
               std::string& out, int& exitCode) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return false;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return false; }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(helper.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(helper.c_str(), argv.data());
        std::fprintf(stderr, "Error: could not exec '%s': %s\n", helper.c_str(), std::strerror(errno));
        _exit(127);
    }
    close(pipefd[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) out.append(buf, static_cast<size_t>(n));
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return true;
}
#else
bool runHelper(const std::string&, const std::vector<std::string>&,
               std::string&, int& exitCode) {
    (void)exitCode;
    return false;  // subprocess dispatch is POSIX-only; --full unsupported here
}
#endif
} // namespace

int main(int argc, char* argv[]) {
    Catch::Session session; // Catch2 entry point

    // `vyb bindgen <header.h> [--full] [-D NAME[=VAL]] [-o out.vyb]` -- generate
    // Vyb FFI bindings from a C header. The default path is the lightweight
    // hand-rolled parser; `--full` selects the libclang full-preprocessor
    // backend (`#include` expansion, conditional evaluation, expression and
    // function-like macros).
    if (argc >= 2 && std::string(argv[1]) == "bindgen") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " bindgen <header.h> [--full] [-D NAME[=VAL]] [-o out.vyb]" << std::endl;
            return 1;
        }
        std::string headerPath = argv[2];
        std::string outputPath;
        bool full = false;
        std::vector<std::string> clangArgs;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-o" && i + 1 < argc) {
                outputPath = argv[++i];
            } else if (a == "--full" || a == "--libclang") {
                full = true;
            } else if (a.compare(0, 2, "-D") == 0) {
                clangArgs.push_back(a);
            } else {
                std::cerr << "Error: unknown argument '" << a << "'" << std::endl;
                return 1;
            }
        }

        std::vector<std::string> bindgenWarnings;
        std::string bindings;
        bool usedFull = false;

        if (full) {
            if (headerPath == "-") {
                std::cerr << "Error: --full needs a file path (stdin has no preprocessor context)" << std::endl;
                return 1;
            }
            std::string helper = helperBinPath(argv[0]);
            if (!std::filesystem::exists(helper)) {
                std::cerr << "Error: libclang helper not found ('" << helper
                          << "'); rebuild with libclang enabled" << std::endl;
                return 1;
            }
            std::string helperOut;
            int helperStatus = 0;
            std::vector<std::string> helperArgs;
            helperArgs.push_back(headerPath);
            for (const auto& d : clangArgs) helperArgs.push_back(d);
            if (!runHelper(helper, helperArgs, helperOut, helperStatus)) {
                std::cerr << "Error: could not start the libclang helper" << std::endl;
                return 1;
            }
            if (helperStatus != 0) {
                std::cerr << "Error: libclang backend exited with code " << helperStatus << std::endl;
                return helperStatus;
            }
            bindings = helperOut;
            usedFull = true;
        }

        if (!usedFull) {
            if (!clangArgs.empty()) {
                std::cerr << "Error: -D flags select the full (libclang) preprocessor; add --full" << std::endl;
                return 1;
            }
            if (headerPath == "-") {
                std::cerr << "Error: could not open C header '-'" << std::endl;
                return 1;
            }
            std::ifstream headerFile(headerPath);
            if (!headerFile) {
                std::cerr << "Error: could not open C header '" << headerPath << "'" << std::endl;
                return 1;
            }
            std::string source((std::istreambuf_iterator<char>(headerFile)),
                               std::istreambuf_iterator<char>());
            headerFile.close();
            bindings = vyb::bindgen::generateBindings(source, headerPath, &bindgenWarnings);
        }

        for (const auto& warning : bindgenWarnings) {
            std::cerr << "warning: " << warning << std::endl;
        }

        if (!outputPath.empty()) {
            std::ofstream outFile(outputPath);
            if (!outFile) {
                std::cerr << "Error: could not write '" << outputPath << "'" << std::endl;
                return 1;
            }
            outFile << bindings;
            outFile.close();
            std::cout << "Wrote bindings to " << outputPath << std::endl;
        } else {
            std::cout << bindings;
        }
        return 0;
    }

    // `vyb build [dir] [--link lib]* [--static] [-O0..3] [-C dir]`: build every
    // [[bin]] in a vyb.toml project, resolving local path dependencies and
    // reusing the module registry + native compile/link pipeline.
    // `vyb new <name>`: scaffold a fresh project.
    if (argc >= 2 && std::string(argv[1]) == "build") {
        return run_build_command(argc - 2, argv + 2, std::string(argv[0]));
    }
    if (argc >= 2 && std::string(argv[1]) == "new") {
        return run_new_command(argc - 2, argv + 2);
    }

    std::vector<std::string> catch_args;
    catch_args.push_back(argv[0]); // Program name

    bool next_arg_is_test_specifier_for_verbose = false;
    bool test_mode_active = false;
    bool parse_only_mode = false;
    bool semantic_only_mode = false;
    bool emit_llvm_ir = false;
    bool compile_mode = false;
    bool build_mode = false;
    std::string compile_output;
    std::string build_output;
    bool static_link = false;
    std::vector<std::string> link_libraries;
    std::vector<std::string> input_files;
    std::vector<fs::path> module_search_paths;
    int optimization_level = 2;  // Default -O2
    bool execute_jit = true;  // By default, execute the code with JIT

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test") {
            test_mode_active = true;
            // Enter test mode for Catch2; do not forward our own flag
            continue;
        } else if (arg == "--parse-only") {
            parse_only_mode = true;
            g_module_parse_options.skipImportResolution = true;
            execute_jit = false;  // Don't execute if parse-only
            continue;
        } else if (arg == "--semantic-only") {
            semantic_only_mode = true;
            execute_jit = false;  // Don't execute if semantic-only
            continue;
        } else if (arg == "--emit-llvm") {
            emit_llvm_ir = true;
            continue;
        } else if (arg == "--no-execute") {
            execute_jit = false;  // Explicitly disable JIT execution
            continue;
        } else if (arg == "--compile" || arg == "-c") {
            compile_mode = true;
            execute_jit = false;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                compile_output = argv[++i];
            }
            continue;
        } else if (arg == "--build" || arg == "-b") {
            build_mode = true;
            execute_jit = false;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                build_output = argv[++i];
            }
            continue;
        } else if (arg == "--static") {
            static_link = true;
            continue;
        } else if (arg == "--link") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --link requires a library name or path" << std::endl;
                return 1;
            }
            link_libraries.emplace_back(argv[++i]);
            continue;
        } else if (arg == "--module-path") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --module-path requires a directory path" << std::endl;
                return 1;
            }
            module_search_paths.emplace_back(argv[++i]);
            continue;
        } else if (arg.substr(0, 2) == "-O") {
            // Parse optimization level: -O0, -O1, -O2, -O3
            if (arg.length() > 2) {
                optimization_level = arg[2] - '0';
                if (optimization_level < 0 || optimization_level > 3) {
                    std::cerr << "Invalid optimization level: " << arg << std::endl;
                    optimization_level = 2;
                }
            }
            continue;
        } else if (arg == "--debug-verbose") {
            if (i + 1 < argc) {
                std::string specifiers_str = argv[++i];
                if (specifiers_str == "all") {
                    g_make_all_tests_verbose = true;
                } else {
                    // Parse comma-separated specifiers
                    size_t start = 0;
                    size_t end = specifiers_str.find(',');
                    while (end != std::string::npos) {
                        g_verbose_test_specifiers.insert(specifiers_str.substr(start, end - start));
                        start = end + 1;
                        end = specifiers_str.find(',', start);
                    }
                    g_verbose_test_specifiers.insert(specifiers_str.substr(start));
                }
            } else {
                std::cerr << "Warning: --debug-verbose requires an argument (e.g., \"all\" or test_name,[tag])." << std::endl;
            }
        } else if (arg == "--no-debug-output") {
            g_suppress_all_debug_output = true;
        } else if (arg == "--debug-parser-verbose") {
            if (i + 1 < argc) {
                std::string spec_str = argv[++i];
                if (spec_str == "all") {
                    vyb::g_make_all_parser_verbose = true;
                } else {
                    size_t start = 0;
                    size_t end = spec_str.find(',');
                    while (end != std::string::npos) {
                        vyb::g_verbose_parser_test_specifiers.insert(spec_str.substr(start, end - start));
                        start = end + 1;
                        end = spec_str.find(',', start);
                    }
                    vyb::g_verbose_parser_test_specifiers.insert(spec_str.substr(start));
                }
            } else {
                std::cerr << "Warning: --debug-parser-verbose requires an argument." << std::endl;
            }
        } else if (arg == "--no-parser-debug-output") {
            vyb::g_suppress_all_parser_debug_output = true;
        } else if (arg == "--debug-codegen") {
            vyb::g_debug_codegen = true;
        }
        else if (test_mode_active || arg[0] == '-' || arg[0] == '+' || arg[0] == '[') {
            // In test mode, or it's a Catch2 flag/tag/filter — pass it along
            catch_args.push_back(arg);
        } else {
            // Non-option argument: treat as an input file, not a Catch2 filter
            input_files.push_back(arg);
        }
    }

    if (!test_mode_active && (g_make_all_tests_verbose || !g_verbose_test_specifiers.empty() || g_suppress_all_debug_output ||
                              vyb::g_make_all_parser_verbose || !vyb::g_verbose_parser_test_specifiers.empty() || vyb::g_suppress_all_parser_debug_output)) {
         std::cerr << "Warning: Debug verbosity flags (--debug-verbose, --no-debug-output, --debug-parser-verbose, --no-parser-debug-output) are intended for use with --test mode." << std::endl;
    }

    g_module_parse_options.cliModulePaths = module_search_paths;
    std::error_code exePathError;
    g_module_parse_options.executablePath = fs::absolute(argv[0], exePathError);
    if (exePathError) {
        g_module_parse_options.executablePath = argv[0];
    }

    // Convert std::vector<std::string> to char* array for Catch2
    std::vector<char*> C_catch_args;
    for(const auto& s : catch_args) {
        C_catch_args.push_back(const_cast<char*>(s.c_str()));
    }

    // Only run Catch2 tests when --test flag is explicitly provided
    if (test_mode_active) {
        int result = session.run(C_catch_args.size(), C_catch_args.data());
        return result;
    }

    // If not in test mode, proceed with original file processing logic
    if (!input_files.empty()) {
        // Use the first input file collected during argument parsing
        std::string filename = input_files[0];
        VYB_CDBG << "Processing file: " << filename << std::endl;
        try {
            // Read the source file
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Error: Could not open file " << filename << std::endl;
                return 1;
            }
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            // In parse-only mode, just tokenize and parse
            if (parse_only_mode) {
                auto parsed = parse_vyb_module(source, filename);

                std::cout << "Parse completed successfully" << std::endl;
                return 0;
            }

            // Generate LLVM IR to a file if requested
            if (emit_llvm_ir) {
                auto parsed = parse_vyb_module(source, filename);

                vyb::Driver driver;

                // CRITICAL: Run semantic analysis to mark functions with needsErrorReturn
                std::cout << "Running semantic analysis..." << std::endl;
                vyb::SemanticAnalyzer semanticAnalyzer(driver);
                driver.setSemanticAnalyzer(&semanticAnalyzer);
                semanticAnalyzer.setModuleScoping(parsed.ownerByName, parsed.effectiveScope);
                semanticAnalyzer.analyze(parsed.ast.get());

                // Check for semantic errors
                const auto& semanticErrors = semanticAnalyzer.getErrors();
                if (!semanticErrors.empty()) {
                    std::cerr << "\nSemantic Errors:" << std::endl;
                    for (const auto& error : semanticErrors) {
                        std::cerr << "  " << error << std::endl;
                    }
                    return 1;
                }
                std::cout << "Semantic analysis completed" << std::endl;

                vyb::LLVMCodegen codegen(driver);

                // Output file: <input>.ll
                std::string out_ll = filename;
                size_t dot = out_ll.find_last_of('.');
                if (dot != std::string::npos) out_ll = out_ll.substr(0, dot);
                out_ll += ".ll";

                codegen.generate(parsed.ast.get(), out_ll);
                std::cout << "LLVM IR generated to " << out_ll << std::endl;
                return 0;
            }

            // In semantic-only mode, run semantic analysis without execution
            if (semantic_only_mode) {
                auto parsed = parse_vyb_module(source, filename);

                vyb::Driver driver;
                vyb::SemanticAnalyzer semanticAnalyzer(driver);
                driver.setSemanticAnalyzer(&semanticAnalyzer);  // Make semantic data available
                semanticAnalyzer.setModuleScoping(parsed.ownerByName, parsed.effectiveScope);
                semanticAnalyzer.analyze(parsed.ast.get());

                const auto& semanticErrors = semanticAnalyzer.getErrors();
                if (!semanticErrors.empty()) {
                    std::cerr << "\nSemantic Errors:" << std::endl;
                    for (const auto& error : semanticErrors) {
                        std::cerr << "  " << error << std::endl;
                    }
                    return 1;
                }

                std::cout << "Semantic analysis completed successfully" << std::endl;
                return 0;
            }

            // Compile mode: emit object file
            if (compile_mode) {
                std::string objFile = compile_output;
                if (objFile.empty()) {
                    // Default: replace extension with .o
                    objFile = filename;
                    size_t dot = objFile.find_last_of('.');
                    if (dot != std::string::npos) {
                        objFile = objFile.substr(0, dot);
                    }
                    objFile += ".o";
                }
                return compile_vyb_to_object(source, filename, objFile, optimization_level);
            }

            // Build mode: compile and link to executable
            if (build_mode) {
                std::string executableName = build_output;
                if (executableName.empty()) {
                    // Default: replace extension with no extension (executable name)
                    executableName = filename;
                    size_t dot = executableName.find_last_of('.');
                    if (dot != std::string::npos) {
                        executableName = executableName.substr(0, dot);
                    }
                }

                // Step 1: Compile to object file
                std::string objFile = executableName + ".o";
                std::cout << "Step 1: Compiling to object file..." << std::endl;
                int compileResult = compile_vyb_to_object(source, filename, objFile, optimization_level);
                if (compileResult != 0) {
                    std::cerr << "Compilation failed" << std::endl;
                    return compileResult;
                }

                // Step 2: Link to executable
                std::cout << "\nStep 2: Linking to executable..." << std::endl;
                std::vector<std::string> objectFiles = { objFile };
                int linkResult = link_vyb_executable(objectFiles, executableName, link_libraries, static_link);

                if (linkResult == 0) {
                    std::cout << "\n✅ Build successful!" << std::endl;
                    std::cout << "Executable: " << executableName << std::endl;
                    std::cout << "Run with: ./" << executableName << std::endl;
                }

                return linkResult;
            }

            // Default behavior: JIT compile and execute the code
            if (execute_jit) {
                try {
                    VYB_CDBG << "Starting JIT execution of " << filename << std::endl;
                    int result = run_vyb_code(source, filename, emit_llvm_ir);
                    return result;
                } catch (const std::exception& e) {
                    std::cerr << "Error during code execution: " << e.what() << std::endl;
                    return 1;
                }
            }

            // --no-execute: parse + semantic analysis to validate the file without running it
            {
                auto parsed = parse_vyb_module(source, filename);

                vyb::Driver driver;
                vyb::SemanticAnalyzer semanticAnalyzer(driver);
                driver.setSemanticAnalyzer(&semanticAnalyzer);
                semanticAnalyzer.setModuleScoping(parsed.ownerByName, parsed.effectiveScope);
                semanticAnalyzer.analyze(parsed.ast.get());

                const auto& semanticErrors = semanticAnalyzer.getErrors();
                if (!semanticErrors.empty()) {
                    std::cerr << "\nSemantic Errors:" << std::endl;
                    for (const auto& err : semanticErrors) {
                        std::cerr << "  " << err << std::endl;
                    }
                    std::cerr << "Error running Vyb code: Semantic analysis failed with "
                              << semanticErrors.size() << " error(s)" << std::endl;
                    return 1;
                }
                return 0;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
            return 1;
        }
    } else {
        std::cout << "Vyb Compiler - Usage: " << argv[0] << " <filename> [options] | --test [catch2_options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --parse-only          Stop after parsing (validates syntax only)" << std::endl;
        std::cout << "  --semantic-only       Stop after semantic analysis" << std::endl;
        std::cout << "  --emit-llvm           Generate LLVM IR to a .ll file" << std::endl;
        std::cout << "  --compile, -c [file]  Compile to object file (.o)" << std::endl;
        std::cout << "  --build, -b [file]    Compile and link to executable (NEW!)" << std::endl;
        std::cout << "  --link <lib-or-path>  Link a native build with -l<lib> or an explicit library/object path (repeatable)" << std::endl;
        std::cout << "  --static              Use static linking (with --build)" << std::endl;
        std::cout << "  --module-path <dir>   Add a module search path (repeatable)" << std::endl;
        std::cout << "  -O0, -O1, -O2, -O3    Set optimization level (default: -O2)" << std::endl;
        std::cout << "  --no-execute          Do not execute the code (JIT is on by default)" << std::endl;
        std::cout << std::endl;
        std::cout << "Examples:" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb                    # JIT compile and run" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb --build myapp      # Build executable" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb --build myapp --link m  # Build and link libm" << std::endl;
        std::cout << "  " << argv[0] << " app.vyb --module-path modules   # Add module search path" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb -b myapp -O3       # Build with max optimization" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb --compile prog.o   # Compile to object file" << std::endl;
        std::cout << std::endl;
        std::cout << "Test Mode Options:" << std::endl;
        std::cout << "  --test                Run test suite" << std::endl;
        std::cout << "  --debug-verbose <all|test_name,[tag],...]> Enable verbose output for tests" << std::endl;
        std::cout << "  --no-debug-output     Suppress all debug output" << std::endl;
        std::cout << "  --debug-parser-verbose <all|test_name,[tag],...]> Enable verbose parser output" << std::endl;
        std::cout << "  --no-parser-debug-output Suppress parser debug output" << std::endl;
    }

    return 0; // Reached only when no input file given and not in test mode (usage printed above)
}
