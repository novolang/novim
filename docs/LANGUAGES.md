# Languages in novim

novim resolves two things per buffer from the file's extension: a
**tree-sitter grammar** (syntax highlighting) and a **language server**
(diagnostics, `gd`, `K`).  The two are independent — a language can have
one, both, or neither, and anything with neither falls back to novim's
built-in per-line tokenizer.

## Supported out of the box

| Extension | Grammar (`tslang.nv`) | Language server (`lsp.nv`) |
|---|---|---|
| `.nv` | `novo` | `novols` (built in) |
| `.md` `.markdown` | `markdown` | `marksman server` |
| `.c` `.h` | `c` | `clangd` |
| `.toml` | `toml` | `taplo lsp stdio` |
| `.py` | `python` | `pyright-langserver --stdio` |
| `.ll` | `llvm` | — *(no established LLVM IR server)* |
| `.rs` | — | `rust-analyzer` |
| `.go` | — | `gopls` |
| `.ts` `.tsx` `.js` `.jsx` | — | `typescript-language-server` |
| `.lua` | — | `lua-language-server` |
| `.json` | — | `vscode-json-language-server` |
| `.cpp` `.cc` `.hpp` | — | `clangd` (cpp) |

Servers are *not* bundled — novim spawns whatever is on `PATH`.  A
missing server is not an error: novim just runs without LSP for that
buffer.

## Where grammars come from

Highlighting needs two files per language: a compiled grammar
(`<lang>.so`) exporting `tree_sitter_<lang>`, and a `highlights.scm`.
novim searches these directories in order:

```
~/.novo/lib                                             ← novo-treesitter installs here
~/.local/share/nvim/lazy/nvim-treesitter/parser         ← nvim-treesitter (lazy.nvim)
~/.local/share/nvim/site/pack/packer/start/nvim-treesitter/parser
/usr/local/lib/nvim/parser
/usr/lib/nvim/parser
```

Both naming conventions are accepted: `tree-sitter-<lang>.so`
(novo-treesitter's install layout) and `<lang>.so` (nvim-treesitter's).
Queries are looked up as `<dir>/<lang>-highlights.scm`, then
`<dir>/../queries/<lang>/highlights.scm`.

**If you already use nvim with nvim-treesitter, you have most of these
already** and nothing needs installing.  Check what resolves on your
machine with:

```sh
bash orbit/novim/tests/test_tslang.sh
```

which prints a per-language `grammar=G/- queries=Q/-` table.

## Configuration

Both layers are overridable from novim's config file.

```toml
[options]
treesitter = true            # highlighting is opt-in

[treesitter]
# replace the search path entirely (default is the list above)
paths = ["~/.novo/lib", "/opt/my-parsers"]

[treesitter.llvm]
# or point one language at exact files
grammar = "~/src/tree-sitter-llvm/llvm.so"
queries = "~/src/tree-sitter-llvm/queries/highlights.scm"

[lsp]
# override any server command, keyed by language id
python = "pylsp"
markdown = "marksman server"
llvm     = "my-llvm-lsp --stdio"    # works even with no built-in entry
```

`~` expands to `$HOME` in every path above.

## Known gaps

- **LLVM IR has no grammar installed by default.**  nvim-treesitter
  ships the `llvm` *queries* but not the parser on most setups, so `.ll`
  files resolve queries and no grammar, and fall back to the native
  tokenizer.  Build `tree-sitter-llvm` and point `[treesitter.llvm]` at
  it to enable highlighting.  There is also no established LLVM IR
  language server to wire up.
- **Markdown inline emphasis is not highlighted.**  Headings, code
  blocks, quotes and list markers come from the block-level `markdown`
  grammar; `**bold**` / `_italic_` / inline links live in the separate
  `markdown_inline` grammar, which tree-sitter applies through *language
  injection*.  novim runs one grammar per buffer and does not yet
  process injections.
- **One language server per session.**  novim spawns a server for the
  first buffer whose type has one; buffers of other languages get
  highlighting but no LSP until the session restarts.  This is
  pre-existing behaviour, unchanged here.
