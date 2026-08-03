# Code improvement candidates — novim

_Structural / readability review (auto-generated). Each item cites `file:line`._

## 1. Long functions

- `src/main.nv:4726` — `fn run_interactive` (~1732 lines): the whole interactive
  session — setup, event loop, input drain, picker, ex/search line editing,
  window commands, and render — live in one body. Extract:
  - **Setup (4726–4973)** → `fn ri_load_keymaps`, `fn ri_init_lsp`,
    `fn ri_init_treesitter`, `fn ri_init_windows`, `fn ri_load_extra_files`.
    Each is already a self-contained comment-delimited block.
  - **Picker key handling (~5300–5700)** → `fn handle_picker_key(...)`. This is
    the single largest inline block; it is dense with `pins_p` bookkeeping loops
    (`5304`, `5621`, `5644`, `5658`, `5672`, `5678`) that belong in named helpers.
  - **Ex-line + search-line editing (~5480 "Ex line", ~5520 "Search line")** →
    `fn edit_ex_line(...)` / `fn edit_search_line(...)` (Up/Down history,
    Left/Right/Home/End cursor motion — near-identical shapes worth sharing).
  - **Ctrl-W window keys (~5730–5827)** → `fn dispatch_window_key(b) -> Int`
    returning the `pending_wcmd` code (see §3).
  - **Deferred window-command apply (6157–6201)** → `fn apply_window_cmd(...)`.
  - **Focus/viewport sync + render (~6341–6458)** → `fn sync_active_window(...)`
    (`render_all` at `src/main.nv` already exists and should be the only render
    surface).

- `src/main.nv:1965` — `fn feed` (~249 lines): the Normal-mode byte dispatcher.
  It is one long `st.pending`/`st.mode` if-ladder (`1976`–`2074`+) mixing
  operator-pending resolution, find/till resolution, count accumulation and
  motion dispatch. Extract: `fn resolve_pending(st, b) -> Step` for the
  `st.pending`-driven arms (`1989`–`2059`), leaving `feed` to handle only
  count + first-key motion dispatch. See §2/§3 — this function is the flagship
  int-code offender.

- `src/main.nv:4235` — `fn draw_window` (~167 lines) with a 20-parameter
  signature (`mode`, `avy`, `avx`, `diag_li`, `diag_msgs`, `ts_spans`, `flash`,
  …). The parameter list is a code smell: bundle the per-window render inputs
  into a `RenderCtx` struct so `draw_window` and `render_all` (`src/main.nv`,
  same 20+ params) stop threading them positionally.

- `src/main.nv:3853` — `fn run_batch` (~138 lines): duplicates much of
  `run_interactive`'s setup + a headless feed loop. Share the setup helpers
  extracted above.

- `src/main.nv:1825` — `fn feed_visual` (~100 lines): Visual-mode byte dispatch;
  same if-ladder shape as `feed`. Candidate for the same `resolve_pending`
  treatment once mode/pending become enums.

## 2. Long / heavy match arms

Most `match` sites here are small (`fs.read`/`proc.spawn`); the heavy dispatch is
in **if/elif ladders** that should each delegate to a named fn:

- `src/main.nv:1976`+ — `fn feed`'s `st.pending` ladder: ~20 arms keyed on magic
  pending codes (`== 1`, `== 2`, `== 3`, `== 4`, `>= 20 and <= 31`, `>= 40 and
  <= 45`, `>= 53 and <= 58`, `== '\b'`/`'\t'`/`'\n'`). Each arm reaches into a
  different operator/motion handler; extract the whole ladder to
  `fn resolve_pending`.
- `src/main.nv:~5300–5700` — picker key dispatch inside `run_interactive`:
  hundreds of lines of inline arms (open/insert/delete/move-selection) that each
  want a `fn handle_picker_key` arm.
- `src/main.nv:~5730–5827` — Ctrl-W window-key arms (`s`/`S`/`v`/`c`/`q`/`o`/
  `w`/`h`/`j`/`k`/`l`) set `pending_wcmd` inline; delegate to
  `fn dispatch_window_key`.
- `src/main.nv:6157` — the `pending_wcmd` apply ladder (`== 1 or == 4` /
  `== 3` / `== 5`) → `fn apply_window_cmd`.

## 3. Integer codes that should be enums / consts

novim uses **no `enum`s in main.nv** even though the sibling `src/window.nv`
already defines enums — the mode/pending model is entirely magic integers.

- `src/main.nv:38` — `InputState.mode: Int` documented inline as
  `0 Normal, 1 Insert, 2 Visual-char, 3 Visual-line` (plus `4` at `1973` and
  `5` = Visual-block at `1867`/`1783`). Compared as raw literals throughout
  (`1486`, `1580`, `1850`, `1867`, `1967`–`1973`, `3213`, `3825`, `3841`, …) and
  passed as `draw_window(... mode: Int ...)` (`4235`) / `render_all(... mode:
  Int ...)`. Replace with `enum Mode { Normal, Insert, VisualChar, VisualLine,
  VisualBlock }`.
- `src/main.nv:37` — `InputState.pending: Int`: a heavily overloaded arithmetic
  namespace built at `1175`–`1187` (`20 + opidx*4 + N`, `32 + opidx`,
  `40 + opidx*2 + N`) and `1647`–`1649` (`53 + kind*2 + N`), with small codes
  `1`/`2`/`3` = gg/dd/yy and `'\b'`/`'\t'`/`'\n'` = f/F/t. The decode side
  (`feed` at `2034`, `2046`, `2053`) re-derives operator/motion by range checks.
  This is the worst offender: replace with an `enum Pending` (or a small
  `struct { op, motion }`) so encode/decode stop doing arithmetic on opcodes.
- `src/main.nv:4903` — `var pending_wcmd` window-command code (`0` none, `1`
  hsplit, `4` vsplit, `3` close, `5` only), set at `5736`–`5827` and decoded at
  `6157`–`6196`. Replace with `enum WinCmd`.
- `src/main.nv:111` — `fn hist_parse(text: Str, kind: Int)` / `:124`
  `fn history_load(kind: Int)`: `kind` selects ex vs search history. Enum.
- `src/main.nv:1549` — `case_byte(b, kind: Int)` and the `kind` thread through
  `case_line_range`/`visual_case`/`feed_case` (`1558`–`1637`): lower/upper/toggle.
  Enum `CaseKind`.
- `src/main.nv:993` — `fn mt(ty: Int, ...)` / `MTarget.ty`: motion-target kind
  code used by `motion_target`. Enum if the finite set is small.

## 4. Traits / generics opportunities

Enums + generics are supported (used in `orbit/hal`, `orbit/novodb`, and this
package's own `src/window.nv`), so these are actionable, not blocked.

- `src/util.nv:47`/`60`/`73` — `set_str_at` / `set_int_at` / `set_bool_at` are
  **byte-for-byte identical modulo the element type** (copy-with-one-element-
  replaced). Collapse to one generic `fn set_at<T>(xs: [T], idx: Int, v: T) ->
  [T]`.
- `src/util.nv:86`/`96`/`106` — `remove_at_str` / `remove_at_int` /
  `remove_at_bool`: same story → `fn remove_at<T>(xs: [T], idx: Int) -> [T]`.

## 5. Reinvented stdlib data structures

- `src/main.nv:2511` — `fn csort` is a hand-rolled **O(n²) selection sort**
  (used by `sort_apply` at `2534`). It exists only because `list.sort` takes no
  comparator, so numeric/case-insensitive ordering can't reuse it. Two options:
  (a) drive `list.sort` with a key-transform, or (b) note the stdlib gap
  (comparator-taking `list.sort_by`) — but the O(n²) algorithm should not ship
  regardless.
- `src/util.nv:47`–`106` — the `set_at`/`remove_at` families (see §4) are a
  stdlib gap: `docs/stdlib/list.md` has `find`/`contains`/`nth`/`drop` but **no
  positional set/remove**. Worth a `list.set_at`/`list.remove_at` upstream so
  every orbit package stops re-rolling them.

## Summary
- Long functions: 5 (worst: `fn run_interactive` @ ~1732 lines)
- Long/heavy dispatch ladders: 4 (flagship: `fn feed`'s `st.pending` ladder)
- Int-code APIs: 6 (pub-surface: `InputState.mode`/`.pending` + `draw_window`
  `mode: Int`)
- Trait/generic opportunities: 2 families (`set_at`, `remove_at` — 6 fns → 2)
- Reinvented structures: 1 (O(n²) `csort`) + a genuine stdlib gap
- Overall priority: split `run_interactive` along its comment-delimited phases
  and replace the `InputState.mode`/`.pending` integer namespace with enums —
  together they account for most of the file's readability debt, and the enum
  change makes `feed`/`feed_visual` legible.
