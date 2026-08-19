// ============================================================================
// QT (Qt5 WebEngine) - extern "C" bridge for the optional QWebEngineView surface.
//
// Like the core Qt bridge, Vyb sees only Int handles (qintptr-sized), Bool/Int
// status flags, and String text; the QWebEngineView object model and its async
// page lifecycle stay here. Loading a URL is asynchronous: WebEngine drives its
// own background/GPU/network processes, and back/forward/reload/title/URL reads
// reflect the page state whenever the Vyb program polls.
//
// Signal surface: loadFinished / titleChanged / loadProgress are connected to
// enqueue a (handle, kind) record into the same polled queue the core bridge
// owns (via __vyb_qt_event_enqueue_widget), so qt_run()'s handler or the
// qt_event_* poll drains them like any other control event.
//
// This file is compiled ONLY when QtWebEngine is found (VYB_HAVE_QT_WEBENGINE);
// otherwise runtime/vyb_qt_stub.cpp supplies __vyb_qt_web_* stubs that report
// the surface as unavailable (qt_web_create == 0).
// ============================================================================
#include <QWebEngineView>
#include <QUrl>
#include <QString>
#include <QByteArray>
#include <QApplication>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <climits>

#if defined(__GNUC__) || defined(__clang__)
#define VYB_WEAK __attribute__((weak))
#else
#define VYB_WEAK
#endif

// A Vyb String handed across the boundary is the { char*, int64_t } struct with
// a registry-registered, owned buffer (adopted by the Vyb String). Same
// convention as the core bridge.
struct vyb_qt_str { char* ptr; int64_t len; };

extern "C" void __vyb_string_register(void* p);
// The core Qt bridge's shared event queue (defined in vyb_qt_bridge.cpp).
extern "C" int64_t __vyb_qt_event_enqueue_widget(int64_t handle, int64_t kind);

// QtEvent::LoadFinished / TitleChanged / LoadProgress (must match the QtEvent
// enum emitted into stdlib/qt/mod.vyb).
#define VYB_QT_EVT_LOADFINISHED 6
#define VYB_QT_EVT_TITLECHANGED 7
#define VYB_QT_EVT_LOADPROGRESS 8

static QWidget* htowed(int64_t h) { return reinterpret_cast<QWidget*>(h); }
static int64_t wetoh(QWidget* w) { return reinterpret_cast<int64_t>(w); }
static QWebEngineView* htowe(int64_t h) { return dynamic_cast<QWebEngineView*>(htowed(h)); }

// Decode a { ptr, len } byte buffer as UTF-8 into a QString.
static QString qt_from_bytes(const char* ptr, int64_t len) {
    if (!ptr || len <= 0) return QString();
    if (len > (int64_t)INT_MAX) len = INT_MAX;
    return QString::fromUtf8(ptr, (int)len);
}

// Export a QString as an owned, registry-registered UTF-8 buffer for a Vyb
// String. The caller adopts the single reference and frees on GC.
static vyb_qt_str qt_to_owned(const QString& s) {
    vyb_qt_str r = { nullptr, 0 };
    QByteArray ba = s.toUtf8();
    if (ba.isEmpty()) return r;
    char* copy = (char*)std::malloc((size_t)ba.size() + 1);
    if (!copy) return r;
    std::memcpy(copy, ba.constData(), (size_t)ba.size());
    copy[ba.size()] = '\0';
    __vyb_string_register(copy);
    r.ptr = copy;
    r.len = (int64_t)ba.size();
    return r;
}

// ----------------------------------------------------------------------------
// QWebEngineView
// ----------------------------------------------------------------------------

// Create a QWebEngineView as a child of `parent` (0 = none). Returns its Int
// handle, or 0 if the GUI is not running / WebEngine cannot be created.
extern "C" VYB_WEAK int64_t __vyb_qt_web_create(int64_t parent) {
    if (!QApplication::instance()) return 0;
    QWidget* pw = parent ? htowed(parent) : nullptr;
    QWebEngineView* v = new QWebEngineView(pw);
    QObject::connect(v, &QWebEngineView::loadFinished,
        [v](bool) { __vyb_qt_event_enqueue_widget(wetoh(v), VYB_QT_EVT_LOADFINISHED); });
    QObject::connect(v, &QWebEngineView::titleChanged,
        [v](const QString&) { __vyb_qt_event_enqueue_widget(wetoh(v), VYB_QT_EVT_TITLECHANGED); });
    QObject::connect(v, &QWebEngineView::loadProgress,
        [v](int) { __vyb_qt_event_enqueue_widget(wetoh(v), VYB_QT_EVT_LOADPROGRESS); });
    return wetoh(v);
}

// Begin loading `url` in the web view (asynchronous). Returns 0, or -1 on a bad
// handle.
extern "C" VYB_WEAK int64_t __vyb_qt_web_load(int64_t h, const char* s, int64_t len) {
    QWebEngineView* v = htowe(h); if (!v) return -1;
    v->load(QUrl(qt_from_bytes(s, len)));
    return 0;
}

// Current page URL (String); "" on a bad handle.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_web_url(int64_t h) {
    QWebEngineView* v = htowe(h); if (!v) return { nullptr, 0 };
    return qt_to_owned(v->url().toString());
}

// Current page title (String); "" on a bad handle.
extern "C" VYB_WEAK vyb_qt_str __vyb_qt_web_title(int64_t h) {
    QWebEngineView* v = htowe(h); if (!v) return { nullptr, 0 };
    return qt_to_owned(v->title());
}

// 1 while the page is still loading, else 0.
extern "C" VYB_WEAK int64_t __vyb_qt_web_loading(int64_t h) {
    QWebEngineView* v = htowe(h); if (!v) return 0;
    return v->isLoading() ? 1 : 0;
}

// Go back in history. Returns 0 on success, -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_web_back(int64_t h) {
    QWebEngineView* v = htowe(h); if (!v) return -1;
    v->back();
    return 0;
}

// Go forward in history. Returns 0 on success, -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_web_forward(int64_t h) {
    QWebEngineView* v = htowe(h); if (!v) return -1;
    v->forward();
    return 0;
}

// Reload the page. Returns 0 on success, -1 on a bad handle.
extern "C" VYB_WEAK int64_t __vyb_qt_web_reload(int64_t h) {
    QWebEngineView* v = htowe(h); if (!v) return -1;
    v->reload();
    return 0;
}
