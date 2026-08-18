# VybLynx curses TUI — status & engineering notes

Milestone-2 shell of the VybLynx full-screen terminal browser (see `RFE.md`
§14/§34/§37/§40). The terminal UI is driven **entirely** through the stdlib
`curses` module; the browser never touches raw escape sequences or libc
terminal calls.

## Run it

A real terminal (tty) is required — `curses_init` refuses to `initscr` on a
redirected stdout, and lets the program exit cleanly with a message instead.

```
cd Vyb
cd demos/VybLynx
# from the repo root: point VYB_STDLIB at the stdlib dir and run the source
VYB_STDLIB=stdlib ../../build/vyb src/main.vyb            # opens http://example.org/
VYB_LYNX_HOME=http://example.com ../../build/vyb src/main.vyb
```

### Controls
- `<n>` — follow the numbered link (single digit 1..9)
- `n` / `space` — next page · `p` — previous page
- `b` — back · `r` — reload
- `g` — open a URL (prompt on the real terminal; see note below)
- `h` — help · `q` — quit

## What is wired
- **Single screen owner.** Only the UI loop mutates ncurses (`clear`, header,
  body, status, `refresh`). Network/content work only publish state (RFE §40).
- **Timed, non-blocking input.** `curses_timeout(80)` + `getch` polls, so the
  loop is responsive and ready for a future async/select event model (RFE §38).
- **Sanitize boundary.** Every text add passes through `sanitize()`, which
  drops control bytes (and `0x7f`) from remote content so a hostile page cannot
  inject terminal control sequences (RFE §46 checklist).
- **Navigation is structured.** `fetch_page` `fail`s with the HTTP status and
  `navigate` `trap`s it, so one bad page becomes an error page, never a crash
  (RFE §20/§21/§35).
- **`T?` models absence** for "no such link" and the cancelled URL prompt.
- **`ensure` guards the render invariant** (non-zero terminal size).
- **Aspect/bind.** `Display` is bound to `Url`; an `Interactive` aspect is bound
  to the rendered `Nav` links (RFE §3/§4).
- **Ownership.** The event loop owns one `BrowserState` (current `Page`,
  history, cursor, status). `load_url`/`draw`/`follow_page` take `their<...>`
  borrows so one owner holds the data (RFE §13/§41); assigning an owned
  `Page`/`String` into a struct field deep-copies/retains the source.
- **Multi-value returns.** `tty_dims()` returns a pair of `Int`s that is
  destructured in `content_width()` and in the event loop (RFE §22).
- **asyncs / Future.** A warm-up fiber is started as a `Future` and awaited at
  launch (RFE §10/§11). The page fetch itself is still synchronous — making it a
  `Future` (so a slow network never blocks the UI) is the next milestone.

## Language-feature → code map
| Feature | Where |
|---|---|
| `struct` | `Nav`, `Page`, `Response`, `BrowserState` |
| `aspect` / `bind` | `Display->Url`, `Interactive->Nav` |
| generic bounded fn | `show<T<Display>>`, `link_line<T<Interactive>>` |
| `T?` + `else` | `follow_page`, `open_url_prompt`, command handling |
| `ensure` | `content_width()` terminal-size invariant |
| `fail` / `trap` | `fetch_page` -> `navigate` boundary |
| collections | `Vec` history stack, page line buffer, navs |
| ownership (`their`/`borrow`) | load_url/draw/follow_page |
| multi-value returns | `tty_dims()` + nested destructuring |
| asyncs / Future | `warm_up`, `await` |
| curses (stdlib) | entire terminal layer |
| sanitize | control-byte scrubbing (RFE security) |

## Compiler bugs this demo surfaced — both fixed
This demo exercised two compiler/JIT defects that are no longer present. They
are recorded here so the fixes are traceable.

1. **JIT double free on owned-struct-field assignment.**
   Keeping a `build_page`-produced `Page` inside a `BrowserState` that is torn
   down at function return crashed with
   `free(): double free detected in tcache 2` (SIGABRT). Root cause: a
   whole-struct store (`st.page = pg`) shallow-copied the owned pointers, so the
   destination field and the producing local both reclaimed the same
   `Vec<String>` buffer on scope exit. Fixed in `cgen_expr.cpp`: assigning a
   struct destination with owned fields now deep-copies a borrowed source and
   reclaims the outgoing value; member `String` overwrites (through a
   `their<T>` borrow) retain the source so a scope-exit release cannot leave the
   field dangling. The demo now owns the page in `BrowserState` without a crash.

2. **Multi-value return destructuring could fail codegen.**
   `a<Int>, b<Int>; a, b = f()` inside a larger module raised
   `Instruction does not dominate all uses!`. Root cause: the destructure
   visitor registered `tuple_destruct_*` allocas in a function-wide map that was
   never cleared, so a later function reused an earlier function's alloca.
   Fixed in `cgen_stmt.cpp`: destructure allocas are now created fresh in each
   function's entry block. The demo uses `tty_dims()` multi-value returns
   unconditionally.

## Next milestones (per RFE §45)
- Make the page fetch a `Future` and add a timed/select event loop so a slow
  load never blocks the UI (RFE §8/§38).
- Route through the stdlib `http`/`https` modules (`ResourceProvider` aspect,
  RFE §3/§6/§44) instead of the raw-socket path.
- Downloads via a `DownloadAgent` + file I/O + progress (RFE §23-§29).
- Resize handling (`curses_resize` shim) and window-based layout (±RFE §14).
