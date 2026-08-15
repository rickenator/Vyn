
# Vyb Unified Function Declaration Syntax

This document defines the unified function declaration syntax in Vyb using the `name<Type>` pattern. This eliminates the `fn` keyword in favor of a clean, execution-order syntax.

---

## 1. Grammar (EBNF)

```ebnf
FunctionDecl ::= Identifier "(" ParamList ")" "<" TypeList ">" "->" Body

TypeList     ::= Type { "," Type }  // Single or multiple return types
ParamList    ::= [ Param { "," Param } ]
Param        ::= [ "const" ] Identifier "<" Type ">" [ "=" Expression ]

Body         ::= Block
               | Expression

Block        ::= "{" Statement* [ Expression ] "}"
Expression   ::= <any single Vyb expression>  // no braces required
```

- **Function name** comes first, following natural execution order
- **Parameters** in parentheses use unified `param<Type>` syntax
- **Return type(s)** are declared in `<Type>` or `<Type1, Type2, ...>` after parameters
- **Multi-value returns** use comma-separated types: `name()<Int, String, Bool>`
- **`->`** is **mandatory**: it clearly demarcates the end of the signature and the start of the body
- **Braces `{}`** around the body are optional only when the body is a single expression

---

## 2. Examples

### 2.1 Block Body (with braces)

```vyb
class Node {
  is_leaf<Bool>

  // Constructor with a block body
  new(const is_leaf_param<Bool>)<Node> -> {
    Node { is_leaf: is_leaf_param }
  }
}
```

### 2.2 Single-Expression Body (braces optional)

```vyb
// A concise function doubling its input
double(x<Int>)<Int> -> x * 2
```

```vyb
// Reading a value from a shared config
is_debug(cfg<our<Config>>)<Bool> -> view(cfg).debug
```

### 2.3 Multi-Value Returns

```vyb
// Function returning multiple values
get_user_info(user_id<Int>)<Int, String> -> {
    return 42, "John Doe"
}

// Multi-value return with auto-serialization in main()
main()<Int, String, Bool> -> {
    return 42, "Hello, World!", true
    // Output: {"Int":42,"String":"Hello, World!","Bool":true}
}

// Calling multi-value function
process_user()<String> -> {
    id<Int>, name<String> = get_user_info(123)
    return "User: " + name
}
```

### 2.4 Using Relaxed Parameter Syntax (shorthand)

```vyb
// Basic unified syntax
add(x<Int>, y<Float>)<Float> -> x + y

// Const parameters
format(prefix<String>, const value<Int>)<String> -> prefix + value.to_string()

// Complex ownership types
process(task<my<Task>>, const data<their<Data const>>)<Void> -> {
    task.run(data)
}

// With generics
first_element<T>(array<my<[T]>>)<T> -> array[0]

// Multi-value return
parse_input(input<String>)<Int, String> -> {
    return input.length(), input.trim()
}
```

---

## 3. Rationale

1. **Execution Order**: `name(params)<ReturnType>` follows natural left-to-right reading flow
2. **Consistency**: Unified `name<Type>` pattern across all language constructs
3. **Clarity**: Eliminates keyword noise (`fn`) in favor of direct declaration
4. **Simplicity**: Single pattern to learn and remember
5. **Natural Flow**: Parameters → Return Type → Implementation mirrors function execution

---

## 4. Notes

- Multi-line or statement-rich bodies should always use braces for clarity
- Single-expression functions can omit braces for conciseness
- The unified syntax represents the evolution toward maximum consistency in Vyb
- Legacy `fn<Type>` syntax remains supported for backward compatibility
