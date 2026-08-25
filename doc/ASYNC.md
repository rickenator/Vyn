# Async runtime: context mechanism, portability, and resource contract

Applies to the async executor in `runtime/vyb_runtime.c` (the ucontext-fiber engine
that powers `async for`, `async_spawn`/`await`, `sleep_ms`, timers, and the I/O
pump). Issue #155.

## Context mechanism (decision)

**ucontext fibers are retained and intentionally scoped to POSIX.** The executor
uses `makecontext` / `swapcontext` (`<ucontext.h>`) to run each async task on a
private 1 MiB stack multiplexed across a small pool of worker threads.

Why this is the right call:

- **The stackful model is the language feature.** Because a Vyb function runs on a
  real fiber stack, `async for (x in ch)` drains a channel linearly and `await`
  suspends *mid-body of an ordinary Vyb function* — no compiler state-machine /
  coroutine transform is required. Replacing ucontext would swap one context-switch
  primitive for another (e.g. libaco / boost.context) or force that transform, with
  no portability payoff: Vyb targets Linux/POSIX first anyway.
- **The real costs are bounded, not structural.** (1) Each task pays
  `VYB_ASYNC_STACK_SIZE` of virtual memory — configurable at startup (see below).
  (2) A fiber must run on the worker that first launched it; the engine honors this
  by *building each fiber's context lazily on its home worker* (`async_make_context`),
  so a context is never fabricated on one thread and launched on another.
- **It is not the ASan liability it once looked like.** The earlier "@skip-asan:
  ucontext-fiber ASan false-positive" on the I/O pump tests turned out to be a real
  heap use-after-free (`async_pump_main` freed an iowait entry then read it), fixed
  in #155. The fiber engine itself is ASan-clean.

### Supported platforms
- **Linux** with glibc **or** musl — primary target; both ship a functional
  `<ucontext.h>`.
- **macOS / BSD** ucontext implementations — same API; not CI-enforced.
- **Windows** is out of scope for this mechanism (Vyb's async surface is
  POSIX-oriented).

### Failure behavior
- Task spawn (`__vyb_async_spawn`) returns `0` on allocation failure; the Vyb
  `async_spawn` maps that to an absent `?`. No abort, no silent truncation.
- If the worker pool could not be spawned, spawn returns `0` as well.
- String-registry exhaustion is the one deterministic *fatal*: when `VYB_STR_REG_CAP`
  is set and the registry is full, `vyb_str_reg_exhausted()` prints a clear message
  and aborts (test #162 depends on this). With the cap unset the registry grows
  without bound (#189).

## Configurable resource limits

Set as environment variables at process start:

| Variable | Default | Range | Effect |
|---|---|---|---|
| `VYB_ASYNC_STACK_SIZE` | `1048576` (1 MiB) | `65536` … `536870912` (64 KiB … 512 MiB) | Per-fiber stack bytes |
| `VYB_WORKER_MAX` | `64` | `1` … `64` | Ceiling on worker threads spawned (also capped by CPU count) |
| `VYB_STR_REG_CAP` | unset (growable) | ≥4 | Hard max string-registry slots; deterministic exhaustion when set (#162) |

Out-of-range values are clamped to the range (not rejected). Invalid/non-numeric
values fall back to the default.

## Fixed-cap table contract (graceful exhaustion)

Every fixed-size table fails **gracefully** — it returns a sentinel the Vyb stdlib
surfaces as a failure value, never UB or silent truncation — except the string
registry's documented fatal-abort path above.

| Table | Cap | On full | Vyb surface |
|---|---|---|---|
| Thread table | `VYB_THREAD_CAP` 256 | `-1` | `thread_spawn` → absent `Int?`; slots reclaimed on `join`/`detach` |
| Agent table | `VYB_AGENT_CAP` 64 | `0` | `agent_start` → `0`; slots reclaimed on `agent_free` |
| Worker pool | `VYB_WORKER_MAX` 64 (config) | pool is fixed/multiplexed — tasks don't consume worker slots; spawn fails only on alloc | `async_spawn` → absent `?` |
| String registry | growable / `VYB_STR_REG_CAP` | grows to cap then `vyb_str_reg_exhausted()` (fatal, #162) | documented fatal |

## Stress-test coverage
- `test/async/async_stress_tasks.vyb` — bulk task creation + cleanup (LSan-clean:
  every fiber stack and task struct reclaimed).
- `test/async/async_cap_limits.vyb` — runs with `VYB_WORKER_MAX=2` and a reduced
  `VYB_ASYNC_STACK_SIZE`, exercising the configurable-limit path and single/limited
  worker correctness.
- Agent table exhaustion is exercised by a dedicated cap test asserting the `0`
  failure sentinel rather than an abort.
