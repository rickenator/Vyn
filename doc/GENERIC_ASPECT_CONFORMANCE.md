# Generic / aspect conformance — inventory & staged suite (#166)

The generic/aspect system has grown enough ad hoc validation branching that the
contract needs executable conformance coverage. This page is the inventory +
coverage map. It is **not** authoritative (see `doc/DOCS_POLICY.md`); the
authoritative surface is `docs/refman/interfaces.md` + `aspects.md` (concepts) and
the `docs/refman/*` per-module pages (concrete `aspect`/`bind`).

## Boundaries to pin down (from real, shipping behavior)

| Boundary | Intended behavior | JIT conformance (existing) |
|---|---|---|
| Generic function monomorphization | `f<T>` instantiates per concrete type; `T` payloads deep-copied | `test/aspect/test_mono_direct.vyb`, `test_generic_object_literal_nested_field_rejects_mismatch.vyb` |
| Generic return of `String` | owned temp reclaim across monomorph args | `test/aspect/test_generic_fn_string_ret.vyb` |
| `bind` to concrete + generic targets | precedence: bounded first | `test/aspect/test_bind_precedence_bounded_first.vyb`, `test_bind_concrete_generic.vyb` |
| Aspect inheritance | `aspect B { sup?? } : A` — cycle rejected, transitive method lookup | `test_aspect_inheritance_valid.vyb`, `test_aspect_inheritance_cycle.vyb` |
| Aspect-bound generic calls | qualified call on a type param; unqualified call on an unbound type param rejected | `test_qualified_call_on_type_param.vyb`, `test_unqualified_call_on_unbound_type_param_rejected.vyb` |
| Bind to primitive target | `bind` onto an `Int`-like primitive | `test_bind_primitive_target.vyb` |
| Cross-module bind visibility | a bind is visible to an importer only if the *aspect name* is imported | see `doc/code-organization.md`; covered indirectly by module tests |

## Known ad hoc / special-case paths (audit targets)

- `src/vre/semantic.cpp`: generic-arg normalization/canonicalization (e.g. ~lines
  998-1037), aspect-bind pruning on narrow imports, monomorphization bail-outs
  when the payload is opaque (e.g. ~line 2016), and enum-payload generic
  substitution (e.g. ~2344).
- `src/vre/llvm/cgen_trait_mono.cpp`: bind/monomorph dispatch.
- `src/vre/llvm/`: generic `my<T>`/`our<T>`/`mild<T>` ownership-codegen paths.

These are the places to grep for "placeholder"/"For now"/ad hoc branching when a
conformance case fails; the durable fix is type-system ownership info, not more
syntactic guessing (see the ownership notes in `doc/code-organization.md`).

## Staged tranche

- **Now (foundation):** this inventory + the existing `test/aspect/*` (`@expect:
  pass|fail`) run in JIT under `--execute-jit`, so every row above already has
  positive/negative JIT coverage. `test/aspect/PHASE_*` docs track the build-out.
- **Next:** (1) add a negative case per row where only the positive exists;
  (2) extend the AOT harness (`test_compilation.sh`) so the aspect suite also
  runs in **native** mode (currently only 3 examples are AOT-covered);
  (3) isolate/remove ad hoc validation paths whose behavior cannot be specified
  and tested. Items (2)-(3) are tracked separately from this issue's foundation.
