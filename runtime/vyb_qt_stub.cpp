// ============================================================================
// QT (Qt5 Widgets native GUI) - stub fallback when Qt5 is unavailable.
//
// So the stdlib `qt` module's symbols always resolve at JIT link time and the
// module degrades gracefully (mirroring how the curses shims in vyb_runtime.c
// report "screen unavailable" without ncursesw): when Qt5 is present this file
// compiles to nothing and the real bridge (runtime/vyb_qt_bridge.cpp) supplies
// the implementations; when Qt5 is absent these stubs report the GUI as
// unavailable (qt_init/… return 0, status/query helpers return -1/0) instead of
// an unresolved-symbol JIT failure.
// ============================================================================
#if !defined(VYB_HAVE_QT5)
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define VYB_WEAK __attribute__((weak))
#else
#define VYB_WEAK
#endif

struct vyb_qt_str { char* ptr; int64_t len; };
static vyb_qt_str qt_stub_str() { return { nullptr, 0 }; }

extern "C" {

// Lifecycle + event loop + timer
VYB_WEAK int64_t __vyb_qt_init(void) { return 0; }
VYB_WEAK int64_t __vyb_qt_quit(void) { return 0; }
VYB_WEAK int64_t __vyb_qt_active(void) { return 0; }
VYB_WEAK int64_t __vyb_qt_process_events(void) { return -1; }
VYB_WEAK int64_t __vyb_qt_set_timer(int64_t ms) { (void)ms; return -1; }
VYB_WEAK int64_t __vyb_qt_timer_fired(void) { return 0; }

// Window
VYB_WEAK int64_t __vyb_qt_window_create(void) { return 0; }
VYB_WEAK int64_t __vyb_qt_window_close(int64_t h) { (void)h; return -1; }
VYB_WEAK int64_t __vyb_qt_window_set_title(int64_t h, const char* s, int64_t len)
    { (void)h; (void)s; (void)len; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_window_title(int64_t h) { (void)h; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_window_resize(int64_t h, int64_t w, int64_t ht)
    { (void)h; (void)w; (void)ht; return -1; }
VYB_WEAK int64_t __vyb_qt_window_width(int64_t h) { (void)h; return -1; }
VYB_WEAK int64_t __vyb_qt_window_height(int64_t h) { (void)h; return -1; }
VYB_WEAK int64_t __vyb_qt_window_show(int64_t h) { (void)h; return -1; }
VYB_WEAK int64_t __vyb_qt_window_hide(int64_t h) { (void)h; return -1; }
VYB_WEAK int64_t __vyb_qt_window_visible(int64_t h) { (void)h; return 0; }

// Label
VYB_WEAK int64_t __vyb_qt_label_create(int64_t parent, const char* s, int64_t len)
    { (void)parent; (void)s; (void)len; return 0; }
VYB_WEAK int64_t __vyb_qt_label_set_text(int64_t h, const char* s, int64_t len)
    { (void)h; (void)s; (void)len; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_label_text(int64_t h) { (void)h; return qt_stub_str(); }

// Buttons
VYB_WEAK int64_t __vyb_qt_button_create(int64_t parent, const char* s, int64_t len)
    { (void)parent; (void)s; (void)len; return 0; }
VYB_WEAK int64_t __vyb_qt_button_set_text(int64_t h, const char* s, int64_t len)
    { (void)h; (void)s; (void)len; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_button_text(int64_t h) { (void)h; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_button_set_enabled(int64_t h, int64_t on)
    { (void)h; (void)on; return -1; }

// Text edits
VYB_WEAK int64_t __vyb_qt_edit_create(int64_t parent, const char* s, int64_t len)
    { (void)parent; (void)s; (void)len; return 0; }
VYB_WEAK vyb_qt_str __vyb_qt_edit_text(int64_t h) { (void)h; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_edit_set_text(int64_t h, const char* s, int64_t len)
    { (void)h; (void)s; (void)len; return -1; }
VYB_WEAK int64_t __vyb_qt_edit_set_placeholder(int64_t h, const char* s, int64_t len)
    { (void)h; (void)s; (void)len; return -1; }

// Checkboxes
VYB_WEAK int64_t __vyb_qt_checkbox_create(int64_t parent, const char* s, int64_t len)
    { (void)parent; (void)s; (void)len; return 0; }
VYB_WEAK int64_t __vyb_qt_checkbox_checked(int64_t h) { (void)h; return -1; }
VYB_WEAK int64_t __vyb_qt_checkbox_set_checked(int64_t h, int64_t on)
    { (void)h; (void)on; return -1; }

// Progress
VYB_WEAK int64_t __vyb_qt_progress_create(int64_t parent, int64_t maxv)
    { (void)parent; (void)maxv; return 0; }
VYB_WEAK int64_t __vyb_qt_progress_set_value(int64_t h, int64_t v)
    { (void)h; (void)v; return -1; }

// Layouts
VYB_WEAK int64_t __vyb_qt_vbox(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_hbox(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_layout_add(int64_t layout, int64_t child)
    { (void)layout; (void)child; return -1; }

// Introspection + polled events
VYB_WEAK int64_t __vyb_qt_kind(int64_t h) { (void)h; return 0; }
VYB_WEAK int64_t __vyb_qt_event_count(void) { return 0; }
VYB_WEAK int64_t __vyb_qt_event_handle(void) { return 0; }
VYB_WEAK int64_t __vyb_qt_event_kind(void) { return 0; }
VYB_WEAK int64_t __vyb_qt_event_pop(void) { return -1; }

} // extern "C"
#endif // !VYB_HAVE_QT5
