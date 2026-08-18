# Vyb Examples

Runnable Vyb programs demonstrating the current language surface.

```bash
build/vyb examples/<file>.vyb
```

All top-level `.vyb` files in this directory are expected to execute
successfully with the current compiler. Support modules live under
`examples/modules/`. Heavier feature walkthroughs live in `demos/`.

## Examples

| Example | Covers |
|---------|--------|
| `main.vyb` | arithmetic, structs, `Vec<T>`, `match`, strings, recursion, `defer` |
| `sort.vyb` | insertion sort with `Vec<Int>` helper functions |
| `quicksort.vyb` | recursive quicksort returning `Vec<Int>` |
| `stack.vyb` | stack helpers over a struct containing `Vec<Int>` |
| `binary_tree_clean.vyb` | flat tree-node storage and lookup with `Vec<TreeNode>` |
| `vec_filter.vyb` | filtering values into a second vector |
| `vec_max.vyb` | scanning a vector for a maximum value |
| `vec_point_distance.vyb` | iterating over `Vec<Point>` structs |
| `memory_semantics.vyb` | `freedom`, `loc<T>`, and `at(ptr)` |
| `mild_references.vyb` | `our<T>` and `mild<T>` syntax |
| `ffi_puts.vyb` | `extern "C"` plus `freedom`-gated calls |
| `module_import.vyb` | local module import from `examples/modules/` |
| `module_visibility.vyb` | `bundle(...)`, `share(...)`, and selective import aliases |
| `term_input.vyb` | `term` module — cooked `stdin_read_line`, raw-mode keypresses, stderr prompts |

### Idiomatic language patterns

The `idiomatic/` subdirectory showcases Vyb's distinctive patterns in small,
self-contained programs. Each file runs with `build/vyb examples/idiomatic/<file>.vyb`
and exits 0.

| Example | Covers |
|---------|--------|
| `aspects.vyb` | `aspect` / `bind`, bounded `<T<Aspect>>` parameters, static dispatch |
| `errors.vyb` | `fail` / `trap` / `ensure`, rich error structs, wildcard traps |
| `ownership.vyb` | `our<T>` / `mild<T>` weak views, `grab()`, release on scope, `my<T>` moves |
| `optionals.vyb` | the native `T?` — `else`, presence `==`, `match` both states |
| `collections_hop.vyb` | pure-Vyb `map` / `filter` / `reduce`, `sorted`, in-place by-ref forms |
| `shape_match.vyb` | tagged-union `enum`s, exhaustive `match` / `select` |
| `json_roundtrip.vyb` | `to_string()` / `Type::from_string()` lossless JSON round trip |
| `bitpack.vyb` | composing a wider `Int64` from bytes with unsigned `as` casts |
| `channels_concurrency.vyb` | typed `chan<T>`, threaded producers, `thread_spawn` / `recv` / `join` |
| `async_fibers.vyb` | `async` functions, `Future<T>`, `await`, overlapping timers |

### Module path demo

This repository also includes a multi-file `--module-path` demo:

```bash
build/vyb examples/module_path_demo/main.vyb --module-path examples/module_path_demo/modules
```

## Known Boundaries

The examples now include runtime `Vec<T>` helper returns for common algorithm
paths. Remaining ownership work is broader: enforcing full move/drop rules for
all owned aggregates and documenting the final borrow/mutation contract.
