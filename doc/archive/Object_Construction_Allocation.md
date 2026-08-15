
# Object Construction & Allocation in Vyb

Vyb provides two primary ways to create instances of user-defined types, each with different semantics and use-cases:

---

## 1. Struct Literals (Value Creation)

```vyb
var<Vector> v1 = Vector { x: 1.0, y: 2.0 }
```

- **By-value:** produces a plain `Vector` stored inline (e.g., on the stack or within another struct).
- **Zero overhead:** no heap allocation or runtime indirection.
- **Implicit copy/move:** passing `v1` to functions moves or copies the value.
- **Ideal for:** small, trivially-copyable types; performance-critical code without allocation.

---

## 2. Constructor Functions (`new`)

```vyb
fn<Vector> Vector::new(var<Float> x, var<Float> y) -> Vector {
    Vector { x, y }
}
var<Vector> v2 = Vector::new(1.0, 2.0)
```

- **Encapsulation point:** a named function to centralize initialization logic.
- **Invariants & validation:** enforce class invariants or perform checks.
- **Future-proofing:** callers use the same API even if implementation changes (e.g., adding pooling, logging, or different storage).
- **Optional heap allocation:** constructors can switch to return `my<T>` for heap-allocated ownership:

  ```vyb
  fn<my<Vector>> Vector::new(var<Float> x, var<Float> y) -> my<Vector> {
      my<Vector>(Vector { x, y })
  }
  var<my<Vector>> v3 = Vector::new(1.0, 2.0)  // now allocates on the heap
  ```

---

## 3. Pass-by-Value vs. Pointer Ownership

- **Default parameter passing** is by-value:
  ```vyb
  fn<Double> sum(var<Vector> a, var<Vector> b) -> Double { … }
  ```
  `a` and `b` are moved/copied into `sum`.

- **Heap-allocated ownership** can be achieved by returning or passing `my<T>` or `our<T>`:
  ```vyb
  fn<Void> process(var<my<Task>> task) -> Void { … }
  ```
  Ensures exclusive ownership and controlled resource lifetime.

---

## 4. When to Use Each Form

| Creation Style    | Use-Case                                                      |
|-------------------|---------------------------------------------------------------|
| **Struct Literal**| Fast, stack-based creation of simple data.                    |
| **`new()` Value** | Encapsulate complex init, enforce invariants, standard API.   |
| **`new()` Heap**  | Allocate on heap, share or transfer ownership transparently.  |

---

## 5. Documentation Recommendations

- **Spell out semantics:** clearly document whether `new()` returns a value (`Vector`) or an owned handle (`my<Vector>`).
- **Example clarity:** show both literal and constructor examples in user guides.
- **API consistency:** use `Type::new(...)` uniformly for type construction, even if it currently just wraps a literal.
- **Highlight future changes:** note that `new()` may change allocation strategy without affecting callers.

---

By explicitly documenting these distinctions—struct literals, constructors, pass-by-value, and optional heap allocation—Vyb’s programming guide will help users choose the right creation pattern for their needs.
