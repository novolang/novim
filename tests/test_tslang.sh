#!/bin/bash
# orbit/novim/tests/test_tslang.sh — filetype → tree-sitter grammar
# resolution (src/tslang.nv).
#
# novim used to highlight with the Novo grammar and nothing else: ts_spans
# took a grammar_path but hardcoded the TSLanguage symbol to
# "tree_sitter_novo", so any other grammar loaded and then failed at dlsym.
# tslang.nv maps extension → language → (grammar .so, symbol, highlights.scm),
# searching ~/.novo/lib first and then any nvim-treesitter parser dirs.
#
# tslang.nv is deliberately free of @ffi declarations so this runs without
# libtree-sitter or the shim — it exercises the tables and the search path,
# not the parser.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PKG_DIR/../.." && pwd)"
NOVO="${NOVO:-$REPO_ROOT/bin/novo}"

if [ ! -x "$NOVO" ]; then
  echo "ERROR: $NOVO not found — build first" >&2; exit 1
fi

PASS=0; FAIL=0
pass() { echo "  ✓ $1"; PASS=$((PASS + 1)); }
fail() { echo "  ✗ $1"; [ -n "${2:-}" ] && echo "    $2"; FAIL=$((FAIL + 1)); }

echo ""
echo "════════════════════════════════════════════════"
echo "  novim — filetype → tree-sitter grammar resolution"
echo "════════════════════════════════════════════════"

WORK=$(mktemp -d /tmp/novim_tslang_XXXXXX)
trap "rm -rf $WORK" EXIT

# A probe that prints one line per extension: lang, symbol, and whether a
# grammar / query file resolved on THIS machine.
cat > "$PKG_DIR/src/_tslang_probe.nv" <<'EOF'
use tslang
use util

fn main() [io, fs]
    let cfg = ""
    for f in ["a.nv", "b.md", "c.c", "d.h", "e.toml", "f.py", "g.ll", "h.xyz"]
        let lang = tslang.ts_lang_of(f)
        let g = tslang.ts_grammar_path(cfg, lang)
        let q = tslang.ts_queries_path(cfg, lang)
        let gs = if str.len(g) > 0 then "G" else "-"
        let qs = if str.len(q) > 0 then "Q" else "-"
        println(f + "|" + lang + "|" + tslang.ts_symbol_of(lang) + "|" + gs + "|" + qs)
    // config override must win over the search path
    let ov = "[treesitter.llvm]\ngrammar = \"/tmp/custom-llvm.so\"\n"
    println("override|" + tslang.ts_grammar_path(ov, "llvm"))
    // a wholesale path override with no grammars in it resolves nothing
    let empty = "[treesitter]\npaths = [\"/nonexistent-parser-dir\"]\n"
    println("emptypath|" + tslang.ts_grammar_path(empty, "c"))
EOF
trap "rm -rf $WORK; rm -f $PKG_DIR/src/_tslang_probe.nv" EXIT

( cd "$PKG_DIR" && "$NOVO" run --llvm src/_tslang_probe.nv ) > "$WORK/out.log" 2>&1
if grep -q '^a.nv|' "$WORK/out.log"; then
  pass "tslang probe builds and runs (no libtree-sitter needed)"
else
  fail "probe failed" "$(tail -8 "$WORK/out.log")"
  echo "Results: $PASS passed, $FAIL failed"; exit 1
fi

field() { grep "^$1|" "$WORK/out.log" | head -1 | cut -d'|' -f"$2"; }

# ── extension → language id ────────────────────────────────────────────
check_lang() {  # ext expected
  got=$(field "$1" 2)
  if [ "$got" = "$2" ]; then pass "$1 → lang '$2'"
  else fail "$1 → lang '$got' (expected '$2')"; fi
}
check_lang "a.nv"   "novo"
check_lang "b.md"   "markdown"
check_lang "c.c"    "c"
check_lang "d.h"    "c"
check_lang "e.toml" "toml"
check_lang "f.py"   "python"
check_lang "g.ll"   "llvm"

# Unknown extension yields no language, which is what keeps the renderer on
# the native tokenizer instead of parsing it as Novo.
if [ -z "$(field 'h.xyz' 2)" ]; then
  pass "unknown extension → no language (native tokenizer)"
else
  fail "unknown extension resolved to '$(field 'h.xyz' 2)'"
fi

# ── language id → TSLanguage constructor symbol ────────────────────────
if [ "$(field 'f.py' 3)" = "tree_sitter_python" ]; then
  pass "python → symbol tree_sitter_python (not tree_sitter_novo)"
else
  fail "wrong symbol: $(field 'f.py' 3)"
fi
if [ -z "$(field 'h.xyz' 3)" ]; then
  pass "no language → empty symbol"
else
  fail "empty language produced symbol '$(field 'h.xyz' 3)'"
fi

# ── config overrides ───────────────────────────────────────────────────
ov=$(grep '^override|' "$WORK/out.log" | cut -d'|' -f2)
if [ "$ov" = "/tmp/custom-llvm.so" ]; then
  pass "[treesitter.<lang>] grammar override wins over the search path"
else
  fail "override ignored (got '$ov')"
fi
ep=$(grep '^emptypath|' "$WORK/out.log" | cut -d'|' -f2)
if [ -z "$ep" ]; then
  pass "[treesitter] paths override replaces the defaults"
else
  fail "paths override did not take effect (got '$ep')"
fi

# ── what actually resolved on this machine (informational + soft gate) ──
# Grammars come from wherever the user installed them, so their presence is
# environment-dependent.  Report it, and only assert the Novo grammar, which
# orbit/novo-treesitter/bin/install-lib.sh is responsible for.
echo ""
echo "  resolved on this machine:"
for e in a.nv b.md c.c e.toml f.py g.ll; do
  printf "    %-8s %-9s grammar=%s queries=%s\n" \
    "$e" "$(field "$e" 2)" "$(field "$e" 4)" "$(field "$e" 5)"
done
echo ""

if [ "$(field 'a.nv' 4)" = "G" ]; then
  pass "novo grammar resolves (installed by novo-treesitter/bin/install-lib.sh)"
else
  echo "  … novo grammar not installed — run orbit/novo-treesitter/bin/install-lib.sh"
  pass "novo grammar absent, resolution degraded cleanly (no crash)"
fi

echo ""
echo "  Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
