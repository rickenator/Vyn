# Vyb Aspect System Design

**Version:** 0.4.2+
**Status:** Partially Implemented
**Priority:** HIGH

## Overview

Vyb's aspect system provides interface-based polymorphism without classes. Aspects define
shared behavior that types can bind to, enabling generic programming with compile-time
guarantees. This document uses Vyb-native vocabulary: `aspect` and `bind`.

> **Terminology note:** Earlier drafts used `trait`/`impl` (Rust vocabulary). Vyb uses
> `aspect`/`bind`. These are not the same language — the concepts are similar but the
> keywords and philosophy differ. See `doc/WHY_TRAITS_NOT_CLASSES.md`.

## Core Principles

1. **Opt-in Polymorphism** - Aspects are optional; simple structs work without them
2. **No Classes** - Structs bind aspects directly; there is no class keyword in Vyb
3. **Compile-time Dispatch** - Static dispatch via monomorphization (no vtables by default)
4. **Explicit Binding** - Types explicitly `bind` aspects; no duck typing
5. **Aspect Bounds** - Generic type parameters can require aspect binding

## Design Philosophy

### Why Not Mandatory Classes?

```vyb
// Simple data without behavior - no class needed
struct Point {
    x<Int>,
    y<Int>
}

// Behavior through standalone functions
distance(p1<Point>, p2<Point>)<Float> -> {
    dx<Int> = p1.x - p2.x
    dy<Int> = p1.y - p2.y
    return sqrt((dx * dx + dy * dy).to_float())
}

// This is valid and preferred for simple data types
```

**Vyb's Approach:**
- Use **structs** for data
- Use **functions** for behavior
- Use **aspects** when you need polymorphism
- **No classes** — aspect composition is sufficient and more composable

## Aspect Declaration Syntax

For a simple instance receiver, write `self`. The compiler canonicalizes it to the bound `Self` type. Use explicit receiver types like `self<their<Self>>` when ownership mode is meaningful.

### Basic Aspect

```vyb
aspect Comparable {
    // Method signatures only - no implementations
    lt(self<their<Self>>, other<their<Self>>)<Bool> ->
    gt(self<their<Self>>, other<their<Self>>)<Bool> ->
    eq(self<their<Self>>, other<their<Self>>)<Bool> ->
}
```

**Key Features:**
- `Self` represents the binding type
- Methods take `their<Self>` (borrowed self) by convention
- No method bodies — pure interface

### Aspect with Default Methods

```vyb
aspect Comparable {
    // Required methods
    lt(self<their<Self>>, other<their<Self>>)<Bool> ->
    eq(self<their<Self>>, other<their<Self>>)<Bool> ->

    // Default implementations (can be overridden)
    gt(self<their<Self>>, other<their<Self>>)<Bool> -> {
        return !self.lt(other) && !self.eq(other)
    }

    lte(self<their<Self>>, other<their<Self>>)<Bool> -> {
        return self.lt(other) || self.eq(other)
    }
}
```

### Aspect Composition (Aspect Inheritance)

```vyb
aspect Equatable {
    eq(self<their<Self>>, other<their<Self>>)<Bool> ->
}

aspect Comparable {
    // Requires Equatable::eq to be bound
    lt(self<their<Self>>, other<their<Self>>)<Bool> ->
    gt(self<their<Self>>, other<their<Self>>)<Bool> ->
}
```

## Binding Aspects to Types

### Binding to Structs

```vyb
struct Point {
    x<Int>,
    y<Int>
}

bind Comparable -> Point {
    lt(self<their<Point>>, other<their<Point>>)<Bool> -> {
        return self.x < other.x || (self.x == other.x && self.y < other.y)
    }

    eq(self<their<Point>>, other<their<Point>>)<Bool> -> {
        return self.x == other.x && self.y == other.y
    }

    // gt() uses default implementation from aspect
}
```

### Binding to Primitive Types

```vyb
// Extend built-in types with new aspects
bind Numeric -> Int {
    add(self<Int>, other<Int>)<Int> -> { return self + other }
    sub(self<Int>, other<Int>)<Int> -> { return self - other }
    mul(self<Int>, other<Int>)<Int> -> { return self * other }
    div(self<Int>, other<Int>)<Int> -> { return self / other }
}
```

### Generic Bindings

```vyb
// Bind aspect for all types that meet constraints
bind<T<Comparable>> Equatable -> Vec<T> {
    eq(self<their<Vec<T>>>, other<their<Vec<T>>>)<Bool> -> {
        if (self.len() != other.len()) {
            return false
        }
        i<Int> = 0
        while (i < self.len()) {
            if (!self.get(i).eq(other.get(i))) {
                return false
            }
            i = i + 1
        }
        return true
    }
}
```

## Using Aspects

### Aspect Bounds on Generic Functions

```vyb
// Single bound — Vyb syntax
min<T<Comparable>>(a<T>, b<T>)<T> -> {
    if (a.lt(b)) {
        return a
    } else {
        return b
    }
}

// Multiple bounds
serialize<T<ToJson><Debug>>(obj<their<T>>)<String> -> {
    json<String> = obj.to_json()
    println("Serializing: " + obj.debug())
    return json
}
```

### Dynamic Dispatch (Planned)

```vyb
// Dynamic dispatch using aspect objects (planned)
draw_shape(shape<dyn Drawable>)<Void> -> {
    shape.draw()  // Runtime dispatch
}

// vs. static dispatch (current — zero cost)
draw_shape<T<Drawable>>(shape<T>)<Void> -> {
    shape.draw()  // Compile-time dispatch via monomorphization
}
```

## Standard Library Aspects

### Core Aspects

```vyb
aspect Equatable {
    eq(self<their<Self>>, other<their<Self>>)<Bool> ->
    ne(self<their<Self>>, other<their<Self>>)<Bool> -> {
        return !self.eq(other)  // Default
    }
}

aspect Comparable {
    lt(self<their<Self>>, other<their<Self>>)<Bool> ->
    lte(self<their<Self>>, other<their<Self>>)<Bool> -> {
        return self.lt(other) || self.eq(other)
    }
    gt(self<their<Self>>, other<their<Self>>)<Bool> -> {
        return !self.lte(other)
    }
    gte(self<their<Self>>, other<their<Self>>)<Bool> -> {
        return !self.lt(other)
    }
}

aspect Numeric {
    add(self<Self>, other<Self>)<Self> ->
    sub(self<Self>, other<Self>)<Self> ->
    mul(self<Self>, other<Self>)<Self> ->
    div(self<Self>, other<Self>)<Self> ->
    zero()<Self> ->  // Static method
}

aspect Hashable {
    hash(self<their<Self>>)<UInt64> ->
}

aspect Cloneable {
    clone(self<their<Self>>)<Self> ->
}

aspect Display {
    to_string(self<their<Self>>)<String> ->
}
```

### Iterator Aspect (Standard Library — requires associated types)

```vyb
aspect Iterator {
    type Item                                    # associated type
    next(self<their<Self>>)<Self::Item?>         # present payload, absent when exhausted
}

// Types that bind Iterator are usable in for loops
// for (item in col) desugars to repeated Iterator::next() calls
```

### Serialization Aspects (Future)

```vyb
aspect Serializable {
    serialize(self<their<Self>>)<Bytes> ->
    deserialize(data<their<Bytes>>)<Self?> ->
}

aspect Async<T> {
    await_val(self<Self>)<T> ->
}
```

## The Vyb Way

**Data = Structs**
```vyb
struct Point { x<Int>, y<Int> }
```

**Behavior = Aspects**
```vyb
aspect Drawable {
    draw(self<their<Self>>)<Void> -> { }
}

bind Drawable -> Point {
    draw(self<their<Point>>)<Void> -> {
        println("Drawing point at (" + self.x.to_string() + ", " + self.y.to_string() + ")")
    }
}
```

**Polymorphism = Aspect Bounds**
```vyb
render<T<Drawable>>(shape<their<T>>)<Void> -> {
    shape.draw()
}
```

**This is all you need.** No classes, no inheritance, no diamond problem.

### Advantages

✅ **Multiple Aspect Bindings** — Unlike single inheritance
✅ **No Fragile Base** — Changes don't break dependents
✅ **Better Composition** — Mix and match behaviors freely
✅ **Extension Without Modification** — Bind aspects to any type
✅ **Static Dispatch** — Zero-cost abstractions
✅ **Simple Mental Model** — Structs are data, aspects are contracts

## Implementation Phases

### Phase 1: Aspect Declarations (v0.4.2) — COMPLETED

**Goal:** Make aspect definitions real and usable

**Tasks:**
1. ✅ Parse aspect declarations
2. ✅ Store aspect definitions in semantic analyzer
3. ✅ Validate aspect method signatures
4. ✅ Register aspect in type system
5. ⬜ Check aspect composition (aspect A requires B)

### Phase 2: Aspect Binding (v0.4.2) — IN PROGRESS

**Goal:** Allow types to bind aspects

**Tasks:**
1. ✅ Parse bind blocks
2. ⬜ Validate bind matches aspect signature
3. ⬜ Check all required methods implemented
4. ⬜ Register binding in type system
5. ⬜ Enable aspect method calls (value.method())

**Test:**
```vyb
struct Point { x<Int>, y<Int> }

bind Comparable -> Point {
    lt(self<their<Point>>, other<their<Point>>)<Bool> -> {
        return self.x < other.x
    }
}

main()<Int> -> {
    p1<Point> = Point { x = 1, y = 2 }
    p2<Point> = Point { x = 3, y = 4 }
    result<Bool> = p1.lt(p2)  // Should work
    return 0
}
```

### Phase 3: Generic Monomorphization (v0.4.3)

**Goal:** Make generic functions with aspect bounds usable

**Tasks:**
1. ⬜ Implement monomorphization (generic expansion)
2. ⬜ Validate aspect bounds during instantiation
3. ⬜ Generate specialized code for each concrete type
4. ⬜ Cache instantiations to avoid duplication
5. ⬜ Support generic functions and structs

### Phase 4: Advanced Aspect Features (v0.5.0)

**Goal:** Complete aspect system

**Tasks:**
1. ⬜ Default method implementations
2. ⬜ Associated types (required for Iterator)
3. ⬜ Aspect objects (dynamic dispatch)
4. ⬜ Generic bind blocks
5. ⬜ Aspect composition requirements

## Roadmap Priority

### Immediate (v0.4.x)
1. **Aspect binding validation** — Connect types to aspects
2. **Aspect method calls** — value.method() syntax
3. **Aspect bounds validation** — Check `<T<Comparable>>` works

### Short-term (v0.5.0)
1. **Generic monomorphization** — `min<T<Comparable>>(a, b)`
2. **Aspect composition** — `aspect A requires B`

### Medium-term (v0.5.x)
1. **Default methods** — Aspect with implementations
2. **Associated types** — `type Item` in Iterator
3. **Generic bind** — `bind<T> Aspect -> Vec<T>`

### Long-term (v0.6.0+)
1. **Aspect objects** — Dynamic dispatch (if needed)
2. **Higher-kinded types** — Advanced generics
3. **Associated constants** — Compile-time values in aspects

## Design Questions

### Q: Why no classes?

**A:** Aspects provide everything classes do, without the complexity:
- **Polymorphism** ✅ via aspect bounds
- **Code reuse** ✅ via default aspect method implementations
- **Multiple "inheritance"** ✅ unlimited aspect bindings (no diamond problem)
- **Encapsulation** ✅ via modules and visibility (planned)

Classes add:
- ❌ Fragile base class problem
- ❌ Diamond problem complexity
- ❌ Forced inheritance hierarchies
- ❌ Runtime overhead (vtables)

See [WHY_TRAITS_NOT_CLASSES.md](WHY_TRAITS_NOT_CLASSES.md) for detailed rationale.

### Q: What about encapsulation?

**A:** Vyb will have module-level visibility:

```vyb
// Private by default in modules
struct BankAccount {
    balance<Int>  // Private field
}

pub get_balance(account<their<BankAccount>>)<Int> -> {
    return account.balance  // Module can access
}

// Outside module: cannot access balance directly
```

### Q: How to handle aspect method name collisions?

```vyb
aspect A { method(self<their<Self>>)<Int> -> }
aspect B { method(self<their<Self>>)<String> -> }

struct MyType { }
bind A -> MyType { ... }
bind B -> MyType { ... }

// Explicit disambiguation
x<Int> = value.A::method()
y<String> = value.B::method()
```

## Full Working Example

```vyb
// Define aspect
aspect Comparable {
    lt(self<their<Self>>, other<their<Self>>)<Bool> ->
    eq(self<their<Self>>, other<their<Self>>)<Bool> ->
}

// Define struct
struct Point {
    x<Int>,
    y<Int>
}

// Bind aspect to struct
bind Comparable -> Point {
    lt(self<their<Point>>, other<their<Point>>)<Bool> -> {
        return self.x < other.x || (self.x == other.x && self.y < other.y)
    }

    eq(self<their<Point>>, other<their<Point>>)<Bool> -> {
        return self.x == other.x && self.y == other.y
    }
}

// Use in generic function
min<T<Comparable>>(a<T>, b<T>)<T> -> {
    if (a.lt(b)) {
        return a
    } else {
        return b
    }
}

main()<Int> -> {
    p1<Point> = Point { x = 1, y = 2 }
    p2<Point> = Point { x = 3, y = 4 }

    smaller<Point> = min(p1, p2)  // Works! Monomorphized to min<Point>

    return smaller.x
}
```

## Conclusion

Vyb's aspect system balances:
- **Simplicity** — Structs for data, aspects for interfaces
- **Power** — Generic programming with compile-time safety
- **Flexibility** — Compose behaviors without class hierarchies
- **Performance** — Zero-cost abstractions via monomorphization

No classes. By design. Forever.
