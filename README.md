# novim

A modal terminal editor written in novo-lang, with Vim's keys.  The buffer is
a vector of lines, the screen is drawn with ANSI escape sequences, and the
editing model (motions, operators, text objects, Visual, Insert and Replace
mode, registers, counts and the dot repeat) is the package
[vim-motions-nv](https://novo-lang.org/packages/vim-motions-nv).  novim adds
the pieces around it: several buffers and split windows, an Ex command line,
search and substitution with Vim-style regular expressions, file, grep and
buffer pickers, tree-sitter highlighting, a client for language servers, and
keymaps and colour schemes read from a TOML file.  novim runs no Lua and no
Vim script: the extension surface is the configuration file.

## Requirements

- The toolchain, novo 0.26.0 or newer, with `clang` 16 or newer.
- libtree-sitter and its development files for the build:
  `libtree-sitter-dev` on Debian and Ubuntu, `tree-sitter` with Homebrew.
  The C shim that wraps libtree-sitter's node-by-value calls is part of this
  package (`csrc/shim.c`) and is compiled into the binary.
- A terminal that understands ANSI escape sequences.  novim draws in 256
  colours, and in 24-bit colour when `COLORTERM` says the terminal takes it.
  `--no-color` and the `NO_COLOR` variable select the terminal's own colours.

Optional, at run time:

- A tree-sitter grammar for each language to highlight (see
  [docs/LANGUAGES.md](docs/LANGUAGES.md)).  Without one, novim highlights
  with its own tokenizer.
- [ripgrep](https://github.com/BurntSushi/ripgrep) (`rg`) for the `:grep`
  picker.
- A language server for each language to get diagnostics and jumps:
  `novols` for `.nv` files, and `rust-analyzer`, `gopls`, `pyright-langserver`,
  `clangd` and others on `PATH`.  novim runs without them.

## Build and run

From a clone of this repository:

```bash
novo pkg build               # produces ./novim
./novim README.md            # opens a file; :q leaves
```

`novo pkg build` fetches vim-motions-nv from the registry on the first build
and pins it in `novo.lock`.

The command line:

```text
novim [flags] [file...]
  --config <toml>      config file to read instead of the default
  --batch <keys>       headless: feed these keys through the real input
                       handler, then dump the buffer
  --fuzzy-rank <query> rank the file's lines against this query
  --lsp-diag           open the file and print the language server's diagnostics
  --lsp-resolve        print the language server novim would spawn for the file
  --dump-keymaps       print the keymaps parsed from the nvim config and exit
  --no-color           draw in the terminal's own colours
```

The first file is the active buffer and the rest open as further buffers.  With
no file, novim opens an empty buffer.

`--batch` is the deterministic mode the tests use: it loads the file, feeds the
keys through the same input handler as the interactive loop, draws nothing,
and prints the buffer.

```bash
./novim --batch 'ddjp' notes.txt
```

## Editing

The keys of Normal, Visual, Insert and Replace mode are Vim's, from
vim-motions-nv: its README lists what it implements and what it leaves out.
novim adds Visual block mode (`Ctrl-v`), `Ctrl-a` and `Ctrl-x`, marks,
macros, and the jump list over it.

`:help` opens a read-only buffer that lists every Ex command, with its
aliases and arguments.  The groups are:

| Group | Commands |
|---|---|
| Files and buffers | `:w` `:q` `:wq` `:wa` `:qa` `:e` `:badd` `:ls` `:b` `:bn` `:bp` `:bd` `:args` `:next` `:prev` |
| Windows | `:split` `:vsplit` `:close` `:only`, and `Ctrl-w` with `s` `v` `c` `q` `w` `h` `j` `k` `l` `+` `-` `<` `>` `=` |
| Search and replace | `/` `?` `n` `N`, `:s` `:%s` with ranges, `:g` `:v` `:noh` |
| Lines | `:d` `:y` `:m` `:t` `:>` `:<` `:sort` `:retab`, and ranges such as `:2,5` `:%` `:'<,'>` |
| Options | `:set` with `number` `relativenumber` `hlsearch` `wrap` `syntax` `expandtab` `tabstop` `treesitter`, and `:colorscheme` |
| Pickers | `:files`, `:grep`, `:bufs`, `:pins`, `:pin`, `:unpin`, `:pinjump` |
| Language server | `:fmt`, `:sym`, `:rename`, and `gd`, `K` for the definition and the hover |

A regular expression is Vim-style, not PCRE.  The command and search
history is kept in `$XDG_STATE_HOME/novim/history`, or
`~/.local/state/novim/history`.

## Configuration

`$NOVIM_CONFIG`, else `$XDG_CONFIG_HOME/novim/config.toml`, else
`~/.config/novim/config.toml`.  Every key is optional, and a missing file
gives the defaults.

```toml
[options]
number       = true
relativenumber = false
hlsearch     = true
tabstop      = 4
expandtab    = true
treesitter   = true          # tree-sitter highlighting is opt-in
timeoutlen   = 500           # ms to wait for the next key of a longer map
ttimeoutlen  = 50            # ms to wait after Esc for the key of an Alt chord

[keymaps]
# lhs = "rhs", Normal mode; \e \r \t \b and \\ are decoded on both sides
gs = ":w\r"
import_nvim = false          # true reads vim.keymap.set from ~/.config/nvim

[colorscheme]
name = "gruvbox"             # novo, novo-light, novo-16, gruvbox, gruvbox-light,
                             # solarized, solarized-light, osaka, none
keyword = 167                # a 256-colour index layered over the base

[treesitter]
paths = ["~/.novo/lib"]      # where grammars are searched

[lsp]
python = "pylsp"             # a language server command for a language id
```

`import_nvim = true` makes novim read the mappings of a Neovim configuration
by pattern: `vim.g.mapleader` and `vim.keymap.set` calls.  It runs no Lua, and
a mapping it cannot read is dropped.  `novim --dump-keymaps` prints what it
took.

An Alt chord reaches novim as an Escape followed by the key.  A terminal that
speaks the kitty keyboard protocol sends it as one report, which novim
reads too.

## Highlighting and language servers

[docs/LANGUAGES.md](docs/LANGUAGES.md) lists the languages that have a
grammar mapping and a language server by file extension, where grammars are
searched for, how to install them, and the ABI trap of a grammar newer than
the system libtree-sitter.  [docs/LSP-DESIGN.md](docs/LSP-DESIGN.md) is the
design of the language server client.

Grammars and their queries for the languages novim maps (`c`, `python`,
`toml`, `markdown`, `llvm` and the novo-lang grammar) come from the package
[novo-treesitter](https://github.com/novolang/novo-treesitter), whose
`bin/install-grammar.sh` fetches and builds them into `~/.novo/lib`, and whose
`bin/install-lib.sh` installs the grammar of novo-lang itself.  A machine with
nvim-treesitter already has most of them, and novim searches those
directories as well.

## Limits

- One language server runs for a session: the first buffer whose type has one
  picks it.
- Markdown inline content is not highlighted: the inline grammar needs
  language injection, which novim does not process.
- Tree-sitter highlighting needs a grammar built for an ABI that the system
  libtree-sitter accepts; [docs/LANGUAGES.md](docs/LANGUAGES.md) explains how
  to check.
- A mapping of a Neovim Lua configuration that is a Lua function, an F-key or
  `<S-Tab>` cannot be imported.
- There is no plugin interface and no scripting language.

## Tests

```bash
novo test                    # the suites under tests/
bash tests/test_tslang.sh    # filetype to grammar resolution, no libtree-sitter needed
```

`tests/nvkeys_tests.nv` and `tests/nvview_tests.nv` test the key handling and
the adapter to vim-motions-nv without a terminal.

## Licence

Apache-2.0.
