# VybLynx Feature-Integration Requirements

VybLynx is not only a terminal web browser.

It is also intended to be a **flagship Vyb systems-programming demonstration**.

The implementation must therefore exercise Vyb's distinctive language/runtime capabilities wherever they are technically appropriate.

Do not write the browser as though Vyb were merely C, Rust, Go, or Python with different syntax.

The implementation should deliberately showcase Vyb.

The following capabilities are divided into:

* **MANDATORY**
* **GOOD PRACTICE**
* **LOW PRIORITY / OPTIONAL**

Mandatory items must appear in actual browser architecture, not merely in contrived demonstration code.

---

# 1. Mandatory feature matrix

The finished VybLynx implementation must make meaningful use of:

* polymorphic `aspect`
* `bind`
* `struct`
* HTTP module
* HTTPS module
* advanced collections
* ncurses terminal module
* Agents
* `ensure`
* `fail`
* `trap`
* `asyncs` module
* Futures
* `T?`
* Ownership types
* file downloads

The implementation must also use multi-value returns where they naturally simplify APIs.

The following are strongly encouraged:

* async
* await
* threads
* channels
* `select`

The following are low-priority demonstrations:

* variadic tuples
* decomposition into bare inference variables
* `freedom`
* external image viewer invocation

Do not force low-priority features into places where they make the code worse.

---

# 2. Architectural principle

Whenever there are two reasonable implementations:

A. conventional procedural implementation

B. implementation that cleanly demonstrates a native Vyb facility

prefer **B**.

However, do not create gratuitous complexity merely to check feature boxes.

Every mandatory feature must have a defensible architectural purpose.

---

# 3. Polymorphic aspect / bind / struct — MANDATORY

This browser must make substantial use of Vyb's polymorphic `aspect`, `bind`, and `struct` capabilities.

These should form part of the core architecture rather than being isolated in a toy example.

Use them to model browser subsystems with shared behavior.

Good candidates include:

* resource providers
* renderable nodes
* interactive document elements
* browser commands
* protocol handlers
* content handlers
* download targets
* UI widgets
* asynchronous jobs

For example, establish an abstraction conceptually similar to:

```
aspect ResourceProvider {
    fetch(...)
}
```

with implementations corresponding to:

```
HttpProvider
HttpsProvider
FileProvider
```

Use whatever actual Vyb syntax and semantics are correct.

Likewise, interactive elements may share an aspect such as:

```
aspect Interactive {
    activate(...)
    describe(...)
}
```

with implementations such as:

```
Link
FormControl
DownloadLink
ImageAnchor
```

The exact design must follow actual Vyb language capabilities.

Do not imitate interfaces or traits from another language if `aspect` works differently.

Inspect existing Vyb examples first.

---

# 4. bind usage — MANDATORY

Use `bind` wherever Vyb intends it to associate behavior, implementations, types, or runtime relationships.

Potential uses include:

* binding protocol-specific fetching behavior
* binding render behavior to node categories
* binding input commands to handlers
* binding content handlers to MIME types
* binding UI behavior to browser modes

The goal is to demonstrate Vyb's actual dispatch/composition model.

Do not reduce all dispatch to giant switch statements if `aspect` + `bind` provides the intended language-level abstraction.

---

# 5. Struct usage — MANDATORY

Core browser state should use Vyb structs extensively.

Expected structured data includes at least:

```
BrowserState
URL
Resource
ResponseMetadata
Document
Node
Element
TextNode
RenderedDocument
RenderLine
RenderSpan
Link
Anchor
HistoryEntry
Download
BrowserMessage
SearchState
```

Use strongly modeled data instead of loosely related arrays or global variables.

---

# 6. HTTP and HTTPS modules — MANDATORY

The browser must directly exercise Vyb's existing:

* HTTP module
* HTTPS module

Do not use libcurl or shell commands for ordinary fetching.

The resource layer should route:

```
http://
```

through Vyb HTTP support and:

```
https://
```

through Vyb HTTPS support.

These paths should be independently testable.

The browser should demonstrate that Vyb's own networking stack is sufficient for a real application.

---

# 7. Advanced collections — MANDATORY

Use Vyb's advanced collection types meaningfully.

Do not implement everything with primitive flat arrays unless that is the only available option.

Potential applications:

* history stack
* forward stack
* DOM children
* attribute maps
* visited URL set
* active downloads
* MIME handler map
* anchor lookup
* link lookup
* browser cache
* event queues
* pending futures
* agent registry
* command dispatch tables

Good examples include conceptually:

```
Map<String, String> attributes
Map<String, Anchor> anchors
Set<URL> visited
Queue<Event> events
Deque<HistoryEntry> history
```

Use the actual Vyb collection library names.

The implementation should demonstrate:

* insertion
* lookup
* iteration
* deletion
* membership
* bounded storage where appropriate

---

# 8. Async execution model

VybLynx must not freeze the terminal interface during slow network or file activity.

Architecture should therefore separate:

```
UI/event processing
```

from:

```
network fetching
downloads
DNS/TLS waits
optional parsing/layout jobs
```

The browser should remain responsive while network operations are underway whenever Vyb's runtime supports this reasonably.

---

# 9. async / await — GOOD PRACTICE

Use native Vyb `async` / `await` where appropriate.

Excellent candidates:

```
fetchPage()
fetchHttp()
fetchHttps()
startDownload()
resolveResource()
asynchronous file writes
```

Conceptually:

```
future = fetchPage(url)
```

and later:

```
resource = await future
```

Do not invent callback pyramids when Vyb supports structured async programming.

---

# 10. asyncs module — MANDATORY

The `asyncs` module must be used directly and meaningfully.

First inspect its actual API.

Do not guess.

Use it for one or more browser-level responsibilities such as:

* spawning asynchronous resource loads
* coordinating downloads
* background tasks
* asynchronous parsing
* status updates
* request cancellation
* asynchronous job groups

The browser should serve as an example of how `asyncs` is intended to be used in a substantial application.

Document the usage in VybLynx developer documentation.

---

# 11. Futures — MANDATORY

VybLynx must use Vyb Futures.

Network fetches are the obvious primary use.

Conceptually:

```
Future<Resource>
```

or the actual Vyb equivalent.

A page-navigation request should be able to exist as an explicit pending operation.

The browser state may contain something conceptually similar to:

```
pendingNavigation: Future<Resource>?
```

Downloads should likewise use futures.

Potential design:

```
Future<DownloadResult>
```

The UI must be able to:

* start future
* detect pending state
* obtain completion
* handle error completion
* display status

Do not immediately await every future at the point it is created, as that would reduce it to synchronous behavior.

Use futures to model genuinely outstanding operations.

---

# 12. `T?` — MANDATORY

Use Vyb's optional / nullable / maybe type facility `T?` throughout APIs where absence is legitimate.

Examples include:

```
String? title
URL? redirectTarget
String? contentType
String? charset
Link? selectedLink
HistoryEntry? previous
BrowserError? error
FormControl? focusedControl
Download? selectedDownload
Node? parent
```

Do not use magic sentinel strings such as:

```
""
```

or magic integers such as:

```
-1
```

when `T?` expresses the semantics more accurately.

This browser should provide strong examples of idiomatic optional-state handling.

---

# 13. Ownership types — MANDATORY

The browser must deliberately use Vyb ownership types.

This is particularly important at subsystem boundaries.

Analyze ownership for:

* ncurses screen
* ncurses windows
* native pointer wrappers
* downloaded buffers
* parsed documents
* rendered documents
* page cache entries
* HTTP response bodies
* futures
* agent-owned state
* file handles
* download streams

Avoid unnecessary copying of large resources.

A navigation lifecycle should have explicit ownership semantics.

For example:

```
fetch resource
    ->
transfer response body ownership
    ->
parser
    ->
document
    ->
renderer
    ->
browser page state
```

Do not blindly clone buffers merely to avoid reasoning about ownership.

Use Vyb's ownership system as intended.

Document any places where native FFI prevents ideal ownership semantics.

---

# 14. ncurses terminal UI module — MANDATORY

The newly created Vyb ncurses stdlib module remains mandatory.

VybLynx must consume this module exclusively for its terminal UI.

Do not perform direct libc terminal manipulation in the browser.

The ncurses module should expose enough functionality for:

* windows
* text
* attributes
* colors
* input
* resize
* cursor management
* refresh
* nonblocking or timed input
* optional mouse support later

The browser should become a significant real-world validation workload for this stdlib module.

---

# 15. Agents — MANDATORY

VybLynx must make meaningful architectural use of Vyb Agents.

This should not be an isolated "hello agent" example.

Use agents where independent stateful actors naturally exist.

Good candidates:

## Network Agent

Owns network request scheduling and completion.

Responsibilities:

* receive fetch requests
* issue HTTP/HTTPS operations
* track pending requests
* return completed resources/errors

## Download Agent

Owns active file downloads.

Responsibilities:

* initiate downloads
* track progress
* write files
* report completion
* report failure

## Browser Agent

Potentially owns navigation state.

Responsibilities:

* current page
* history
* pending navigation
* navigation messages

## UI Agent

Potentially receives browser events and produces UI updates.

Do not necessarily create all four if the resulting architecture is excessive.

At least one major browser subsystem must be naturally implemented as an Agent.

Prefer two or more if the model remains clean.

---

# 16. Agent message design

Define explicit message structs.

For example conceptually:

```
FetchRequest
FetchComplete
FetchFailed

DownloadRequest
DownloadProgress
DownloadComplete
DownloadFailed

NavigateRequest
NavigateComplete
```

Avoid passing loosely structured arbitrary values between agents.

This provides another opportunity to showcase Vyb's type system.

---

# 17. Channels — GOOD PRACTICE

Where Vyb Agents or asynchronous tasks communicate through channels, use channels explicitly.

Candidate channels:

```
UI -> Network Agent
Network Agent -> Browser
Download Agent -> UI
worker -> event loop
```

Channels should carry strongly typed browser messages if Vyb supports typed channels.

---

# 18. select — GOOD PRACTICE

If Vyb's `select` supports waiting on multiple channels/futures/events, use it in the browser event architecture.

An excellent use would be a browser event loop that responds to:

* keyboard events
* completed fetches
* download updates
* agent messages
* timer/status events

Conceptually:

```
select {
    keyEvent => ...
    networkEvent => ...
    downloadEvent => ...
}
```

The exact implementation must follow actual Vyb semantics.

Do not fake `select` with polling if a correct native mechanism is available.

---

# 19. Threads — GOOD PRACTICE

Threads may be used where Vyb's runtime or blocking native APIs require them.

Possible case:

ncurses input might block the current execution path while the network runs elsewhere.

Potential architectures include:

```
UI thread
network worker
download worker
```

However, prefer higher-level async/agent primitives where practical.

Threads should solve an actual scheduling problem, not merely demonstrate thread creation.

---

# 20. ensure / fail / trap — MANDATORY

VybLynx must extensively demonstrate Vyb's structured failure facilities:

* `ensure`
* `fail`
* `trap`

These should replace conventional ad hoc error checking where appropriate.

Use `ensure` for invariants and preconditions.

Examples:

```
ensure(screen initialized)
ensure(URL scheme supported)
ensure(window dimensions valid)
ensure(download destination valid)
```

Use `fail` for exceptional operations such as:

```
malformed URL
invalid HTTP response
inaccessible file
parser limit exceeded
unsupported protocol
native ncurses initialization failure
```

Use `trap` at subsystem boundaries.

Examples:

```
navigation boundary
browser event loop
resource loader
download worker
ncurses initialization
agent message processing
```

Expected behavior:

A failed page request should not crash VybLynx.

Instead:

```
fetchPage()
    fail NetworkError(...)
```

and the browser should trap that failure and display a controlled message.

---

# 21. Error taxonomy

Define meaningful structured failures.

For example:

```
UrlError
NetworkError
TlsError
HttpError
FileError
HtmlParseError
RenderError
DownloadError
TerminalError
```

Use the actual Vyb error/failure model.

Do not collapse every problem into:

```
fail "error"
```

when structured values are supported.

---

# 22. Multi-value returns

Use multi-value returns where multiple related results naturally belong together but do not justify an artificial structure.

Excellent candidates include:

URL parsing:

```
scheme, authority, path, query, fragment
```

terminal dimensions:

```
rows, cols
```

line layout:

```
line, remainingText
```

parser functions:

```
node, nextPosition
```

text decoding:

```
decoded, bytesConsumed
```

lookup functions:

```
value, found
```

download destination resolution:

```
path, filename
```

HTTP helper APIs:

```
body, metadata
```

Do not overuse tuples when a named struct communicates semantics better.

---

# 23. File downloads — MANDATORY

VybLynx must support downloading resources to local files.

This is part of the first serious feature set, not a remote stretch goal.

At minimum support:

* choose/download current link
* save resource to disk
* download arbitrary non-HTML resources
* asynchronous download
* progress/status
* clean failure reporting

Suggested command:

```
d
```

when a link is selected:

```
d
    Download selected link
```

Potential future command:

```
D
    Download current document
```

---

# 24. Download architecture

Downloads should exercise:

* HTTP/HTTPS
* asyncs
* Futures
* Agents
* ownership
* file I/O
* structured error handling

A clean conceptual pipeline is:

```
UI
  |
  v
DownloadRequest
  |
  v
DownloadAgent
  |
  +--> HTTP/HTTPS Future
  |
  +--> streamed or buffered body
  |
  +--> file writer
  |
  v
DownloadProgress / Complete / Failed
```

The terminal UI should remain responsive during the download.

---

# 25. Download filenames

Derive a reasonable filename from:

* Content-Disposition if supported
* URL path basename
* fallback generated name

Sanitize filenames.

Do not permit URLs to write outside the selected destination through constructs such as:

```
../../foo
```

Avoid direct use of untrusted path strings.

---

# 26. Download destination

Initially use one of:

* current working directory
* configurable download directory

Display destination before or when initiating the download.

A simple prompt is acceptable:

```
Save as: example.pdf
```

Use Vyb's file I/O APIs, not shell redirection.

---

# 27. Download progress

If Content-Length is available, display progress such as:

```
Downloading file.pdf  1.2 MB / 8.4 MB  14%
```

If unknown:

```
Downloading file.pdf  1.2 MB
```

Progress reporting should be asynchronous.

Do not block the browser screen until completion.

---

# 28. Download state collection

Track active downloads in an advanced collection.

Conceptually:

```
Map<DownloadId, Download>
```

or equivalent.

This gives a natural use of:

* maps
* IDs
* agents
* futures
* optional state
* message passing

A future download manager UI can build on this.

---

# 29. Download ownership

Make body ownership explicit.

Avoid unnecessary pattern:

```
networkBuffer copy
  ->
browser copy
  ->
download copy
  ->
file copy
```

Prefer moving/transferring ownership where Vyb supports it.

Streaming is preferable for large downloads if existing HTTP APIs permit it.

Otherwise document that download buffering is currently constrained by the HTTP API.

---

# 30. Optional external image opening

A selected `<img>` pseudo-anchor may optionally support opening the image using an external viewer.

This is LOW PRIORITY.

Example interaction:

```
[Image: diagram]
```

When selected:

```
o
    Open externally
```

If Vyb's `freedom` capability is the intended mechanism for controlled external process execution, use it here.

The browser might invoke a user-configured command such as:

```
xdg-open
```

Do not hardcode the implementation until the semantics of `freedom` are inspected.

This feature must remain disabled or unavailable where external execution is not permitted.

---

# 31. freedom — LOW PRIORITY / OPTIONAL

If implemented, use `freedom` for controlled external execution.

Potential use cases:

* display image externally
* open downloaded file
* invoke MIME handler

The purpose is to demonstrate Vyb's capability model, not to create unrestricted shell execution.

Never concatenate untrusted URLs or filenames into shell command strings.

Pass arguments structurally if the API supports it.

---

# 32. Variadic tuples / bare inference variables — LOW PRIORITY

If Vyb supports useful variadic tuple decomposition, look for a natural location to demonstrate it.

Potential candidates:

* parser combinators
* event decomposition
* generic dispatch helpers
* command argument processing

Do not contort browser architecture merely to demonstrate this feature.

If no natural use emerges, document it as intentionally not used in the initial browser implementation.

---

# 33. Recommended concurrency architecture

A strong Vyb-oriented implementation would resemble:

```
Main/UI execution context
    |
    | keyboard
    |
    +-------------------------------+
    |                               |
    v                               v
Browser Agent                 Download Agent
    |                               |
    | NavigationRequest             | DownloadRequest
    v                               v
Resource Agent                asyncs/Futures
    |
    +--> HTTP
    +--> HTTPS
    +--> file
    |
    v
Future<Resource>
    |
    v
Browser Agent
    |
    +--> parse
    +--> render/layout
    |
    v
UI update
```

Communication should use:

* typed messages
* channels where appropriate
* futures for outstanding operations
* select where multiple asynchronous sources converge

This is a recommended model, not an instruction to create needless complexity.

Adapt it based on Vyb's actual Agent runtime.

---

# 34. UI responsiveness acceptance test

This is mandatory.

Start loading a deliberately slow URL.

While the request is outstanding, the browser must still be able to perform whatever operations remain logically safe, such as:

* redraw
* respond to resize
* show loading state
* potentially cancel
* navigate back
* inspect help

The entire process must not appear frozen merely because a network operation is pending.

---

# 35. Navigation future lifecycle

Model navigation explicitly.

Conceptually:

```
currentNavigation: Future<Resource>?
```

Starting navigation sets:

```
currentNavigation = fetch(url)
```

The browser status becomes:

```
Loading...
```

Completion produces either:

```
Resource
```

or a trapped failure.

Only after successful fetch + parse + render should the browser commit the new page.

This creates a natural demonstration of:

* `T?`
* Future
* async
* trap
* ownership transfer
* browser state transitions

---

# 36. Cancellation

If Vyb Futures/asyncs support cancellation, use it.

Examples:

* user begins navigating to A
* before A completes, user enters B
* A should be cancelled or its eventual result ignored safely

Likewise:

* quitting should cancel pending work where possible

Do not add a homemade cancellation system if the runtime already provides one.

---

# 37. Browser event model

Define a central event type or equivalent.

Potential events:

```
KeyPressed
TerminalResized
NavigationStarted
NavigationCompleted
NavigationFailed
DownloadStarted
DownloadProgress
DownloadCompleted
DownloadFailed
AgentMessage
Timer
```

Use polymorphic aspects or structured event values according to Vyb's strengths.

This event model should reduce coupling between ncurses, networking, and browser state.

---

# 38. select-based event loop

If Vyb's `select` can multiplex the relevant sources cleanly, strongly prefer an event loop conceptually similar to:

```
while running {
    select {
        keyboardChannel => handleKeyboard(...)
        navigationChannel => handleNavigation(...)
        downloadChannel => handleDownload(...)
    }

    drawIfNeeded()
}
```

If ncurses cannot participate directly in `select`, use a small input worker or timed ncurses polling.

Document the design choice.

---

# 39. Terminal input worker

If necessary, one thread/task may own ncurses input.

For example:

```
TerminalInputAgent
    |
    getKey()
    |
    Channel<KeyEvent>
    |
    Main Browser loop
```

This can provide a clean demonstration of:

* threads or asyncs
* Agents
* channels
* select
* ncurses

Do not access ncurses from multiple threads unless the library/runtime guarantees that usage is safe.

Prefer one terminal owner.

---

# 40. Main terminal ownership

Exactly one subsystem should own ncurses screen mutation.

Do not allow:

```
Network Agent
Download Agent
Parser task
```

to write directly to curses windows.

Instead they publish state/events.

The UI owner renders them.

This avoids thread-safety problems and keeps terminal logic deterministic.

---

# 41. Agent isolation rule

Agents should own mutable state wherever practical.

Other components should communicate through messages rather than mutate agent-owned data directly.

Examples:

Download Agent owns:

```
activeDownloads
```

Browser Agent owns:

```
current page
history
navigation state
```

This should provide a real-world demonstration of Vyb's actor/agent model.

---

# 42. Browser feature-to-Vyb feature mapping

Maintain a document in the repository similar to:

```
docs/vyblynx-language-features.md
```

It should map Vyb features to actual implementation locations.

Example:

```
aspect/bind
    resource providers
    interactive document elements

Agents
    network agent
    download agent

asyncs
    resource fetch orchestration

Future
    pending navigation
    active downloads

T?
    selected link
    optional response metadata

ownership
    Resource.body
    DOM lifetime
    native ncurses handles

ensure/fail/trap
    resource loader
    parser boundaries
    terminal startup

collections
    history
    anchor lookup
    active downloads
```

This document is part of the deliverable.

---

# 43. Mandatory code-review criterion

At each major milestone, review whether the implementation is accidentally drifting toward conventional procedural code.

Ask explicitly:

* Could `aspect` simplify this?
* Could `bind` eliminate manual dispatch?
* Should this be an Agent?
* Is a Future appropriate?
* Should this field be `T?`?
* Are ownership semantics explicit?
* Should this failure use `ensure`, `fail`, or `trap`?
* Would an advanced collection better represent this state?
* Is asynchronous work blocking the UI?

Do not mechanically rewrite clean code just to use features.

But make these considerations explicit.

---

# 44. Suggested first-class VybLynx subsystems

The implementation should likely contain abstractions corresponding to:

```
Browser
BrowserState
BrowserEvent
ResourceProvider
HttpResourceProvider
HttpsResourceProvider
FileResourceProvider

NetworkAgent
DownloadAgent

Future<Resource>
Future<DownloadResult>

Document
RenderedDocument

InteractiveElement
Link
FormControl
ImageAnchor

TerminalUI
```

This structure provides natural places for nearly every required Vyb facility.

---

# 45. Revised milestone order

Update the project milestones as follows.

## Milestone 1

ncurses stdlib module.

Focus:

* FFI
* ownership
* ensure/fail/trap
* terminal API

## Milestone 2

ncurses demo and terminal event architecture.

Exercise:

* ncurses
* asyncs
* channels/thread if needed
* select if practical

## Milestone 3

URL and resource abstraction.

Exercise:

* struct
* aspect
* bind
* T?
* multi-value returns
* advanced collections

## Milestone 4

HTTP/HTTPS/file ResourceProviders.

Exercise:

* HTTP
* HTTPS
* Future
* async
* await
* Agents
* trap

## Milestone 5

browser shell and asynchronous navigation.

Exercise:

* Agent
* Future
* channels
* select
* T?

## Milestone 6

HTML parser and DOM.

Exercise:

* ownership
* advanced collections
* polymorphic node behavior where appropriate

## Milestone 7

renderer and links.

Exercise:

* aspect/bind
* structs
* optional values
* collections

## Milestone 8

history/search/navigation.

Exercise:

* collections
* Future lifecycle
* Agents

## Milestone 9

file downloads.

Exercise:

* DownloadAgent
* Future
* asyncs
* HTTP/HTTPS
* ownership
* file I/O
* channels
* progress events

## Milestone 10

forms and richer HTML.

## Milestone 11

optional external image handling via `freedom`.

---

# 46. Mandatory acceptance checklist

Before calling VybLynx feature-complete for its first major release, verify all of the following:

* [ ] `aspect` is used in central browser architecture
* [ ] `bind` is used meaningfully
* [ ] multiple core `struct` types are used
* [ ] Vyb HTTP module performs real page fetches
* [ ] Vyb HTTPS module performs real page fetches
* [ ] advanced collections are used for real state
* [ ] ncurses exists in stdlib
* [ ] VybLynx exclusively uses the stdlib ncurses layer
* [ ] at least one major subsystem is an Agent
* [ ] `ensure` is used for meaningful invariants
* [ ] `fail` is used for structured failures
* [ ] `trap` prevents ordinary failures from terminating the browser
* [ ] `asyncs` is used in production browser flow
* [ ] Future objects represent outstanding operations
* [ ] `T?` models legitimate absence
* [ ] ownership types are used intentionally
* [ ] multi-value returns are used where natural
* [ ] file downloads work
* [ ] downloads do not freeze the UI
* [ ] slow navigation does not freeze the UI
* [ ] network errors do not terminate the browser
* [ ] terminal resize works while asynchronous work is pending
* [ ] fetched content cannot inject raw terminal control sequences
* [ ] language-feature mapping documentation exists

Strongly preferred:

* [ ] async/await is used
* [ ] channels are used
* [ ] `select` is used
* [ ] threads are used where they solve a genuine blocking problem

Optional:

* [ ] variadic tuple feature has a natural demonstration
* [ ] bare inference-variable decomposition is demonstrated
* [ ] `freedom` can invoke an external image viewer safely

---

# 47. Architectural definition of success

VybLynx should make an experienced programmer looking through its source say:

> This is a serious browser-shaped application that could only reasonably have been written this way because Vyb has these language/runtime features.

It should not look like:

> Somebody translated a small C browser into Vyb syntax.

VybLynx exists to prove that Vyb can support:

* terminal UI
* native library integration
* networking
* concurrency
* asynchronous state
* ownership
* polymorphism
* structured error handling
* collections
* agents
* real application architecture

The browser is the workload.

**Vyb itself is the demonstration.**
