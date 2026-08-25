# Code organization: subsystem boundaries for large units (#153)

This issue's *practical* split — the low-risk, already-done part — removes the
orphaned/backup artifacts and inactive codegen units (see the `#153` close note).
What remains is a **phased, behavioral-preserving split** of the two monoliths.
Doing it in one shot is high-risk; the safe sequence is one subsystem per commit,
each gated by the full JIT suite + 0 regression.

## Current sizes (the coupling hotspots)

| File | Lines | Suggested subsystem slices |
|---|---|---|
| `src/vre/llvm/cgen_expr.cpp` | ~11,350 | by expression kind: literal/identifier/index/lambda FnExpression; calls + arg-temp ownership; member-access + `trap`/error; Future/async bridge; `soft`/`grab`/`our`/`mild`; String/char ops; cast/`as` |
| `src/vre/semantic.cpp` | ~10,680 | by pass concern: symbol/scope table; expression type-only (no side effects); statement semantic-check; async/task/Future; ownership/reference kinds; trap/defers; module import/resolution |
| `src/main.cpp` | ~3,300 | CLI dispatch vs orchestrator vs bindgen/VM setup (split the `vyb build`/driver orchestrator out) |

Both files are **visitor methods on one big `LLVMCodegen` / `SemanticAnalyzer` class**,
so the mechanical move is: create `cgen_<slice>.cpp`/`semantic_<slice>.cpp`, move the
member definitions, keep the declarations in `codegen.hpp`/`semantic.hpp`. No ABI or
data-layout change; the class is unchanged.

## Phasing (lowest risk first)

1. **Leaf, dependency-free slices**: extract a slice whose methods reference only
   already-extractable helpers (e.g. `cgen_cast.cpp` for `visit(CastExpression)` +
   `as`/`String(char_at)` conversions). Big functions can be moved wholesale; only
   file-static state (if any) needs care.
2. **Run the gate after EACH extraction**: `python3 test/run_tests.py --vyb build/vyb
   --test-dir test --execute-jit` must be 1098/1098 (or whatever the current count
   is) with exactly 0 new failures, and `build-asan/vyb --version` + an ASan sweep
   of the touched area.
3. **Only then** extract a slice that calls into another not-yet-extracted file — the
   header already declares everything, so ordering is free; re-grouping is purely
   mechanical.
4. Keep `semantic.cpp` slices side-effect-free for the type-checking core; anything
   touching symbol mutation stays in the symbol-table slice.

## Success criteria
- No behavioral change: identical CLI, identical test counts, `git diff` of any
  intermediate build must show only move-only hunk relocations for those methods.
- Each commit = one slice moved + suite green. Stop at any unexplained regression
  and fix it before the next slice.
- Re-run `wc -l` at the end: no unit above ~4,000 lines is the rough target (the
  `cgen_expr.cpp`+`semantic.cpp` split into 6–8 slices each).

The above is a *plan*; execute it incrementally, one commit per slice, verifying the
full suite each time (per the #153 criterion "preserve behavior with targeted
regression tests during each extraction").
