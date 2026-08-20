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
// Shared stub helpers: needed whenever either the core Qt or the optional
// QWebEngineView stub set compiles (i.e. Qt5 and/or QtWebEngine is absent).
#if !defined(VYB_HAVE_QT5) || !defined(VYB_HAVE_QT_WEBENGINE)
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define VYB_WEAK __attribute__((weak))
#else
#define VYB_WEAK
#endif

struct vyb_qt_str { char* ptr; int64_t len; };
static vyb_qt_str qt_stub_str() { return { nullptr, 0 }; }
#endif // !VYB_HAVE_QT5 || !VYB_HAVE_QT_WEBENGINE

#if !defined(VYB_HAVE_QT5)
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
VYB_WEAK int64_t __vyb_qt_screen_width() { return -1; }
VYB_WEAK int64_t __vyb_qt_screen_height() { return -1; }
VYB_WEAK int64_t __vyb_qt_screen_dpi() { return 100; }
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
VYB_WEAK int64_t __vyb_qt_layout_add_layout(int64_t layout, int64_t sub)
    { (void)layout; (void)sub; return -1; }
VYB_WEAK int64_t __vyb_qt_layout_set_stretch(int64_t layout, int64_t index, int64_t stretch)
    { (void)layout; (void)index; (void)stretch; return -1; }
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
VYB_WEAK int64_t __vyb_qt_post_event(int64_t h, int64_t kind)
    { (void)h; (void)kind; return 0; }
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
VYB_WEAK int64_t __vyb_qt_widget_set_visible(int64_t h, int64_t on)
    { (void)h; (void)on; return -1; }
VYB_WEAK int64_t __vyb_qt_widget_visible(int64_t h) { (void)h; return 0; }
VYB_WEAK int64_t __vyb_qt_tabs_create(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_tabs_add(int64_t tabs, const char* text, int64_t len)
    { (void)tabs; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_tabs_count(int64_t tabs) { (void)tabs; return -1; }
VYB_WEAK int64_t __vyb_qt_tabs_current(int64_t tabs) { (void)tabs; return -1; }
VYB_WEAK int64_t __vyb_qt_tabs_set_current(int64_t tabs, int64_t idx)
    { (void)tabs; (void)idx; return -1; }
VYB_WEAK int64_t __vyb_qt_list_create(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_list_add(int64_t list, const char* text, int64_t len)
    { (void)list; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_list_count(int64_t list) { (void)list; return -1; }
VYB_WEAK int64_t __vyb_qt_list_current(int64_t list) { (void)list; return -1; }
VYB_WEAK int64_t __vyb_qt_list_set_current(int64_t list, int64_t idx)
    { (void)list; (void)idx; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_list_item_text(int64_t list, int64_t idx)
    { (void)list; (void)idx; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_main_window_create() { return 0; }
VYB_WEAK int64_t __vyb_qt_menubar(int64_t mw) { (void)mw; return 0; }
VYB_WEAK int64_t __vyb_qt_menu_add(int64_t mw, const char* title, int64_t len)
    { (void)mw; (void)title; return 0; }
VYB_WEAK int64_t __vyb_qt_action_add(int64_t menu, const char* text, int64_t len)
    { (void)menu; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_action_count(int64_t menu) { (void)menu; return -1; }
VYB_WEAK int64_t __vyb_qt_statusbar_message(int64_t mw, const char* text, int64_t len)
    { (void)mw; (void)text; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_statusbar_text(int64_t mw)
    { (void)mw; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_toolbar_create(int64_t mw, const char* title, int64_t len)
    { (void)mw; (void)title; return 0; }
VYB_WEAK int64_t __vyb_qt_msg_info(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_msg_warn(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_msg_error(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_msg_about(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_msg_question(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_file_open(int64_t parent, const char* title, int64_t len, const char* filter, int64_t len2)
    { (void)parent; (void)title; (void)filter; return qt_stub_str(); }
VYB_WEAK vyb_qt_str __vyb_qt_file_save(int64_t parent, const char* title, int64_t len, const char* filter, int64_t len2)
    { (void)parent; (void)title; (void)filter; return qt_stub_str(); }
VYB_WEAK vyb_qt_str __vyb_qt_dir_select(int64_t parent, const char* title, int64_t len)
    { (void)parent; (void)title; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_dlg_info(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_dlg_warn(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_dlg_error(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_dlg_about(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_dlg_question(int64_t parent, const char* title, int64_t len, const char* text, int64_t len2)
    { (void)parent; (void)title; (void)text; return 0; }
VYB_WEAK int64_t __vyb_qt_dlg_open(int64_t parent, const char* title, int64_t len, const char* filter, int64_t len2)
    { (void)parent; (void)title; (void)filter; return 0; }
VYB_WEAK int64_t __vyb_qt_dlg_save(int64_t parent, const char* title, int64_t len, const char* filter, int64_t len2)
    { (void)parent; (void)title; (void)filter; return 0; }
VYB_WEAK int64_t __vyb_qt_dlg_dir(int64_t parent, const char* title, int64_t len)
    { (void)parent; (void)title; return 0; }
VYB_WEAK int64_t __vyb_qt_dlg_close(int64_t h) { (void)h; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_dlg_selected(int64_t h)
    { (void)h; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_event_result() { return 0; }
VYB_WEAK int64_t __vyb_qt_rich_create(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_rich_set_html(int64_t ed, const char* html, int64_t len)
    { (void)ed; (void)html; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_rich_html(int64_t ed)
    { (void)ed; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_rich_set_plain(int64_t ed, const char* text, int64_t len)
    { (void)ed; (void)text; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_rich_plain(int64_t ed)
    { (void)ed; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_rich_append(int64_t ed, const char* text, int64_t len)
    { (void)ed; (void)text; return -1; }
VYB_WEAK int64_t __vyb_qt_rich_clear(int64_t ed) { (void)ed; return -1; }
VYB_WEAK int64_t __vyb_qt_rich_set_text_color(int64_t ed, int64_t r, int64_t g, int64_t b)
    { (void)ed; (void)r; (void)g; (void)b; return -1; }
VYB_WEAK int64_t __vyb_qt_widget_set_font_size(int64_t h, int64_t pt)
    { (void)h; (void)pt; return -1; }
VYB_WEAK int64_t __vyb_qt_widget_set_font_bold(int64_t h, int64_t on)
    { (void)h; (void)on; return -1; }
VYB_WEAK int64_t __vyb_qt_widget_set_text_color(int64_t h, int64_t r, int64_t g, int64_t b)
    { (void)h; (void)r; (void)g; (void)b; return -1; }
// gen_qt[stub]: end
} // extern "C"
#endif // !VYB_HAVE_QT5

// QWebEngineView stubs: resolve the __vyb_qt_web_* symbols so the qt module's
// web surface degrades gracefully when QtWebEngine is not linked into the build
// (qt_web_create returns 0; qt_web_load returns -1; queries return "" / 0).
#if !defined(VYB_HAVE_QT_WEBENGINE)
extern "C" {
// gen_qt[web_stub]: begin
VYB_WEAK int64_t __vyb_qt_web_create(int64_t parent) { (void)parent; return 0; }
VYB_WEAK int64_t __vyb_qt_web_load(int64_t web, const char* url, int64_t len)
    { (void)web; (void)url; return -1; }
VYB_WEAK vyb_qt_str __vyb_qt_web_url(int64_t web)
    { (void)web; return qt_stub_str(); }
VYB_WEAK vyb_qt_str __vyb_qt_web_title(int64_t web)
    { (void)web; return qt_stub_str(); }
VYB_WEAK int64_t __vyb_qt_web_loading(int64_t web) { (void)web; return 0; }
VYB_WEAK int64_t __vyb_qt_web_back(int64_t web) { (void)web; return -1; }
VYB_WEAK int64_t __vyb_qt_web_forward(int64_t web) { (void)web; return -1; }
VYB_WEAK int64_t __vyb_qt_web_reload(int64_t web) { (void)web; return -1; }
VYB_WEAK int64_t __vyb_qt_web_zoom_in(int64_t web) { (void)web; return -1; }
VYB_WEAK int64_t __vyb_qt_web_zoom_out(int64_t web) { (void)web; return -1; }
// gen_qt[web_stub]: end
} // extern "C"
#endif // !VYB_HAVE_QT_WEBENGINE
