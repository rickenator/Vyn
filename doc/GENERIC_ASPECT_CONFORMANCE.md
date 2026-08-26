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

## Coverage status

- **JIT conformance**: the full `test/aspect/*` suite (`@expect: pass|fail`) runs
  under `--execute-jit`; every row above has positive **and** negative coverage
  (inheritance cycle / phantom-super, unbound-qualified call, invalid/nonexistent
  bounds, missing-super, etc.).
- **Native (AOT) conformance**: `test_compilation.sh` compiles+runs a
  representative set of the positive aspect tests natively (`native_aspect`,
  one per inventory row; currently 15 cases covering aspect inheritance,
  bind precedence, monomorphization, generic String return, qualified/
  unqualified type-param calls, associated types, primitive binds, receivers).
  Runs in CI via the `aot-native` CTest (label `lang;aot`).

## Remaining (tracked separately)

- Isolate/remove the ad hoc validation paths listed below whose behavior cannot
  be fully specified and tested -- a code refactor, not a tests task. The durable
  direction is type-system ownership/canonicalization rather than more special
  cases; see `doc/code-organization.md`.
