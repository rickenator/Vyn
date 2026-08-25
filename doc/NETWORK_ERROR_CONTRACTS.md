# Network Error Contracts

Authoritative error-model specification for the `network` stdlib module
(`stdlib/network/mod.vyb`), where every fallible operation follows the
engine-wide native-optional (`T?`) shape. These contracts are what the
`http`/`https`/`tls` modules (see
[`HTTP_CAPABILITY_BOUNDARIES.md`](HTTP_CAPABILITY_BOUNDARIES.md)) build on.

Tracking: issue #167.

---

## 1. The core rule

> **Absence is failure.** Every operation that can fail returns a native
> optional (`T?`): present = the operation **and its value** succeeded, absent
> (`?`) = the operation failed. **No sentinel** `fd` / `-1` / `""` ever crosses
> the public surface as a "not really an error" value.

Consequences:

- Callers unwrap with `match`/`else`. The `?` arm is the failure arm.
- Present-empty vs absent is meaningful and lossless:
  - `socket_recv` returns the present **`""`** at end-of-stream and absent on a
    transport error — so "peer closed" and "I/O failed" are distinct.
  - `udp_recv_from` / `UdpSocketOps::recv_from` return present `""` for a
    legitimate empty datagram and absent on error — an empty datagram is never
    conflated with a failed receive.
- Diagnostics are available from the raw-layer last result: `socket_error_code()`
  (`0` = no error) and `socket_error_message()`.

## 2. Raw socket helpers (`socket_*`)

All cross `__vyb_net_*` in `runtime/vyb_runtime.c`; the Vyb surface stays
allocation/pointer-free. IP addresses are strings: dotted-quad IPv4 or
colon-hex IPv6 literal.

| Function | Returns | Failure = absent | Notes |
|---|---|---|---|
| `socket_open(domain, type, proto)` | `Int?` | absent (no fd) | descriptor ≥ 0 when present. Use `Socket::*` constants. |
| `socket_accept(fd)` | `Int?` | absent | new connected descriptor when present. |
| `socket_local_port(fd)` | `Int?` | absent | bound port; read `0` back after an ephemeral bind. |
| `socket_close(fd)` | `Bool?` | absent | present on success. |
| `socket_bind(fd, ip, port)` | `Bool?` | absent | `"0.0.0.0"`/`""` = any interface. |
| `socket_listen(fd, backlog)` | `Bool?` | absent | |
| `socket_connect(fd, ip, port)` | `Bool?` | absent — **does not resolve** | connect target must already be an IP literal. |
| `socket_set_timeout(fd, ms)` | `Bool?` | absent (`-1` descriptor reported, not ignored) | `ms ≤ 0` disables. |
| `socket_send(fd, data)` | `Int?` | absent | present = bytes written. |
| `socket_recv(fd, maxlen)` | `String?` | absent (transport error) | present `""` at EOF (see §1). |
| `socket_resolve(host)` | `String?` | absent (unresolvable) | hostname → IPv4 (or IPv6) literal string. |

## 3. Wrapper types (`TcpStream` / `TcpListener` / `UdpSocket`)

Acquisition wraps the descriptor **inside** the value, so callers never see a
sentinel fd:

| Function | Returns | Failure = absent |
|---|---|---|
| `tcp_connect(ip, port)` | `TcpStream?` | absent (open/connect failure) |
| `tcp_listen(ip, port, backlog)` | `TcpListener?` | absent |
| `tcp_accept(listener)` | `TcpStream?` | absent (blocking accept) |
| `udp_bind(ip, port)` | `UdpSocket?` | absent |

Bound methods (via `TcpStreamOps` / `TcpListenerOps` / `UdpSocketOps`):

| Method | Returns | Failure = absent |
|---|---|---|
| `.write(data)` | `Int?` | absent — present = bytes written |
| `.read(max)` | `String?` | absent (error); present `""` at EOF |
| `.send_to(ip, port, data)` | `Int?` | absent |
| `.recv_from(max)` | `String?` | absent (error); present `""` for empty datagram |
| `.close()` | `Bool?` | absent |
| `.peer_ip()` / `.peer_port()` | `String` / `Int` | non-fallible (metadata) |
| `.local_ip()` / `.local_port()` | `String` / `Int` | non-fallible (metadata) |

Standalone datagram helpers `udp_send_to`, `udp_recv_from`, `udp_close` mirror
the bound methods exactly.

## 4. UDP peer error contract (last-peer probes)

The sender of a received datagram is exposed through process-wide probes that
follow the same lossless rule:

- `udp_last_peer_ip()` → `String?` — **present only after** a datagram has been
  received. A real IPv4/IPv6 address is never the empty string, so absence
  unambiguously means "no datagram yet" — stale/zero peer info is never
  mistaken for a real sender.
- `udp_last_peer_port()` → `Int?` — present after a datagram, absent before.

## 5. Async I/O error contract (event-loop executor)

The `async_*` variants suspend the calling fiber on the event loop instead of
blocking a worker; they must be called from an `async fn()` (seeded via
`asyncs::async_spawn`). **The `T?` shape is identical** to the blocking
counterparts, so failure handling does not change:

| Function | Returns | Failure = absent |
|---|---|---|
| `async_tcp_accept(listener)` | `TcpStream?` | absent |
| `async_tcp_connect(ip, port)` | `TcpStream?` | absent |
| `async_tcp_write(stream, data)` | `Int?` | absent — present = bytes written |
| `async_tcp_read(stream, max)` | `String?` | absent (error); present `""` at close |
| `async_udp_send_to(sock, ip, port, data)` | `Int?` | absent |
| `async_udp_recv_from(sock, max)` | `String?` | absent (error); present `""` for empty datagram |

Note the `#155` tracking: the async runtime is built on ucontext fibers with
fixed resource caps; these contracts hold, but the underlying runtime is
separately scoped there.

## 6. Escalation into fail/trap (typed errors)

Because absence carries no reason string, escalate with
`network::net_error(op, target)` → `NetError`:

```vyb
struct NetError { operation<String>, target<String>, message<String> }
net_error(operation, target) -> NetError   # message = socket_error_message()
```

Typical pattern:

```vyb
match (tcp_connect(host, port)) {
    s -> { body(s) }
    ? -> { fail net_error("connect", host) }
}
```

`NetError` is shared across modules and caught by a cross-module `trap`
(`test/modules/test_net_error_trap.vyb`).

## 7. Contract enforcement tests

| Contract | Test |
|---|---|
| Raw socket `T?` shape + loopback echo | `test/modules/test_network_socket.vyb` |
| IPv6 + resolve + timeout | `test/modules/test_network_ipv6.vyb`, `test_network_resolve_ipv4.vyb`, `test_network_timeout.vyb` |
| UDP lossless `String?` + last-peer probes | `test/modules/test_udp_socket.vyb` |
| `NetError` cross-module trap | `test/modules/test_net_error_trap.vyb` |
| Async wrappers (`async_tcp_*` / `async_udp_*`) | `test/async/async_net_wrappers.vyb` |
