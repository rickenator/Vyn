# Lambda Expressions and Closures in Vyb

## Overview

Vyb supports **lambda expressions** (anonymous functions) with **closure capture**, enabling functional programming patterns like map/filter/reduce, callbacks, and higher-order functions.

## Syntax

### Basic Lambda

```vyb
// Syntax: |param1, param2| -> expression
add = |x, y| -> x + y

result<Int> = add(5, 3)  // result = 8
```

### Lambda with Type Annotations

Parameter types can be explicitly specified:

```vyb
// Optional type annotations for clarity
multiply = |x<Int>, y<Int>| -> x * y
```

### Lambda with Block Body

For multi-line lambdas, use a block with explicit `return`:

```vyb
compute = |n| -> {
    result<Int> = n * 2
    result = result + 1
    return result
}
```

### Empty Parameter List

```vyb
// No parameters: || -> expression
getRandom = || -> 42
```

## Closure Capture

Lambdas can **capture** variables from their enclosing scope, creating **closures**:

```vyb
makeAdder(base<Int>) -> {
    // Lambda captures 'base' from outer scope
    return |x| -> x + base
}

addTen = makeAdder(10)
result<Int> = addTen(5)  // result = 15
```

### Capture Semantics

- **Captured by value**: Variables are copied into the closure at creation time
- **Ownership types**: Captured variables with ownership types (`my<T>`, `our<T>`, `mild<T>`) follow standard ownership rules
- **Thread-safety**: Closures with `our<T>` captures use atomic reference counting

**Example with ownership:**

```vyb
makeObserver(data<our<Data>>) -> {
    // 'data' is captured - increments strong reference count
    return || -> {
        return data  // Returns shared reference
    }
}
```

## Higher-Order Functions

Functions that accept or return lambdas:

### Map

```vyb
map(arr<[Int]>, transform) -> {
    result<[Int]> = []
    for item in arr {
        result.push(transform(item))
    }
    return result
}

numbers<[Int]> = [1, 2, 3, 4]
doubled<[Int]> = map(numbers, |x| -> x * 2)  // [2, 4, 6, 8]
```

### Filter

```vyb
filter(arr<[Int]>, predicate) -> {
    result<[Int]> = []
    for item in arr {
        if predicate(item) {
            result.push(item)
        }
    }
    return result
}

numbers<[Int]> = [1, 2, 3, 4, 5, 6]
evens<[Int]> = filter(numbers, |x| -> x % 2 == 0)  // [2, 4, 6]
```

### Reduce

```vyb
reduce(arr<[Int]>, initial<Int>, accumulator) -> {
    result<Int> = initial
    for item in arr {
        result = accumulator(result, item)
    }
    return result
}

numbers<[Int]> = [1, 2, 3, 4]
sum<Int> = reduce(numbers, 0, |acc, x| -> acc + x)  // 10
```

## Async Lambdas

Lambdas can be asynchronous when combined with `async`/`await`:

```vyb
// TODO: Async lambda syntax (future feature)
// asyncOp<async fn(String) -> String> = async |name| -> {
//     result<String> = await fetchData(name)
//     return result
// }
```

## Implementation Details

### Parsing

Lambdas are parsed in `parse_primary()` when a `|` (PIPE) token is encountered:

1. Parse parameter list: `|param1, param2|`
2. Optionally parse type annotations: `|x<Int>, y<String>|`
3. Expect `->` arrow token
4. Parse body (expression or block)
5. Create `FunctionExpression` AST node

### Semantic Analysis

The semantic analyzer:

1. **Detects captured variables**: While visiting the lambda body it records the
   identifiers referenced and the names declared locally (parameters + local
   declarations), then stores the free variables that resolve to an enclosing
   scope in the `FunctionExpression`'s `capturedVariables` list. Inner
   declarations shadowing an outer name are excluded, so a capture is only made
   for a reference that truly resolves outside the lambda.
2. **Type inference**: Infers parameter types from usage if not explicitly annotated
3. **Closure validation**: Ensures captured variables are in scope and have valid lifetimes

### Code Generation (LLVM)

For each lambda a *closure value* is produced — the runtime type of a `fn` is the
uniform struct `{ ptr env, ptr fn }`:

1. **Generate unique function**: Create LLVM function with mangled name (`lambda_1`, `lambda_2`, etc.)
2. **Capture environment**: If the lambda captures, malloc a per-capture struct
   and copy each captured variable's *current value* into it at creation time
   (by-value semantics). Non-capturing lambdas use a null environment.
3. **Hidden parameter**: The lambda function takes the environment as a hidden
   first parameter (`ptr %env`).
4. **Extract captures**: In the lambda's prologue, GEP+load each capture from the
   environment into a local alloca so the body reads it like any local.
5. **Return closure**: Return the closure struct `{ env, fn }` (stored, passed,
   and called; a call extracts `fn` and `env` and passes `env` first).

**Example IR for a capturing closure `|x<Int>| -> x + base` (captures `base`):**

```llvm
; Closure struct for: |x| -> x + base
%closure_t = type { i32 }  ; Contains 'base' (Int)
; Uniform Vyb fn type
%fn_t = type { ptr, ptr }  ; { env, fn }

; Lambda function with closure parameter
define i32 @lambda_1(i8* %closure_ptr, i32 %x) {
entry:
  ; Extract 'base' from closure
  %closure = bitcast i8* %closure_ptr to %closure_t*
  %base_ptr = getelementptr %closure_t, %closure_t* %closure, i32 0, i32 0
  %base = load i32, i32* %base_ptr

  ; Compute: x + base
  %result = add i32 %x, %base
  ret i32 %result
}
```

Capture forms

- **By value (default)** — The environment holds a copy from closure creation, so
  later writes to the outer variable do not affect an already-created closure,
  and each closure instantiation gets its own independent copy.
- **Mutable** — When a lambda body assigns to a captured variable, the
  environment stores the *address* of the outer variable. Each invocation
  snapshots the current value into a local alloca, and assignments (plain and
  compound, including `-=`/`*=`/etc.) write back through that address, so the
  enclosing scope observes every mutation and later invocations start from the
  latest value.
- **Move** — Capturing a `my<T>` transfers ownership into the closure; the
  semantic analyzer marks the outer variable as moved, and reading it afterward
  is a use-after-move diagnostic.
- **`our<T>` (shared)** — Capturing an `our<T>` bumps its strong count so the
  shared value stays alive for the closure's lifetime. The closure environment is
  heap-allocated and currently never freed, so the count is intentionally not
  balanced by a closure-side release.

**Notes & safety**: a mutable capture holds a pointer to the outer variable's
stack location, so a closure with mutable captures must not outlive its defining
function. Returning such a closure is rejected at compile time (a dangling
pointer) instead of silently producing a use-after-free. Block lambdas follow
named-function semantics: their value comes from explicit `return` statements;
a block lambda with no `return` is `void` (and can be called as a `fn(...) ->
void`). Zero-arg lambdas may be written `|| -> body`.

## Examples

### Event Handlers

```vyb
struct Button {
    onClick
}

counter<Int> = 0
button<Button> = Button {
    onClick: || -> {
        counter = counter + 1
        println("Clicked: " + counter)
    }
}
```

### Custom Iterators

```vyb
forEach(arr<[Int]>, action) -> {
    for item in arr {
        action(item)
    }
}

numbers<[Int]> = [1, 2, 3]
forEach(numbers, |n| -> println(n))
```

### Function Composition

```vyb
compose(f, g) -> {
    return |x| -> f(g(x))
}

addTwo = |x| -> x + 2
mulThree = |x| -> x * 3

combined = compose(addTwo, mulThree)
result<Int> = combined(5)  // (5 * 3) + 2 = 17
```

### Observer Pattern with Closures

```vyb
struct Subject {
    observers

    notify(self, message<String>) -> {
        for observer in self.observers {
            observer(message)
        }
    }
}

subject<Subject> = Subject { observers: [] }

// Add observers that capture different contexts
name<String> = "Alice"
subject.observers.push(|msg| -> {
    println(name + " received: " + msg)
})

subject.notify("Hello!")  // "Alice received: Hello!"
```

## Current Limitations

1. **Codegen not yet implemented**: Lambda parsing and semantic analysis work, but LLVM code generation is TODO
2. **No async lambdas**: Async support requires integration with the async runtime
3. **No move semantics**: Closures currently copy captures; move semantics for `my<T>` captures not yet supported
4. **No generic lambdas**: Type parameters on lambdas not yet supported

## Future Enhancements

- **Move capture**: Transfer ownership of captured variables into the lambda
- **Mutable capture**: Allow modifying captured variables
- **Generic lambdas**: `|x<T>| -> ...` with type parameters
- **Async lambdas**: `async |x| -> await process(x)`

## Comparison with Other Languages

### Rust

```rust
// Rust
let add = |x, y| x + y;
let adder = |x| move |y| x + y;  // Move capture
```

### JavaScript

```javascript
// JavaScript
const add = (x, y) => x + y;
const adder = (x) => (y) => x + y;  // Closure
```

### Python

```python
# Python
add = lambda x, y: x + y
adder = lambda x: lambda y: x + y  # Closure
```

### Vyb

```vyb
// Vyb
add = |x, y| -> x + y
adder = |x| -> |y| -> x + y  // Closure
```

## See Also

- [AST Overview](AST_Overview.md) - `FunctionExpression` node
- [Memory Operations](Memory_Operations.md) - Ownership in closures
- [Async/Await](Async_Programming_Debug_System.md) - Future async lambda support
