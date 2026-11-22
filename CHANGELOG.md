# Changelog

Newest first.  Below `1.0.0` a breaking change bumps the minor number, as
[Version numbers in the Orbit package registry](https://novo-lang.org/docs/registry/semver.html)
describes.

## 0.1.2

The package has a repository of its own, https://github.com/novolang/novim.

- The C shim over libtree-sitter is the package's own source, `csrc/shim.c`,
  compiled into the binary through the `[native]` table of the manifest.  The
  build needs libtree-sitter and its development files, and nothing in
  `~/.novo/lib`; the earlier build linked a `libnovo_treesitter_shim.so` that
  `bin/install-lib.sh` of novo-treesitter had installed there.  The grammars and
  queries are still searched for in `~/.novo/lib` at run time.  `csrc/shim.c`
  and `csrc/shim.h` are copies of the files of novo-treesitter.
- The manifest carries `license`, `repository`, `tags`, `maintainers`,
  `stability` and `novo`, and its description no longer points at a file of
  another repository.
- `README.md` states the requirements, the build, the command line, the
  configuration and the limits.  The documents and the comments name no path
  of the repository the package was developed in.
- `tests/test_tslang.sh` finds the compiler on `PATH` when it is not in a
  checkout of the toolchain.
