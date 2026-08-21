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
         doc="Shut the GUI down (terminal - do not use Qt handles afterwards). Returns `Bool?` -- present once the GUI has quit."),
    dict(mod="qt_active", vn="vyb_qt_active", cn="__vyb_qt_active", args=[], ret="Bool", shape="int0", stub="0",
         doc="true while the GUI is initialized."),
    dict(mod="qt_process_events", vn="vyb_qt_process_events", cn="__vyb_qt_process_events", args=[], ret="Int",
         shape="int0", stub="-1", doc="Pump the Qt event loop once. Returns `Bool?` -- present on success, absent if the GUI is not running."),
    dict(mod="qt_set_timer", vn="vyb_qt_set_timer", cn="__vyb_qt_set_timer", args=[("ms", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Arm a repeating timer every `ms` ms; poll qt_timer_fired(). Returns `Bool?` -- present on success, absent on failure."),
    dict(mod="qt_timer_fired", vn="vyb_qt_timer_fired", cn="__vyb_qt_timer_fired", args=[], ret="Bool", shape="int0",
         stub="0", doc="true once the armed timer has fired since the last check (read clears)."),

    # Window (QWidget)
    dict(mod="qt_window_create", vn="vyb_qt_window_create", cn="__vyb_qt_window_create", args=[], ret="Int",
         shape="int0", stub="0", doc="Create a top-level window. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_window_close", vn="vyb_qt_window_close", cn="__vyb_qt_window_close", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Destroy window `w` (children go with it). Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_window_set_title", vn="vyb_qt_window_set_title", cn="__vyb_qt_window_set_title",
         args=[("w", "Int"), ("title", "String")], ret="Int", shape="text", stub="-1",
         doc="Set titlebar text. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_window_title", vn="vyb_qt_window_title", cn="__vyb_qt_window_title", args=[("w", "Int")], ret="String",
         shape="str1", stub="qt_stub_str()", doc="Current titlebar text (String); \"\" on a bad handle."),
    dict(mod="qt_window_resize", vn="vyb_qt_window_resize", cn="__vyb_qt_window_resize",
         args=[("w", "Int"), ("width", "Int"), ("height", "Int")], ret="Int", shape="3int", stub="-1",
         doc="Resize to (width, height) pixels. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_window_width", vn="vyb_qt_window_width", cn="__vyb_qt_window_width", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Content width in pixels. Returns `Int?` -- present holding the width, absent on a bad handle."),
    dict(mod="qt_window_height", vn="vyb_qt_window_height", cn="__vyb_qt_window_height", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Content height in pixels. Returns `Int?` -- present holding the height, absent on a bad handle."),
    dict(mod="qt_window_show", vn="vyb_qt_window_show", cn="__vyb_qt_window_show", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Show (map) the window. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_window_hide", vn="vyb_qt_window_hide", cn="__vyb_qt_window_hide", args=[("w", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Hide (unmap) the window. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_window_visible", vn="vyb_qt_window_visible", cn="__vyb_qt_window_visible", args=[("w", "Int")],
         ret="Bool", shape="int1", stub="0", doc="true while window `w` is visible, else false."),

    # Screen (QApplication::primaryScreen)
    dict(mod="qt_screen_width", vn="vyb_qt_screen_width", cn="__vyb_qt_screen_width", args=[], ret="Int",
         shape="int0", stub="-1", doc="Primary screen width in logical pixels. Returns `Int?` -- present holding the width, absent if no screen is available."),
    dict(mod="qt_screen_height", vn="vyb_qt_screen_height", cn="__vyb_qt_screen_height", args=[], ret="Int",
         shape="int0", stub="-1", doc="Primary screen height in logical pixels. Returns `Int?` -- present holding the height, absent if no screen is available."),
    dict(mod="qt_screen_dpi", vn="vyb_qt_screen_dpi", cn="__vyb_qt_screen_dpi", args=[], ret="Int",
         shape="int0", stub="-1", doc="Primary screen device-pixel-ratio scaled to 96dpi (100 = 1.0x). Returns `Int?` -- present holding the DPI, absent if no screen is available."),

    # Label (QLabel)
    dict(mod="qt_label_create", vn="vyb_qt_label_create", cn="__vyb_qt_label_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a label with `text` under `parent` (0 = none). Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_label_set_text", vn="vyb_qt_label_set_text", cn="__vyb_qt_label_set_text",
         args=[("label", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Replace label text. Returns `Bool?` -- present on success, absent on a non-label handle."),
    dict(mod="qt_label_text", vn="vyb_qt_label_text", cn="__vyb_qt_label_text", args=[("label", "Int")], ret="String",
         shape="str1", stub="qt_stub_str()", doc="Current label text (String); \"\" on a non-label handle."),

    # Buttons (QPushButton)
    dict(mod="qt_button_create", vn="vyb_qt_button_create", cn="__vyb_qt_button_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a push button under `parent`; a click enqueues QtEvent::Click. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_button_set_text", vn="vyb_qt_button_set_text", cn="__vyb_qt_button_set_text",
         args=[("button", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Replace button label. Returns `Bool?` -- present on success, absent on a non-button handle."),
    dict(mod="qt_button_text", vn="vyb_qt_button_text", cn="__vyb_qt_button_text", args=[("button", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()", doc="Current button label (String); \"\" on a non-button handle."),
    dict(mod="qt_button_set_enabled", vn="vyb_qt_button_set_enabled", cn="__vyb_qt_button_set_enabled",
         args=[("button", "Int"), ("enabled", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Enable (true) / disable (false) the button. Returns `Bool?` -- present on success, absent on a non-button handle.",
         body=("if (enabled) {\n        return vyb_qt_button_set_enabled(button, 1)\n    }\n"
               "    return vyb_qt_button_set_enabled(button, 0)")),

    # Text edits (QLineEdit)
    dict(mod="qt_edit_create", vn="vyb_qt_edit_create", cn="__vyb_qt_edit_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a single-line text edit under `parent`; text changes enqueue QtEvent::TextChanged. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_edit_text", vn="vyb_qt_edit_text", cn="__vyb_qt_edit_text", args=[("edit", "Int")], ret="String",
         shape="str1", stub="qt_stub_str()", doc="Current edit text (String); \"\" on a non-edit handle."),
    dict(mod="qt_edit_set_text", vn="vyb_qt_edit_set_text", cn="__vyb_qt_edit_set_text",
         args=[("edit", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Replace edit text. Returns `Bool?` -- present on success, absent on a non-edit handle."),
    dict(mod="qt_edit_set_placeholder", vn="vyb_qt_edit_set_placeholder", cn="__vyb_qt_edit_set_placeholder",
         args=[("edit", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Set placeholder (ghost) text. Returns `Bool?` -- present on success, absent on a non-edit handle."),

    # Checkboxes (QCheckBox)
    dict(mod="qt_checkbox_create", vn="vyb_qt_checkbox_create", cn="__vyb_qt_checkbox_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a checkbox under `parent`; toggles enqueue QtEvent::Toggled. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_checkbox_checked", vn="vyb_qt_checkbox_checked", cn="__vyb_qt_checkbox_checked", args=[("box", "Int")],
         ret="Bool", shape="int1", stub="-1", doc="true if the checkbox is checked, else false."),
    dict(mod="qt_checkbox_set_checked", vn="vyb_qt_checkbox_set_checked", cn="__vyb_qt_checkbox_set_checked",
         args=[("box", "Int"), ("on", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Check (true) / uncheck (false) the box (enqueues QtEvent::Toggled). Returns `Bool?` -- present on success, absent on a non-checkbox handle.",
         body=("if (on) {\n        return vyb_qt_checkbox_set_checked(box, 1)\n    }\n"
               "    return vyb_qt_checkbox_set_checked(box, 0)")),

    # Progress bars (QProgressBar)
    dict(mod="qt_progress_create", vn="vyb_qt_progress_create", cn="__vyb_qt_progress_create",
         args=[("parent", "Int"), ("max", "Int")], ret="Int", shape="value", stub="0",
         doc="Create a progress bar with range 0..max under `parent`. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_progress_set_value", vn="vyb_qt_progress_set_value", cn="__vyb_qt_progress_set_value",
         args=[("bar", "Int"), ("value", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set the progress value (clamped). Returns `Bool?` -- present on success, absent on a bad handle."),

    # Box layouts
    dict(mod="qt_vbox", vn="vyb_qt_vbox", cn="__vyb_qt_vbox", args=[("parent", "Int")], ret="Int", shape="int1",
         stub="0", doc="Create a vertical box layout on window `parent`. Returns `Int?` -- present with the live layout handle, absent if creation failed."),
    dict(mod="qt_hbox", vn="vyb_qt_hbox", cn="__vyb_qt_hbox", args=[("parent", "Int")], ret="Int", shape="int1",
         stub="0", doc="Create a horizontal box layout on window `parent`. Returns `Int?` -- present with the live layout handle, absent if creation failed."),
    dict(mod="qt_layout_add", vn="vyb_qt_layout_add", cn="__vyb_qt_layout_add", args=[("layout", "Int"), ("child", "Int")],
         ret="Int", shape="value", stub="-1", doc="Add widget `child` to box-layout `layout`. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_layout_add_layout", vn="vyb_qt_layout_add_layout", cn="__vyb_qt_layout_add_layout",
         args=[("layout", "Int"), ("sub", "Int")], ret="Int", shape="value", stub="-1",
         doc="Nest sub-layout `sub` inside box-layout `layout` (adds it as the next item). Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_layout_set_stretch", vn="vyb_qt_layout_set_stretch", cn="__vyb_qt_layout_set_stretch",
         args=[("layout", "Int"), ("index", "Int"), ("stretch", "Int")], ret="Int", shape="3int", stub="-1",
         doc="Set the stretch factor of the item at `index` (add order, 0-based) to `stretch`. Returns `Bool?` -- present on success, absent on a bad handle."),

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
         doc="Stop a running qt_run() loop (graceful; GUI stays up). Returns `Bool?` -- present if a loop was stopped; absent if none was running."),
    dict(mod="qt_on_event", vn="vyb_qt_on_event", cn="__vyb_qt_on_event",
         args=[("env", "Ptr"), ("fn", "Ptr")], ret="Int", shape="cb", stub="0",
         doc="Register fn(handle, kind) called for each control event queued while qt_run() runs. Returns 0.",
         wrap_sig="handler<fn(Int, Int) -> Void>",
         wrap_body="return vyb_qt_on_event(handler)"),
    dict(mod="qt_post_event", vn="vyb_qt_post_event", cn="__vyb_qt_post_event",
         args=[("h", "Int"), ("kind", "Int")], ret="Int", shape="value", stub="0",
         doc="Enqueue a synthetic control event for handle `h` (thread-safe: a background async can signal the UI loop without touching widgets off-thread). Returns `Bool?` -- present on success, absent on failure."),

    # Combo boxes (QComboBox)
    dict(mod="qt_combo_create", vn="vyb_qt_combo_create", cn="__vyb_qt_combo_create", args=[("parent", "Int")], ret="Int",
         shape="int1", stub="0", doc="Create a combo box under `parent`; index changes enqueue QtEvent::IndexChanged. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_combo_add_item", vn="vyb_qt_combo_add_item", cn="__vyb_qt_combo_add_item",
         args=[("combo", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Append `text` as the last combo item. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_combo_count", vn="vyb_qt_combo_count", cn="__vyb_qt_combo_count", args=[("combo", "Int")], ret="Int",
         shape="int1", stub="-1", doc="Combo item count, or -1 on a bad handle."),
    dict(mod="qt_combo_current_index", vn="vyb_qt_combo_current_index", cn="__vyb_qt_combo_current_index",
         args=[("combo", "Int")], ret="Int", shape="int1", stub="-1",
         doc="Currently selected combo index (0-based), or -1 on a bad handle."),
    dict(mod="qt_combo_set_current_index", vn="vyb_qt_combo_set_current_index", cn="__vyb_qt_combo_set_current_index",
         args=[("combo", "Int"), ("idx", "Int")], ret="Int", shape="value", stub="-1",
         doc="Select combo item `idx`. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_combo_item_text", vn="vyb_qt_combo_item_text", cn="__vyb_qt_combo_item_text",
         args=[("combo", "Int"), ("idx", "Int")], ret="String", shape="str2", stub="qt_stub_str()",
         doc="Text of combo item `idx` (String); \"\" on a bad handle/index."),

    # Spin boxes (QSpinBox)
    dict(mod="qt_spin_create", vn="vyb_qt_spin_create", cn="__vyb_qt_spin_create",
         args=[("parent", "Int"), ("min", "Int"), ("max", "Int")], ret="Int", shape="3int", stub="0",
         doc="Create an integer spin box with range [min, max]; value changes enqueue QtEvent::ValueChanged. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_spin_value", vn="vyb_qt_spin_value", cn="__vyb_qt_spin_value", args=[("spin", "Int")], ret="Int",
         shape="int1", stub="0", doc="Current spin box value, or 0 on a bad handle."),
    dict(mod="qt_spin_set_value", vn="vyb_qt_spin_set_value", cn="__vyb_qt_spin_set_value",
         args=[("spin", "Int"), ("value", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set the spin box value (clamped). Returns `Bool?` -- present on success, absent on a bad handle."),

    # Sliders (QSlider)
    dict(mod="qt_slider_create", vn="vyb_qt_slider_create", cn="__vyb_qt_slider_create",
         args=[("parent", "Int"), ("min", "Int"), ("max", "Int")], ret="Int", shape="3int", stub="0",
         doc="Create a horizontal slider with range [min, max]; value changes enqueue QtEvent::ValueChanged. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_slider_value", vn="vyb_qt_slider_value", cn="__vyb_qt_slider_value", args=[("slider", "Int")], ret="Int",
         shape="int1", stub="0", doc="Current slider value, or 0 on a bad handle."),
    dict(mod="qt_slider_set_value", vn="vyb_qt_slider_set_value", cn="__vyb_qt_slider_set_value",
         args=[("slider", "Int"), ("value", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set the slider value (clamped). Returns `Bool?` -- present on success, absent on a bad handle."),

    # Dials (QDial)
    dict(mod="qt_dial_create", vn="vyb_qt_dial_create", cn="__vyb_qt_dial_create",
         args=[("parent", "Int"), ("min", "Int"), ("max", "Int")], ret="Int", shape="3int", stub="0",
         doc="Create a dial with range [min, max]; value changes enqueue QtEvent::ValueChanged. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_dial_value", vn="vyb_qt_dial_value", cn="__vyb_qt_dial_value", args=[("dial", "Int")], ret="Int",
         shape="int1", stub="0", doc="Current dial value, or 0 on a bad handle."),
    dict(mod="qt_dial_set_value", vn="vyb_qt_dial_set_value", cn="__vyb_qt_dial_set_value",
         args=[("dial", "Int"), ("value", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set the dial value (clamped). Returns `Bool?` -- present on success, absent on a bad handle."),

    # Group boxes (QGroupBox): titled container.
    dict(mod="qt_group_create", vn="vyb_qt_group_create", cn="__vyb_qt_group_create",
         args=[("parent", "Int"), ("title", "String")], ret="Int", shape="text", stub="0",
         doc="Create a titled group-box container under `parent`; put a layout on it. Returns `Int?` -- present with the live handle, absent if creation failed."),

    # Multi-line text editor (QPlainTextEdit).
    dict(mod="qt_text_edit_create", vn="vyb_qt_text_edit_create", cn="__vyb_qt_text_edit_create",
         args=[("parent", "Int")], ret="Int", shape="int1", stub="0",
         doc="Create a multi-line plain-text editor under `parent`; edits enqueue QtEvent::TextChanged. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_text_edit_text", vn="vyb_qt_text_edit_text", cn="__vyb_qt_text_edit_text", args=[("ed", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()", doc="Current editor text (String); \"\" on a bad handle."),
    dict(mod="qt_text_edit_set_text", vn="vyb_qt_text_edit_set_text", cn="__vyb_qt_text_edit_set_text",
         args=[("ed", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Replace editor text. Returns `Bool?` -- present on success, absent on a non-editor handle."),

    # Radio buttons (QRadioButton): exclusive toggled.
    dict(mod="qt_radio_create", vn="vyb_qt_radio_create", cn="__vyb_qt_radio_create",
         args=[("parent", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Create a radio button under `parent`; toggles enqueue QtEvent::Toggled. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_radio_checked", vn="vyb_qt_radio_checked", cn="__vyb_qt_radio_checked", args=[("radio", "Int")],
         ret="Bool", shape="int1", stub="-1", doc="true if the radio button is checked, else false."),
    dict(mod="qt_radio_set_checked", vn="vyb_qt_radio_set_checked", cn="__vyb_qt_radio_set_checked",
         args=[("radio", "Int"), ("on", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Check (true) / uncheck (false) the radio (enqueues QtEvent::Toggled). Returns `Bool?` -- present on success, absent on a non-radio handle.",
         body=("if (on) {\n        return vyb_qt_radio_set_checked(radio, 1)\n    }\n"
               "    return vyb_qt_radio_set_checked(radio, 0)")),

    # Generic widget enable / visibility.
    dict(mod="qt_widget_set_enabled", vn="vyb_qt_widget_set_enabled", cn="__vyb_qt_widget_set_enabled",
         args=[("h", "Int"), ("on", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Enable (true) / disable (false) any widget. Returns `Bool?` -- present on success, absent on a non-widget handle.",
         body=("if (on) {\n        return vyb_qt_widget_set_enabled(h, 1)\n    }\n"
               "    return vyb_qt_widget_set_enabled(h, 0)")),
    dict(mod="qt_widget_enabled", vn="vyb_qt_widget_enabled", cn="__vyb_qt_widget_enabled", args=[("h", "Int")],
         ret="Bool", shape="int1", stub="0", doc="true while widget `h` is enabled, else false."),

    # Grid layout (QGridLayout).
    dict(mod="qt_grid", vn="vyb_qt_grid", cn="__vyb_qt_grid", args=[("parent", "Int")], ret="Int", shape="int1",
         stub="0", doc="Create a grid layout on window `parent`. Returns `Int?` -- present with the live layout handle, absent if creation failed."),
    dict(mod="qt_grid_add", vn="vyb_qt_grid_add", cn="__vyb_qt_grid_add",
         args=[("layout", "Int"), ("child", "Int"), ("row", "Int"), ("col", "Int")], ret="Int", shape="4int",
         stub="-1", doc="Add widget `child` to grid-layout `layout` at (row, col). Returns `Bool?` -- present on success, absent on a bad handle."),

    # Generic widget visibility.
    dict(mod="qt_widget_set_visible", vn="vyb_qt_widget_set_visible", cn="__vyb_qt_widget_set_visible",
         args=[("h", "Int"), ("on", "Bool")], ret="Int", shape="value", stub="-1",
         doc="Show (true) / hide (false) any widget. Returns `Bool?` -- present on success, absent on a non-widget handle.",
         body=("if (on) {\n        return vyb_qt_widget_set_visible(h, 1)\n    }\n"
               "    return vyb_qt_widget_set_visible(h, 0)")),
    dict(mod="qt_widget_visible", vn="vyb_qt_widget_visible", cn="__vyb_qt_widget_visible", args=[("h", "Int")],
         ret="Bool", shape="int1", stub="0", doc="true while widget `h` is visible, else false."),

    # Tab widget (QTabWidget): page container.
    dict(mod="qt_tabs_create", vn="vyb_qt_tabs_create", cn="__vyb_qt_tabs_create", args=[("parent", "Int")],
         ret="Int", shape="int1", stub="0", doc="Create a tab container under `parent`. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_tabs_add", vn="vyb_qt_tabs_add", cn="__vyb_qt_tabs_add",
         args=[("tabs", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Append a tab titled `text`. Returns `Int?` -- present with the page's widget handle (put a layout on it), absent if the tab could not be added."),
    dict(mod="qt_tabs_count", vn="vyb_qt_tabs_count", cn="__vyb_qt_tabs_count", args=[("tabs", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Tab count, or -1 on a bad handle."),
    dict(mod="qt_tabs_current", vn="vyb_qt_tabs_current", cn="__vyb_qt_tabs_current", args=[("tabs", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Current (0-based) tab index, or -1 on a bad handle."),
    dict(mod="qt_tabs_set_current", vn="vyb_qt_tabs_set_current", cn="__vyb_qt_tabs_set_current",
         args=[("tabs", "Int"), ("idx", "Int")], ret="Int", shape="value", stub="-1",
         doc="Select tab `idx` (enqueues QtEvent::CurrentChanged). Returns `Bool?` -- present on success, absent on a bad handle."),

    # List widget (QListWidget).
    dict(mod="qt_list_create", vn="vyb_qt_list_create", cn="__vyb_qt_list_create", args=[("parent", "Int")],
         ret="Int", shape="int1", stub="0", doc="Create an item list under `parent`; selection changes enqueue QtEvent::CurrentChanged. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_list_add", vn="vyb_qt_list_add", cn="__vyb_qt_list_add",
         args=[("list", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Append `text` as the last list item. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_list_count", vn="vyb_qt_list_count", cn="__vyb_qt_list_count", args=[("list", "Int")],
         ret="Int", shape="int1", stub="-1", doc="List item count, or -1 on a bad handle."),
    dict(mod="qt_list_current", vn="vyb_qt_list_current", cn="__vyb_qt_list_current", args=[("list", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Current (0-based) list index, or -1 when none / bad handle."),
    dict(mod="qt_list_set_current", vn="vyb_qt_list_set_current", cn="__vyb_qt_list_set_current",
         args=[("list", "Int"), ("idx", "Int")], ret="Int", shape="value", stub="-1",
         doc="Select list item `idx`. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_list_item_text", vn="vyb_qt_list_item_text", cn="__vyb_qt_list_item_text",
         args=[("list", "Int"), ("idx", "Int")], ret="String", shape="str2", stub="qt_stub_str()",
         doc="Text of list item `idx` (String); \"\" on a bad handle/index."),

    # Main-window chrome (QMainWindow): menubar, menus, actions, statusbar, toolbar.
    dict(mod="qt_main_window_create", vn="vyb_qt_main_window_create", cn="__vyb_qt_main_window_create",
         args=[], ret="Int", shape="int0", stub="0",
         doc="Create a top-level QMainWindow (kind Window) with menubar/statusbar/toolbar support. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_menubar", vn="vyb_qt_menubar", cn="__vyb_qt_menubar", args=[("mw", "Int")],
         ret="Int", shape="int1", stub="0", doc="Obtain the main window's menu bar handle (created on demand). Returns `Int?` -- present with the live handle, absent if unavailable."),
    dict(mod="qt_menu_add", vn="vyb_qt_menu_add", cn="__vyb_qt_menu_add",
         args=[("mw", "Int"), ("title", "String")], ret="Int", shape="text", stub="0",
         doc="Add a top-level menu titled `title` to the menu bar. Returns `Int?` -- present with the live menu handle, absent if creation failed."),
    dict(mod="qt_action_add", vn="vyb_qt_action_add", cn="__vyb_qt_action_add",
         args=[("menu", "Int"), ("text", "String")], ret="Int", shape="text", stub="0",
         doc="Add an action to `menu`; its trigger enqueues QtEvent::Click. Returns `Int?` -- present with the live action handle, absent if creation failed."),
    dict(mod="qt_action_count", vn="vyb_qt_action_count", cn="__vyb_qt_action_count", args=[("menu", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Number of actions in `menu`, or -1 on a bad handle."),
    dict(mod="qt_statusbar_message", vn="vyb_qt_statusbar_message", cn="__vyb_qt_statusbar_message",
         args=[("mw", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Show `text` in the main window's status bar. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_statusbar_text", vn="vyb_qt_statusbar_text", cn="__vyb_qt_statusbar_text", args=[("mw", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()", doc="Current status-bar message (String); \"\" on a bad handle."),
    dict(mod="qt_toolbar_create", vn="vyb_qt_toolbar_create", cn="__vyb_qt_toolbar_create",
         args=[("mw", "Int"), ("title", "String")], ret="Int", shape="text", stub="0",
         doc="Add a toolbar titled `title` to the main window. Returns `Int?` -- present with the live handle, absent if creation failed."),

    # Modal dialogs (QMessageBox / QFileDialog). These block on the main thread
    # for user input and return the chosen result (standard-button code for
    # message boxes; selected path or "" for file/dir dialogs). Under the
    # offscreen QPA platform a modal exec() has no user to click, so the bridge
    # honors an opt-in VYB_QT_DIALOG_AUTO env var to auto-accept for tests.
    dict(mod="qt_msg_info", vn="vyb_qt_msg_info", cn="__vyb_qt_msg_info",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2", stub="-1",
         doc="Show a modal informational message box (blocks until dismissed). Returns `Bool?` -- present once the box was shown and dismissed."),
    dict(mod="qt_msg_warn", vn="vyb_qt_msg_warn", cn="__vyb_qt_msg_warn",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2", stub="-1",
         doc="Show a modal warning message box (blocks until dismissed). Returns `Bool?` -- present once the box was shown and dismissed."),
    dict(mod="qt_msg_error", vn="vyb_qt_msg_error", cn="__vyb_qt_msg_error",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2", stub="-1",
         doc="Show a modal critical-error message box (blocks until dismissed). Returns `Bool?` -- present once the box was shown and dismissed."),
    dict(mod="qt_msg_about", vn="vyb_qt_msg_about", cn="__vyb_qt_msg_about",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2", stub="-1",
         doc="Show a modal 'about' box (blocks until dismissed). Returns `Bool?` -- present once the box was shown and dismissed."),
    dict(mod="qt_msg_question", vn="vyb_qt_msg_question", cn="__vyb_qt_msg_question",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2", stub="-1",
         doc="Show a modal Yes/No question. Returns `Int?` -- present holding 1 for Yes / 0 for No, absent when the GUI is not running."),
    dict(mod="qt_file_open", vn="vyb_qt_file_open", cn="__vyb_qt_file_open",
         args=[("parent", "Int"), ("title", "String"), ("filter", "String")], ret="String", shape="str3",
         stub="qt_stub_str()", opt_vn="vyb_qt_file_open_opt", opt_cn="__vyb_qt_file_open_opt", opt_shape="optstr3",
         doc="Modal 'open file' picker. Returns `String?` -- present holding the chosen path (or the present \"\" on user cancel), absent when the GUI is not running."),
    dict(mod="qt_file_save", vn="vyb_qt_file_save", cn="__vyb_qt_file_save",
         args=[("parent", "Int"), ("title", "String"), ("filter", "String")], ret="String", shape="str3",
         stub="qt_stub_str()", opt_vn="vyb_qt_file_save_opt", opt_cn="__vyb_qt_file_save_opt", opt_shape="optstr3",
         doc="Modal 'save file' picker. Returns `String?` -- present holding the chosen path (or the present \"\" on user cancel), absent when the GUI is not running."),
    dict(mod="qt_dir_select", vn="vyb_qt_dir_select", cn="__vyb_qt_dir_select",
         args=[("parent", "Int"), ("title", "String")], ret="String", shape="strtext", stub="qt_stub_str()",
         opt_vn="vyb_qt_dir_select_opt", opt_cn="__vyb_qt_dir_select_opt", opt_shape="optstr2",
         doc="Modal directory picker. Returns `String?` -- present holding the chosen path (or the present \"\" on user cancel), absent when the GUI is not running."),

    # Async (non-blocking) dialogs. These pair with the event loop: create +
    # show the dialog, return its Int handle immediately, and when the user
    # finishes it an event (QtEvent::dialog) is enqueued whose result payload
    # (qt_event_result: 1 for Yes/Accepted, else 0) a qt_on_event handler or the
    # qt_event_* poll reads. File/dir pickers also record the chosen path,
    # readable any time via qt_dlg_selected(handle).
    dict(mod="qt_dlg_info", vn="vyb_qt_dlg_info", cn="__vyb_qt_dlg_info",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2",
         stub="0", doc="Open a non-blocking information box. Returns `Int?` -- present with the dialog handle, absent if creation failed."),
    dict(mod="qt_dlg_warn", vn="vyb_qt_dlg_warn", cn="__vyb_qt_dlg_warn",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2",
         stub="0", doc="Open a non-blocking warning box. Returns `Int?` -- present with the dialog handle, absent if creation failed."),
    dict(mod="qt_dlg_error", vn="vyb_qt_dlg_error", cn="__vyb_qt_dlg_error",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2",
         stub="0", doc="Open a non-blocking error box. Returns `Int?` -- present with the dialog handle, absent if creation failed."),
    dict(mod="qt_dlg_about", vn="vyb_qt_dlg_about", cn="__vyb_qt_dlg_about",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2",
         stub="0", doc="Open a non-blocking about box. Returns `Int?` -- present with the dialog handle, absent if creation failed."),
    dict(mod="qt_dlg_question", vn="vyb_qt_dlg_question", cn="__vyb_qt_dlg_question",
         args=[("parent", "Int"), ("title", "String"), ("text", "String")], ret="Int", shape="text2",
         stub="0", doc="Open a non-blocking Yes/No box; finishes with result 1 for Yes, 0 otherwise. Returns `Int?` -- present with the dialog handle, absent if creation failed."),
    dict(mod="qt_dlg_open", vn="vyb_qt_dlg_open", cn="__vyb_qt_dlg_open",
         args=[("parent", "Int"), ("title", "String"), ("filter", "String")], ret="Int", shape="text2",
         stub="0", doc="Open a non-blocking file-open picker; finishes with result 1 (Accepted) + qt_dlg_selected. Returns `Int?` -- present with the dialog handle, absent if creation failed."),
    dict(mod="qt_dlg_save", vn="vyb_qt_dlg_save", cn="__vyb_qt_dlg_save",
         args=[("parent", "Int"), ("title", "String"), ("filter", "String")], ret="Int", shape="text2",
         stub="0", doc="Open a non-blocking file-save picker; finishes with result 1 (Accepted) + qt_dlg_selected. Returns `Int?` -- present with the dialog handle, absent if creation failed."),
    dict(mod="qt_dlg_dir", vn="vyb_qt_dlg_dir", cn="__vyb_qt_dlg_dir",
         args=[("parent", "Int"), ("title", "String")], ret="Int", shape="text",
         stub="0", doc="Open a non-blocking directory picker; finishes with result 1 (Accepted) + qt_dlg_selected. Returns `Int?` -- present with the dialog handle, absent if creation failed."),
    dict(mod="qt_dlg_close", vn="vyb_qt_dlg_close", cn="__vyb_qt_dlg_close", args=[("h", "Int")],
         ret="Int", shape="int1", stub="-1",
         doc="Finish dialog `h` as rejected (enqueues QtEvent::dialog with result 0). Returns `Bool?` -- present on success, absent on a non-dialog or bad handle."),
    dict(mod="qt_dlg_selected", vn="vyb_qt_dlg_selected", cn="__vyb_qt_dlg_selected", args=[("h", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()", opt_vn="vyb_qt_dlg_selected_opt",
         opt_cn="__vyb_qt_dlg_selected_opt", opt_shape="optstr1",
         doc="Path chosen by a finished file/dir dialog `h`. Returns `String?` -- present holding the recorded path, absent when no result is available yet (bad / non-picker / unfinished dialog)."),
    dict(mod="qt_event_result", vn="vyb_qt_event_result", cn="__vyb_qt_event_result", args=[], ret="Int",
         shape="int0", stub="0",
         doc="Result payload of the oldest queued dialog event, or of the event being dispatched by qt_run (1 = Yes/Accepted, else 0)."),

    # Rich-text editor (QTextEdit) + font/color helpers.
    dict(mod="qt_rich_create", vn="vyb_qt_rich_create", cn="__vyb_qt_rich_create", args=[("parent", "Int")],
         ret="Int", shape="int1", stub="0",
         doc="Create a rich-text editor (QTextEdit) under `parent`; edits enqueue QtEvent::TextChanged. Returns `Int?` -- present with the live handle, absent if creation failed."),
    dict(mod="qt_rich_set_html", vn="vyb_qt_rich_set_html", cn="__vyb_qt_rich_set_html",
         args=[("ed", "Int"), ("html", "String")], ret="Int", shape="text", stub="-1",
         doc="Set the rich-text editor body from `html` (supports <b>/<i>/<font color> etc). Returns `Bool?` -- present on success, absent on a non-editor handle."),
    dict(mod="qt_rich_html", vn="vyb_qt_rich_html", cn="__vyb_qt_rich_html", args=[("ed", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()",
         doc="Current rich-text body as HTML (String); \"\" on a bad handle."),
    dict(mod="qt_rich_set_plain", vn="vyb_qt_rich_set_plain", cn="__vyb_qt_rich_set_plain",
         args=[("ed", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Set the rich-text editor's plain text (clears formatting). Returns `Bool?` -- present on success, absent on a non-editor handle."),
    dict(mod="qt_rich_plain", vn="vyb_qt_rich_plain", cn="__vyb_qt_rich_plain", args=[("ed", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()",
         doc="Current plain text (String); \"\" on a bad handle."),
    dict(mod="qt_rich_append", vn="vyb_qt_rich_append", cn="__vyb_qt_rich_append",
         args=[("ed", "Int"), ("text", "String")], ret="Int", shape="text", stub="-1",
         doc="Append `text` at the end (keeps the current character format). Returns `Bool?` -- present on success, absent on a non-editor handle."),
    dict(mod="qt_rich_clear", vn="vyb_qt_rich_clear", cn="__vyb_qt_rich_clear", args=[("ed", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Clear all rich-text content. Returns `Bool?` -- present on success, absent on a non-editor handle."),
    dict(mod="qt_rich_set_text_color", vn="vyb_qt_rich_set_text_color", cn="__vyb_qt_rich_set_text_color",
         args=[("ed", "Int"), ("r", "Int"), ("g", "Int"), ("b", "Int")], ret="Int", shape="4int", stub="-1",
         doc="Set the editor's text color to (r,g,b) each 0-255. Returns `Bool?` -- present on success, absent on a non-editor handle."),
    dict(mod="qt_widget_set_font_size", vn="vyb_qt_widget_set_font_size", cn="__vyb_qt_widget_set_font_size",
         args=[("h", "Int"), ("pt", "Int")], ret="Int", shape="value", stub="-1",
         doc="Set a widget's font point size. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_widget_set_font_bold", vn="vyb_qt_widget_set_font_bold", cn="__vyb_qt_widget_set_font_bold",
         args=[("h", "Int"), ("on", "Int")], ret="Int", shape="value", stub="-1",
         doc="Toggle a widget's font bold (on != 0). Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_widget_set_text_color", vn="vyb_qt_widget_set_text_color", cn="__vyb_qt_widget_set_text_color",
         args=[("h", "Int"), ("r", "Int"), ("g", "Int"), ("b", "Int")], ret="Int", shape="4int", stub="-1",
         doc="Set a widget's foreground text color via palette (r,g,b each 0-255). Returns `Bool?` -- present on success, absent on a bad handle."),
]

QT_WEB_FUNCS = [
    # QWebEngineView (optional QtWebEngine): the symbols always resolve (stub
    # fallback) but actual rendering needs QtWebEngine linked into the build.
    dict(mod="qt_web_create", vn="vyb_qt_web_create", cn="__vyb_qt_web_create", args=[("parent", "Int")],
         ret="Int", shape="int1", stub="0",
         doc="Create a QWebEngineView under `parent`. Returns `Int?` -- present with the live handle, absent if QtWebEngine is unavailable or creation failed."),
    dict(mod="qt_web_load", vn="vyb_qt_web_load", cn="__vyb_qt_web_load",
         args=[("web", "Int"), ("url", "String")], ret="Int", shape="text", stub="-1",
         doc="Begin loading `url` in the web view (async). Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_web_url", vn="vyb_qt_web_url", cn="__vyb_qt_web_url", args=[("web", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()", doc="Current page URL (String); \"\" on a bad handle."),
    dict(mod="qt_web_title", vn="vyb_qt_web_title", cn="__vyb_qt_web_title", args=[("web", "Int")],
         ret="String", shape="str1", stub="qt_stub_str()", doc="Current page title (String); \"\" on a bad handle."),
    dict(mod="qt_web_loading", vn="vyb_qt_web_loading", cn="__vyb_qt_web_loading", args=[("web", "Int")],
         ret="Bool", shape="int1", stub="0", doc="true while the page is still loading."),
    dict(mod="qt_web_back", vn="vyb_qt_web_back", cn="__vyb_qt_web_back", args=[("web", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Go back in history. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_web_forward", vn="vyb_qt_web_forward", cn="__vyb_qt_web_forward", args=[("web", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Go forward in history. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_web_reload", vn="vyb_qt_web_reload", cn="__vyb_qt_web_reload", args=[("web", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Reload the page. Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_web_zoom_in", vn="vyb_qt_web_zoom_in", cn="__vyb_qt_web_zoom_in", args=[("web", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Zoom the web view in (larger text). Returns `Bool?` -- present on success, absent on a bad handle."),
    dict(mod="qt_web_zoom_out", vn="vyb_qt_web_zoom_out", cn="__vyb_qt_web_zoom_out", args=[("web", "Int")],
         ret="Int", shape="int1", stub="-1", doc="Zoom the web view out (smaller text). Returns `Bool?` -- present on success, absent on a bad handle."),
]

QT_ALL = QT_WEB_FUNCS + QT_FUNCS

# Lossless opt intrinsics for the modal pickers + qt_dlg_selected. The C shim
# takes a `vyb_qt_str*` out-param and returns 1 (present) or 0 (absent),
# mirroring __vyb_net_recv_opt, so user-cancel (present-empty/present-path) is
# distinct from failure (absent). opt_shape picks the cgen arity handle:
#   optstr3: (parent, title, filter)   optstr2: (parent, title)   optstr1: (h)
QT_OPT = [
    {"vn": f["opt_vn"], "cn": f["opt_cn"], "shape": f["opt_shape"], "args": f["args"]}
    for f in QT_ALL if "opt_vn" in f
]

# ---------------------------------------------------------------------------
# T? migration for the public Vyb wrappers (issue #134).
#
# The underlying C intrinsic for every Qt function still returns a plain
# int64_t (so the codegen/semantic/stub layers, which key off `ret`/`shape`,
# are untouched). These keys only change the *public wrapper* signature and
# body in mod.vyb:
#
#   "Int?"   -> creator functions: an absent Int? means the handle alloc
#               failed (the intrinsic returned 0); a present Int?(h) is the
#               live handle.
#   "Bool?"  -> op-status functions: an absent Bool? means the operation
#               failed (the intrinsic returned != 0); a present Bool?(true)
#               means it succeeded.
#
# Value/probe getters with an in-domain -1 "found nothing" sentinel
# (qt_*_text/url/count/value/current/index, qt_init/qt_active/qt_timer_fired,
# qt_run, qt_wait_event, qt_event_*, qt_rich_html/plain) stay as-is.
#
# qt_file_open/qt_file_save/qt_dir_select (modal pickers) and qt_dlg_selected
# are instead handled via lossless opt intrinsics (present-empty on user cancel,
# absent on no-GUI / no-result) so cancel is not conflated with failure.
# ---------------------------------------------------------------------------
MIGRATION = {
    # Creators / handle-returning -> Int?
    "qt_web_create": "Int?", "qt_window_create": "Int?", "qt_label_create": "Int?",
    "qt_button_create": "Int?", "qt_edit_create": "Int?", "qt_checkbox_create": "Int?",
    "qt_progress_create": "Int?", "qt_vbox": "Int?", "qt_hbox": "Int?", "qt_grid": "Int?",
    "qt_combo_create": "Int?", "qt_spin_create": "Int?", "qt_slider_create": "Int?",
    "qt_dial_create": "Int?", "qt_group_create": "Int?", "qt_text_edit_create": "Int?",
    "qt_radio_create": "Int?", "qt_tabs_create": "Int?", "qt_tabs_add": "Int?",
    "qt_list_create": "Int?", "qt_main_window_create": "Int?", "qt_menubar": "Int?",
    "qt_menu_add": "Int?", "qt_action_add": "Int?", "qt_toolbar_create": "Int?",
    "qt_rich_create": "Int?",
    "qt_dlg_info": "Int?", "qt_dlg_warn": "Int?", "qt_dlg_error": "Int?",
    "qt_dlg_about": "Int?", "qt_dlg_question": "Int?", "qt_dlg_open": "Int?",
    "qt_dlg_save": "Int?", "qt_dlg_dir": "Int?",
    # Op-status -> Bool?
    "qt_quit": "Bool?", "qt_process_events": "Bool?", "qt_set_timer": "Bool?",
    "qt_run_stop": "Bool?", "qt_post_event": "Bool?",
    "qt_window_close": "Bool?", "qt_window_set_title": "Bool?", "qt_window_resize": "Bool?",
    "qt_window_show": "Bool?", "qt_window_hide": "Bool?",
    "qt_label_set_text": "Bool?", "qt_button_set_text": "Bool?", "qt_button_set_enabled": "Bool?",
    "qt_edit_set_text": "Bool?", "qt_edit_set_placeholder": "Bool?",
    "qt_checkbox_set_checked": "Bool?", "qt_progress_set_value": "Bool?",
    "qt_layout_add": "Bool?", "qt_layout_add_layout": "Bool?", "qt_layout_set_stretch": "Bool?",
    "qt_combo_add_item": "Bool?", "qt_combo_set_current_index": "Bool?",
    "qt_spin_set_value": "Bool?", "qt_slider_set_value": "Bool?", "qt_dial_set_value": "Bool?",
    "qt_text_edit_set_text": "Bool?", "qt_radio_set_checked": "Bool?",
    "qt_widget_set_enabled": "Bool?", "qt_widget_set_visible": "Bool?",
    "qt_grid_add": "Bool?", "qt_tabs_set_current": "Bool?",
    "qt_list_add": "Bool?", "qt_list_set_current": "Bool?",
    "qt_statusbar_message": "Bool?",
    "qt_msg_info": "Bool?", "qt_msg_warn": "Bool?", "qt_msg_error": "Bool?", "qt_msg_about": "Bool?",
    "qt_dlg_close": "Bool?",
    "qt_rich_set_html": "Bool?", "qt_rich_set_plain": "Bool?", "qt_rich_append": "Bool?",
    "qt_rich_clear": "Bool?", "qt_rich_set_text_color": "Bool?",
    "qt_widget_set_font_size": "Bool?", "qt_widget_set_font_bold": "Bool?",
    "qt_widget_set_text_color": "Bool?",
    "qt_web_load": "Bool?", "qt_web_back": "Bool?", "qt_web_forward": "Bool?",
    "qt_web_reload": "Bool?", "qt_web_zoom_in": "Bool?", "qt_web_zoom_out": "Bool?",
    # Value/dimension getters with no in-domain -1 -> Int? (present = value,
    # absent = no screen / bad handle). "Int?@<sentinel>" means absent when the
    # intrinsic returns that sentinel ("@0" for creators, "@-1" for dimensions).
    "qt_screen_width": "Int?@-1", "qt_screen_height": "Int?@-1", "qt_screen_dpi": "Int?@-1",
    "qt_window_width": "Int?@-1", "qt_window_height": "Int?@-1",
    # Modal button result: present = the Yes/No answer, absent = GUI not running.
    "qt_msg_question": "Int?@-1",
}


def _qt_call(f, truthy):
    """Render an intrinsic call, coercing Bool args to 1/0 (intrinsics take Int)."""
    args = []
    for n, t in f["args"]:
        if t == "Bool":
            args.append("1" if truthy else "0")
        else:
            args.append(n)
    return "%s(%s)" % (f["vn"], ", ".join(args))


def migrate_body(f):
    want = MIGRATION[f["mod"]]
    base, _, sentinel = want.partition("@")
    if base == "Int?":
        sentinel = sentinel or "0"
        call = _qt_call(f, False)
        return ("v = %s\n"
                "    if (v == %s) { return Int?() }\n"
                "    return Int?(v)") % (call, sentinel)
    elif want == "Bool?":
        bnames = [n for n, t in f["args"] if t == "Bool"]
        if not bnames:
            return ("if (%s != 0) { return Bool?() }\n"
                    "    return Bool?(true)") % _qt_call(f, False)
        # Single Bool control arg -> emit the 1/0 branches (assume one Bool arg).
        bname = bnames[0]
        return ("if (%s) {\n"
                "        if (%s != 0) { return Bool?() }\n"
                "        return Bool?(true)\n"
                "    }\n"
                "    if (%s != 0) { return Bool?() }\n"
                "    return Bool?(true)") % (bname, _qt_call(f, True), _qt_call(f, False))
    raise SystemExit("gen_qt: bad MIGRATION target %r for %s" % (want, f["mod"]))



QT_EVENTS = [
    ("none", 0), ("click", 1), ("textChanged", 2), ("toggled", 3),
    ("indexChanged", 4), ("valueChanged", 5),
    ("loadFinished", 6), ("titleChanged", 7), ("loadProgress", 8),
    ("currentChanged", 9),
    ("dialog", 10),
    ("editReturn", 11),
    ("zoomIn", 12),
    ("zoomOut", 13),
]

QT_WIDGETS = [
    ("none", 0), ("window", 1), ("label", 2), ("button", 3), ("edit", 4),
    ("checkbox", 5), ("progress", 6), ("combo", 7), ("spin", 8), ("slider", 9),
    ("dial", 10), ("group", 11), ("textEdit", 12), ("radio", 13), ("web", 14),
    ("tabs", 15), ("list", 16), ("menuBar", 17), ("menu", 18), ("toolbar", 19),
    ("rich", 20),
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


def cpp_opt_args(o, out_type="vyb_qt_str"):
    parts = []
    lens = 0
    for name, typ in o["args"]:
        if typ == "String":
            ln = "len" if lens == 0 else "len%d" % (lens + 1)
            lens += 1
            parts.append("const char* %s, int64_t %s" % (name, ln))
        else:
            parts.append("int64_t %s" % name)
    parts.append("%s* out" % out_type)
    return ", ".join(parts)


def cpp_args(f):
    parts = []
    lens = 0
    for name, typ in f["args"]:
        if typ == "String":
            ln = "len" if lens == 0 else "len%d" % (lens + 1)
            lens += 1
            parts.append("const char* %s, int64_t %s" % (name, ln))
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
    if s.lstrip("-").isdigit():
        expr = "return %s;" % s
    elif s == "qt_stub_str()":
        expr = "return qt_stub_str();"
    else:  # qt_stub_str()
        expr = "return qt_stub_str();"
    casts = void_casts(f)
    if len(f["args"]) > 1 or ret == "vyb_qt_str":
        inner = "%s %s" % (casts, expr) if casts else expr
        return "%s\n    { %s }" % (sig, inner.strip())
    inner = "%s %s" % (casts, expr) if casts else expr
    return "%s { %s }" % (sig, inner.strip())


# -- cgen -------------------------------------------------------------------

# Lossless String? opt-intrinsic cgen bodies: mirror __vyb_net_recv_opt (i64
# present flag + out-param slot) over the qt {ptr,len} string type.
OPT_BRANCH = {
    "optstr3": ('if (!checkArity(3)) return;\n'
     'llvm::Value* a = needArg(0); llvm::Value* s = needArg(1); llvm::Value* t = needArg(2);\n'
     'if (!a || !s || !t) return;\n'
     'llvm::StructType* strTy = qtStrRet();\n'
     'llvm::StructType* optTy = llvm::StructType::get(*context, {strTy, llvm::Type::getInt1Ty(*context)}, false);\n'
     'llvm::Value* slotA = builder->CreateAlloca(strTy, nullptr, "qt.opt.slot");\n'
     'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType, int64Type, int8PtrType, int64Type, llvm::PointerType::get(*context, 0)}, false);\n'
     'llvm::Value* has = builder->CreateCall(getQtFn(ft), {toI64(a), toStrPtr(s), strLenOf(s), toStrPtr(t), strLenOf(t), slotA}, "qt.opt.has");\n'
     'llvm::Value* val = builder->CreateLoad(strTy, slotA, "qt.opt.val");\n'
     'llvm::Value* opts = llvm::UndefValue::get(optTy);\n'
     'opts = builder->CreateInsertValue(opts, val, 0, "qt.opt.v");\n'
     'opts = builder->CreateInsertValue(opts, builder->CreateTrunc(has, llvm::Type::getInt1Ty(*context), "qt.opt.h"), 1);\n'
     'm_currentLLVMValue = opts;\n'),
    "optstr2": ('if (!checkArity(2)) return;\n'
     'llvm::Value* a = needArg(0); llvm::Value* s = needArg(1);\n'
     'if (!a || !s) return;\n'
     'llvm::StructType* strTy = qtStrRet();\n'
     'llvm::StructType* optTy = llvm::StructType::get(*context, {strTy, llvm::Type::getInt1Ty(*context)}, false);\n'
     'llvm::Value* slotA = builder->CreateAlloca(strTy, nullptr, "qt.opt.slot");\n'
     'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType, int64Type, llvm::PointerType::get(*context, 0)}, false);\n'
     'llvm::Value* has = builder->CreateCall(getQtFn(ft), {toI64(a), toStrPtr(s), strLenOf(s), slotA}, "qt.opt.has");\n'
     'llvm::Value* val = builder->CreateLoad(strTy, slotA, "qt.opt.val");\n'
     'llvm::Value* opts = llvm::UndefValue::get(optTy);\n'
     'opts = builder->CreateInsertValue(opts, val, 0, "qt.opt.v");\n'
     'opts = builder->CreateInsertValue(opts, builder->CreateTrunc(has, llvm::Type::getInt1Ty(*context), "qt.opt.h"), 1);\n'
     'm_currentLLVMValue = opts;\n'),
    "optstr1": ('if (!checkArity(1)) return;\n'
     'llvm::Value* a = needArg(0); if (!a) return;\n'
     'llvm::StructType* strTy = qtStrRet();\n'
     'llvm::StructType* optTy = llvm::StructType::get(*context, {strTy, llvm::Type::getInt1Ty(*context)}, false);\n'
     'llvm::Value* slotA = builder->CreateAlloca(strTy, nullptr, "qt.opt.slot");\n'
     'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, llvm::PointerType::get(*context, 0)}, false);\n'
     'llvm::Value* has = builder->CreateCall(getQtFn(ft), {toI64(a), slotA}, "qt.opt.has");\n'
     'llvm::Value* val = builder->CreateLoad(strTy, slotA, "qt.opt.val");\n'
     'llvm::Value* opts = llvm::UndefValue::get(optTy);\n'
     'opts = builder->CreateInsertValue(opts, val, 0, "qt.opt.v");\n'
     'opts = builder->CreateInsertValue(opts, builder->CreateTrunc(has, llvm::Type::getInt1Ty(*context), "qt.opt.h"), 1);\n'
     'm_currentLLVMValue = opts;\n'),
}


def emit_opt_stub_funcs():
    return "\n".join(stub_opt_body(o) for o in QT_OPT)


def stub_opt_body(o):
    argnames = [name for name, _ in o["args"]] + ["out"]
    casts = " ".join("(void)%s;" % nm for nm in argnames) + " "
    sig = "VYB_WEAK int64_t %s(%s)" % (o["cn"], cpp_opt_args(o))
    return "%s\n    { %sreturn 0; }" % (sig, casts)


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


SHAPES = ["int0", "int1", "str1", "str2", "str3", "text", "text2", "strtext",
          "value", "3int", "4int", "cb"]

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
    "str3": ('if (!checkArity(3)) return;\n'
             'llvm::Value* a = needArg(0); llvm::Value* s = needArg(1); llvm::Value* t = needArg(2);\n'
             'if (!a || !s || !t) return;\n'
             'llvm::FunctionType* ft = llvm::FunctionType::get(qtStrRet(), '
             '{int64Type, int8PtrType, int64Type, int8PtrType, int64Type}, false);\n'
             'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), '
             '{toI64(a), toStrPtr(s), strLenOf(s), toStrPtr(t), strLenOf(t)}, "qt.s3");'),
    "text": ('if (!checkArity(2)) return;\n'
             'llvm::Value* a = needArg(0); llvm::Value* s = needArg(1);\n'
             'if (!a || !s) return;\n'
             'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType, int64Type}, false);\n'
             'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a), toStrPtr(s), strLenOf(s)}, "qt.text");'),
    "text2": ('if (!checkArity(3)) return;\n'
              'llvm::Value* a = needArg(0); llvm::Value* s = needArg(1); llvm::Value* t = needArg(2);\n'
              'if (!a || !s || !t) return;\n'
              'llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, '
              '{int64Type, int8PtrType, int64Type, int8PtrType, int64Type}, false);\n'
              'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), '
              '{toI64(a), toStrPtr(s), strLenOf(s), toStrPtr(t), strLenOf(t)}, "qt.t2");'),
    "strtext": ('if (!checkArity(2)) return;\n'
                'llvm::Value* a = needArg(0); llvm::Value* s = needArg(1);\n'
                'if (!a || !s) return;\n'
                'llvm::FunctionType* ft = llvm::FunctionType::get(qtStrRet(), {int64Type, int8PtrType, int64Type}, false);\n'
                'm_currentLLVMValue = builder->CreateCall(getQtFn(ft), {toI64(a), toStrPtr(s), strLenOf(s)}, "qt.st");'),
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
    for f in QT_ALL:
        groups.setdefault(f["shape"], []).append(f["vn"])
    out = []
    out.append("        const std::string& fname = identCallee->name;")
    out.append("        std::string rtName;")
    for i, f in enumerate(QT_ALL):
        kw = "if" if i == 0 else "else if"
        out.append('        %s (fname == "%s") rtName = "%s";' % (kw, f["vn"], f["cn"]))
    for i, o in enumerate(QT_OPT):
        kw = "else if" if (QT_ALL or i) else "if"
        out.append('        %s (fname == "%s") rtName = "%s";' % (kw, o["vn"], o["cn"]))
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
    ogroups = OrderedDict()
    for o in QT_OPT:
        ogroups.setdefault(o["shape"], []).append(o["vn"])
    optfrags = []
    first = True
    for oshape, names in ogroups.items():
        opener = "            if (%s) {" if first else "            } else if (%s) {"
        optfrags.append(opener % cond_lines(names))
        for ln in OPT_BRANCH[oshape].splitlines():
            optfrags.append("                " + ln)
        optfrags.append("                return;")
        first = False
    if optfrags:
        optfrags.append("            }")
        out.extend(optfrags)
    out.append("        }")
    return "\n".join(out)


def emit_sem_allow():
    names = [f["vn"] for f in QT_ALL] + [o["vn"] for o in QT_OPT]
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
    return "\n".join('                "%s",' % f["vn"] for f in QT_ALL)


def emit_sem_str():
    return "\n".join('                "%s",' % f["vn"] for f in QT_ALL if f["ret"] == "String")


def emit_main_decl():
    out = []
    for f in QT_ALL:
        ret = "vyb_file_str" if f["ret"] == "String" else "int64_t"
        out.append("    %s %s(%s);" % (ret, f["cn"], cpp_args(f)))
    for o in QT_OPT:
        out.append("    int64_t %s(%s);" % (o["cn"], cpp_opt_args(o, "vyb_file_str")))
    return "\n".join(out)


def emit_main_reg():
    out = []
    for f in QT_ALL:
        out.append('        runtimeSymbols[mangle("%s")] = llvm::orc::ExecutorSymbolDef(' % f["cn"])
        out.append("            llvm::orc::ExecutorAddr::fromPtr(&%s), llvm::JITSymbolFlags::Exported);" % f["cn"])
    for o in QT_OPT:
        out.append('        runtimeSymbols[mangle("%s")] = llvm::orc::ExecutorSymbolDef(' % o["cn"])
        out.append("            llvm::orc::ExecutorAddr::fromPtr(&%s), llvm::JITSymbolFlags::Exported);" % o["cn"])
    return "\n".join(out)


def emit_stub_funcs():
    return "\n".join(stub_body(f) for f in QT_FUNCS)


def emit_web_stub_funcs():
    return "\n".join(stub_body(f) for f in QT_WEB_FUNCS)


def emit_mod_enums():
    def enum(name, members):
        return ("share(all)\nenum %s {\n%s\n}" % (name, "\n".join("    %s = %d" % (m, v) for m, v in members)))
    return enum("QtEvent", QT_EVENTS) + "\n\n" + enum("QtWidgetKind", QT_WIDGETS)


def emit_mod_wrappers():
    out = []
    for f in QT_ALL:
        out.append("# %s" % f["doc"])
        out.append("share(all)")
        if "wrap_sig" in f:
            sig = f["wrap_sig"]
        else:
            sig = ", ".join("%s<%s>" % (n, t) for n, t in f["args"])
        if "opt_vn" in f:
            ret = "String?"
        else:
            ret = MIGRATION.get(f["mod"], f["ret"]).split("@")[0] if f["mod"] in MIGRATION else f["ret"]
        out.append("%s(%s)<%s> -> {" % (f["mod"], sig, ret))
        if "opt_vn" in f:
            out.append("    return %s(%s)" % (f["opt_vn"], ", ".join(n for n, _ in f["args"])))
        elif f["mod"] in MIGRATION:
            out.append("    " + migrate_body(f))
        elif "wrap_body" in f:
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
    ("runtime/vyb_qt_stub.cpp", "cpp", "web_stub", emit_web_stub_funcs),
    ("runtime/vyb_qt_stub.cpp", "cpp", "opt_stub", emit_opt_stub_funcs),
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
