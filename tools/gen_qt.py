#!/usr/bin/env python3
"""gen_qt.py - table-driven generator for the stdlib `qt` module's mechanical layers.

The hand-written C++ logic lives in runtime/vyb_qt_bridge.cpp. Everything else -
the stub fallback, the codegen dispatch, the semantic allow-list and Int/String
classification, the JIT symbol registration, and the Vyb wrappers + enums - is
owned by this tool and driven by the QT_FUNCS / QT_EVENTS / QT_WIDGETS tables.
Adding a widget is: add a row, write the bridge C++ by hand, run this tool, rebuild.

Each owned region is delimited by sentinel comments (`gen_qt[<key>]: begin` / end)
whose body this tool rewrites in place. `--check` verifies no owned region drifted
(the same pattern tools/refman.py uses for the docs).

Usage:
    python3 tools/gen_qt.py            # regenerate all owned regions in place
    python3 tools/gen_qt.py --check    # exit 1 if any owned region is stale
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---------------------------------------------------------------------------
# The source of truth.
#   mod  : public wrapper name (stdlib/qt/mod.vyb)
#   vn   : Vyb intrinsic name (cgen dispatch + semantic + mod.vyb call)
#   cn   : C runtime symbol (stub + main.cpp)
#   args : [(name, Vyb_type)]   Vyb_type in {Int, String, Bool}
#   ret  : Vyb return type (Int / String / Bool)
#   shape: cgen marshaling group (int0/int1/str1/str2/text/value/3int)
#   stub : default stub return expression ('0' / '-1' / 'qt_stub_str()')
#   doc  : one-line doc rendered above the Vyb wrapper
#   body : optional verbatim mod.vyb body (default derived from args/ret)
# ---------------------------------------------------------------------------
QT_FUNCS = [
    # Lifecycle + event loop + timer
    dict(mod="qt_init", vn="vyb_qt_init", cn="__vyb_qt_init", args=[], ret="Bool", shape="int0", stub="0",
         doc="Initialize the QApplication (once); true if the GUI is available."),
    dict(mod="qt_quit", vn="vyb_qt_quit", cn="__vyb_qt_quit", args=[], ret="Int", shape="int0", stub="0",
         doc="Shut the GUI down (terminal - do not use Qt handles afterwards). Returns 0."),
    dict(mod="qt_active", vn="vyb_qt_active", cn="__vyb_qt_active", args=[], ret="Bool", shape="int0", stub="0",
         doc="true while the GUI is initialized."),
    dict(mod="qt_process_events", vn="vyb_qt_process_events", cn="__vyb_qt_process_events", args=[], ret="Int",
         shape="int0", stub="-1", doc="Pump the Qt event loop once. Returns 0, or -1 if the GUI is not running."),
    dict(mod="qt_set_timer", vn="vyb_qt_set_timer", cn="__vyb_qt_set_timer", args=[("ms", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Arm a repeating timer every `ms` ms; poll qt_timer_fired(). Returns 0."),
    dict(mod="qt_timer_fired", vn="vyb_qt_timer_fired", cn="__vyb_qt_timer_fired", args=[], ret="Bool", shape="int0",
         stub="0", doc="true once the armed timer has fired since the last check (read clears)."),

    # Window (QWidget)
    dict(mod="qt_window_create", vn="vyb_qt_window_create", cn="__vyb_qt_window_create", args=[], ret="Int",
         shape="int0", stub="0", doc="Create a top-level window; returns its Int handle or 0 on failure."),
    dict(mod="qt_window_close", vn="vyb_qt_window_close", cn="__vyb_qt_window_close", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Destroy window `w` (children go with it). Returns 0."),
    dict(mod="qt_window_set_title", vn="vyb_qt_window_set_title", cn="__vyb_qt_window_set_title",
         args=[("w", "Int"), ("title", "String")], ret="Int", shape="text", stub="-1",
         doc="Set titlebar text. Returns 0, or -1 on a bad handle."),
    dict(mod="qt_window_title", vn="vyb_qt_window_title", cn="__vyb_qt_window_title", args=[("w", "Int")], ret="String",
         shape="str1", stub="qt_stub_str()", doc="Current titlebar text (String); \"\" on a bad handle."),
    dict(mod="qt_window_resize", vn="vyb_qt_window_resize", cn="__vyb_qt_window_resize",
         args=[("w", "Int"), ("width", "Int"), ("height", "Int")], ret="Int", shape="3int", stub="-1",
         doc="Resize to (width, height) pixels. Returns 0."),
    dict(mod="qt_window_width", vn="vyb_qt_window_width", cn="__vyb_qt_window_width", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Content width in pixels, or -1 on a bad handle."),
    dict(mod="qt_window_height", vn="vyb_qt_window_height", cn="__vyb_qt_window_height", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Content height in pixels, or -1 on a bad handle."),
    dict(mod="qt_window_show", vn="vyb_qt_window_show", cn="__vyb_qt_window_show", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Show (map) the window. Returns 0."),
    dict(mod="qt_window_hide", vn="vyb_qt_window_hide", cn="__vyb_qt_window_hide", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Hide (unmap) the window. Returns 0."),
    dict(mod="qt_window_visible", vn="vyb_qt_window_visible", cn="__vyb_qt_window_visible", args=[("w", "Int")],
         ret="Bool", shape="int1", stub="0", doc="true while window `w` is visible, else false."),

    # Label (QLabel)
    dict(mod="qt_label_create", vn="vyb_qt_label_create", cn="__vyb_qt_label_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a label with `text` under `parent` (0 = none); returns its handle or 0."),
    dict(mod="qt_label_set_text", vn="vyb_qt_label_set_text", cn="__vyb_qt_label_set_text",
         args=[("label", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Replace label text. Returns 0, or -1 on a non-label handle."),
    dict(mod="qt_label_text", vn="vyb_qt_label_text", cn="__vyb_qt_label_text", args=[("label", "Int")], ret="String",
         shape="str1", stub="qt_stub_str()", doc="Current label text (String); \"\" on a non-label handle."),

    # Buttons (QPushButton)
    dict(mod="qt_button_create", vn="vyb_qt_button_create", cn="__vyb_qt_button_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a push button under `parent`; a click enqueues QtEvent::Click. Returns its handle."),
    dict(mod="qt_button_set_text", vn="vyb_qt_button_set_text", cn="__vyb_qt_button_set_text",
         args=[("button", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Replace button label. Returns 0, or -1 on a non-button handle."),
    dict(mod="qt_button_text", vn="vyb_qt_button_text", cn="__vyb_qt_button_text", args=[("button", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()", doc="Current button label (String); \"\" on a non-button handle."),
    dict(mod="qt_button_set_enabled", vn="vyb_qt_button_set_enabled", cn="__vyb_qt_button_set_enabled",
         args=[("button", "Int"), ("enabled", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Enable (true) / disable (false) the button. Returns 0.",
         body=("if (enabled) {\n        return vyb_qt_button_set_enabled(button, 1)\n    }\n"
               "    return vyb_qt_button_set_enabled(button, 0)")),

    # Text edits (QLineEdit)
    dict(mod="qt_edit_create", vn="vyb_qt_edit_create", cn="__vyb_qt_edit_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a single-line text edit under `parent`; text changes enqueue QtEvent::TextChanged."),
    dict(mod="qt_edit_text", vn="vyb_qt_edit_text", cn="__vyb_qt_edit_text", args=[("edit", "Int")], ret="String",
         shape="str1", stub="qt_stub_str()", doc="Current edit text (String); \"\" on a non-edit handle."),
    dict(mod="qt_edit_set_text", vn="vyb_qt_edit_set_text", cn="__vyb_qt_edit_set_text",
         args=[("edit", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Replace edit text. Returns 0, or -1 on a non-edit handle."),
    dict(mod="qt_edit_set_placeholder", vn="vyb_qt_edit_set_placeholder", cn="__vyb_qt_edit_set_placeholder",
         args=[("edit", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Set placeholder (ghost) text. Returns 0."),

    # Checkboxes (QCheckBox)
    dict(mod="qt_checkbox_create", vn="vyb_qt_checkbox_create", cn="__vyb_qt_checkbox_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a checkbox under `parent`; toggles enqueue QtEvent::Toggled. Returns its handle."),
    dict(mod="qt_checkbox_checked", vn="vyb_qt_checkbox_checked", cn="__vyb_qt_checkbox_checked", args=[("box", "Int")],
         ret="Bool", shape="int1", stub="-1", doc="true if the checkbox is checked, else false."),
    dict(mod="qt_checkbox_set_checked", vn="vyb_qt_checkbox_set_checked", cn="__vyb_qt_checkbox_set_checked",
         args=[("box", "Int"), ("on", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Check (true) / uncheck (false) the box (enqueues QtEvent::Toggled). Returns 0.",
         body=("if (on) {\n        return vyb_qt_checkbox_set_checked(box, 1)\n    }\n"
               "    return vyb_qt_checkbox_set_checked(box, 0)")),

    # Progress bars (QProgressBar)
    dict(mod="qt_progress_create", vn="vyb_qt_progress_create", cn="__vyb_qt_progress_create",
         args=[("parent", "Int"), ("max", "Int")], ret="Int", shape="value", stub="0",
         doc="Create a progress bar with range 0..max under `parent`. Returns its handle or 0."),
    dict(mod="qt_progress_set_value", vn="vyb_qt_progress_set_value", cn="__vyb_qt_progress_set_value",
         args=[("bar", "Int"), ("value", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set the progress value (clamped). Returns 0."),

    # Box layouts
    dict(mod="qt_vbox", vn="vyb_qt_vbox", cn="__vyb_qt_vbox", args=[("parent", "Int")], ret="Int", shape="int1",
         stub="0", doc="Create a vertical box layout on window `parent`. Returns a layout handle."),
    dict(mod="qt_hbox", vn="vyb_qt_hbox", cn="__vyb_qt_hbox", args=[("parent", "Int")], ret="Int", shape="int1",
         stub="0", doc="Create a horizontal box layout on window `parent`. Returns a layout handle."),
    dict(mod="qt_layout_add", vn="vyb_qt_layout_add", cn="__vyb_qt_layout_add", args=[("layout", "Int"), ("child", "Int")],
         ret="Int", shape="value", stub="-1", doc="Add widget `child` to box-layout `layout`. Returns 0."),

    # Kind introspection
    dict(mod="qt_kind", vn="vyb_qt_kind", cn="__vyb_qt_kind", args=[("h", "Int")], ret="Int", shape="int1", stub="0",
         doc="Static widget kind (QtWidgetKind) of handle `h`, or 0 if not a widget."),

    # Polled event queue
    dict(mod="qt_event_count", vn="vyb_qt_event_count", cn="__vyb_qt_event_count", args=[], ret="Int", shape="int0",
         stub="0", doc="Number of pending control events queued since the last drains."),
    dict(mod="qt_event_handle", vn="vyb_qt_event_handle", cn="__vyb_qt_event_handle", args=[], ret="Int", shape="int0",
         stub="0", doc="Handle of the oldest queued event's widget (0 if empty)."),
    dict(mod="qt_event_kind", vn="vyb_qt_event_kind", cn="__vyb_qt_event_kind", args=[], ret="Int", shape="int0",
         stub="0", doc="Kind of the oldest queued event (QtEvent); 0 if empty."),
    dict(mod="qt_event_pop", vn="vyb_qt_event_pop", cn="__vyb_qt_event_pop", args=[], ret="Int", shape="int0",
         stub="-1", doc="Remove the oldest queued event. Returns 0, or -1 if empty."),

    # Scheduling
    dict(mod="qt_wait_event", vn="vyb_qt_wait_event", cn="__vyb_qt_wait_event", args=[("timeout", "Int")], ret="Bool",
         shape="int1", stub="-1",
         doc="Pump Qt until a control event (true) or `timeout` ms lapses (false); negative waits."),

    # Native event loop (qt_run): QApplication::exec() with callback dispatch.
    dict(mod="qt_run", vn="vyb_qt_run", cn="__vyb_qt_run", args=[], ret="Int", shape="int0", stub="-1",
         doc="Run the Qt native event loop, dispatching queued control events to the qt_on_event handler, until qt_run_stop()/qt_quit(). Returns exit code."),
    dict(mod="qt_run_stop", vn="vyb_qt_run_stop", cn="__vyb_qt_run_stop", args=[], ret="Int", shape="int0", stub="-1",
         doc="Stop a running qt_run() loop (graceful; GUI stays up). Returns 0, or -1 if not running."),
    dict(mod="qt_on_event", vn="vyb_qt_on_event", cn="__vyb_qt_on_event",
         args=[("env", "Ptr"), ("fn", "Ptr")], ret="Int", shape="cb", stub="0",
         doc="Register fn(handle, kind) called for each control event queued while qt_run() runs. Returns 0.",
         wrap_sig="handler<fn(Int, Int) -> Void>",
         wrap_body="return vyb_qt_on_event(handler)"),

    # Combo boxes (QComboBox)
    dict(mod="qt_combo_create", vn="vyb_qt_combo_create", cn="__vyb_qt_combo_create", args=[("parent", "Int")], ret="Int",
         shape="int1", stub="0", doc="Create a combo box under `parent`; index changes enqueue QtEvent::IndexChanged."),
    dict(mod="qt_combo_add_item", vn="vyb_qt_combo_add_item", cn="__vyb_qt_combo_add_item",
         args=[("combo", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Append `text` as the last combo item. Returns 0."),
    dict(mod="qt_combo_count", vn="vyb_qt_combo_count", cn="__vyb_qt_combo_count", args=[("combo", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Combo item count, or -1 on a bad handle."),
    dict(mod="qt_combo_current_index", vn="vyb_qt_combo_current_index", cn="__vyb_qt_combo_current_index",
         args=[("combo", "Int")], ret="Int", shape="int1", stub="-1",
         doc="Currently selected combo index (0-based), or -1 on a bad handle."),
    dict(mod="qt_combo_set_current_index", vn="vyb_qt_combo_set_current_index", cn="__vyb_qt_combo_set_current_index",
         args=[("combo", "Int"), ("idx", "Int")], ret="Int", shape="value", stub="-1",
         doc="Select combo item `idx`. Returns 0."),
    dict(mod="qt_combo_item_text", vn="vyb_qt_combo_item_text", cn="__vyb_qt_combo_item_text",
         args=[("combo", "Int"), ("idx", "Int")], ret="String", shape="str2", stub="qt_stub_str()",
         doc="Text of combo item `idx` (String); \"\" on a bad handle/index."),

    # Spin boxes (QSpinBox)
    dict(mod="qt_spin_create", vn="vyb_qt_spin_create", cn="__vyb_qt_spin_create",
         args=[("parent", "Int"), ("min", "Int"), ("max", "Int")], ret="Int", shape="3int", stub="0",
         doc="Create an integer spin box with range [min, max]; value changes enqueue QtEvent::ValueChanged."),
    dict(mod="qt_spin_value", vn="vyb_qt_spin_value", cn="__vyb_qt_spin_value", args=[("spin", "Int")], ret="Int",
         shape="int1", stub="0", doc="Current spin box value, or 0 on a bad handle."),
    dict(mod="qt_spin_set_value", vn="vyb_qt_spin_set_value", cn="__vyb_qt_spin_set_value",
         args=[("spin", "Int"), ("value", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set the spin box value (clamped). Returns 0."),

    # Sliders (QSlider)
    dict(mod="qt_slider_create", vn="vyb_qt_slider_create", cn="__vyb_qt_slider_create",
         args=[("parent", "Int"), ("min", "Int"), ("max", "Int")], ret="Int", shape="3int", stub="0",
         doc="Create a horizontal slider with range [min, max]; value changes enqueue QtEvent::ValueChanged."),
    dict(mod="qt_slider_value", vn="vyb_qt_slider_value", cn="__vyb_qt_slider_value", args=[("slider", "Int")], ret="Int",
         shape="int1", stub="0", doc="Current slider value, or 0 on a bad handle."),
    dict(mod="qt_slider_set_value", vn="vyb_qt_slider_set_value", cn="__vyb_qt_slider_set_value",
         args=[("slider", "Int"), ("value", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set the slider value (clamped). Returns 0."),

    # Dials (QDial)
    dict(mod="qt_dial_create", vn="vyb_qt_dial_create", cn="__vyb_qt_dial_create",
         args=[("parent", "Int"), ("min", "Int"), ("max", "Int")], ret="Int", shape="3int", stub="0",
         doc="Create a dial with range [min, max]; value changes enqueue QtEvent::ValueChanged."),
    dict(mod="qt_dial_value", vn="vyb_qt_dial_value", cn="__vyb_qt_dial_value", args=[("dial", "Int")], ret="Int",
         shape="int1", stub="0", doc="Current dial value, or 0 on a bad handle."),
    dict(mod="qt_dial_set_value", vn="vyb_qt_dial_set_value", cn="__vyb_qt_dial_set_value",
         args=[("dial", "Int"), ("value", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set the dial value (clamped). Returns 0."),

    # Group boxes (QGroupBox): titled container.
    dict(mod="qt_group_create", vn="vyb_qt_group_create", cn="__vyb_qt_group_create",
         args=[("parent", "Int"), ("title", "String")], ret="Int", shape="text", stub="0",
         doc="Create a titled group-box container under `parent`; put a layout on it. Returns its handle or 0."),

    # Multi-line text editor (QPlainTextEdit).
    dict(mod="qt_text_edit_create", vn="vyb_qt_text_edit_create", cn="__vyb_qt_text_edit_create",
         args=[("parent", "Int")], ret="Int", shape="int1", stub="0",
         doc="Create a multi-line plain-text editor under `parent`; edits enqueue QtEvent::TextChanged. Returns its handle."),
    dict(mod="qt_text_edit_text", vn="vyb_qt_text_edit_text", cn="__vyb_qt_text_edit_text", args=[("ed", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()", doc="Current editor text (String); \"\" on a bad handle."),
    dict(mod="qt_text_edit_set_text", vn="vyb_qt_text_edit_set_text", cn="__vyb_qt_text_edit_set_text",
         args=[("ed", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Replace editor text. Returns 0, or -1 on a non-editor handle."),

    # Radio buttons (QRadioButton): exclusive toggled.
    dict(mod="qt_radio_create", vn="vyb_qt_radio_create", cn="__vyb_qt_radio_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a radio button under `parent`; toggles enqueue QtEvent::Toggled. Returns its handle."),
    dict(mod="qt_radio_checked", vn="vyb_qt_radio_checked", cn="__vyb_qt_radio_checked", args=[("radio", "Int")],
         ret="Bool", shape="int1", stub="-1", doc="true if the radio button is checked, else false."),
    dict(mod="qt_radio_set_checked", vn="vyb_qt_radio_set_checked", cn="__vyb_qt_radio_set_checked",
         args=[("radio", "Int"), ("on", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Check (true) / uncheck (false) the radio (enqueues QtEvent::Toggled). Returns 0.",
         body=("if (on) {\n        return vyb_qt_radio_set_checked(radio, 1)\n    }\n"
               "    return vyb_qt_radio_set_checked(radio, 0)")),

    # Generic widget enable / visibility.
    dict(mod="qt_widget_set_enabled", vn="vyb_qt_widget_set_enabled", cn="__vyb_qt_widget_set_enabled",
         args=[("h", "Int"), ("on", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Enable (true) / disable (false) any widget. Returns 0, or -1 on a non-widget handle.",
         body=("if (on) {\n        return vyb_qt_widget_set_enabled(h, 1)\n    }\n"
               "    return vyb_qt_widget_set_enabled(h, 0)")),
    dict(mod="qt_widget_enabled", vn="vyb_qt_widget_enabled", cn="__vyb_qt_widget_enabled", args=[("h", "Int")],
         ret="Bool", shape="int1", stub="0", doc="true while widget `h` is enabled, else false."),

    # Grid layout (QGridLayout).
    dict(mod="qt_grid", vn="vyb_qt_grid", cn="__vyb_qt_grid", args=[("parent", "Int")], ret="Int", shape="int1",
         stub="0", doc="Create a grid layout on window `parent`. Returns a layout handle."),
    dict(mod="qt_grid_add", vn="vyb_qt_grid_add", cn="__vyb_qt_grid_add",
         args=[("layout", "Int"), ("child", "Int"), ("row", "Int"), ("col", "Int")], ret="Int", shape="4int",
         stub="-1", doc="Add widget `child` to grid-layout `layout` at (row, col). Returns 0."),
]

QT_EVENTS = [
    ("none", 0), ("click", 1), ("textChanged", 2), ("toggled", 3),
    ("indexChanged", 4), ("valueChanged", 5),
]

QT_WIDGETS = [
    ("none", 0), ("window", 1), ("label", 2), ("button", 3), ("edit", 4),
    ("checkbox", 5), ("progress", 6), ("combo", 7), ("spin", 8), ("slider", 9),
    ("dial", 10), ("group", 11), ("textEdit", 12), ("radio", 13),
]


def marker(lang, key, which):
    return ("// gen_qt[%s]: %s" % (key, which)) if lang == "cpp" else ("# gen_qt[%s]: %s" % (key, which))


def replace_region(text, lang, key, body):
    bm, em = marker(lang, key, "begin"), marker(lang, key, "end")
    b = text.find(bm)
    if b < 0:
        raise SystemExit("gen_qt: missing begin marker %r" % bm)
    e = text.find(em, b)
    if e < 0:
        raise SystemExit("gen_qt: missing end marker %r" % em)
    b = text.find("\n", b) + 1
    body = body.rstrip("\n") + "\n"
    if text[b:e] == body:
        return text, True
    return text[:b] + body + text[e:], False


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def cpp_args(f):
    parts = []
    for name, typ in f["args"]:
        if typ == "String":
            parts.append("const char* %s, int64_t len" % name)
        elif typ == "Ptr":
            parts.append("void* %s" % name)
        else:
            parts.append("int64_t %s" % name)
    return ", ".join(parts)


def void_casts(f):
    return " ".join("(void)%s;" % name for name, _ in f["args"]).rstrip()


def stub_body(f):
    ret = "vyb_qt_str" if f["ret"] == "String" else "int64_t"
    sig = "VYB_WEAK %s %s(%s)" % (ret, f["cn"], cpp_args(f))
    s = f["stub"]
    if s in ("0", "-1"):
        expr = "return %s;" % s
    else:  # qt_stub_str()
        expr = "return qt_stub_str();"
    casts = void_casts(f)
    if len(f["args"]) > 1 or ret == "vyb_qt_str":
        inner = "%s %s" % (casts, expr) if casts else expr
        return "%s\n    { %s }" % (sig, inner.strip())
    inner = "%s %s" % (casts, expr) if casts else expr
    return "%s { %s }" % (sig, inner.strip())


# -- cgen -------------------------------------------------------------------
def cond_lines(names, indent="                "):
    if not names:
        return ""
    line = 'fname == "%s"' % names[0]
    for nm in names[1:]:
        add = ' || fname == "%s"' % nm
        if len(indent + line + add) > 100:
            line += " ||\n%s%s" % (indent, 'fname == "%s"' % nm)
        else:
            line += add
    return line


SHAPES = ["int0", "int1", "str1", "str2", "text", "value", "3int", "4int", "cb"]

BRANCH = {
    "int0": ('if (!checkArity(0)) return;\n'
             'llvm::FunctionType* ft0 = llvm::FunctionType::get(int64Type, {}, false);\n'
             'm_currentLLVMValue = builder->CreateCall(getQtFn(ft0), {}, "qt.ret");'),
    "int1": ('if (!checkArity(1)) return;\n'
             'llvm::Value* a = needArg(0); if (!a) return;\n'
             'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type}, false);\n'
             'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a)}, "qt.i1");'),
    "str1": ('if (!checkArity(1)) return;\n'
             'llvm::Value* a = needArg(0); if (!a) return;\n'
             'llvm::FunctionType* ft = llvm::FunctionType::get(qtStrRet(), {int64Type}, false);\n'
             'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a)}, "qt.s1");'),
    "str2": ('if (!checkArity(2)) return;\n'
             'llvm::Value* a = needArg(0); llvm::Value* b = needArg(1);\n'
             'if (!a || !b) return;\n'
             'llvm::FunctionType* ft = llvm::FunctionType::get(qtStrRet(), {int64Type, int64Type}, false);\n'
             'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a), toI64(b)}, "qt.s2");'),
    "text": ('if (!checkArity(2)) return;\n'
             'llvm::Value* a = needArg(0); llvm::Value* s = needArg(1);\n'
             'if (!a || !s) return;\n'
             'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType, int64Type}, false);\n'
             'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a), toStrPtr(s), strLenOf(s)}, "qt.text");'),
    "value": ('if (!checkArity(2)) return;\n'
              'llvm::Value* a = needArg(0); llvm::Value* b = needArg(1);\n'
              'if (!a || !b) return;\n'
              'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int64Type}, false);\n'
              'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a), toI64(b)}, "qt.v2");'),
    "3int": ('if (!checkArity(3)) return;\n'
             'llvm::Value* a = needArg(0); llvm::Value* b = needArg(1); llvm::Value* c = needArg(2);\n'
             'if (!a || !b || !c) return;\n'
             'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int64Type, int64Type}, false);\n'
             'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a), toI64(b), toI64(c)}, "qt.i3");'),
    "4int": ('if (!checkArity(4)) return;\n'
               'llvm::Value* a=needArg(0); llvm::Value* b=needArg(1); llvm::Value* c=needArg(2); llvm::Value* d=needArg(3);\n'
               'if (!a || !b || !c || !d) return;\n'
               'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int64Type, int64Type, int64Type}, false);\n'
               'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a), toI64(b), toI64(c), toI64(d)}, "qt.i4");'),
    "cb": ('if (!checkArity(1)) return;\n'
           'node->arguments[0]->accept(*this);\n'
           'llvm::Value* cl = m_currentLLVMValue; if (!cl) return;\n'
           'llvm::StructType* closureTy = getClosureStructType();\n'
           'llvm::Value* envPtr = nullptr; llvm::Value* fnPtr = nullptr;\n'
           'if (cl->getType()->isStructTy()) {\n'
           '    envPtr = builder->CreateExtractValue(cl, 0, "qt.env");\n'
           '    fnPtr = builder->CreateExtractValue(cl, 1, "qt.fn");\n'
           '} else if (cl->getType()->isPointerTy()) {\n'
           '    llvm::Value* closureVal = builder->CreateLoad(closureTy, cl, "qt.closure");\n'
           '    envPtr = builder->CreateExtractValue(closureVal, 0, "qt.env");\n'
           '    fnPtr = builder->CreateExtractValue(closureVal, 1, "qt.fn");\n'
           '} else {\n'
           '    logError(node->loc, "vyb_qt_on_event argument is not a fn() closure");\n'
           '    m_currentLLVMValue = nullptr; return;\n'
           '}\n'
           'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int8PtrType, int8PtrType}, false);\n'
           'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {envPtr, fnPtr}, "qt.cb");'),
}


def emit_cgen():
    from collections import OrderedDict
    groups = OrderedDict()
    for f in QT_FUNCS:
        groups.setdefault(f["shape"], []).append(f["vn"])
    out = []
    out.append("        const std::string& fname = identCallee->name;")
    out.append("        std::string rtName;")
    for i, f in enumerate(QT_FUNCS):
        kw = "if" if i == 0 else "else if"
        out.append('        %s (fname == "%s") rtName = "%s";' % (kw, f["vn"], f["cn"]))
    out.append("        if (!rtName.empty()) {")
    out.append("            auto getQtFn = [&](llvm::FunctionType* ft) -> llvm::Function* {")
    out.append("                llvm::Function* f2 = module->getFunction(rtName);")
    out.append("                if (!f2) f2 = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, rtName, module.get());")
    out.append("                return f2;")
    out.append("            };")
    out.append("            auto toI64 = [&](llvm::Value* v) -> llvm::Value* {")
    out.append("                if (!v) return v;")
    out.append("                if (v->getType()->isIntegerTy(64)) return v;")
    out.append("                if (v->getType()->isIntegerTy())")
    out.append('                    return builder->CreateSExt(v, int64Type, "qt.toi64");')
    out.append("                return v;")
    out.append("            };")
    out.append("            auto toStrPtr = [&](llvm::Value* v) -> llvm::Value* {")
    out.append("                if (v && v->getType()->isStructTy())")
    out.append('                    return builder->CreateExtractValue(v, 0, "qt.strptr");')
    out.append("                return v;")
    out.append("            };")
    out.append("            auto strLenOf = [&](llvm::Value* v) -> llvm::Value* {")
    out.append("                if (v && v->getType()->isStructTy())")
    out.append('                    return builder->CreateExtractValue(v, 1, "qt.strlen");')
    out.append("                return llvm::ConstantInt::get(int64Type, 0);")
    out.append("            };")
    out.append("            auto qtStrRet = [&]() -> llvm::StructType* {")
    out.append("                std::vector<llvm::Type*> f = {int8PtrType, int64Type};")
    out.append("                return llvm::StructType::get(*context, f, false);")
    out.append("            };")
    out.append("            auto needArg = [&](size_t idx) -> llvm::Value* {")
    out.append("                if (idx >= node->arguments.size()) { m_currentLLVMValue = nullptr; return nullptr; }")
    out.append("                node->arguments[idx]->accept(*this);")
    out.append("                return m_currentLLVMValue;")
    out.append("            };")
    out.append("            auto checkArity = [&](size_t n) -> bool {")
    out.append("                if (node->arguments.size() != n) {")
    out.append('                    logError(node->loc, rtName + " expects " + std::to_string(n) + " argument(s)");')
    out.append("                    m_currentLLVMValue = nullptr;")
    out.append("                    return false;")
    out.append("                }")
    out.append("                return true;")
    out.append("            };")
    out.append("")
    frags = []
    for shape in SHAPES:
        names = groups.get(shape, [])
        if not names:
            continue
        opener = "            if (%s) {" if not frags else "            } else if (%s) {"
        frags.append(opener % cond_lines(names))
        for ln in BRANCH[shape].splitlines():
            frags.append("                " + ln)
        frags.append("                return;")
    if frags:
        frags.append("            }")
        out.extend(frags)
    out.append("        }")
    return "\n".join(out)


def emit_sem_allow():
    names = [f["vn"] for f in QT_FUNCS]
    lines = []
    for i in range(0, len(names), 2):
        pair = names[i:i + 2]
        joined = " || ".join('name == "%s"' % n for n in pair)
        if i + 2 < len(names):
            lines.append("            " + joined + " ||")
        else:
            lines.append("            " + joined + ") {")
    return "\n".join(lines)


def emit_sem_int():
    return "\n".join('                "%s",' % f["vn"] for f in QT_FUNCS)


def emit_sem_str():
    return "\n".join('                "%s",' % f["vn"] for f in QT_FUNCS if f["ret"] == "String")


def emit_main_decl():
    out = []
    for f in QT_FUNCS:
        ret = "vyb_file_str" if f["ret"] == "String" else "int64_t"
        out.append("    %s %s(%s);" % (ret, f["cn"], cpp_args(f)))
    return "\n".join(out)


def emit_main_reg():
    out = []
    for f in QT_FUNCS:
        out.append('        runtimeSymbols[mangle("%s")] = llvm::orc::ExecutorSymbolDef(' % f["cn"])
        out.append("            llvm::orc::ExecutorAddr::fromPtr(&%s), llvm::JITSymbolFlags::Exported);" % f["cn"])
    return "\n".join(out)


def emit_stub_funcs():
    return "\n".join(stub_body(f) for f in QT_FUNCS)


def emit_mod_enums():
    def enum(name, members):
        return ("share(all)\nenum %s {\n%s\n}" % (name, "\n".join("    %s = %d" % (m, v) for m, v in members)))
    return enum("QtEvent", QT_EVENTS) + "\n\n" + enum("QtWidgetKind", QT_WIDGETS)


def emit_mod_wrappers():
    out = []
    for f in QT_FUNCS:
        out.append("# %s" % f["doc"])
        out.append("share(all)")
        if "wrap_sig" in f:
            sig = f["wrap_sig"]
        else:
            sig = ", ".join("%s<%s>" % (n, t) for n, t in f["args"])
        out.append("%s(%s)<%s> -> {" % (f["mod"], sig, f["ret"]))
        if "wrap_body" in f:
            out.append("    " + f["wrap_body"])
        elif "body" in f:
            out.append("    " + f["body"])
        else:
            call = "%s(%s)" % (f["vn"], ", ".join(n for n, _ in f["args"]))
            out.append("    return %s%s" % (call, " == 1" if f["ret"] == "Bool" else ""))
        out.append("}")
        out.append("")
    return "\n".join(out).rstrip("\n") + "\n"


FILES = [
    ("runtime/vyb_qt_stub.cpp", "cpp", "stub", emit_stub_funcs),
    ("src/vre/llvm/cgen_expr.cpp", "cpp", "cgen", emit_cgen),
    ("src/vre/semantic.cpp", "cpp", "sem_allow", emit_sem_allow),
    ("src/vre/semantic.cpp", "cpp", "sem_int", emit_sem_int),
    ("src/vre/semantic.cpp", "cpp", "sem_str", emit_sem_str),
    ("src/main.cpp", "cpp", "main_decl", emit_main_decl),
    ("src/main.cpp", "cpp", "main_reg", emit_main_reg),
    ("stdlib/qt/mod.vyb", "vyb", "mod_enums", emit_mod_enums),
    ("stdlib/qt/mod.vyb", "vyb", "mod_wrappers", emit_mod_wrappers),
]


def main():
    ap = argparse.ArgumentParser(description="Regenerate the stdlib qt module's mechanical layers from the table.")
    ap.add_argument("--check", action="store_true", help="exit 1 if any owned region is stale")
    args = ap.parse_args()

    drift = False
    for rel, lang, key, emit in FILES:
        path = os.path.join(ROOT, rel)
        text = read(path)
        new_text, noop = replace_region(text, lang, key, emit())
        if noop:
            continue
        drift = True
        if args.check:
            continue
        write(path, new_text)
        print("gen_qt: regenerated %s [%s]" % (rel, key))
    if args.check:
        if drift:
            print("gen_qt: DRIFT - run `python3 tools/gen_qt.py` to regenerate")
            return 1
        print("gen_qt: OK (state matches the table)")
        return 0
    if not drift:
        print("gen_qt: no changes (state matches the table)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
