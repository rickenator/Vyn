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

// gen_qt[stub]: begin
VYB_WEAK int64_t __vyb_qt_init() { return 0; }
VYB_WEAK int64_t __vyb_qt_quit() { return 0; }
VYB_WEAK int64_t __vyb_qt_active() { return 0; }
VYB_WEAK int64_t __vyb_qt_process_events() { return -1; }
VYB_WEAK int64_t __vyb_qt_set_timer(int64_t ms) { (void)ms; return -1; }
VYB_WEAK int64_t __vyb_qt_timer_fired() { return 0; }
VYB_WEAK int64_t __vyb_qt_window_create() { return 0; }
VYB_WEAK int64_t __vyb_qt_window_close(int64_t w) { (void)w; return -1; }
VYB_WEAK int64_t __vyb_qt_window_set_title(int64_t w, const char* title, int64_t len)
    { (void)w; (void)title; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_window_title(int64_t w)
    { (void)w; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_window_resize(int64_t w, int64_t width, int64_t height)
    { (void)w; (void)width; (void)height; return -1; }
VYB_WEAK int64_t __vyb_qt_window_width(int64_t w) { (void)w; return -1; }
VYB_WEAK int64_t __vyb_qt_window_height(int64_t w) { (void)w; return -1; }
VYB_WEAK int64_t __vyb_qt_window_show(int64_t w) { (void)w; return -1; }
VYB_WEAK int64_t __vyb_qt_window_hide(int64_t w) { (void)w; return -1; }
VYB_WEAK int64_t __vyb_qt_window_visible(int64_t w) { (void)w; return 0; }
VYB_WEAK int64_t __vyb_qt_label_create(int64_t parent, const char* text, int64_t len)
    { (void)parent; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_label_set_text(int64_t label, const char* text, int64_t len)
    { (void)label; (void)text; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_label_text(int64_t label)
    { (void)label; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_button_create(int64_t parent, const char* text, int64_t len)
    { (void)parent; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_button_set_text(int64_t button, const char* text, int64_t len)
    { (void)button; (void)text; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_button_text(int64_t button)
    { (void)button; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_button_set_enabled(int64_t button, int64_t enabled)
    { (void)button; (void)enabled; return -1; }
VYB_WEAK int64_t __vyb_qt_edit_create(int64_t parent, const char* text, int64_t len)
    { (void)parent; (void)text; return 0; }
VYB_WEAK vyb_qt_str __vyb_qt_edit_text(int64_t edit)
    { (void)edit; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_edit_set_text(int64_t edit, const char* text, int64_t len)
    { (void)edit; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_edit_set_placeholder(int64_t edit, const char* text, int64_t len)
    { (void)edit; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_checkbox_create(int64_t parent, const char* text, int64_t len)
    { (void)parent; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_checkbox_checked(int64_t box) { (void)box; return -1; }
VYB_WEAK int64_t __vyb_qt_checkbox_set_checked(int64_t box, int64_t on)
    { (void)box; (void)on; return -1; }
VYB_WEAK int64_t __vyb_qt_progress_create(int64_t parent, int64_t max)
    { (void)parent; (void)max; return 0; }
VYB_WEAK int64_t __vyb_qt_progress_set_value(int64_t bar, int64_t value)
    { (void)bar; (void)value; return -1; }
VYB_WEAK int64_t __vyb_qt_vbox(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_hbox(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_layout_add(int64_t layout, int64_t child)
    { (void)layout; (void)child; return -1; }
VYB_WEAK int64_t __vyb_qt_kind(int64_t h) { (void)h; return 0; }
VYB_WEAK int64_t __vyb_qt_event_count() { return 0; }
VYB_WEAK int64_t __vyb_qt_event_handle() { return 0; }
VYB_WEAK int64_t __vyb_qt_event_kind() { return 0; }
VYB_WEAK int64_t __vyb_qt_event_pop() { return -1; }
VYB_WEAK int64_t __vyb_qt_wait_event(int64_t timeout) { (void)timeout; return -1; }
VYB_WEAK int64_t __vyb_qt_run() { return -1; }
VYB_WEAK int64_t __vyb_qt_run_stop() { return -1; }
VYB_WEAK int64_t __vyb_qt_on_event(void* env, void* fn)
    { (void)env; (void)fn; return 0; }
VYB_WEAK int64_t __vyb_qt_combo_create(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_combo_add_item(int64_t combo, const char* text, int64_t len)
    { (void)combo; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_combo_count(int64_t combo) { (void)combo; return -1; }
VYB_WEAK int64_t __vyb_qt_combo_current_index(int64_t combo) { (void)combo; return -1; }
VYB_WEAK int64_t __vyb_qt_combo_set_current_index(int64_t combo, int64_t idx)
    { (void)combo; (void)idx; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_combo_item_text(int64_t combo, int64_t idx)
    { (void)combo; (void)idx; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_spin_create(int64_t parent, int64_t min, int64_t max)
    { (void)parent; (void)min; (void)max; return 0; }
VYB_WEAK int64_t __vyb_qt_spin_value(int64_t spin) { (void)spin; return 0; }
VYB_WEAK int64_t __vyb_qt_spin_set_value(int64_t spin, int64_t value)
    { (void)spin; (void)value; return -1; }
VYB_WEAK int64_t __vyb_qt_slider_create(int64_t parent, int64_t min, int64_t max)
    { (void)parent; (void)min; (void)max; return 0; }
VYB_WEAK int64_t __vyb_qt_slider_value(int64_t slider) { (void)slider; return 0; }
VYB_WEAK int64_t __vyb_qt_slider_set_value(int64_t slider, int64_t value)
    { (void)slider; (void)value; return -1; }
VYB_WEAK int64_t __vyb_qt_dial_create(int64_t parent, int64_t min, int64_t max)
    { (void)parent; (void)min; (void)max; return 0; }
VYB_WEAK int64_t __vyb_qt_dial_value(int64_t dial) { (void)dial; return 0; }
VYB_WEAK int64_t __vyb_qt_dial_set_value(int64_t dial, int64_t value)
    { (void)dial; (void)value; return -1; }
VYB_WEAK int64_t __vyb_qt_group_create(int64_t parent, const char* title, int64_t len)
    { (void)parent; (void)title; return 0; }
VYB_WEAK int64_t __vyb_qt_text_edit_create(int64_t parent) { (void)parent; return 0; }
VYB_WEAK vyb_qt_str __vyb_qt_text_edit_text(int64_t ed)
    { (void)ed; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_text_edit_set_text(int64_t ed, const char* text, int64_t len)
    { (void)ed; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_radio_create(int64_t parent, const char* text, int64_t len)
    { (void)parent; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_radio_checked(int64_t radio) { (void)radio; return -1; }
VYB_WEAK int64_t __vyb_qt_radio_set_checked(int64_t radio, int64_t on)
    { (void)radio; (void)on; return -1; }
VYB_WEAK int64_t __vyb_qt_widget_set_enabled(int64_t h, int64_t on)
    { (void)h; (void)on; return -1; }
VYB_WEAK int64_t __vyb_qt_widget_enabled(int64_t h) { (void)h; return 0; }
VYB_WEAK int64_t __vyb_qt_grid(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_grid_add(int64_t layout, int64_t child, int64_t row, int64_t col)
    { (void)layout; (void)child; (void)row; (void)col; return -1; }
// gen_qt[stub]: end
} // extern "C"
#endif // !VYB_HAVE_QT5
