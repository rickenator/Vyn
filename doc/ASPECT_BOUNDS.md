# Aspect Bounds in Vyb

**Version:** 0.4.1
**Status:** Implementation in Progress (Phase 6 Step 5)
**Last Updated:** 2025-10-19

## Critical Concept: What Does a Bind Do?

A **bind** adds an aspect to a type, making that aspect's methods **callable by external code**. The bind implementation defines **how** those methods work.

Simple receiver parameters are written as `self`. This is canonical sugar for `self<Self>` in aspect and bind methods. Keep explicit receiver types, such as `self<their<Self>>`, when ownership or borrowing mode is part of the contract.

### Example:
```vyb
bind Display -> Point {
    show(self)<Void> -> {
        println("Point(x, y)");
    }
}

// Result: External code can now call:
point<Point> = Point { x = 5, y = 10 };
point.show();  // ✅ Works! Prints "Point(x, y)"
```

**The bind gave Point the Display aspect.**

## Unbounded vs Bounded Binds

### Unbounded: `bind<T> Aspect -> Type<T>`

**What It Does:**
- Gives the aspect to `Type<T>` for **ANY type T**
- External code can call aspect methods no matter what T is
- `Box<Int>`, `Box<String>`, `Box<Anything>` all get the aspect

**Inside the Bind Implementation:**
- T is **opaque** - you don't know what methods T has
- Can access struct fields, but **cannot call T's methods**
- Cannot assume T has any aspects

**Example:**
```vyb
bind<T> Display -> Box<T> {
    show(self)<Void> -> {
        println("Box contains a value (type unknown)");
        // ❌ CANNOT: self.value.show()
        // T might not have Display!
    }
}

// External usage:
box1<Box<Int>> = Box { value = 42 };
box1.show();  // ✅ Works: "Box contains a value"

box2<Box<String>> = Box { value = "hello" };
box2.show();  // ✅ Works: "Box contains a value"
```

**Use Case:** Generic wrapper that displays itself, not its contents.

### Bounded: `bind<T<Aspect1, Aspect2>> Aspect -> Type<T>`

**What It Does:**
- Gives the aspect to `Type<T>` **ONLY when T has the required aspects**
- External code can call aspect methods **only if T satisfies bounds**
- `Box<Point>` gets the aspect **IF** Point has required aspects
- `Box<Int>` does NOT get this bind if Int lacks required aspects

**Inside the Bind Implementation:**
- T is **known** to have the bound aspects
- You **CAN call T's aspect methods**
- Lets you delegate to T's implementations or use them in custom ways

**Example:**
```vyb
bind<T<Display>> Display -> Box<T> {
    show(self)<Void> -> {
        println("Box containing:");
        self.value.show();  // ✅ ALLOWED: bound guarantees T has Display
    }
}

// External usage:
// Assume Point has Display, Int does not

box1<Box<Point>> = Box { value = p };
box1.show();  // ✅ Works: "Box containing:\nPoint(x, y)"

box2<Box<Int>> = Box { value = 42 };
box2.show();  // ❌ ERROR: Box<Int> doesn't have this Display bind
              // (Int doesn't satisfy T<Display> bound)
```

**Use Case:** Wrapper that shows its contents by delegating to T's aspect.

## Multiple Bounds

Use commas to require multiple aspects:

```vyb
bind<T<Display, Clone>> Clone -> Box<T> {
    clone(self)<Self> -> {
        println("Cloning box and its contents");
        clonedValue<T> = self.value.clone();  // ✅ T has Clone
        clonedValue.show();  // ✅ T has Display
        result<Box<T>> = Box { value = clonedValue };
        return result;
    }
}
```

Comma means **AND** - T must have ALL specified aspects.

## Generic Functions with Bounds

Bounds work the same way for generic functions:

```vyb
// Function only works when T has Display
printItem<T<Display>>(item<T>)<Void> -> {
    item.show();  // ✅ ALLOWED: bound guarantees T has Display
}

// Usage:
printItem(point);  // ✅ Works if Point has Display
printItem(42);     // ❌ ERROR: Int doesn't have Display
```

### Multiple Bounds in Functions

```vyb
duplicateAndShow<T<Display, Clone>>(item<T>)<T> -> {
    copy<T> = item.clone();  // ✅ T has Clone
    copy.show();  // ✅ T has Display
    return copy;
}
```

## Key Insights

### 1. Bounds Affect Implementation, Not Just Usage

**Unbounded:**
- **External:** Any `Box<T>` can use the method
- **Internal:** Implementation can't use T's methods

**Bounded:**
- **External:** Only `Box<T>` where T has required aspects can use the method
- **Internal:** Implementation CAN use T's aspect methods

### 2. Both Add Usable Methods

Both bounded and unbounded binds **add methods that external code can call**. The difference is:
- **When** the bind applies (always vs conditionally)
- **How** the implementation works (generic vs T-aware)

### 3. Bounds Are Compile-Time Guarantees

When you write `<T<Display>>`:
- **Semantic Analysis:** Validates that bounds are actual aspects
- **Type Checking:** Ensures T has required aspects at call sites
- **Inside Implementation:** Compiler knows T's methods exist - no runtime checks

### 4. Multiple Binds Can Coexist

```vyb
// Both can exist:
bind<T> Display -> Box<T> { ... }           // Applies to all Box<T>
bind<T<Display>> Display -> Box<T> { ... }  // Applies when T has Display

// The more specific (bounded) version takes precedence when applicable
```

## Syntax Summary

```vyb
// Aspect definition
aspect AspectName {
    method(self)<ReturnType>  // Mandatory (no arrow)
    method(self)<ReturnType> -> { ... }  // Optional with default
}

// Unbounded bind (T is opaque)
bind<T> Aspect -> Type<T> { ... }

// Single bound (T must have Aspect1)
bind<T<Aspect1>> Aspect -> Type<T> { ... }

// Multiple bounds (T must have Aspect1 AND Aspect2)
bind<T<Aspect1, Aspect2>> Aspect -> Type<T> { ... }

// Generic function with bounds
functionName<T<Aspect1, Aspect2>>(param<T>)<ReturnType> -> { ... }
```

## Associated Types (First Practical Slice)

Associated types can now be declared on aspects and assigned in binds.

```vyb
aspect Iterator {
    type Item
    next(self)<Self::Item>
}

bind Iterator -> CounterIter {
    type Item = Int

    next(self)<Int> -> {
        current<Iterator::Item> = self.value
        return current
    }
}
```

### Current validation

- Missing associated type assignment in a `bind` is a semantic error.
- Unknown associated type assignment in a `bind` is a semantic error.
- Duplicate associated type assignment in the same `bind` is a semantic error.

### Current limitations (not in this slice)

- No associated type defaults.
- No where-clause-style associated type constraints.
- No `dyn Aspect` associated type dispatch support.

## Implementation Status

### ✅ Completed (v0.4.1)
- Parser support for `<T<Aspect1, Aspect2>>` syntax
- Semantic validation that bounds are actual aspects
- Bounds stored in symbol table for type parameters
- Error reporting for invalid bounds
- Method calls on bounded type parameters allowed
- First associated-type slice: declarations in `aspect`, assignments in `bind`, and semantic diagnostics for missing/unknown/duplicate assignments

### ⏳ In Progress
- Monomorphization with bounds checking
- Bind selection based on bounds satisfaction

### 📋 Planned
- Associated type defaults and richer associated-type bounds/constraints
- Trait objects for dynamic dispatch
- Higher-order bounds

## Examples

See [`test/aspect/test_aspect_bounds.vyb`](../test/aspect/test_aspect_bounds.vyb) for comprehensive examples.

## Related Documentation

- [Aspect System Overview](AST_Design_Considerations.md)
- [AST Overview](AST_Overview.md)
- [TODO](../TODO.md) - Road to 1.0 checklist
