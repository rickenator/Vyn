# HTTP/HTTPS Client & Server Capability Boundaries

Authoritative scope for the `http` (`stdlib/http/mod.vyb`), `https`
(`stdlib/https/mod.vyb`), and `tls` (`stdlib/tls/mod.vyb`) stdlib modules.
These modules are **minimal by design**: real, tested HTTP/1.x composition over
the `network` + `tls` primitives, but deliberately not a general-purpose modern
web stack. This document is the explicit boundary so callers know what is
supported, what fails, and what is deliberately absent (so unsupported behavior
is a *known* limitation, not an accident).

Related tracking: issue #159 (this capability boundary). The network error
contracts these modules build on are specified in
[`NETWORK_ERROR_CONTRACTS.md`](NETWORK_ERROR_CONTRACTS.md).

---

## 1. Protocol versions

| Aspect | Supported |
|---|---|
| Client request line | `HTTP/1.1` from the full client (`http_get_full`, `https_get_full`, verified variant); `HTTP/1.0` from the `http_request(method, path, host)` string helper. |
| Server response line | `HTTP/1.1` (`http_response`). |
| Response parsing | Any `HTTP/x.y <code> <reason>` status line via `http_status_code` (the minor version is not validated — a 1.0 or 1.1 server both parse). |
| TLS | TLS 1.x negotiated by OpenSSL through the `tls` module; on the wire the HTTPS client speaks `HTTP/1.1` exactly like the plain client. |

## 2. Request methods

- **Client**: **GET only.** Both `http_get_*` and `https_get_*` build a `GET`
  request and send no body. There is **no POST / PUT / DELETE / PATCH**
  convenience and no request-body support. A caller wanting another method must
  hand-assemble the request head with `http_request` (or raw sockets) — the
  framing/parse helpers (all `http_*` parse functions, `http_read_*`,
  `HttpsResponse` parsing) still work on the response end.
- **Server**: answers an arbitrary request head with `200` echoing the path
  (`http_serve_conn`). It does not dispatch on method or route; `http_response`
  knows the `200`/`404`/`500` reason phrases and falls back to a generic reason
  for other codes.

## 3. Request & response framing

- **Requests** always carry `Host:` and `Connection: close`.
- **Response bodies** are framed three ways, read in this order by
  `http_get_full`/`https_get_full`:
  1. `Transfer-Encoding: chunked` → decoded by `http_read_chunked`
     (`stdlib/http/mod.vyb:404`; the HTTPS twin `tls_read_chunked`), draining
     hex size lines, data, trailing CRLF, the 0-terminator, and trailers.
  2. `Content-Length` → read exactly N bytes (`http_read_exact`).
  3. Neither → read until the peer closes (`http_read_all`, cap 65536).
- The **server** (`http_response`) always emits `Content-Length`; it never
  chunk-encodes.
- Framing is strictly sequential and single-bodied: no HTTP/1.1
  pipelining, no multi-part, no trailers-as-meaningful-metadata.

## 4. Explicitly NOT supported (non-goals / known boundaries)

These are **non-goals for this milestone** — documented so their absence is a
deliberate decision, not a surprise. Tracked as roadmap candidates, not
promised.

| Capability | Status | Behavior today |
|---|---|---|
| **Redirects** | Not followed (non-goal) | A `3xx` response is returned **as-is** with its status; the caller must inspect and re-issue. Never silently followed. |
| **Compression** | Not decoded (non-goal) | The client never sends `Accept-Encoding` and does **not** decode `Content-Encoding: gzip`/`deflate`. If a peer sends a compressed body, the raw bytes are returned verbatim; the caller must decode. |
| **Connection reuse / keep-alive** | Not supported (non-goal) | Every request opens a fresh socket and sends `Connection: close`. **No pooling**, no persistent connections, one request per connection. |
| **HTTP/2 (and HTTP/3)** | Not supported (non-goal) | HTTP/1.x only. |
| **URL parsing** | Minimal (boundary) | The client takes `host`, `port`, `path` as separate arguments — there is **no URL parser**, no percent-decoding, no query-string handling. The path (including any `?query`) is sent verbatim. `http_request_path` merely extracts the first space-delimited token of a request head. |
| **Timeouts** | None at the http/https layer (boundary) | `http_get_*`/`https_get_*` **block** until the peer responds or closes. The raw `network::socket_set_timeout(fd, ms)` exists and can be applied to a descriptor, but the http/https clients do not expose a timeout parameter — a hung peer can block the caller indefinitely. Caller responsibility if needed. |

## 5. Certificate verification (HTTPS)

`https` splits into two explicit client contexts — this is a **security
boundary** and callers must choose deliberately:

| Client | Context | Verification |
|---|---|---|
| `https_get_full` / `https_get` / `https_selfhost` | `tls_client_context()` | **Unverified** — no peer CA chain check. Correct for loopback / self-signed development only. **MITM-vulnerable by design** when used against a real host over the internet. |
| `https_get_full_verified` / `https_get_verified` / `https_selfhost_verified` | `tls_client_context_verified(ca_pem)` | Pins `ca_pem` (or uses the system default CA paths when `""`), performs the SNI + certificate hostname check. Intended for real hosts. |

There is **no runtime guard** warning when the unverified context is used
against a real host (noted in `FEATURE_STATUS.md`); a future
`--require-tls-verify` build flag is a candidate improvement.

## 6. Hostname resolution (unified across clients)

All three client shapes resolve a hostname to an IPv4 **before** connecting,
keeping the hostname for SNI / the `Host:` header (see `#188`):

- `http://http_get_full` — `stdlib/http/mod.vyb:430`
- `https://https_get_full_verified` — `stdlib/https/mod.vyb:195`
- `https://https_get_full` (unverified) — `stdlib/https/mod.vyb:184` (fixed in `#188`)

If resolution fails, the client returns an **absent** optional (see §7) rather
than attempting a bogus raw-hostname connect.

## 7. Error model (explicit, not accidental)

- **Absent `T?` = "the round-trip could not be completed."** `http_get_full` →
  `HttpResponse?`, `https_get_full` → `HttpResponse?`, `http_get`/`https_get` →
  `String?`, all **absent on failure** (resolve failure, connect failure, TLS
  handshake failure, truncated/ unparseable response). Callers unwrap with
  `match`/`else`; they must treat absence as a transport-level failure, not a
  "no body" — an empty body is a *present* `""`.
- **`HttpResponse.status == -1`** = the status line did not parse
  (`http_status_code` returns `-1`). `reason` may be `""` for non-standard
  status codes.
- **Escalation** to the fail/trap framework is explicit and typed:
  - `http::http_error(op, target)` → `HttpError` (`stdlib/http/mod.vyb:266`)
  - `http::http_error(op, target)` message is a fixed `"http <op> failed"`
    (http is pure Vyb; the low-level reason lives at the network layer).
  - `network::net_error(op, target)` → `NetError` captures the real last
    `socket_error_message()` (`stdlib/network/mod.vyb:177`).
  - `tls::TlsError` + variants for the TLS layer.
- Every helper is already `@expect: pass`-tested through the fail/trap path
  (`test/modules/test_http_error_trap.vyb`, `test_net_error_trap.vyb`,
  `test/tls/test_tls_error_trap.vyb`), so an unsupported/ failed operation is a
  **typed, catchable error**, never an unhandled runtime abort.

## 8. Interop test matrix (issue #159 acceptance criterion 2)

| Behavior | Test |
|---|---|
| TCP loopback echo (raw socket) | `test/modules/test_network_socket.vyb` |
| IPv6 + resolve + timeout | `test/modules/test_network_ipv6.vyb`, `test_network_resolve_ipv4.vyb`, `test_network_timeout.vyb` |
| UDP send/recv + last-peer probes | `test/modules/test_udp_socket.vyb` |
| HTTP client (Content-Length body, header lookup) | `test/modules/test_http_client.vyb` |
| HTTP server end-to-end (status + body) | `test/modules/test_http_server.vyb` |
| Threaded concurrent server | `test/modules/test_http_threaded.vyb` |
| HTTP string helpers / parsing | `test/modules/test_http_parse.vyb` |
| **Chunked transfer-encoding decode** | `test/modules/test_http_chunked.vyb` |
| HTTPS unverified client **resolves hostnames** | `test/tls/test_https_client.vyb` (`localhost`, regression for `#188`) |
| HTTPS verified client (pinned CA + hostname) | `test/tls/test_https_verified.vyb` |
| TLS handshake/echo loopback | `test/tls/test_tls_loopback.vyb` |

## 9. Decisions recorded

- **HTTP/2, connection pooling, automatic redirects, automatic decompression
  → non-goals** for this milestone; candidates for the roadmap if VybOS
  URL-driven realization (the `#188` driver) needs them.
- **GET-only client** and **echo server** accepted for now; a request-method /
  routing module is future work if needed.
- The three client shapes now **agree on hostname resolution** (`#188`);
  there is no longer an IP-only outlier.
