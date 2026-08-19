// ============================================================================
// QT (Qt5 Widgets native GUI) - extern "C" bridge for the stdlib `qt` module.
//
// The `qt` stdlib module surfaces a small, deterministic Widgets subset (a
// window, a label, a polling event loop, and a timer) to Vyb programs. The
// QWidget*/QLabel*/QApplication C++ object model stays entirely inside this
// bridge; Vyb sees only Int handles (qintptr-sized), Int dimensions, Bool/
// Int status flags, and String text. String arguments cross as { ptr, len }
// byte buffers (decoded as UTF-8); String results are returned as owned,
// registry-registered heap buffers (via __vyb_string_register) that the Vyb
// String built over them adopts and reference-counted cleanup frees - exactly
// the convention the network/regex/exec shims follow.
//
// Constructing a QApplication requires a Qt platform (X11/xcb under a display,
// offscreen, etc.). To keep `qt_init()` deterministic on a headless box, if
// neither the QPA platform nor a DISPLAY is set we default to the `offscreen`
// platform before creating the app. If the platform is genuinely unavailable
// the process aborts (Qt's qFatal on a failed platform integration), mirroring
// how curses refuses to run without a real terminal - GUI code expects a GUI.
//
// Threading: a QApplication must be created and its widgets driven from the
// main thread, which is where a Vyb program's `main` runs. The event loop is
// *polled* (qt_process_events) rather than callback-driven so tests stay
// deterministic under xvfb/offscreen and the FFI stays Int/String-shaped.
// ============================================================================
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QString>
#include <QByteArray>
#include <chrono>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <climits>

#if defined(__GNUC__) || defined(__clang__)
#define VYB_WEAK __attribute__((weak))
#else
#define VYB_WEAK
#endif

// A Vyb String that the bridge hands across the boundary is the { char*,
// int64_t } struct; the buffer is registered with the runtime string registry
// (refcount 1) and adopted by the Vyb String as an owned transfer.
struct vyb_qt_str { char* ptr; int64_t len; };

extern "C" void __vyb_string_register(void* p);

static QApplication* g_app = nullptr;           // outer QApplication, created once
static int64_t g_timer_ms = 0;                  // 0 = timer not armed
// A polled timer is a steady-clock deadline rather than a Qt QTimer: QTimer
// expiry needs the platform event dispatcher to run (offscreen Xvfb-only and
// other headless QPA platforms never fire it via plain processEvents()), where
// a deadline fires deterministically on every platform.
static std::chrono::steady_clock::time_point g_timer_start = std::chrono::steady_clock::now();

// Decode a { ptr, len } byte buffer as UTF-8 into a QString.
static QString qt_from_bytes(const char* ptr, int64_t len) {
    if (!ptr || len <= 0) return QString();
    if (len > (int64_t)INT_MAX) len = INT_MAX;
    return QString::fromUtf8(ptr, (int)len);
}

// Export a QString as an owned, registry-registered UTF-8 buffer for a Vyb
// String. The caller (Vyb) adopts the single reference and frees on GC.
static vyb_qt_str qt_to_owned(const QString& s) {
    vyb_qt_str r = { nullptr, 0 };
    QByteArray ba = s.toUtf8();
    if (ba.isEmpty()) return r;
    char* copy = (char*)std::malloc(static_cast<size_t>(ba.size()) + 1);
    if (!copy) return r;
    std::memcpy(copy, ba.constData(), static_cast<size_t>(ba.size()));
    copy[ba.size()] = '\0';
    __vyb_string_register(copy);
    r.ptr = copy;
    r.len = static_cast<int64_t>(ba.size());
    return r;
}

static QWidget* htowed(int64_t h) { return reinterpret_cast<QWidget*>(h); }
static int64_t wetoh(QWidget* w) { return reinterpret_cast<int64_t>(w); }

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

// Create the QApplication (once). Returns 1 when the GUI is available, 0 if it
// is already torn down or creation cannot proceed.
extern "C" VYB_WEAK int64_t __vyb_qt_init(void) {
    if (g_app) return 1;
    // Headless robustness: default to the offscreen QPA platform rather than
    // letting Qt fail/crash because there is no $DISPLAY and no platform set.
    const char* platform = qgetenv("QT_QPA_PLATFORM").constData();
    if (!platform || !*platform) {
        const char* dpy = std::getenv("DISPLAY");
        if (!dpy || !*dpy) qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    static int argc = 1;
    static char app0[] = "vyb";
    static char* argv[] = { app0, nullptr };
    QApplication* app = new QApplication(argc, argv);
    if (!app) return 0;
    app->setQuitOnLastWindowClosed(false);
    g_app = app;
    return 1;
}

// Shut the GUI down, destroying any live widgets and timers. Terminal.
extern "C" VYB_WEAK int64_t __vyb_qt_quit(void) {
    g_timer_ms = 0;
    delete g_app; g_app = nullptr;
    return 0;
}

// 1 while the GUI is initialized, else 0.
extern "C" VYB_WEAK int64_t __vyb_qt_active(void) { return g_app ? 1 : 0; }

// ----------------------------------------------------------------------------
// Event loop + timer
// ----------------------------------------------------------------------------

// Pump the event loop once (delivers paint, timer, and input events). Returns 0
// on success, -1 if the GUI is not initialized.
extern "C" VYB_WEAK int64_t __vyb_qt_process_events(void) {
    if (!g_app) return -1;
    QCoreApplication::processEvents();
    return 0;
}

// Arm a repeating timer every `ms` milliseconds (0 or negative disarms it). The
// timer is a steady-clock deadline: each qt_timer_fired() poll that observes an
// elapsed interval re-arms for the next one. Returns 0 on success, -1 if the GUI
// is not initialized.
extern "C" VYB_WEAK int64_t __vyb_qt_set_timer(int64_t ms) {
    if (!g_app) return -1;
    g_timer_ms = ms;
    g_timer_start = std::chrono::steady_clock::now();
    return 0;
}

// 1 if at least one timer interval has elapsed since the last check (and/or
// since set_timer), re-arming for the next interval; else 0. Prefer polling
// this together with qt_process_events() in a UI loop.
extern "C" VYB_WEAK int64_t __vyb_qt_timer_fired(void) {
    if (g_timer_ms <= 0) return 0;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_timer_start).count();
    if (elapsed < g_timer_ms) return 0;
    g_timer_start = now; // re-arm for the next interval
    return 1;
}

// ----------------------------------------------------------------------------
// Window (QWidget)
// ----------------------------------------------------------------------------

// Create a top-level window. Returns an Int handle, or 0 on failure.
extern "C" VYB_WEAK int64_t __vyb_qt_window_create(void) {
    if (!g_app) return 0;
    QWidget* w = new QWidget();
    return wetoh(w);
}

// Destroy a window handle (deletes it; children like labels go with it). Safe
// to call with a 0 handle. Returns 0.
extern "C" VYB_WEAK int64_t __vyb_qt_window_close(int64_t h) {
    if (!h) return 0;
    delete htowed(h);
    return 0;
}

extern "C" VYB_WEAK int64_t __vyb_qt_window_set_title(int64_t h, const char* s, int64_t len) {
    QWidget* w = htowed(h); if (!w) return -1;
    w->setWindowTitle(qt_from_bytes(s, len));
    return 0;
}

extern "C" VYB_WEAK vyb_qt_str __vyb_qt_window_title(int64_t h) {
    QWidget* w = htowed(h); if (!w) return { nullptr, 0 };
    return qt_to_owned(w->windowTitle());
}

extern "C" VYB_WEAK int64_t __vyb_qt_window_resize(int64_t h, int64_t w, int64_t ht) {
    QWidget* q = htowed(h); if (!q) return -1;
    q->resize(static_cast<int>(w), static_cast<int>(ht));
    return 0;
}

extern "C" VYB_WEAK int64_t __vyb_qt_window_width(int64_t h) {
    QWidget* q = htowed(h); if (!q) return -1;
    return static_cast<int64_t>(q->width());
}

extern "C" VYB_WEAK int64_t __vyb_qt_window_height(int64_t h) {
    QWidget* q = htowed(h); if (!q) return -1;
    return static_cast<int64_t>(q->height());
}

extern "C" VYB_WEAK int64_t __vyb_qt_window_show(int64_t h) {
    QWidget* q = htowed(h); if (!q) return -1;
    q->show();
    return 0;
}

extern "C" VYB_WEAK int64_t __vyb_qt_window_hide(int64_t h) {
    QWidget* q = htowed(h); if (!q) return -1;
    q->hide();
    return 0;
}

extern "C" VYB_WEAK int64_t __vyb_qt_window_visible(int64_t h) {
    QWidget* q = htowed(h); if (!q) return -1;
    return q->isVisible() ? 1 : 0;
}

// ----------------------------------------------------------------------------
// Label (QLabel)
// ----------------------------------------------------------------------------

// Create a label child of window handle `parent` (0 = top-level). Returns an Int
// handle, or 0 on failure.
extern "C" VYB_WEAK int64_t __vyb_qt_label_create(int64_t parent, const char* s, int64_t len) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QLabel* l = new QLabel(pw);
    l->setText(qt_from_bytes(s, len));
    return wetoh(l);
}

extern "C" VYB_WEAK int64_t __vyb_qt_label_set_text(int64_t h, const char* s, int64_t len) {
    QLabel* l = dynamic_cast<QLabel*>(htowed(h)); if (!l) return -1;
    l->setText(qt_from_bytes(s, len));
    return 0;
}

extern "C" VYB_WEAK vyb_qt_str __vyb_qt_label_text(int64_t h) {
    QLabel* l = dynamic_cast<QLabel*>(htowed(h)); if (!l) return { nullptr, 0 };
    return qt_to_owned(l->text());
}
