# Vyn Runtime: Mutability, Ownership, and References

## 1. Overview

Vyn's memory model is designed for safety and explicitness, drawing inspiration from modern systems languages. It distinguishes between:
1.  **Binding Mutability**: Whether a variable can be reassigned (`var` vs `const`).
2.  **Ownership**: Who is responsible for managing the memory of data (`my<T>`, `our<T>`, `their<T>`).
3.  **Data Mutability**: Whether the data itself can be changed, often indicated by `const` on the type (e.g., `my<T const>`).
4.  **References (Borrows)**: How non-owning pointers (`their<T>`) are created and used via `borrow` and `view` keywords.

This document details these aspects, aligning with the `mem_RFC.md` proposal.

## 2. Variable Declarations and Binding Mutability

Vyn uses two keywords for variable bindings:

*   **`var`**: Declares a mutable binding. The variable can be reassigned to a new value or a different instance of its type.
    ```vyn
    var x: Int = 10;
    x = 20; // Allowed

    var item: my<String> = make_my("hello");
    item = make_my("world"); // Allowed, old "hello" is dropped
    ```

*   **`const`**: Declares an immutable binding. The variable cannot be reassigned after initialization.
    ```vyn
    const PI: Float = 3.14159;
    // PI = 3.0; // Error: cannot reassign a const binding

    const GREETING: my<String> = make_my("Hello");
    // GREETING = make_my("Hi"); // Error
    ```
    Note: `const` on a binding only prevents reassignment. If the bound value holds a mutable type (e.g., `my<Data>`), the data *within* that value might still be modifiable through methods on `Data`, unless the type itself is immutable (e.g., `my<Data const>`).

    ```vyn
    class Counter { var value: Int = 0; fn increment(&mut self) { self.value = self.value + 1; } }
    const c: my<Counter> = make_my(Counter{});
    // c = make_my(Counter{}); // Error: c is a const binding
    c.increment(); // Allowed, if Counter.increment takes their<Counter> (or similar for owned)
                   // and modifies internal state. The binding `c` is const,
                   // but the object it points to can be mutated if its type allows.
                   // To prevent internal mutation, use `my<Counter const>`.
    ```

## 3. Ownership Qualifiers and Data Mutability

Vyn employs ownership types to manage memory and control data access:

*   **`my<T>`**: Unique-owning pointer (similar to Rust's `Box<T>`). Only one `my<T>` can own the data. When a `my<T>` goes out of scope, the data is deallocated.
*   **`our<T>`**: Shared-owning pointer (reference-counted, like `Rc<T>`/`Arc<T>`). Multiple `our<T>` pointers can co-own the data. The data is deallocated when the last `our<T>` is dropped.
*   **`their<T>`**: Borrowed pointer (non-owning reference, like `&T`/`&mut T`). It provides temporary access to data owned by `my<T>` or `our<T>`, or other `their<T>`.
*   **`ptr<T>`**: Raw pointer (like `T*`). Operations involving `ptr<T>` are typically restricted to `unsafe` blocks.

**Data Mutability** is controlled by applying `const` to the type `T` *within* the ownership wrapper:
*   `my<T>`: Unique ownership of mutable data `T`.
*   `my<T const>`: Unique ownership of immutable data `T`.
*   `our<T>`: Shared ownership of mutable data `T` (requires synchronization for thread-safety).
*   `our<T const>`: Shared ownership of immutable data `T` (inherently thread-safe for reading).
*   `their<T>`: A mutable borrow of data `T`.
*   `their<T const>`: An immutable borrow (view) of data `T`.

## 4. Borrowing: Creating `their<T>` References

Borrowed references (`their<T>`) are created using the `borrow` and `view` keywords, which act as prefix operators:

*   **`view <expr>`**: Creates an immutable borrow `their<T const>`. This provides a read-only view of the data.
    ```vyn
    var owner: my<Foo> = make_my(Foo{ value: 10 });
    var immutable_ref: their<Foo const> = view owner;
    // immutable_ref.value = 20; // Error: cannot modify through their<T const>
    print(immutable_ref.value); // OK
    ```

*   **`borrow <expr>`**: Creates a mutable borrow `their<T>`. This allows modification of the data, subject to borrowing rules (e.g., no other active borrows to the same data).
    ```vyn
    var owner: my<Foo> = make_my(Foo{ value: 10 });
    var mutable_ref: their<Foo> = borrow owner;
    mutable_ref.value = 20; // OK, owner.value is now 20
    print(mutable_ref.value); // OK
    ```

The compiler enforces borrow-checking rules to ensure memory safety (e.g., preventing simultaneous mutable and immutable borrows to the same data, or ensuring borrows do not outlive the owner).

## 5. Function Parameters

Function parameters use ownership types to define how arguments are passed:

*   **`param: T`** (where `T` is a value type like `Int`, `Bool`, or a struct passed by value): The argument is passed by value (copied).
    ```vyn
    fn process_value(data: Int) { /* ... */ }
    process_value(10);
    ```

*   **`param: my<T>`** (or `our<T>`): The argument is moved into the function. The caller loses ownership.
    ```vyn
    fn consume_data(data: my<Foo>) { /* data is now owned by this function */ }
    var my_foo: my<Foo> = make_my(Foo{});
    consume_data(my_foo);
    // my_foo is no longer valid here
    ```

*   **`param: their<T>`**: The function receives a mutable borrow. The original data must be accessible via a mutable path.
    ```vyn
    fn modify_data(data: their<Foo>) {
        data.value = data.value + 1;
    }
    var owner: my<Foo> = make_my(Foo{value: 5});
    modify_data(borrow owner); // owner.value becomes 6
    ```

*   **`param: their<T const>`**: The function receives an immutable borrow. The original data can be mutable or immutable.
    ```vyn
    fn read_data(data: their<Foo const>) {
        print(data.value);
        // data.value = 10; // Error
    }
    var owner_mut: my<Foo> = make_my(Foo{value: 7});
    const owner_const: my<Foo const> = make_my(Foo{value: 8});

    read_data(view owner_mut);
    read_data(view owner_const);
    ```

## 6. Struct and Class Fields

Fields within structs and classes are declared with a name and a type. Their mutability characteristics are primarily determined by the type system and the mutability of the struct/class instance.

```vyn
struct Point {
    x: Int; // A field of value type
    y: Int;
    meta: my<String>; // An owned field
}

// Instance mutability:
var p1: my<Point> = make_my(Point { x: 10, y: 20, meta: make_my("info") });
p1.x = 15; // Allowed, p1 is mutable and x is a value type field
p1.meta = make_my("new_info"); // Allowed, p1 is mutable, old meta is dropped

const p2: my<Point> = make_my(Point { x: 0, y: 0, meta: make_my("const_info") });
// p2.x = 5; // Error: if p2 is a const binding to my<Point>, direct field mutation
            // might be disallowed or depend on whether Point is a "const-friendly" type.
            // Typically, for `const p: my<T>`, `T` itself must be treated as `T const`.
            // To make fields behave like `const` bindings, use `my<Point const>`
            // or declare fields with immutable types like `my<String const>`.

// For more fine-grained control, fields can also have `var` or `const` specifiers,
// though this is less common than controlling mutability via the instance or type.
// The AST supports `isMutable` for FieldDeclaration, implying `var field: Type` or `const field: Type`.
// Example if `var`/`const` field modifiers are used:
struct Config {
    var refresh_rate: Int;
    const api_key: String;
}
var my_config = Config { refresh_rate: 60, api_key: "xyz" };
my_config.refresh_rate = 30; // OK
// my_config.api_key = "abc"; // Error: api_key is a const field binding
```
The interaction between instance binding mutability (`var`/`const`), ownership types (`my<T>`, `my<T const>`), and potential field-level `var`/`const` specifiers defines the overall mutability. The primary mechanism should be instance binding and type-level constness.

## 7. Rationale

This memory model, centered around `var`/`const` bindings, `my`/`our`/`their` ownership, and `view`/`borrow` for references, aims to:
-   **Ensure Memory Safety**: Through compile-time checks like borrow checking and ownership tracking.
-   **Provide Clarity**: Explicit ownership and borrowing make data flow and lifetime predictable.
-   **Offer Control**: Developers have fine-grained control over mutability at both the binding and type levels.
-   **Enable Concurrency**: `our<T const>` is inherently safe for concurrent reads, and `our<Mutex<T>>` (or similar) can be used for shared mutable state.

This model aligns Vyn with modern practices for safe and efficient systems programming.
