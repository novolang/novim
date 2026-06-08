# novim ↔ novols (LSP) integration — design

Status: **design / not yet implemented**.  Target: novim as an LSP *client* of
`bin/novols`, the existing Novo language server.  This doc is the plan; it
should be read before writing any code, and updated as decisions land.

---

## 1. Goal & non-goals

**Goal.**  When editing a `.nv` file, novim spawns `novols`, streams the buffer
to it, and surfaces the server's intelligence in the TUI:

- **Diagnostics** — errors/warnings underlined + a sign in the gutter (the
  headline feature; the one that pays for the whole transport).
- **Go-to-definition** (`gd`), **hover** (`K`).
- Later: completion (`Ctrl-N`/insert), references (`gr`), rename, format,
  document symbols (`:ls`-style jump).

**Non-goals (v1).**
- ~~Multiple language servers / non-Novo files.  One server, `.nv` only.~~
  **Shipped (Tier 1):** novim now drives a server chosen from the active file's
  type — `.nv`→novols, plus rust-analyzer / gopls / pyright / clangd / … for
  other extensions, with a `[lsp]` config override and marker-based project-root
  detection.  Still one server *per session* (the first LSP-capable buffer's
  language wins); a multi-server pool is Tier 2.  See §12.
- Workspace-wide features beyond what a single open document needs.
- Semantic tokens (we already have a native syntax highlighter; novols doesn't
  emit semantic tokens anyway).
- Perfect UTF-16 position fidelity on day 1 (see §9.3).

`novols` already implements the methods we need (confirmed in
`compiler/lsp/novols.ml`):
`initialize`, `initialized`, `shutdown`, `textDocument/{didOpen,didChange,
didClose,didSave,publishDiagnostics,definition,hover,completion,documentSymbol,
formatting,prepareRename,rename}`.

---

## 2. Constraints from novim's architecture

novim is deliberately split:

- **`feed` is pure** — `feed(ed, st, b) -> Step`.  No `[io]`, no async, no
  server.  LSP must NOT leak into `feed`.
- **The drivers are impure** — `run_interactive` (`[io, time]`) owns the
  terminal, the viewport, options, registers (`EditState`).  **All LSP state and
  I/O live here.**
- The main loop is **synchronous**: `novo_pty_poll_h(0, 100)` blocks up to
  100 ms on terminal stdin, then drains available bytes, then renders.

The integration is therefore entirely a `run_interactive` concern: a new
`LspState` value threaded through the loop, plus a few key interceptions (`gd`,
`K`, …) handled at the driver level exactly like the scroll keys — never in
`feed`.  `run_batch` does **not** get LSP (no viewport, no async; keeps the
deterministic test path clean).

---

## 3. Available primitives (all confirmed present)

| Need | Primitive | Notes |
|------|-----------|-------|
| spawn server | `proc.spawn(argv: [Str]) -> ?Subprocess` | long-lived child |
| server pipes | `proc.stdin_fd(self)`, `proc.stdout_fd(self)` | raw fds |
| lifecycle | `proc.try_wait`, `proc.kill`, `proc.close` | |
| poll a fd | `novo_unix_poll_h(fd, timeout_ms) -> Int` | readable bit |
| poll many | `novo_pty_poll_many_h(fds_h, n, timeout) -> Int` | bitmask of ready fds |
| read/write fd | `novo_unix_read_byte_h(fd)`, `novo_unix_write_byte_h(fd, b)` | byte-level |
| terminal in | `novo_pty_poll_h(0, t)`, `novo_pty_stdin_read_byte_h()` | existing loop |
| JSON | `json.*` (`json_get`, `json_get_str/int/list`, `json_array`, `json_int`, …) | parse + build |

The server is `bin/novols` (resolve via the same logic as the install path, or
an absolute path; PATH is not guaranteed — mirror novim's own `~/.novo/bin`
convention or accept a `[options] lsp_cmd`).

---

## 4. Transport & event loop

### 4.1 JSON-RPC framing

LSP messages are `Content-Length: <n>\r\n\r\n<json-bytes>`.

- **Write** (`lsp_send`): build the JSON string, then write
  `"Content-Length: " + len + "\r\n\r\n" + json` to `stdin_fd` byte-by-byte via
  `novo_unix_write_byte_h`.
- **Read** (`lsp_recv`): read bytes from `stdout_fd`, accumulate into a
  per-server buffer, parse the `Content-Length` header, then read exactly that
  many body bytes.  Because reads are non-blocking and byte-at-a-time, the
  reader is a small state machine over a persisted byte buffer in `LspState`
  (header-scan → body-collect → emit one message → repeat).

### 4.2 The combined poll

The single hardest design point: novim's loop currently blocks only on the
terminal.  novols' stdout must be drained too, or diagnostics never arrive.

**Decision:** each loop iteration polls *both* fds with one short timeout and
services whichever is ready:

```
let ready = poll_many([0, lsp.out_fd], 50)     # or two novo_unix_poll_h calls
if ready has fd 0:        drain terminal bytes → feed_edit (unchanged path)
if ready has lsp.out_fd:  drain server bytes → lsp_recv → dispatch messages
if anything changed:      re-render
```

This keeps the editor responsive (≤50 ms latency to a diagnostic) and costs
nothing when idle.  `novo_pty_poll_many_h` already exists; if its packing is
awkward, fall back to two `novo_unix_poll_h(fd, 25)` calls per iteration.

### 4.3 Request/response correlation

- Each **request** (initialize, definition, hover, …) gets a monotonically
  increasing integer `id`.  `LspState` keeps `next_id` and a small map of
  `id -> pending-kind` (e.g. 1=definition, 2=hover) so a response can be routed.
- **Notifications** (didOpen, didChange, publishDiagnostics) have no `id`.
- `publishDiagnostics` is server→client *notification*, handled whenever it
  arrives (async).

### 4.4 Synchronous vs async for user-triggered requests

`gd`/`K` need a result *now*.  Two options:

1. **Bounded pump** — send the request, then loop `lsp_recv` (with a deadline,
   e.g. 1 s) until the matching response id appears, buffering any diagnostics
   notifications that arrive meanwhile.  Simple, good enough; the server is
   fast and local.
2. **Fully async** — fire the request, return to the loop, act on the response
   when it lands.  Cleaner but needs a "pending UI action" concept.

**Decision: start with (1)** (bounded pump) for `gd`/`K`; revisit if it ever
feels laggy.  Diagnostics stay fully async regardless.

---

## 5. Document sync

- **Open:** on entering `run_interactive` with a `.nv` file (or on first edit),
  send `initialize` → wait for the result → `initialized` → `didOpen` with the
  full buffer text and `version: 1`.
- **Change:** v1 uses **full-document sync** — on each buffer-changing command,
  bump `version` and send `didChange` with the whole text
  (`contentChanges: [{ text: <buf> }]`).  Incremental sync (ranges) is a later
  optimization; full sync is correct and trivial, and Novo files are small.
  - To avoid a `didChange` per keystroke in insert mode, **debounce**: only send
    when the buffer text actually changed since the last send (we already
    compute `buf_text` for undo/dot change-detection — reuse it) and optionally
    coalesce within one input drain.
- **Close/quit:** `didClose`, then `shutdown` + `exit`, then `proc.kill`/`close`.

`textDocument` is identified by a `file://` URI built from the absolute path.

---

## 6. Feature → method → UI mapping

| novim trigger | LSP method | UI |
|---------------|-----------|----|
| (automatic) | `publishDiagnostics` | gutter sign (`E`/`W`) + colored underline on the range; count in status line |
| `gd` | `textDocument/definition` | jump cursor (same file) or open the target file; push a jump so `Ctrl-O` returns |
| `K` | `textDocument/hover` | render the hover markdown in the status line / a one-line popup row above the cursor |
| `gr` | `textDocument/references` *(novols: none yet — see note)* | quickfix-style list |
| insert `Ctrl-N` | `textDocument/completion` | inline completion menu |
| `=` / `:fmt` | `textDocument/formatting` | replace buffer with formatted text (undoable) |
| `:rename`/`grn` | `prepareRename` + `rename` | apply workspace edits |
| `:sym` | `textDocument/documentSymbol` | symbol jump list |

(Note: `references` isn't in the novols method list above; `gr` waits on the
server gaining it, or is dropped from v1.)

---

## 7. Rendering integration

The render pipeline already supports per-column overlays (`emit_line_styled`
takes a syntax-color array; selection > search > syntax).  Diagnostics add one
more overlay tier and a gutter glyph:

- **Diagnostics store:** `LspState.diags` = a per-line list of
  `{ start_col, end_col, severity }` for the current document version.  Cleared
  and rebuilt on each `publishDiagnostics`.
- **Gutter:** when a line has a diagnostic, replace the first gutter cell with
  `E`/`W` (red/yellow).  This composes with the number gutter (the number still
  shows; the sign can be a leading colored column, widening the gutter by 1, or
  reuse the existing 4-wide field).
- **Underline:** in `emit_line_styled`, add a `diag ranges` parameter; columns
  in a diagnostic range get an undercurl/underline SGR (`4` or `4:3` where
  supported) in the severity color.  Priority: selection > search > diagnostic
  > syntax (so an error under the cursor selection still shows selected).
- **Status line:** show `E:n W:m` and, when the cursor is on a diagnostic, that
  diagnostic's message (the most useful, low-effort surface — do this before any
  popup work).
- **Hover/definition popups** are the only genuinely new rendering surface; v1
  can route both to the **status line** (single line) to avoid a floating-window
  renderer.

---

## 8. Phasing (slice plan)

Each phase is independently shippable and testable.

- **Phase A — transport + diagnostics (the spine).**
  `LspState` + `proc.spawn` + framed send/recv + the combined poll +
  initialize/initialized/didOpen.  Render `publishDiagnostics` as the status-line
  count + the on-cursor message + gutter signs.  *This is the bulk of the risk;
  do it first and prove the loop.*
- **Phase B — live sync.**  Debounced full-document `didChange` on edits;
  diagnostics update as you type.  `didClose`/`shutdown` on quit.
- **Phase C — navigation.**  `gd` (definition, bounded pump) and `K` (hover) to
  the status line.  Jump list for `Ctrl-O`/`Ctrl-I` (also independently useful).
- **Phase D — the rest.**  completion, formatting, rename, documentSymbol — each
  its own slice.

Underline/undercurl rendering and a real popup window are optional polish that
can slot in after Phase A's status-line versions work.

---

## 9. Key risks & decisions

### 9.1 The sync↔async impedance (highest risk)
Resolved by §4.2 (poll both fds each iteration) + §4.4 (bounded pump for
user-triggered requests).  The thing to validate *first* in Phase A: that
draining novols stdout never blocks the terminal and vice-versa.  Both reads
must be non-blocking (`O_NONBLOCK` on the fds; `novo_unix_read_byte_h` returns a
sentinel when empty — confirm its no-data return value before building on it).

### 9.2 JSON in Novo
`json.*` exists (parse + build).  **Verify in Phase A** that we can both
*build* request objects (nested objects/arrays for `initialize` capabilities and
`didOpen`) and *parse* responses (`json_get`, `json_get_list`, `json_get_str/int`
into diagnostic ranges).  If building deeply-nested JSON is painful, a tiny
string-concatenation request builder is an acceptable fallback for the handful
of outgoing message shapes.

### 9.3 Position encoding (UTF-16 vs bytes)
LSP positions are `line` + **UTF-16 code-unit** character offset.  novim is
byte-oriented (`cx` = byte index).  For ASCII (the overwhelming majority of Novo
source) byte == UTF-16 unit, so v1 can use the byte column directly and **note
the gap** for non-ASCII lines (multi-byte UTF-8 will misplace ranges/cursor).
A proper converter (byte↔UTF-16) is a bounded follow-up once the rest works.
We already have `is_utf8_cont` from the cursor-drift fix to build on.

### 9.4 EditState vs a new struct
LSP state is large and impure; do **not** stuff it into `EditState` (which is
threaded through the pure feed wrapper).  Use a separate `LspState` var local to
`run_interactive`, alongside `opts`/`top`/`left`.  This keeps the pure path and
the 25-field `EditState` untouched.

### 9.5 Lifecycle & failure modes
- Server not found / spawn fails → run with LSP disabled (no crash, optional
  status note).  novim must stay a usable editor without novols.
- Server dies mid-session (`try_wait` returns) → drop diagnostics, optionally
  respawn once.
- On `:q` always `proc.kill` + `close` so no zombie novols.

### 9.6 Testing strategy
- Phase A is **integration-testable with the real `bin/novols`**: open a `.nv`
  with a deliberately broken expression in a pty, wait, and assert the
  diagnostic count / message reaches the status line.  novols is deterministic
  for fixed input.  The test harness already uses `$ROOT/...` paths, so it can
  point at `$ROOT/bin/novols`.
- Lower-level: a unit test for the framing reader (feed it a canned
  `Content-Length` stream, assert it yields the right message boundaries) and the
  JSON request builders — these can be plain `--batch`-style or standalone Novo
  probes, no server needed.
- Guard the suite from a developer's environment exactly as slice 69 did
  (`NOVIM_CONFIG`); add an `lsp = false` escape so non-LSP tests never spawn a
  server.

---

## 10. Open questions for the user

1. **Server discovery** — hardcode `~/.novo/bin/novols`, search PATH, or a
   `[options] lsp_cmd = "..."`?  (Recommend: PATH then `~/.novo/bin`, overridable
   by config.)
2. **Diagnostics visual** — gutter sign + status message first (cheap, no popup
   renderer), with underline/undercurl as follow-on?  (Recommend: yes.)
3. **Hover/definition surface** — status line for v1, or invest in a floating
   popup row immediately?  (Recommend: status line first.)
4. **Scope of v1** — is "diagnostics + `gd` + `K`" the right cut to call the LSP
   feature shipped, with completion/rename/format as later slices?

---

## 12. Multi-language support (Tier 1 — shipped)

novim drives **one** language server per session, resolved from the active
file's extension.  `.nv` stays on novols and is byte-identical to v1 (same
`languageId: "novo"`, same `rootUri: null`); the layer is purely additive.

**Resolution (in `lsp.nv`).**

| Function | Role |
|----------|------|
| `lsp_ext_of(p)` | extension without the dot (`"rs"`) |
| `lsp_default_for(ext) -> LspSpec` | built-in `{cmd, language, roots}` table |
| `lsp_lang_of(p)` | the LSP languageId, or `""` (no server for this type) |
| `lsp_cmd_override(cfg, language)` | a `[lsp]` config command override, or `""` |
| `lsp_cmd_for(p, cfg)` | override if present, else the built-in default |
| `lsp_find_root(p, markers)` | nearest ancestor dir containing any marker |
| `lsp_root_uri_for(p)` | workspace `file://` rootUri (`""` → `null`) |
| `lsp_spawn_cmd(cmd)` | `proc.spawn` after splitting argv on spaces |

**Built-in servers.**

| Ext | languageId | Default command | Root markers |
|-----|------------|-----------------|--------------|
| `nv` | novo | `novols` | *(none → null root)* |
| `rs` | rust | `rust-analyzer` | `Cargo.toml` `.git` |
| `go` | go | `gopls` | `go.mod` `go.work` `.git` |
| `py` | python | `pyright-langserver --stdio` | `pyproject.toml` `setup.py` `setup.cfg` `requirements.txt` `.git` |
| `c` `h` | c | `clangd` | `compile_commands.json` `.clangd` `.git` |
| `cpp` `cc` `cxx` `hpp` `hh` | cpp | `clangd` | *(same as c)* |
| `ts` `tsx` | typescript | `typescript-language-server --stdio` | `tsconfig.json` `package.json` `.git` |
| `js` `jsx` `mjs` `cjs` | javascript | `typescript-language-server --stdio` | `package.json` `.git` |
| `lua` | lua | `lua-language-server` | `.luarc.json` `.luarc.jsonc` `.git` |
| `json` | json | `vscode-json-language-server --stdio` | `.git` |

**Config — `[lsp]` overrides** (keyed by languageId; value is the full argv
command line).  The default command is used when a language has no override:

```toml
[lsp]
rust   = "rust-analyzer"
python = "pylsp"                       # prefer pylsp over the default pyright
go     = "gopls -remote=auto"
```

`.nv` is never rerouted (there is no `novo` key in the table by convention).

**Session model.**  The first LSP-capable buffer opened spawns the server for
*its* language and pins `lsp_lang`.  Buffers whose `lsp_lang_of` matches join
the same server (didOpen / diagnostics / didClose); buffers of other languages
get no LSP this session.  A multi-server pool keyed by language is Tier 2.

**Debugging — `--lsp-resolve <file>`** prints the resolved languageId, command,
and rootUri without spawning anything; this is also how the resolver is
regression-tested headlessly (test_novim.sh slice 113), since CI has no
community servers installed.

```
$ novim --lsp-resolve src/main.rs
language: rust
command: rust-analyzer
rootUri: file:///home/me/project
```

---

## Summary

The integration is **feasible with existing primitives** — `proc.spawn` for the
server, `novo_unix_poll/read/write` for non-blocking pipe I/O, `json.*` for the
protocol, and novols already implements every method we need.  The only genuine
new machinery is (a) draining a second fd in the main loop and (b) a small
framed-JSON-RPC reader state machine; everything else is wiring LSP responses to
novim's existing render and jump facilities.  Phase A (transport + diagnostics)
carries the risk and should be built and proven before anything else.
