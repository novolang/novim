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
| `.ll` | `llvm` (see below) | — *(no established LLVM IR server)* |
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

## Installing a grammar nvim-treesitter doesn't ship

LLVM IR is the case that comes up: nvim-treesitter ships the `llvm`
*queries* but not the parser, so `.ll` resolves a query file and no
grammar. One command fixes it:

```sh
orbit/novo-treesitter/bin/install-grammar.sh llvm
```

It clones, builds, and installs `~/.novo/lib/tree-sitter-llvm.so`, which
is the first name the search path looks for.  Pass a repo URL as a
second argument for any other language.

### The ABI trap it handles for you

Grammar repos regenerate `src/parser.c` with whatever tree-sitter CLI
their maintainer had, and that is routinely **newer** than the
libtree-sitter on your system.  `tree-sitter-llvm`'s HEAD emits ABI 15;
Debian's `libtree-sitter0` is 0.20.3, which accepts 13–14.  A HEAD build
compiles and links cleanly, then fails at load:

```
novo-treesitter: ts_parser_set_language failed (incompatible ABI?)
```

and — because the language handle is then invalid — the *query* compile
fails too, which reads like a broken `highlights.scm` rather than a
version mismatch.  `install-grammar.sh` reads the ceiling out of
`tree_sitter/api.h` and walks the grammar's history for the newest
commit within it, instead of trusting HEAD.

ABI 15 support arrives in **tree-sitter 0.25.0** (0.22–0.24 are all
13–14).  `min_compatible` stays 13 there, so upgrading libtree-sitter is
backward compatible with every grammar you already have.

### Upgrading libtree-sitter for ABI 15+

Distro packages lag badly — Debian ships 0.20.3 — so this means building
from source.  Into a private prefix, not over the system library:

```sh
git clone --depth 1 -b v0.25.8 https://github.com/tree-sitter/tree-sitter
cd tree-sitter && make -j"$(nproc)" PREFIX="$HOME/.novo" && make install PREFIX="$HOME/.novo"

# the .pc emits no rpath, and SONAME libtree-sitter.so.0.25 is not in the
# system cache, so bake one in or binaries fail at load:
sed -i 's|^Libs: -L${libdir} -ltree-sitter$|Libs: -L${libdir} -ltree-sitter -Wl,-rpath,${libdir}|' \
  "$HOME/.novo/lib/pkgconfig/tree-sitter.pc"

export PKG_CONFIG_PATH="$HOME/.novo/lib/pkgconfig:$PKG_CONFIG_PATH"
```

Nothing in the repo changes: `novo.toml` binds libtree-sitter through
`[ffi.tree_sitter] pkg_config = "tree-sitter"`, so `PKG_CONFIG_PATH`
alone decides which library a rebuild links.  That is deliberate — no
machine-specific absolute path belongs in a committed manifest.  Confirm
with `ldd`:

```
$ ldd orbit/novo-treesitter/novo-treesitter | grep tree-sitter
libtree-sitter.so.0.25 => /home/you/.novo/lib/libtree-sitter.so.0.25
```

The SONAME differs from the system library's (`.so.0`), so the two
coexist and neither shadows the other.

**Keep the variable and the grammars in sync.**  A grammar only loads in
a binary whose libtree-sitter ceiling covers its ABI.  Install an ABI-15
grammar with `PKG_CONFIG_PATH` set, then rebuild novim *without* it, and
that grammar silently stops loading — highlighting just disappears, with
no error pointing at the cause.  `install-grammar.sh` warns when it
installs an ABI above the system library's ceiling.  Either export
`PKG_CONFIG_PATH` persistently in your shell profile, or leave it unset
and let the script pick system-compatible ABIs; both are consistent, the
mixture is not.

Verified on this repo: with 0.25.8 linked, ABI-15 `tree-sitter-llvm`
HEAD parses, **and** every existing ABI-14 grammar (novo, python, c,
toml, markdown) still loads — `min_compatible = 13` holds in practice,
and `orbit/novo-treesitter/tests/run.sh` is 22/22 under both libraries.

## Known gaps

- **No LLVM IR language server.**  Highlighting works once the grammar
  is installed, but there is no established `.ll` language server to
  point at.  `[lsp] llvm = "..."` works if you find one.
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
