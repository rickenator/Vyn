# Agents — Design

**Status:** Draft (design-first, per the `[DECIDED]` note in `TODO.md`; channels
are shipped and this sits on top of them). Not yet implemented.

An **agent** is Vyb's lightweight, isolated message-passing unit. It is the
composition of the concurrency pieces already shipped — the cooperative
`asyncs` executor, `spawn`, and typed `chan<T>`/`strchan` channels — into a
single handle that owns its behavior, its state, and its mailbox.

---

## 1. Why agents, given channels + spawn exist

`chan<T>` gives a thread-safe buffered mailbox. `async_spawn` / `task_spawn`
give you a running unit of work. Neither alone is a *component*: a channel has
no associated behavior and no lifecycle; a spawned task has no mailbox and no
identity you can hand around and message later.

An agent ties the two together, and adds the properties that make message
passing safe and ergonomic:

- **Isolation** — the agent owns its internal state exclusively. No handle can
  reach into it; the only way to interact is to post a message. This is the
  concurrency face of Vyb's ownership story (`my`/`their`/`our`): an agent is a
  `my`-owned unit with a `their`-visible mailbox.
- **Identity** — a handle you can store, pass to other tasks/agents, send to,
  ask about, and stop. Multiple producers can message one agent without
  coordinating with each other.
- **Lifecycle** — started, running, draining, stopped, reaped. Shutdown is
  explicit and cooperatively observed, so no messages are silently dropped on
  a live agent and no resources leak on shutdown.

### Non-goals (v1)

- No dataflow/static task graphs (that's a pipeline library).
- No distributed agents / transparent RPC.
- No supervision trees or automatic restart (a possible case study after the
  core shape is proven; error *channeling* is in scope, restart isn't).
- Not a replacement for `threads`/`tasks` (parallel CPU workers) or for raw
  channels (single-purpose buffering). Agents are the component layer.

---

## 2. The model

```
                 ┌────────────────────────────────────────────┐
   producer ───► │  agent<M> handle                            │
                 │   ┌───────────────┐   enqueue               │
   producer ───► │   │ mailbox chan<M>│ ─────────────────┐      │
                 │   └───────────────┘                   ▼      │
                 │                            ┌────────────────┐ │
                 │                            │ behavior task   │ │
                 │                            │ (async fiber)   │ │
                 │                            │  loop {         │ │
                 │                            │   m = recv()?   │ │
                 │                            │   handle(m)     │ │
                 │                            │   change state  │ │
                 │                            │   send to others│ │
                 │                            │ }               │ │
                 │                            └────────────────┘ │
                 │          state<...>  (owned by the agent)     │
                 └────────────────────────────────────────────┘
```

### 2.1 The handle: built-in generic `agent<M>`

`agent<M>` is a built-in generic type, modelled on `chan<T>` (cgen_types.cpp
maps `chan<T>` to a 64-bit runtime handle). `agent<M>` maps to the same
64-bit handle shape, so an agent handle is a first-class, copyable value that
crosses task/thread/agent boundaries freely. `M` is the message type the agent
accepts (the mailbox payload type).

### 2.2 The mailbox

Each agent owns one mailbox — an unbounded `chan<M>` by default, or a bounded
`chan<M>(cap)` when the agent wants explicit backpressure. The mailbox reuses
the existing `chan<T>` runtime (int-slot `__vyb_chan_*` / refcounted string
`__vyb_strchan_*`) plus the generic method dispatch. `chan<M>` already supports
send / recv / try / len / close / select; the agent layer consumes those.

### 2.3 The behavior task

The agent's behavior is a closure that runs as an async task (a stackful fiber
on the cooperative `asyncs` executor). Its body is handed the mailbox and any
initial state:

```
fn(mailbox<chan<M>>, state<State>) -> Void
```

The behavior loop is:

```
while (msg = mailbox.recv_opt()) {
    handle(msg)      // may mutate state, send to other agents/self
}
# mailbox closed and drained -> exit, agent reaps
```

`recv_opt` returns the native `M?` (present value / absent on closed+drained),
so agents use the same lossless drain as `async for` and need no reserved
sentinel value. Because fibers are stackful, the behavior can itself `await`
other async work (child tasks, async I/O, other agents' replies) mid-body
without a state-machine transform — the same property the `asyncs` module
documents.

### 2.4 State isolation

State lives inside the closure environment and is snapshotted/closed over by
the behavior (mirroring the async worker env: String buffers retained, `our`/
`mild` control-block refs bumped, `Vec`/struct params deep-copied). The agent
is the sole observer of that state; nothing else holds an alias to it. When the
agent reaps, the env is reclaimed by the existing env-dtor machinery — so
non-trivial owned state (a `my<Struct>`, a `Vec<String>`, a set of child agent
handles) is released exactly once.

---

## 3. Lifecycle

| State | Meaning |
|-------|---------|
| **Running** | Behavior task live; mailbox accepts sends. |
| **Draining** | Mailbox closed; behavior consumes what's buffered, then exits. |
| **Stopped** | Behavior returned; no more sends accepted. |
| **Reaped** | Resources (mailbox handle, closure env, table slot) reclaimed. |

Transitions:

- `Running → Stopped` — the behavior returns on its own *or* the mailbox is
  closed: once `recv_opt` reports absent, the loop exits and the agent stops.
- `Running → Draining → Stopped` — `agent_close(h)` closes the mailbox; the
  agent drains buffered messages (so no message is lost), then stops. Closing
  is the cooperative, lossless shutdown.
- `Stopped → Reaped` — `agent_free(h)` (or `agent_run_all`/atexit) reclaims.
  `agent_send` on a stopped/reaped handle returns 0. After a graceful close,
  senders observe discard without blocking or crashing.

The agent handle table reuses the channel-table pattern: fixed slots, a
refcount on the handle itself, and a state field so `agent_alive` /
`agent_status` can be queried cheaply.

---

## 4. API surface (proposed)

A `stdlib/agents` module provides the creator / lifecycle surface; the
`agent<M>` handle's own methods dispatch through the built-in generic path (as
`chan<T>.send()` etc. do today).

```
import agents

# --- creation ---------------------------------------------------------
# Start an agent running `behavior(mailbox, state)`. Returns a handle (0 on
# allocation failure or if behavior is null).
agent_start(behavior<fn(chan<M>, State) -> Void>, state<State>)<agent<M>>
agent_start(behavior<fn(chan<M>) -> Void>)<agent<M>>        # stateless form

# --- messaging ----------------------------------------------------------
# Post a message; 1 on accepted, 0 if the agent is stopped/closed (or a
# bounded mailbox is full). Non-blocking, like chan_send.
a.send(msg<M>)<Bool>

# -- queries --------------------------------------------------------------
a.len()<Int>         # buffered-but-unhandled messages (-1 on bad handle)
a.alive()<Bool>      # Running (or Draining); false once Stopped

# --- shutdown / cleanup --------------------------------------------------
agent_close(a)<Int>  # close mailbox; agent drains then stops (lossless)
agent_free(a)<Int>   # reclaim a stopped agent
```

Message ownership follows channels: a `String` (or `our`/`mild`/`my<Struct>`)
payload transfers its reference into the mailbox on send and hands it to the
behavior on recv — no shared mutable state crosses the boundary.

### Request/response (v1, no new machinery)

Agents who need an answer are sent a request carrying a *reply-to channel* —
the idiomatic Vyb shape, needing no special codegen:

```
struct Ping { reply_to<chan<Pong>> }
struct Pong { n<Int> }

pinger = agent_start(|mb| -> {
    while (true) {
        p = mb.recv()
        if (p.reply_to) { p.reply_to.send(Pong { n = p.n * 2 }) }
    }
})

ch = chan_new()
pinger.send(Ping { reply_to = ch })
pong = ch.recv()    # Pong { n = ... }
```

The reply channel is just a `chan<R>`; `select` over agent handles *is*
`chan_select` over their mailbox handles (each `agent<M>` exposes its raw
mailbox handle via the `chan` handle surface). Nothing new is required to
compose agents into fan-in/fan-out pipelines.

---

## 5. Runtime mapping

| Concern | Reuses |
|---------|--------|
| Mailbox buffering | `chan<T>` / `strchan` runtime (mutex + condvar ring buffer, bounded/unbounded) |
| Message dispatch | built-in generic method emitter (`emitChannelMethod` pattern) |
| Behavior scheduling | cooperative `asyncs` executor (`async_spawn`), stackful fibers |
| Behavior env (state + captures) | async worker env + env-dtor reclaim (String retain, `our`/`mild` bump, `Vec`/`struct` deep copy) |
| Handle identity / table | channel-table slot pattern (`{ mailbox handle, task, refcount, state, dtor }`) |
| Error transport | the just-shipped failable async transport (worker failure → recorded on the task → surfaced at `await`) |

### Agent failure & error channeling

If the behavior *fails*, the agent does not crash the process. The failure is
recorded on the agent (its state becomes an error state and is observable via
a `status`/`error` query), and the mailbox is closed so senders see 0 rather
than hang on a dead agent. A dead-letter channel is a stated v2 option: a
`supervisor` that forwards failed agents' errors (or an out-of-band
`dead_letters<chan<AgentFailure>>` channel per agent group). The transport
mechanism is the failable-async error path already built.

---

## 6. Open design questions

1. **Threading model** — v1 agents are *cooperative* (fibers on the `asyncs`
   executor): cheap, high concurrency, idiomatic with `await`. Blocking-agency
   (an agent whose behavior does CPU-bound or blocking work) is a stated
   follow-up that would pin the behavior to a `threads`/`tasks` worker instead.
   Keep the handle ABI identical so the backing is an implementation detail.
2. **Bounded mailboxes / backpressure** — mirror `chan_bounded(cap)`. A full
   bounded `send` returns 0 immediately (non-blocking); callers apply their own
   backpressure, consistent with channels. Whether a select-backed `send`
   (blocks until the agent drains) is worth adding is a case-by-case decision.
3. **Message breadth** — the payload suite (`Int` first, then `String`/
   `Bool`/`Float`, then `our`/`mild`/`my<Struct>`) mirrors the async-param
   staging that is already valgrind-clean, so Agent stages inherit tested
   ownership semantics rather than inventing new ones.
4. **Supervision** — explicit stop/close is v1. Automatic restart is explicitly
   out; keep the table shape amenable so a supervisor is a plain userland agent
   that owns child handles and respawns on failure reports.

---

## 7. Proposed implementation stages

Mirror the `test/async/` staging discipline (each stage ships with a
valgrind-clean test under `test/agents/`).

- **Stage 1 — Core shape.** `agent<M>` handle type in codegen; runtime table
  `{ mailbox chan, task id, refcount, state }`; `agent_start` spawning an async
  task bound to a fresh mailbox; `agent_send`; `agent_alive`/`agent_len`;
  `agent_close` (drain-then-stop) and `agent_free`. Int payloads first.
  `test/agents/test_agent_basic.vyb`.
- **Stage 2 — Payload breadth.** `String` (refcounted transfer), `Bool`/`Float`,
  then `our`/`mild`/`my<Struct>` messages, reusing the async-param env
  machinery. `test/agents/test_agent_payloads.vyb`, valgrind-clean.
- **Stage 3 — Request/response + composition.** reply-to channels, `select`
  across agent mailboxes, pipelines. `test/agents/test_agent_request_reply.vyb`.
- **Stage 4 — Failure channeling.** behavior `fail`/`panic` handled gracefully,
  `error`/`status` queries, dead-letter channel. Reuses the failable-async
  transport. `test/agents/test_agent_failure.vyb`.
- **Stage 5 — Backpressure + bounded mailboxes.** bounded `agent_start`,
  non-blocking full-`send` semantics. `test/agents/test_agent_bounded.vyb`.

---

## 8. Docs integration

Once implemented, `stdlib/agents/mod.vyb` doc comments feed the auto-generated
`docs/refman/agents.md` (source scope already lists single-file modules). This
design doc is the authority for the model; the refman is the generated surface
reference. Both stay in sync with `TODO.md`'s `[DECIDED]` section.
