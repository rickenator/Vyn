# Vyb Unified Declaration Syntax

Vyb uses a unified **`name<Type>`** syntax pattern across all language constructs for consistency and clarity. This replaces the old keyword-heavy approach with a clean, uniform pattern.

## Core Syntax Principles

The unified syntax follows these patterns:
- **Variables**: `name<Type> = value`
- **Constants**: `const name<Type> = value`
- **Functions**: `name(params)<ReturnType> -> { ... }`
- **Struct Fields**: `field<Type> = default`
- **Parameters**: `param<Type> = default`

## Variable Declarations

```vyb
// Mutable variables
x<Int> = 42
name<String> = "Alice"
items<[String; 5]> = ["a", "b", "c", "d", "e"]

// Immutable constants
const PI<Float> = 3.14159
const MAX_SIZE<Int> = 1000

// Ownership wrappers
task<my<Task>> = my(Task { id: 1 })
config<our<Config>> = our(Config { debug: true })
borrowed<their<Foo>> = borrow(owner)
```

## Function Declarations

```vyb
// Basic function
add(x<Int>, y<Int>)<Int> -> {
    return x + y
}

// No parameters
main()<Int> -> {
    println("Hello, World!")
    return 0
}

// Multiple return types (tuples)
divide(a<Int>, b<Int>)<(Int, Int)> -> {
    return (a / b, a % b)
}

// Template function
max<T>(a<T>, b<T>)<T> -> {
    if a > b { return a } else { return b }
}
```

## Struct Definitions

```vyb
struct Person {
    id<Int> = 0,
    name<String>,
    age<Int> = 18,
    active<Bool> = true
}

struct Point<T> {
    x<T>,
    y<T>
}
```

## Function Parameters

```vyb
// Basic parameters
process(data<String>, count<Int>)<Void> -> { ... }

// Optional parameters with defaults
create_user(name<String>, age<Int> = 25, active<Bool> = true)<User> -> { ... }

// Const parameters
hash_data(const data<String>)<Int> -> { ... }
```

## Rationale

**Consistency**: Single `name<Type>` pattern across all constructs eliminates mental overhead of remembering different syntaxes.

**Clarity**: Type information is prominent and standardized, making code easier to read and understand.

**Simplicity**: Eliminates keywords like `var`, `fn`, and complex colon syntaxes in favor of clean, uniform patterns.

**Execution Order**: Function syntax `name(params)<ReturnType>` follows natural left-to-right reading and execution flow.

## Migration from Legacy Syntax

Old syntax is still supported for backward compatibility but new code should use unified patterns:

```vyb
// Legacy (deprecated)
var x: Int = 42
fn add(x: Int, y: Int) -> Int { return x + y }

// Modern unified syntax
x<Int> = 42
add(x<Int>, y<Int>)<Int> -> { return x + y }
```

The unified syntax represents Vyb's evolution toward maximum clarity and consistency across all language constructs.