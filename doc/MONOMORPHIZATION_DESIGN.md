# Vyb Monomorphization Design — Sealed

**Version:** 1.0  
**Status:** SEALED — No changes to this design without explicit approval  
**Last Updated:** 2026-08-06

## Core Decision

Vyb uses **compile-time monomorphization** for all generic code. There is **no runtime polymorphism**, **no vtables**, and **no trait/aspect objects**.

This is a permanent design decision. The only way to change it would be a language-level redesign approved by the project lead.

## How It Works

### 1. Structs — Data Only

Structs are pure data containers. No methods, no inheritance, no virtual dispatch.

```vyb
struct Box<T> {
    value<T>
}
```

When `Box<Int>` is used, the compiler generates a concrete struct with `value` as `i64`.  
When `Box<String>` is used, a separate struct is generated with `value` as `ptr`.

### 2. Aspects — Behavior Contracts

Aspects define method signatures (interfaces). They have no state and no inheritance chain.

```vyb
aspect Display {
    show(self<Self>)<Void>
}
```

### 3. Bind — Connect Data to Behavior

A `bind` block implements an aspect for a specific type. It adds methods that external code can call.

```vyb
bind Display -> Box<T> {
    show(self<Self>)<Void> -> {
        println("Box contains a value")
    }
}
```

### 4. Aspect Bounds — Compile-Time Guarantees

Generic parameters can require aspect binding:

```vyb
printItem<T<Display>>(item<T>)<Void> -> {
    item.show()  // ✅ Compiler knows T has Display::show()
}
```

The compiler validates at the call site that the concrete type satisfies all bounds. If not, compilation fails.

### 5. Generic Function Monomorphization

When a generic function is called, the compiler:

1. **Infers** concrete types from argument types (first argument determines first type parameter)
2. **Validates** aspect bounds against the concrete types
3. **Generates** a specialized function with substituted types
4. **Caches** the result to avoid duplicate code

```vyb
// Source: min<T<Comparable>>(a, b)
// Called with: min(p1, p2) where p1, p2 are Point
// Generated: min_Point(Point, Point)<Point>
```

## What This Means

### ✅ What Vyb Has

| Feature | Mechanism | Runtime Cost |
|---------|-----------|-------------|
| Generic structs | Monomorphized LLVM types | Zero (compile-time) |
| Generic functions | Monomorphized LLVM functions | Zero (compile-time) |
| Polymorphism | Aspect + bind + bounds | Zero (static dispatch) |
| Code reuse | Default aspect methods | Zero (inline) |
| Extension | Bind to any type | Zero |

### ❌ What Vyb Does NOT Have

| Feature | Why Not |
|---------|---------|
| vtables | Adds runtime overhead, fragile base class problem |
| Dynamic dispatch (`dyn Trait`) | Not needed — monomorphization covers all use cases |
| Class inheritance | Aspects provide composition without hierarchy |
| Duck typing | Vyb is statically typed — all resolution at compile time |
| Runtime type introspection for dispatch | `typeof` exists for metadata, not dispatch |

## Implementation Details

### Mangled Names

Generic instantiations get unique LLVM names:

- `Box<Int>` → `Box_Int`
- `Vec<Box<Int>>` → `Vec_Box_Int`
- `min_Point` → monomorphized `min<T>` called with `Point`
- `duplicateAndShow_Box_Int` → monomorphized with `Box<Int>`

### Caching

Both struct and function monomorphization are cached:

```cpp
monomorphizedStructs["Box_Int"]  // Cached LLVM struct type
monomorphizedFunctions["min_Point"]  // Cached LLVM function
```

Same instantiation always returns the same generated code.

### Type Substitution Flow

```
1. Generic template parsed → stored in genericFunctionTemplates / genericStructTemplates
2. Call site analyzed → concrete types inferred from arguments
3. Bounds validated → check concrete type satisfies all aspect bounds
4. Monomorphization triggered → substitute types, generate LLVM IR
5. Result cached → future calls reuse the same code
```

### Aspect Bound Validation

When a generic function `f<T<Display, Clone>>` is called with type `Point`:

1. Check if `Point` has a `Display` bind → yes
2. Check if `Point` has a `Clone` bind → yes
3. If either missing → compile error: "Type 'Point' does not implement required aspect 'Clone'"

### Generic Bind Selection

When both bounded and unbounded binds exist:

```vyb
bind<T> Display -> Box<T>           // Applies to all Box<T>
bind<T<Display>> Display -> Box<T>  // Applies when T has Display
```

The **more specific** (bounded) bind takes precedence when its conditions are met. The unbounded bind is a fallback.

## Design Rationale

### Why Monomorphization Over Dynamic Dispatch?

1. **Zero-cost abstractions** — No vtable lookups, no pointer indirection for method calls
2. **Inlining** — LLVM can inline monomorphized functions, enabling aggressive optimization
3. **Type safety** — All resolution at compile time; no runtime type errors
4. **Simplicity** — One dispatch model instead of two (static + dynamic)
5. **Vyb-native** — Fits with ownership semantics (`my`/`our`/`their`) which are compile-time concepts

### Why Aspects Over Classes?

1. **Composition over inheritance** — Unlimited aspect bindings, no diamond problem
2. **Extension without modification** — Bind aspects to types you don't own
3. **No fragile base class** — Each bind is independent
4. **Better with ownership** — Aspect bounds work naturally with `my`/`our`/`their`

## Current Implementation Status

| Component | Status | Notes |
|-----------|--------|-------|
| Generic struct parsing | ✅ Complete | `struct Box<T>` parses correctly |
| Generic struct monomorphization | ✅ Working | LLVM types generated and cached |
| Generic function parsing | ✅ Complete | `f<T>(x<T>)` parses correctly |
| Generic function monomorphization | ✅ Working | Specialized functions generated on call |
| Aspect declaration parsing | ✅ Complete | `aspect Trait { method() }` works |
| Bind parsing | ✅ Complete | `bind Trait -> Type { ... }` works |
| Aspect bounds validation | ✅ Complete | Bounds checked at compile time |
| Generic bind (bind<T>) | ✅ Working | `bind<T> Trait -> Vec<T>` works |
| Bounded generic bind | ✅ Working | `bind<T<Display>> Trait -> Box<T>` works |
| Associated types in aspects | ✅ Working | `type Item` in aspect, `type Item = T` in bind |
| Multi-argument type inference | ⚠️ Partial | Only first argument used; needs improvement |
| Nested generic monomorphization | ⚠️ Partial | `Vec<Box<Int>>` works but parser has edge cases |
| Generic bind method resolution | ✅ Working | Aspect methods on generic types resolve correctly |

## Future Work (Within This Design)

These are implementation improvements, **not** design changes:

1. **Multi-argument type inference** — Match type parameters to arguments by position and aspect bounds
2. **Generic bind selection** — Properly pick bounded vs unbounded binds at call sites
3. **Nested generic parser support** — Handle `>>>` vs `>> >` ambiguity in parser
4. **Aspect metadata generation** — Include aspect info in type metadata for serialization
5. **Error messages** — Better diagnostics when bounds are not satisfied

## This Document Is Sealed

The design decisions in this document are final. Implementation details may change, but the core architecture (monomorphization + aspects, no vtables, no classes) is permanent.

If you find yourself wanting to add dynamic dispatch or class inheritance, stop and read this document first. Then read `doc/TRAIT_SYSTEM_DESIGN.md` and `doc/WHY_TRAITS_NOT_CLASSES.md`.
