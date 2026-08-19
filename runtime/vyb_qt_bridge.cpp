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
// main thread, which is where a Vyb program's `main` runs. Two event-loop
// models coexist:
//   * Polled (qt_process_events / qt_wait_event + qt_event_*) is the default
//     and stays deterministic under xvfb/offscreen.
//   * Native (qt_run) enters QApplication::exec() and dispatches queued control
//     events into a callback registered with qt_on_event, so a GUI program can
//     run Qt's own main loop and still drive Vyb handlers from it. Background
//     asyncs keep running on their worker threads and their enqueued results
//     are picked up by the loop's queue-draining tick.
// ============================================================================
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QProgressBar>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QDial>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QMessageBox>
#include <QFileDialog>
#include <QFont>
#include <QPalette>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QMainWindow>
#include <QDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QString>
#include <QByteArray>
#if defined(VYB_HAVE_QT_WEBENGINE)
#include <QWebEngineView>
#endif
#include <chrono>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <thread>

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
// Paths chosen by finished async file/dir dialogs, keyed by dialog handle.
// The poll event queue carries only Int payloads, so a Qt String path is kept
// here (mutex-guarded) and handed to Vyb via qt_dlg_selected on demand.
static std::unordered_map<int64_t, QString> g_dlg_paths;
static std::mutex g_dlg_paths_mtx;
// A polled timer is a steady-clock deadline rather than a Qt QTimer: QTimer
// expiry needs the platform event dispatcher to run (offscreen Xvfb-only and
// other headless QPA platforms never fire it via plain processEvents()), where
// a deadline fires deterministically on every platform.
static std::chrono::steady_clock::time_point g_timer_start = std::chrono::steady_clock::now();

// ----------------------------------------------------------------------------
// Polled signal queue
// ----------------------------------------------------------------------------
// Qt signals never call into Vyb. Instead the bridge connects each control's
// primary signal (clicked / textChanged / toggled) to a lambda that enqueues a
// (widget handle, event kind) record; Vyb drains the queue with
// qt_event_count/handle/kind/pop and dispatches to its own handler map. This
// keeps the FFI Int-shaped and deterministic under the offscreen platform.
#define VYB_QT_EVT_CLICK     1
#define VYB_QT_EVT_TEXTCHANGED 2
#define VYB_QT_EVT_TOGGLED   3
#define VYB_QT_EVT_INDEXCHANGED 4
#define VYB_QT_EVT_VALUECHANGED 5
#define VYB_QT_EVT_CURRENTCHANGED 9
#define VYB_QT_EVT_DIALOG    10

// A queued record carries an optional int64 `result` payload (default 0) so a
// finished dialog can hand its answer (1 = Yes/Accepted, else 0) to the
// qt_on_event handler / qt_event_* poll without a second message.
struct vyb_qt_event { int64_t handle; int64_t kind; int64_t result; };
static std::deque<vyb_qt_event> g_events;
static std::mutex g_events_mtx;

static void vyb_qt_enqueue(int64_t handle, int64_t kind) {
    std::lock_guard<std::mutex> lk(g_events_mtx);
    g_events.push_back(vyb_qt_event{handle, kind, 0});
}

static void vyb_qt_enqueue(int64_t handle, int64_t kind, int64_t result) {
    std::lock_guard<std::mutex> lk(g_events_mtx);
    g_events.push_back(vyb_qt_event{handle, kind, result});
}

// Exposed so the optional QWebEngineView bridge (a separate translation unit,
// compiled only when QtWebEngine is linked) can enqueue its control events into
// the same polled queue that qt_on_event / qt_event_* drain.
extern "C" VYB_WEAK int64_t __vyb_qt_event_enqueue_widget(int64_t handle, int64_t kind) {
    vyb_qt_enqueue(handle, kind);
    return 0;
}

// Enqueue a synthetic control event for handle `h` from any thread (the queue
// is mutex-guarded). This is the safe way for a background async/worker fiber
// to signal the UI loop without touching QWidget off the main thread: the
// worker posts (handle, kind) and the qt_run handler / qt_event_* poll resolves
// it on the main thread. Returns 0.
extern "C" VYB_WEAK int64_t __vyb_qt_post_event(int64_t h, int64_t kind) {
    vyb_qt_enqueue(h, kind);
    return 0;
}

// ----------------------------------------------------------------------------
// Callback dispatch (qt_run native event loop)
// ----------------------------------------------------------------------------
// Qt signals never call into Vyb; the polled queue is the seam. qt_run() owns
// the native event loop (QApplication::exec()) and drives a short QTimer tick
// that drains the queued (handle, kind) records into the registered Vyb fn.
// That dispatch runs on the main thread (Qt thread affinity) and is the only
// place the handler is invoked, so GUI code never touches widgets off-thread.
extern "C" void* __vyb_closure_retain(void* env);
extern "C" void  __vyb_closure_release(void* env);

static void* g_handler_env = nullptr;   // closure env (retained while registered)
static void* g_handler_fn = nullptr;    // fn(env, handle, kind) -> Void
static bool  g_loop_active = false;     // qt_run() is inside QApplication::exec()
static bool  g_quit_requested = false;  // teardown pending until exec() returns
static bool  g_dispatching = false;     // a native dispatch drain is running
static int64_t g_cur_result = 0;        // result payload of the event currently dispatched

static void vyb_qt_release_handler() {
    if (g_handler_env) { __vyb_closure_release(g_handler_env); g_handler_env = nullptr; }
    g_handler_fn = nullptr;
}

// Drain every queued control event into the registered handler. Safe no-op when
// no handler is registered (the polled qt_event_* path still owns the queue).
static void vyb_qt_dispatch_queued() {
    if (!g_handler_fn) return;
    typedef void (*handler_t)(void*, int64_t, int64_t);
    handler_t cb = (handler_t)g_handler_fn;
    g_dispatching = true;
    for (;;) {
        int64_t handle = 0, kind = 0, result = 0;
        {
            std::lock_guard<std::mutex> lk(g_events_mtx);
            if (g_events.empty()) break;
            handle = g_events.front().handle;
            kind = g_events.front().kind;
            result = g_events.front().result;
            g_events.pop_front();
        }
        g_cur_result = result;
        cb(g_handler_env, handle, kind);
    }
    g_dispatching = false;
    g_cur_result = 0;
}

// Register the `fn(handle, kind) -> Void` closure (a `{ env, fn }` pair) that
// qt_run() dispatches queued control events into. The env is retained for the
// lifetime of the registration; a re-registration and full teardown both release
// it. Returns 0 on success, -1 on a null callback.
extern "C" VYB_WEAK int64_t __vyb_qt_on_event(void* env, void* fn) {
    if (!fn) return -1;
    if (g_handler_env != env || g_handler_fn != fn) {
        vyb_qt_release_handler();                  // drop the prior handler's ref
        if (env) env = __vyb_closure_retain(env);  // hold ours for the app's life
        g_handler_env = env;
        g_handler_fn = fn;
    }
    return 0;
}

// Opaque handle helpers: widgets cross as qintptr-sized QWidget; layouts as
// QLayout (never conflated - the typed accessors dynamic_cast to validate).
static QLayout* htolay(int64_t h) { return reinterpret_cast<QLayout*>(h); }

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

// Full teardown: drop the callback handler's env reference, clear the queue and
// timer, and destroy the app and any live widgets. Only safe after qt_run() has
// returned (the exec() stack is unwound). Terminal.
static void vyb_qt_full_teardown() {
    g_timer_ms = 0;
    vyb_qt_release_handler();
    { std::lock_guard<std::mutex> lk(g_events_mtx); g_events.clear(); }
    { std::lock_guard<std::mutex> lk(g_dlg_paths_mtx); g_dlg_paths.clear(); }
    delete g_app; g_app = nullptr;
}

// Shut the GUI down, destroying any live widgets and timers. Terminal. If the
// native qt_run() loop is currently live, this requests the loop to stop and
// defers the actual teardown until exec() has returned (so a handler calling
// qt_quit() cannot delete the app out from under the running loop).
extern "C" VYB_WEAK int64_t __vyb_qt_quit(void) {
    if (!g_app) return -1;
    if (g_loop_active) {
        g_quit_requested = true;
        g_app->quit();
        return 0;
    }
    vyb_qt_full_teardown();
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

// Run the Qt native event loop, dispatching queued control events to the
// qt_on_event handler as they arrive. A 2 ms QTimer tick drains the queue on the
// main thread (Qt thread affinity), so real input/paint/timer signals from Qt and
// records enqueued by background asyncs are both delivered to the handler. Runs
// until __vyb_qt_run_stop()/qt_quit() quits the loop; returns the exit code, or
// -1 if the GUI is not running. If qt_quit() requested teardown from inside a
// handler, it is finalized here once exec() has unwound.
extern "C" VYB_WEAK int64_t __vyb_qt_run(void) {
    if (!g_app) return -1;
    g_loop_active = true;
    g_quit_requested = false;
    QTimer tick;
    tick.setInterval(2);
    tick.setTimerType(Qt::PreciseTimer);
    QObject::connect(&tick, &QTimer::timeout, [] { vyb_qt_dispatch_queued(); });
    tick.start();
    int rc = g_app->exec();
    tick.stop();
    g_loop_active = false;
    if (g_quit_requested) {
        g_quit_requested = false;
        vyb_qt_full_teardown();
    }
    return rc;
}

// Stop a running qt_run() loop gracefully (the GUI stays up). Returns 0 on
// success, -1 if there is no running loop to stop.
extern "C" VYB_WEAK int64_t __vyb_qt_run_stop(void) {
    if (!g_app || !g_loop_active) return -1;
    g_app->quit();
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

// ----------------------------------------------------------------------------
// Buttons (QPushButton)
// ----------------------------------------------------------------------------

extern "C" VYB_WEAK int64_t __vyb_qt_button_create(int64_t parent, const char* s, int64_t len) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QPushButton* b = new QPushButton(qt_from_bytes(s, len), pw);
    QObject::connect(b, &QPushButton::clicked, [b]() { vyb_qt_enqueue(wetoh(b), VYB_QT_EVT_CLICK); });
    return wetoh(b);
}

extern "C" VYB_WEAK int64_t __vyb_qt_button_set_text(int64_t h, const char* s, int64_t len) {
    QPushButton* b = dynamic_cast<QPushButton*>(htowed(h)); if (!b) return -1;
    b->setText(qt_from_bytes(s, len));
    return 0;
}

extern "C" VYB_WEAK vyb_qt_str __vyb_qt_button_text(int64_t h) {
    QPushButton* b = dynamic_cast<QPushButton*>(htowed(h)); if (!b) return { nullptr, 0 };
    return qt_to_owned(b->text());
}

extern "C" VYB_WEAK int64_t __vyb_qt_button_set_enabled(int64_t h, int64_t on) {
    QPushButton* b = dynamic_cast<QPushButton*>(htowed(h)); if (!b) return -1;
    b->setEnabled(on ? true : false);
    return 0;
}

// ----------------------------------------------------------------------------
// Text edits (QLineEdit)
// ----------------------------------------------------------------------------

extern "C" VYB_WEAK int64_t __vyb_qt_edit_create(int64_t parent, const char* s, int64_t len) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QLineEdit* e = new QLineEdit(qt_from_bytes(s, len), pw);
    QObject::connect(e, &QLineEdit::textChanged, [e]() { vyb_qt_enqueue(wetoh(e), VYB_QT_EVT_TEXTCHANGED); });
    return wetoh(e);
}

extern "C" VYB_WEAK vyb_qt_str __vyb_qt_edit_text(int64_t h) {
    QLineEdit* e = dynamic_cast<QLineEdit*>(htowed(h)); if (!e) return { nullptr, 0 };
    return qt_to_owned(e->text());
}

extern "C" VYB_WEAK int64_t __vyb_qt_edit_set_text(int64_t h, const char* s, int64_t len) {
    QLineEdit* e = dynamic_cast<QLineEdit*>(htowed(h)); if (!e) return -1;
    e->setText(qt_from_bytes(s, len));
    return 0;
}

extern "C" VYB_WEAK int64_t __vyb_qt_edit_set_placeholder(int64_t h, const char* s, int64_t len) {
    QLineEdit* e = dynamic_cast<QLineEdit*>(htowed(h)); if (!e) return -1;
    e->setPlaceholderText(qt_from_bytes(s, len));
    return 0;
}

// ----------------------------------------------------------------------------
// Checkboxes (QCheckBox)
// ----------------------------------------------------------------------------

extern "C" VYB_WEAK int64_t __vyb_qt_checkbox_create(int64_t parent, const char* s, int64_t len) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QCheckBox* c = new QCheckBox(qt_from_bytes(s, len), pw);
    QObject::connect(c, &QCheckBox::toggled, [c]() { vyb_qt_enqueue(wetoh(c), VYB_QT_EVT_TOGGLED); });
    return wetoh(c);
}

extern "C" VYB_WEAK int64_t __vyb_qt_checkbox_checked(int64_t h) {
    QCheckBox* c = dynamic_cast<QCheckBox*>(htowed(h)); if (!c) return -1;
    return c->isChecked() ? 1 : 0;
}

extern "C" VYB_WEAK int64_t __vyb_qt_checkbox_set_checked(int64_t h, int64_t on) {
    QCheckBox* c = dynamic_cast<QCheckBox*>(htowed(h)); if (!c) return -1;
    c->setChecked(on ? true : false);
    return 0;
}

// ----------------------------------------------------------------------------
// Progress bars (QProgressBar)
// ----------------------------------------------------------------------------

extern "C" VYB_WEAK int64_t __vyb_qt_progress_create(int64_t parent, int64_t maxv) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QProgressBar* p = new QProgressBar(pw);
    p->setRange(0, (int)maxv);
    return wetoh(p);
}

extern "C" VYB_WEAK int64_t __vyb_qt_progress_set_value(int64_t h, int64_t v) {
    QProgressBar* p = dynamic_cast<QProgressBar*>(htowed(h)); if (!p) return -1;
    p->setValue((int)v);
    return 0;
}

// ----------------------------------------------------------------------------
// Box layouts
// ----------------------------------------------------------------------------

extern "C" VYB_WEAK int64_t __vyb_qt_vbox(int64_t parent) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    return reinterpret_cast<int64_t>(new QVBoxLayout(pw));
}

extern "C" VYB_WEAK int64_t __vyb_qt_hbox(int64_t parent) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    return reinterpret_cast<int64_t>(new QHBoxLayout(pw));
}

// Add a widget `child` into a box-layout `layout` (the layout takes ownership
// / parented to its window). Returns 0 on success, -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_layout_add(int64_t layout, int64_t child) {
    QLayout* l = htolay(layout); if (!l) return -1;
    QWidget* c = htowed(child); if (!c) return -1;
    l->addWidget(c);
    return 0;
}

// ----------------------------------------------------------------------------
// Kind introspection
// ----------------------------------------------------------------------------

// Static widget kind (dynamic_cast), so Vyb can validate a QtWidget wrapper
// before calling type-specific accessors. Layout handles are not widgets and
// report 0. enum QtWidgetKind matches mod.vyb.
extern "C" VYB_WEAK int64_t __vyb_qt_kind(int64_t h) {
    QWidget* w = htowed(h); if (!w) return 0;
    if (dynamic_cast<QPushButton*>(w))     return 3; // Button
    if (dynamic_cast<QLineEdit*>(w))       return 4; // Edit
    if (dynamic_cast<QCheckBox*>(w))       return 5; // Checkbox
    if (dynamic_cast<QProgressBar*>(w))    return 6; // Progress
    if (dynamic_cast<QComboBox*>(w))       return 7; // Combo
    if (dynamic_cast<QSpinBox*>(w))        return 8; // Spin
    if (dynamic_cast<QSlider*>(w))         return 9; // Slider
    if (dynamic_cast<QDial*>(w))           return 10; // Dial
    if (dynamic_cast<QGroupBox*>(w))       return 11; // GroupBox
    if (dynamic_cast<QTabWidget*>(w))      return 15; // Tabs
    if (dynamic_cast<QListWidget*>(w))     return 16; // List
    if (dynamic_cast<QMenuBar*>(w))        return 17; // MenuBar
    if (dynamic_cast<QMenu*>(w))           return 18; // Menu
    if (dynamic_cast<QToolBar*>(w))        return 19; // Toolbar
    if (dynamic_cast<QPlainTextEdit*>(w))  return 12; // TextEdit
    if (dynamic_cast<QTextEdit*>(w))       return 20; // Rich
    if (dynamic_cast<QRadioButton*>(w))    return 13; // Radio
#if defined(VYB_HAVE_QT_WEBENGINE)
    if (dynamic_cast<QWebEngineView*>(w))  return 14; // Web
#endif
    if (dynamic_cast<QLabel*>(w))          return 2; // Label
    return 1;                                        // Window (plain QWidget)
}

// ----------------------------------------------------------------------------
// Polled event queue
// ----------------------------------------------------------------------------

extern "C" VYB_WEAK int64_t __vyb_qt_event_count(void) {
    std::lock_guard<std::mutex> lk(g_events_mtx);
    return (int64_t)g_events.size();
}

extern "C" VYB_WEAK int64_t __vyb_qt_event_handle(void) {
    std::lock_guard<std::mutex> lk(g_events_mtx);
    return g_events.empty() ? 0 : g_events.front().handle;
}

extern "C" VYB_WEAK int64_t __vyb_qt_event_kind(void) {
    std::lock_guard<std::mutex> lk(g_events_mtx);
    return g_events.empty() ? 0 : g_events.front().kind;
}

// Result payload (0 default) of the oldest queued event, or of the event a
// qt_run dispatch is currently handing to the handler. For a finished dialog
// this is 1 (Yes/Accepted) or 0 (No/Rejected).
extern "C" VYB_WEAK int64_t __vyb_qt_event_result(void) {
    if (g_dispatching) return g_cur_result;
    std::lock_guard<std::mutex> lk(g_events_mtx);
    return g_events.empty() ? 0 : g_events.front().result;
}

extern "C" VYB_WEAK int64_t __vyb_qt_event_pop(void) {
    std::lock_guard<std::mutex> lk(g_events_mtx);
    if (g_events.empty()) return -1;
    g_events.pop_front();
    return 0;
}

// Block the calling (main) thread, pumping the Qt event loop, until a control
// event is queued (returns 1) or `timeout_ms` elapses (returns 0). A negative
// timeout waits until an event arrives. The event loop is *driven* here (a real
// QCoreApplication::processEvents pump), so real input/paint/timer signals are
// delivered while we wait and their queued records become visible. This is the
// UI scheduling primitive: GUI code stays on the main thread (Qt thread
// affinity), while the asyncs pool runs background/timer/IO fibers concurrently
// and their mutator calls enqueue records here that this function observes.
// Returns 1 if events are available (caller then drains with __vyb_qt_event_*),
// 0 on timeout, -1 if the GUI is not running.
extern "C" VYB_WEAK int64_t __vyb_qt_wait_event(int64_t timeout_ms) {
    if (!g_app) return -1;
    auto mono_ms = []() -> int64_t {
        return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    int64_t deadline = -1;
    if (timeout_ms > 0) deadline = mono_ms() + timeout_ms;
    for (;;) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        {
            std::lock_guard<std::mutex> lk(g_events_mtx);
            if (!g_events.empty()) return 1;
        }
        if (timeout_ms == 0) return 0;                 // single pump, no wait
        if (deadline >= 0 && mono_ms() >= deadline) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));  // cooperatively wait
    }
}

// ----------------------------------------------------------------------------
// Combo boxes (QComboBox)
// ----------------------------------------------------------------------------

// Create a read-only combo box as a child of `parent` (0 = none). Returns its
// Int handle, or 0 on failure. A current-index change enqueues a
// QtEvent::IndexChanged record.
extern "C" VYB_WEAK int64_t __vyb_qt_combo_create(int64_t parent) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QComboBox* c = new QComboBox(pw);
    QObject::connect(c, qOverload<int>(&QComboBox::currentIndexChanged),
        [c](int) { vyb_qt_enqueue(wetoh(c), VYB_QT_EVT_INDEXCHANGED); });
    return wetoh(c);
}

// Append `text` as the last combo item. Returns 0 on success, -1 on a bad combo
// handle.
extern "C" VYB_WEAK int64_t __vyb_qt_combo_add_item(int64_t h, const char* s, int64_t len) {
    QComboBox* c = dynamic_cast<QComboBox*>(htowed(h)); if (!c) return -1;
    c->addItem(qt_from_bytes(s, len));
    return 0;
}

// Number of combo items, or -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_combo_count(int64_t h) {
    QComboBox* c = dynamic_cast<QComboBox*>(htowed(h)); if (!c) return -1;
    return (int64_t)c->count();
}

// Index of the currently selected combo item (0-based), or -1 on a bad handle
// (or when nothing is selected).
extern "C" VYB_WEAK int64_t __vyb_qt_combo_current_index(int64_t h) {
    QComboBox* c = dynamic_cast<QComboBox*>(htowed(h)); if (!c) return -1;
    return (int64_t)c->currentIndex();
}

// Select combo item `idx` (0-based). Returns 0 on success, -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_combo_set_current_index(int64_t h, int64_t idx) {
    QComboBox* c = dynamic_cast<QComboBox*>(htowed(h)); if (!c) return -1;
    c->setCurrentIndex((int)idx);
    return 0;
}

// Text of combo item `idx` (String). Returns "" on a bad handle/index.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_combo_item_text(int64_t h, int64_t idx) {
    QComboBox* c = dynamic_cast<QComboBox*>(htowed(h)); if (!c) return { nullptr, 0 };
    if (idx < 0 || idx >= c->count()) return { nullptr, 0 };
    return qt_to_owned(c->itemText((int)idx));
}

// ----------------------------------------------------------------------------
// Spin boxes (QSpinBox)
// ----------------------------------------------------------------------------

// Create an integer spin box with range [min, max] as a child of `parent`.
// Returns its Int handle, or 0 on failure. A value change enqueues a
// QtEvent::ValueChanged record.
extern "C" VYB_WEAK int64_t __vyb_qt_spin_create(int64_t parent, int64_t minv, int64_t maxv) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QSpinBox* s = new QSpinBox(pw);
    s->setRange((int)minv, (int)maxv);
    QObject::connect(s, qOverload<int>(&QSpinBox::valueChanged),
        [s](int) { vyb_qt_enqueue(wetoh(s), VYB_QT_EVT_VALUECHANGED); });
    return wetoh(s);
}

// Current spin box value, or 0 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_spin_value(int64_t h) {
    QSpinBox* s = dynamic_cast<QSpinBox*>(htowed(h)); if (!s) return 0;
    return (int64_t)s->value();
}

// Set the spin box value (clamped to its range). Returns 0 on success.
extern "C" VYB_WEAK int64_t __vyb_qt_spin_set_value(int64_t h, int64_t v) {
    QSpinBox* s = dynamic_cast<QSpinBox*>(htowed(h)); if (!s) return -1;
    s->setValue((int)v);
    return 0;
}

// ----------------------------------------------------------------------------
// Sliders (QSlider)
// ----------------------------------------------------------------------------

// Create a horizontal slider with range [min, max] as a child of `parent`.
// Returns its Int handle, or 0 on failure. A value change enqueues a
// QtEvent::ValueChanged record.
extern "C" VYB_WEAK int64_t __vyb_qt_slider_create(int64_t parent, int64_t minv, int64_t maxv) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QSlider* sl = new QSlider(Qt::Horizontal, pw);
    sl->setRange((int)minv, (int)maxv);
    QObject::connect(sl, &QSlider::valueChanged,
        [sl](int) { vyb_qt_enqueue(wetoh(sl), VYB_QT_EVT_VALUECHANGED); });
    return wetoh(sl);
}

// Current slider value, or 0 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_slider_value(int64_t h) {
    QSlider* sl = dynamic_cast<QSlider*>(htowed(h)); if (!sl) return 0;
    return (int64_t)sl->value();
}

// Set the slider value (clamped to its range). Returns 0 on success.
extern "C" VYB_WEAK int64_t __vyb_qt_slider_set_value(int64_t h, int64_t v) {
    QSlider* sl = dynamic_cast<QSlider*>(htowed(h)); if (!sl) return -1;
    sl->setValue((int)v);
    return 0;
}

// ----------------------------------------------------------------------------
// Dials (QDial)
// ----------------------------------------------------------------------------

// Create a dial with range [min, max] as a child of `parent`. Returns its Int
// handle, or 0 on failure. A value change enqueues a QtEvent::ValueChanged
// record.
extern "C" VYB_WEAK int64_t __vyb_qt_dial_create(int64_t parent, int64_t minv, int64_t maxv) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QDial* d = new QDial(pw);
    d->setRange((int)minv, (int)maxv);
    QObject::connect(d, &QDial::valueChanged,
        [d](int) { vyb_qt_enqueue(wetoh(d), VYB_QT_EVT_VALUECHANGED); });
    return wetoh(d);
}

// Current dial value, or 0 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_dial_value(int64_t h) {
    QDial* d = dynamic_cast<QDial*>(htowed(h)); if (!d) return 0;
    return (int64_t)d->value();
}

// Set the dial value (clamped to its range). Returns 0 on success.
extern "C" VYB_WEAK int64_t __vyb_qt_dial_set_value(int64_t h, int64_t v) {
    QDial* d = dynamic_cast<QDial*>(htowed(h)); if (!d) return -1;
    d->setValue((int)v);
    return 0;
}

// ----------------------------------------------------------------------------
// Grid layout (QGridLayout)
// ----------------------------------------------------------------------------

// Create a grid layout on window `parent` (0 = none). Returns its Int handle.
extern "C" VYB_WEAK int64_t __vyb_qt_grid(int64_t parent) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    return reinterpret_cast<int64_t>(new QGridLayout(pw));
}

// Add widget `child` into grid-layout `layout` at (row, col). Returns 0 on
// success, -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_grid_add(int64_t layout, int64_t child, int64_t row, int64_t col) {
    QLayout* l = htolay(layout); if (!l) return -1;
    QWidget* c = htowed(child); if (!c) return -1;
    QGridLayout* gl = dynamic_cast<QGridLayout*>(l); if (!gl) return -1;
    gl->addWidget(c, (int)row, (int)col);
    return 0;
}

// ----------------------------------------------------------------------------
// Group boxes (QGroupBox)
// ----------------------------------------------------------------------------

// Create a titled group box as a child of `parent` (0 = none). Returns its Int
// handle, or 0 on failure. The group is a plain container; put a layout on it
// and add widgets to that layout.
extern "C" VYB_WEAK int64_t __vyb_qt_group_create(int64_t parent, const char* s, int64_t len) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QGroupBox* g = new QGroupBox(qt_from_bytes(s, len), pw);
    return wetoh(g);
}

// Set group-box title text. Returns 0, or -1 on a non-group-box handle.
extern "C" VYB_WEAK int64_t __vyb_qt_group_set_title(int64_t h, const char* s, int64_t len) {
    QGroupBox* g = dynamic_cast<QGroupBox*>(htowed(h)); if (!g) return -1;
    g->setTitle(qt_from_bytes(s, len));
    return 0;
}

// ----------------------------------------------------------------------------
// Multi-line text editor (QPlainTextEdit)
// ----------------------------------------------------------------------------

// Create a multi-line plain-text editor as a child of `parent` (0 = none).
// Returns its Int handle, or 0 on failure. A text change enqueues a
// QtEvent::TextChanged record.
extern "C" VYB_WEAK int64_t __vyb_qt_text_edit_create(int64_t parent) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QPlainTextEdit* e = new QPlainTextEdit(pw);
    QObject::connect(e, &QPlainTextEdit::textChanged,
        [e]() { vyb_qt_enqueue(wetoh(e), VYB_QT_EVT_TEXTCHANGED); });
    return wetoh(e);
}

// Current editor text (String); "" on a bad handle.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_text_edit_text(int64_t h) {
    QPlainTextEdit* e = dynamic_cast<QPlainTextEdit*>(htowed(h)); if (!e) return { nullptr, 0 };
    return qt_to_owned(e->toPlainText());
}

// Replace editor text. Returns 0 on success, -1 on a non-editor handle.
extern "C" VYB_WEAK int64_t __vyb_qt_text_edit_set_text(int64_t h, const char* s, int64_t len) {
    QPlainTextEdit* e = dynamic_cast<QPlainTextEdit*>(htowed(h)); if (!e) return -1;
    e->setPlainText(qt_from_bytes(s, len));
    return 0;
}

// ----------------------------------------------------------------------------
// Radio buttons (QRadioButton)
// ----------------------------------------------------------------------------

// Create a radio button as a child of `parent` (0 = none). Returns its Int
// handle, or 0 on failure. A check-state change enqueues a QtEvent::Toggled
// record; radios in the same parent are exclusive by default.
extern "C" VYB_WEAK int64_t __vyb_qt_radio_create(int64_t parent, const char* s, int64_t len) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QRadioButton* r = new QRadioButton(qt_from_bytes(s, len), pw);
    QObject::connect(r, &QRadioButton::toggled,
        [r](bool) { vyb_qt_enqueue(wetoh(r), VYB_QT_EVT_TOGGLED); });
    return wetoh(r);
}

// true if the radio button is checked, else false.
extern "C" VYB_WEAK int64_t __vyb_qt_radio_checked(int64_t h) {
    QRadioButton* r = dynamic_cast<QRadioButton*>(htowed(h)); if (!r) return -1;
    return r->isChecked() ? 1 : 0;
}

// Check (true) / uncheck (false) the radio (enqueues QtEvent::Toggled).
// Returns 0, or -1 on a non-radio handle.
extern "C" VYB_WEAK int64_t __vyb_qt_radio_set_checked(int64_t h, int64_t on) {
    QRadioButton* r = dynamic_cast<QRadioButton*>(htowed(h)); if (!r) return -1;
    r->setChecked(on ? true : false);
    return 0;
}

// ----------------------------------------------------------------------------
// Generic widget enable / visibility
// ----------------------------------------------------------------------------

// Enable (true) / disable (false) any widget. Returns 0, or -1 on a non-widget
// handle.
extern "C" VYB_WEAK int64_t __vyb_qt_widget_set_enabled(int64_t h, int64_t on) {
    QWidget* w = htowed(h); if (!w) return -1;
    w->setEnabled(on ? true : false);
    return 0;
}

// true while widget `h` is enabled, else false.
extern "C" VYB_WEAK int64_t __vyb_qt_widget_enabled(int64_t h) {
    QWidget* w = htowed(h); if (!w) return 0;
    return w->isEnabled() ? 1 : 0;
}

// ----------------------------------------------------------------------------
// Generic widget visibility (QWidget)
// ----------------------------------------------------------------------------

// Show (true) / hide (false) any widget. Returns 0, or -1 on a non-widget handle.
extern "C" VYB_WEAK int64_t __vyb_qt_widget_set_visible(int64_t h, int64_t on) {
    QWidget* w = htowed(h); if (!w) return -1;
    w->setVisible(on ? true : false);
    return 0;
}

// true while widget `h` is visible, else false.
extern "C" VYB_WEAK int64_t __vyb_qt_widget_visible(int64_t h) {
    QWidget* w = htowed(h); if (!w) return 0;
    return w->isVisible() ? 1 : 0;
}

// ----------------------------------------------------------------------------
// Tab widget (QTabWidget)
// ----------------------------------------------------------------------------

// Create a tab container as a child of `parent` (0 = none). Returns its Int
// handle, or 0 on failure. A tab change enqueues a QtEvent::CurrentChanged
// record.
extern "C" VYB_WEAK int64_t __vyb_qt_tabs_create(int64_t parent) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QTabWidget* t = new QTabWidget(pw);
    QObject::connect(t, &QTabWidget::currentChanged,
        [t](int) { vyb_qt_enqueue(wetoh(t), VYB_QT_EVT_CURRENTCHANGED); });
    return wetoh(t);
}

// Append a tab titled `text`; returns the page widget's handle (0 if no tab
// widget / no GUI). Put a layout on the returned page and add widgets to it.
extern "C" VYB_WEAK int64_t __vyb_qt_tabs_add(int64_t tabs, const char* s, int64_t len) {
    QTabWidget* t = dynamic_cast<QTabWidget*>(htowed(tabs)); if (!t) return 0;
    QWidget* page = new QWidget(t);
    t->addTab(page, qt_from_bytes(s, len));
    return wetoh(page);
}

// Tab count, or -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_tabs_count(int64_t h) {
    QTabWidget* t = dynamic_cast<QTabWidget*>(htowed(h)); if (!t) return -1;
    return (int64_t)t->count();
}

// Current (0-based) tab index, or -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_tabs_current(int64_t h) {
    QTabWidget* t = dynamic_cast<QTabWidget*>(htowed(h)); if (!t) return -1;
    return (int64_t)t->currentIndex();
}

// Select tab `idx` (enqueues QtEvent::CurrentChanged). Returns 0.
extern "C" VYB_WEAK int64_t __vyb_qt_tabs_set_current(int64_t h, int64_t idx) {
    QTabWidget* t = dynamic_cast<QTabWidget*>(htowed(h)); if (!t) return -1;
    t->setCurrentIndex((int)idx);
    return 0;
}

// ----------------------------------------------------------------------------
// List widget (QListWidget)
// ----------------------------------------------------------------------------

// Create an item list as a child of `parent` (0 = none). Returns its Int handle,
// or 0 on failure. A selection change enqueues a QtEvent::CurrentChanged record.
extern "C" VYB_WEAK int64_t __vyb_qt_list_create(int64_t parent) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QListWidget* l = new QListWidget(pw);
    QObject::connect(l, &QListWidget::currentRowChanged,
        [l](int) { vyb_qt_enqueue(wetoh(l), VYB_QT_EVT_CURRENTCHANGED); });
    return wetoh(l);
}

// Append `text` as the last list item. Returns 0, or -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_list_add(int64_t h, const char* s, int64_t len) {
    QListWidget* l = dynamic_cast<QListWidget*>(htowed(h)); if (!l) return -1;
    l->addItem(qt_from_bytes(s, len));
    return 0;
}

// List item count, or -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_list_count(int64_t h) {
    QListWidget* l = dynamic_cast<QListWidget*>(htowed(h)); if (!l) return -1;
    return (int64_t)l->count();
}

// Current (0-based) list index, or -1 when none selected / bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_list_current(int64_t h) {
    QListWidget* l = dynamic_cast<QListWidget*>(htowed(h)); if (!l) return -1;
    return (int64_t)l->currentRow();
}

// Select list item `idx`. Returns 0, or -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_list_set_current(int64_t h, int64_t idx) {
    QListWidget* l = dynamic_cast<QListWidget*>(htowed(h)); if (!l) return -1;
    l->setCurrentRow((int)idx);
    return 0;
}

// Text of list item `idx` (String); "" on a bad handle/index.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_list_item_text(int64_t h, int64_t idx) {
    QListWidget* l = dynamic_cast<QListWidget*>(htowed(h)); if (!l) return { nullptr, 0 };
    if (idx < 0 || idx >= l->count()) return { nullptr, 0 };
    QListWidgetItem* item = l->item((int)idx);
    if (!item) return { nullptr, 0 };
    return qt_to_owned(item->text());
}

// ----------------------------------------------------------------------------
// Main-window chrome (QMainWindow): menubar, menus, actions, statusbar, toolbar
// ----------------------------------------------------------------------------
// A QMainWindow behaves like a plain window (qt_window_* apply) but also owns a
// menubar and status bar. QAction handles are opaque and NOT QWidgets — their
// triggered signal enqueues a Click record whose handle is the action handle, so
// a qt_on_event handler can match it; avoid passing an action handle to widget
// APIs.
extern "C" VYB_WEAK int64_t __vyb_qt_main_window_create(void) {
    if (!g_app) return 0;
    return reinterpret_cast<int64_t>(new QMainWindow());
}

// The main window's menu bar (created on demand). Returns its handle, or 0 on a
// bad / non-QMainWindow handle.
extern "C" VYB_WEAK int64_t __vyb_qt_menubar(int64_t h) {
    QMainWindow* mw = dynamic_cast<QMainWindow*>(htowed(h)); if (!mw) return 0;
    return reinterpret_cast<int64_t>(mw->menuBar());
}

// Add a top-level menu titled `title` to the menu bar. Returns its menu handle,
// or 0 on a bad / non-QMainWindow handle.
extern "C" VYB_WEAK int64_t __vyb_qt_menu_add(int64_t h, const char* s, int64_t len) {
    QMainWindow* mw = dynamic_cast<QMainWindow*>(htowed(h)); if (!mw) return 0;
    return reinterpret_cast<int64_t>(mw->menuBar()->addMenu(qt_from_bytes(s, len)));
}

// Add an action to `menu`; its trigger enqueues QtEvent::Click with the action
// handle. Returns the action handle, or 0 on a bad / non-QMenu handle.
extern "C" VYB_WEAK int64_t __vyb_qt_action_add(int64_t h, const char* s, int64_t len) {
    QMenu* m = dynamic_cast<QMenu*>(htowed(h)); if (!m) return 0;
    QAction* a = m->addAction(qt_from_bytes(s, len));
    QObject::connect(a, &QAction::triggered,
        [a](bool) { vyb_qt_enqueue(reinterpret_cast<int64_t>(a), VYB_QT_EVT_CLICK); });
    return reinterpret_cast<int64_t>(a);
}

// Number of actions in `menu`, or -1 on a bad / non-QMenu handle.
extern "C" VYB_WEAK int64_t __vyb_qt_action_count(int64_t h) {
    QMenu* m = dynamic_cast<QMenu*>(htowed(h)); if (!m) return -1;
    return (int64_t)m->actions().size();
}

// Show `text` in the main window's status bar. Returns 0, or -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_statusbar_message(int64_t h, const char* s, int64_t len) {
    QMainWindow* mw = dynamic_cast<QMainWindow*>(htowed(h)); if (!mw) return -1;
    mw->statusBar()->showMessage(qt_from_bytes(s, len));
    return 0;
}

// Current status-bar message (String); "" on a bad handle.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_statusbar_text(int64_t h) {
    QMainWindow* mw = dynamic_cast<QMainWindow*>(htowed(h)); if (!mw) return { nullptr, 0 };
    return qt_to_owned(mw->statusBar()->currentMessage());
}

// Add a toolbar titled `title` to the main window. Returns its handle, or 0.
extern "C" VYB_WEAK int64_t __vyb_qt_toolbar_create(int64_t h, const char* s, int64_t len) {
    QMainWindow* mw = dynamic_cast<QMainWindow*>(htowed(h)); if (!mw) return 0;
    return reinterpret_cast<int64_t>(mw->addToolBar(qt_from_bytes(s, len)));
}

// ----------------------------------------------------------------------------
// Modal dialogs (QMessageBox / QFileDialog)
// ----------------------------------------------------------------------------
// These block on the main thread for user input, exactly like a real GUI app.
// A modal dialog's exec() runs a private nested event loop; under the offscreen
// QPA platform there is no user to click, so the bridge honors an opt-in env
// var VYB_QT_DIALOG_AUTO=1 that auto-answers the dialog (Ok / Yes / the accept
// path) once it is shown, keeping CI/GUI tests deterministic. Production use is
// unchanged: with the var unset the box blocks until the user responds.

static bool vyb_qt_dialog_auto() {
    const char* v = std::getenv("VYB_QT_DIALOG_AUTO");
    return v && *v;
}

// Shared modal message-box driver. `question` selects Yes/No (returns 1 for
// Yes, 0 for No/dismiss); the rest use Ok and return 0.
static int64_t vyb_qt_message_box(QWidget* parent, const char* title, int64_t tlen,
                                  const char* text, int64_t nlen, QMessageBox::Icon icon,
                                  bool question) {
    if (!g_app) return -1;
    QMessageBox box(icon, qt_from_bytes(title, tlen), qt_from_bytes(text, nlen),
                    QMessageBox::NoButton, parent);
    QMessageBox::StandardButtons btns = question ? (QMessageBox::Yes | QMessageBox::No)
                                                 : QMessageBox::Ok;
    QMessageBox::StandardButton dflt = question ? QMessageBox::Yes : QMessageBox::Ok;
    box.setStandardButtons(btns);
    box.setDefaultButton(dflt);
    if (vyb_qt_dialog_auto())
        QTimer::singleShot(0, &box, [&box, dflt]() { box.done(dflt); });
    QMessageBox::StandardButton r = (QMessageBox::StandardButton)box.exec();
    qApp->processEvents();                             // drain the auto-accept pump
    if (question) return (r == QMessageBox::Yes) ? 1 : 0;
    return 0;
}

extern "C" VYB_WEAK int64_t __vyb_qt_msg_info(int64_t parent, const char* title, int64_t tlen,
                                              const char* text, int64_t nlen) {
    return vyb_qt_message_box(parent ? htowed(parent) : nullptr, title, tlen, text, nlen,
                              QMessageBox::Information, false);
}

extern "C" VYB_WEAK int64_t __vyb_qt_msg_warn(int64_t parent, const char* title, int64_t tlen,
                                              const char* text, int64_t nlen) {
    return vyb_qt_message_box(parent ? htowed(parent) : nullptr, title, tlen, text, nlen,
                              QMessageBox::Warning, false);
}

extern "C" VYB_WEAK int64_t __vyb_qt_msg_error(int64_t parent, const char* title, int64_t tlen,
                                               const char* text, int64_t nlen) {
    return vyb_qt_message_box(parent ? htowed(parent) : nullptr, title, tlen, text, nlen,
                              QMessageBox::Critical, false);
}

extern "C" VYB_WEAK int64_t __vyb_qt_msg_about(int64_t parent, const char* title, int64_t tlen,
                                               const char* text, int64_t nlen) {
    return vyb_qt_message_box(parent ? htowed(parent) : nullptr, title, tlen, text, nlen,
                              QMessageBox::Information, false);
}

extern "C" VYB_WEAK int64_t __vyb_qt_msg_question(int64_t parent, const char* title, int64_t tlen,
                                                  const char* text, int64_t nlen) {
    return vyb_qt_message_box(parent ? htowed(parent) : nullptr, title, tlen, text, nlen,
                              QMessageBox::Question, true);
}

// Shared native file-dialog driver. `mode` 0=open, 1=save, 2=directory.
static vyb_qt_str vyb_qt_file_dialog(int64_t parent, const char* title, int64_t tlen,
                                     const char* filter, int64_t flen, int mode) {
    if (!g_app) return { nullptr, 0 };
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QFileDialog dlg(pw, qt_from_bytes(title, tlen), QString(), qt_from_bytes(filter, flen));
    if (mode == 0) {
        dlg.setAcceptMode(QFileDialog::AcceptOpen);
        dlg.setFileMode(QFileDialog::ExistingFile);
    } else if (mode == 1) {
        dlg.setAcceptMode(QFileDialog::AcceptSave);
    } else {
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
    }
    if (vyb_qt_dialog_auto())
        QTimer::singleShot(0, &dlg, [&dlg]() { dlg.QDialog::done(QDialog::Accepted); });
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty())
        return qt_to_owned(dlg.selectedFiles().first());
    return qt_to_owned(QString());
}

extern "C" VYB_WEAK vyb_qt_str __vyb_qt_file_open(int64_t parent, const char* title, int64_t tlen,
                                                  const char* filter, int64_t flen) {
    return vyb_qt_file_dialog(parent, title, tlen, filter, flen, 0);
}

extern "C" VYB_WEAK vyb_qt_str __vyb_qt_file_save(int64_t parent, const char* title, int64_t tlen,
                                                  const char* filter, int64_t flen) {
    return vyb_qt_file_dialog(parent, title, tlen, filter, flen, 1);
}

extern "C" VYB_WEAK vyb_qt_str __vyb_qt_dir_select(int64_t parent, const char* title, int64_t tlen) {
    return vyb_qt_file_dialog(parent, title, tlen, nullptr, 0, 2);
}

// ----------------------------------------------------------------------------
// Async (non-blocking) dialogs
// ----------------------------------------------------------------------------
// Unlike the blocking qt_msg_*/qt_file_* tier, these pair with the event loop:
// create + show the dialog and return its Int handle immediately; when the
// user finishes it, Qt emits finished() and the bridge enqueues a
// QtEvent::dialog record whose result payload (qt_event_result: 1 for
// Yes/Accepted, else 0) is delivered to the qt_on_event handler or the
// qt_event_* poll. File/dir pickers also record the chosen path (a Qt String is
// not ABI-safe across the poll queue), read back via qt_dlg_selected(handle).
// Dialogs are parented to their window and self-delete on close, so a handle is
// only valid while the dialog is open (matching opaque-handle semantics).

static void vyb_qt_auto_answer(QWidget* w, bool accept) {
    if (!vyb_qt_dialog_auto() || !w) return;
    if (QMessageBox* mb = dynamic_cast<QMessageBox*>(w)) {
        QTimer::singleShot(0, mb, [mb]() {
            QPushButton* def = mb->defaultButton();
            if (def) def->click();
            else mb->QDialog::accept();
        });
        return;
    }
    QDialog* d = dynamic_cast<QDialog*>(w);
    if (!d) return;
    QTimer::singleShot(0, d, [d, accept]() {
        if (accept) d->QDialog::accept();
        else d->reject();
    });
}

// Shared async message-box driver: builds + shows a box returning its handle.
// `question` maps the finished StandardButton to 1 (Yes) / 0.
static int64_t vyb_qt_dlg_message(int64_t parent, const char* title, int64_t tlen,
                                  const char* text, int64_t nlen, QMessageBox::Icon icon,
                                  bool question) {
    if (!g_app) return 0;
    QMessageBox* box = new QMessageBox(icon, qt_from_bytes(title, tlen), qt_from_bytes(text, nlen),
                                       QMessageBox::NoButton, parent ? htowed(parent) : nullptr);
    box->setStandardButtons(question ? (QMessageBox::Yes | QMessageBox::No) : QMessageBox::Ok);
    box->setDefaultButton(question ? QMessageBox::Yes : QMessageBox::Ok);
    box->setAttribute(Qt::WA_DeleteOnClose);
    int64_t handle = wetoh(box);
    QObject::connect(box, &QMessageBox::finished, [handle, question](int result) {
        int64_t r = 0;
        if (question) r = (result == (int)QMessageBox::Yes) ? 1 : 0;
        vyb_qt_enqueue(handle, VYB_QT_EVT_DIALOG, r);
    });
    box->show();
    vyb_qt_auto_answer(box, true);   // tests: auto-pick the default button
    return handle;
}

// Shared async file/dir dialog driver. `mode` 0=open, 1=save, 2=directory.
static int64_t vyb_qt_dlg_file(int64_t parent, const char* title, int64_t tlen,
                               const char* filter, int64_t flen, int mode) {
    if (!g_app) return 0;
    QFileDialog* dlg = new QFileDialog(parent ? htowed(parent) : nullptr,
                                       qt_from_bytes(title, tlen), QString(), qt_from_bytes(filter, flen));
    if (mode == 0) {
        dlg->setAcceptMode(QFileDialog::AcceptOpen);
        dlg->setFileMode(QFileDialog::ExistingFile);
    } else if (mode == 1) {
        dlg->setAcceptMode(QFileDialog::AcceptSave);
    } else {
        dlg->setFileMode(QFileDialog::Directory);
        dlg->setOption(QFileDialog::ShowDirsOnly, true);
    }
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    int64_t handle = wetoh(dlg);
    QObject::connect(dlg, &QFileDialog::finished, [handle, dlg](int result) {
        int64_t r = (result == QDialog::Accepted) ? 1 : 0;
        if (r) {
            QString path = dlg->selectedFiles().isEmpty() ? QString() : dlg->selectedFiles().first();
            std::lock_guard<std::mutex> lk(g_dlg_paths_mtx);
            g_dlg_paths[handle] = path;
        }
        vyb_qt_enqueue(handle, VYB_QT_EVT_DIALOG, r);
    });
    dlg->show();
    vyb_qt_auto_answer(dlg, true);   // tests: auto-accept
    return handle;
}

extern "C" VYB_WEAK int64_t __vyb_qt_dlg_info(int64_t parent, const char* title, int64_t tlen,
                                              const char* text, int64_t nlen) {
    return vyb_qt_dlg_message(parent, title, tlen, text, nlen, QMessageBox::Information, false);
}

extern "C" VYB_WEAK int64_t __vyb_qt_dlg_warn(int64_t parent, const char* title, int64_t tlen,
                                              const char* text, int64_t nlen) {
    return vyb_qt_dlg_message(parent, title, tlen, text, nlen, QMessageBox::Warning, false);
}

extern "C" VYB_WEAK int64_t __vyb_qt_dlg_error(int64_t parent, const char* title, int64_t tlen,
                                               const char* text, int64_t nlen) {
    return vyb_qt_dlg_message(parent, title, tlen, text, nlen, QMessageBox::Critical, false);
}

extern "C" VYB_WEAK int64_t __vyb_qt_dlg_about(int64_t parent, const char* title, int64_t tlen,
                                               const char* text, int64_t nlen) {
    return vyb_qt_dlg_message(parent, title, tlen, text, nlen, QMessageBox::Information, false);
}

extern "C" VYB_WEAK int64_t __vyb_qt_dlg_question(int64_t parent, const char* title, int64_t tlen,
                                                  const char* text, int64_t nlen) {
    return vyb_qt_dlg_message(parent, title, tlen, text, nlen, QMessageBox::Question, true);
}

extern "C" VYB_WEAK int64_t __vyb_qt_dlg_open(int64_t parent, const char* title, int64_t tlen,
                                              const char* filter, int64_t flen) {
    return vyb_qt_dlg_file(parent, title, tlen, filter, flen, 0);
}

extern "C" VYB_WEAK int64_t __vyb_qt_dlg_save(int64_t parent, const char* title, int64_t tlen,
                                              const char* filter, int64_t flen) {
    return vyb_qt_dlg_file(parent, title, tlen, filter, flen, 1);
}

extern "C" VYB_WEAK int64_t __vyb_qt_dlg_dir(int64_t parent, const char* title, int64_t tlen) {
    return vyb_qt_dlg_file(parent, title, tlen, nullptr, 0, 2);
}

// Finish dialog `h` as rejected (enqueues QtEvent::dialog with result 0).
// Returns 0, or -1 on a bad / non-QDialog handle.
extern "C" VYB_WEAK int64_t __vyb_qt_dlg_close(int64_t h) {
    QDialog* d = dynamic_cast<QDialog*>(htowed(h)); if (!d) return -1;
    d->reject();
    return 0;
}

// Path chosen by a finished file/dir dialog `h` (String); "" if none.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_dlg_selected(int64_t h) {
    std::lock_guard<std::mutex> lk(g_dlg_paths_mtx);
    auto it = g_dlg_paths.find(h);
    if (it == g_dlg_paths.end()) return { nullptr, 0 };
    return qt_to_owned(it->second);
}

// ----------------------------------------------------------------------------
// Rich-text editor (QTextEdit) and font/color helpers
// ----------------------------------------------------------------------------

// Create a rich-text editor as a child of `parent` (0 = none). Returns its Int
// handle, or 0 on failure. A text change enqueues a QtEvent::TextChanged record.
extern "C" VYB_WEAK int64_t __vyb_qt_rich_create(int64_t parent) {
    if (!g_app) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QTextEdit* e = new QTextEdit(pw);
    QObject::connect(e, &QTextEdit::textChanged,
        [e]() { vyb_qt_enqueue(wetoh(e), VYB_QT_EVT_TEXTCHANGED); });
    return wetoh(e);
}

// Set the rich-text body from HTML (supports <b>/<i>/<u>/<font color> etc).
extern "C" VYB_WEAK int64_t __vyb_qt_rich_set_html(int64_t h, const char* s, int64_t len) {
    QTextEdit* e = dynamic_cast<QTextEdit*>(htowed(h)); if (!e) return -1;
    e->setHtml(qt_from_bytes(s, len));
    return 0;
}

// Current rich-text body as HTML (String); "" on a bad handle.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_rich_html(int64_t h) {
    QTextEdit* e = dynamic_cast<QTextEdit*>(htowed(h)); if (!e) return { nullptr, 0 };
    return qt_to_owned(e->toHtml());
}

// Replace the rich-text editor's content with plain text (clears formatting).
extern "C" VYB_WEAK int64_t __vyb_qt_rich_set_plain(int64_t h, const char* s, int64_t len) {
    QTextEdit* e = dynamic_cast<QTextEdit*>(htowed(h)); if (!e) return -1;
    e->setPlainText(qt_from_bytes(s, len));
    return 0;
}

// Current plain text (String); "" on a bad handle.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_rich_plain(int64_t h) {
    QTextEdit* e = dynamic_cast<QTextEdit*>(htowed(h)); if (!e) return { nullptr, 0 };
    return qt_to_owned(e->toPlainText());
}

// Append `text` at the end, keeping the current character format.
extern "C" VYB_WEAK int64_t __vyb_qt_rich_append(int64_t h, const char* s, int64_t len) {
    QTextEdit* e = dynamic_cast<QTextEdit*>(htowed(h)); if (!e) return -1;
    e->append(qt_from_bytes(s, len));
    return 0;
}

// Clear all rich-text content.
extern "C" VYB_WEAK int64_t __vyb_qt_rich_clear(int64_t h) {
    QTextEdit* e = dynamic_cast<QTextEdit*>(htowed(h)); if (!e) return -1;
    e->clear();
    return 0;
}

// Set the editor's text color from (r,g,b) 0-255 (applies to the entire body).
extern "C" VYB_WEAK int64_t __vyb_qt_rich_set_text_color(int64_t h, int64_t r, int64_t g, int64_t b) {
    QTextEdit* e = dynamic_cast<QTextEdit*>(htowed(h)); if (!e) return -1;
    QTextCharFormat cf = e->currentCharFormat();
    cf.setForeground(QBrush(QColor((int)(r & 0xff), (int)(g & 0xff), (int)(b & 0xff))));
    e->setCurrentCharFormat(cf);
    e->selectAll();
    QTextCursor cur = e->textCursor();
    cur.mergeCharFormat(cf);
    e->setTextCursor(cur);
    e->moveCursor(QTextCursor::Start);
    return 0;
}

// Set a widget's font point size (any QWidget).
extern "C" VYB_WEAK int64_t __vyb_qt_widget_set_font_size(int64_t h, int64_t pt) {
    QWidget* w = htowed(h); if (!w) return -1;
    QFont f = w->font();
    f.setPointSize((int)pt);
    w->setFont(f);
    return 0;
}

// Toggle a widget's font bold (on != 0).
extern "C" VYB_WEAK int64_t __vyb_qt_widget_set_font_bold(int64_t h, int64_t on) {
    QWidget* w = htowed(h); if (!w) return -1;
    QFont f = w->font();
    f.setBold(on ? true : false);
    w->setFont(f);
    return 0;
}

// Set a widget's foreground text color via its palette (r,g,b 0-255). Works on
// any QWidget that paints text from WindowText (QLabel, QLineEdit, buttons...).
extern "C" VYB_WEAK int64_t __vyb_qt_widget_set_text_color(int64_t h, int64_t r, int64_t g, int64_t b) {
    QWidget* w = htowed(h); if (!w) return -1;
    QPalette pal = w->palette();
    pal.setColor(w->foregroundRole(), QColor((int)(r & 0xff), (int)(g & 0xff), (int)(b & 0xff)));
    w->setPalette(pal);
    return 0;
}
