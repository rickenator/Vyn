# Aspects vs Classes: Why Vyb Chose Aspects

## Philosophy: Composition Over Inheritance

Vyb's aspect system provides all the benefits of object-oriented programming without the
complexity and pitfalls of class hierarchies. This document explains why **structs +
aspects** are sufficient for modern software development.

> **Terminology:** Vyb uses `aspect`/`bind`. Earlier drafts used `trait`/`impl` (Rust
> vocabulary). This document uses Vyb-native terms throughout.

## The Problem with Classes

### 1. The Diamond Problem
```java
// Java/C++ style - ambiguity!
class Animal { void eat() { ... } }
class Mammal extends Animal { void eat() { ... } }
class Bird extends Animal { void eat() { ... } }
class Bat extends Mammal, Bird { // Which eat()? }
```

**Aspect Solution:**
```vyb
aspect Eats { eat(self<their<Self>>)<Void> -> { } }
aspect Flies { fly(self<their<Self>>)<Void> -> { } }

struct Bat { ... }
bind Eats -> Bat { ... }   # Clear binding
bind Flies -> Bat { ... }  # No ambiguity
```

### 2. Fragile Base Class Problem
```python
# Python - base class change breaks subclasses
class Animal:
    def move(self):
        self.walk()  # Original implementation

class Fish(Animal):
    def swim(self):
        ...
    # Assumes parent's move() calls walk()

# Later: Base class changes
class Animal:
    def move(self):
        self.run()  # Changed! Breaks Fish
```

**Aspect Solution:** No inheritance = no fragile bases. Each `bind` block stands alone.

### 3. Forced Hierarchy
```csharp
// C# - locked into wrong abstraction
class Vehicle { ... }
class Car : Vehicle { ... }
class Boat : Vehicle { ... }
class Plane : Vehicle { ... }

// Later: need AmphibiousCar
class AmphibiousCar : Car { ... }  // Can't also extend Boat!
```

**Aspect Solution:**
```vyb
aspect Drivable { ... }
aspect Floatable { ... }
aspect Flyable { ... }

struct AmphibiousCar { ... }
bind Drivable -> AmphibiousCar { ... }
bind Floatable -> AmphibiousCar { ... }
# Mix and match as needed!
```

## What Aspects Give You (Without Classes)

### ✅ Polymorphism
```vyb
aspect Drawable {
    draw(self<their<Self>>)<Void> -> { }
}

bind Drawable -> Circle { ... }
bind Drawable -> Rectangle { ... }
bind Drawable -> Text { ... }

# Generic function works with any Drawable
render<T<Drawable>>(shape<their<T>>)<Void> -> {
    shape.draw()
}
```

### ✅ Code Reuse via Default Implementations
```vyb
aspect Iterator {
    type Item                                    # associated type
    # Required
    next(self<their<Self>>)<Self::Item?> -> { }

    # Free implementation for all iterators!
    count(self<their<Self>>)<Int> -> {
        n<Int> = 0
        loop {
            match (self.next()) {
                item -> { n = n + 1 }
                ? -> { break }
            }
        }
        return n
    }

    # More default methods...
    collect(self<their<Self>>)<Vec<Self::Item>> -> { ... }
}

# Any type binding Iterator gets methods for free!
```

### ✅ Multiple "Inheritance" (Aspect Bounds)
```vyb
# Combine multiple aspect requirements
serialize<T<ToJson><Debug>>(obj<their<T>>)<String> -> {
    json<String> = obj.to_json()
    println("Serializing: " + obj.debug())
    return json
}

struct User { name<String>, age<Int> }
bind ToJson -> User { ... }
bind Debug -> User { ... }

# User satisfies both bounds - no inheritance needed!
```

### ✅ Extension Without Modification
```vyb
# Extend types you don't own
bind ToString -> Int {
    to_string(self<Int>)<String> -> {
        return int_to_str(self)
    }
}

# Even built-in types gain new capabilities
x<Int> = 42
s<String> = x.to_string()  # "42"
```

### ✅ Interface Segregation
```vyb
# Small, focused aspects
aspect Read {
    read(self<their<Self>>, buf<their<Vec<Byte>>>)<Int> -> { }
}

aspect Write {
    write(self<their<Self>>, data<their<Vec<Byte>>>)<Int> -> { }
}

# Bind only what makes sense
struct File { ... }
bind Read -> File { ... }
bind Write -> File { ... }

struct ReadOnlyArchive { ... }
bind Read -> ReadOnlyArchive { ... }
# No Write binding - type system enforces it!
```

## Advantages Over Classes

| Feature | Classes | Aspects |
|---------|---------|---------|
| **Multiple "Inheritance"** | ❌ Complex (C++) or forbidden (Java) | ✅ Unlimited aspect bindings |
| **Diamond Problem** | ❌ Requires complex resolution | ✅ Cannot occur |
| **Fragile Base** | ❌ Parent changes break children | ✅ No inheritance chain |
| **Extension** | ❌ Can't extend foreign types | ✅ Bind aspects to any type |
| **Flexibility** | ❌ Locked into hierarchy | ✅ Compose behaviors freely |
| **Testability** | ⚠️ Requires mocking frameworks | ✅ Easy to create test bindings |
| **Performance** | ⚠️ Often requires vtables | ✅ Static dispatch (zero cost) |

## Real-World Examples

### Example 1: HTTP Client
```vyb
# Without classes - compose behaviors
struct HttpClient {
    timeout<Int>,
    base_url<String>
}

bind Read -> HttpClient {
    read(self<their<Self>>, buf<their<Vec<Byte>>>)<Int> -> {
        # HTTP GET implementation
    }
}

bind Write -> HttpClient {
    write(self<their<Self>>, data<their<Vec<Byte>>>)<Int> -> {
        # HTTP POST implementation
    }
}

bind Closeable -> HttpClient {
    close(self<own<Self>>)<Void> -> {
        # Clean up connections
    }
}

# Client can be used anywhere that needs Read, Write, or Closeable
# No forced class hierarchy!
```

### Example 2: Game Entities
```vyb
# Bad class design:
# class Entity > Renderable > Movable > Collidable > Enemy
# What if we need a static collidable object?
# What if we need a moving decoration that doesn't collide?

# Aspects solution:
aspect Renderable { render(self<their<Self>>)<Void> }
aspect Movable { move(self<their<Self>>, delta<Float>)<Void> }
aspect Collidable { check_collision(self<their<Self>>, other<their<Box>>)<Bool> }
aspect Enemy { take_damage(self<their<Self>>, amount<Int>)<Void> }

# Mix and match as needed:
struct MovingEnemy { ... }
bind Renderable -> MovingEnemy { ... }
bind Movable -> MovingEnemy { ... }
bind Collidable -> MovingEnemy { ... }
bind Enemy -> MovingEnemy { ... }

struct StaticWall { ... }
bind Renderable -> StaticWall { ... }
bind Collidable -> StaticWall { ... }
# No Movable - type system prevents moving it!

struct Decoration { ... }
bind Renderable -> Decoration { ... }
bind Movable -> Decoration { ... }
# No Collidable - can move through it!
```

### Example 3: Serialization
```vyb
aspect ToJson {
    to_json(self<their<Self>>)<String> -> { }
}

# Bind for primitive types
bind ToJson -> Int { ... }
bind ToJson -> String { ... }
bind ToJson -> Bool { ... }

# Generic binding for Vec
bind<T<ToJson>> ToJson -> Vec<T> {
    to_json(self<their<Vec<T>>>)<String> -> {
        elements<Vec<String>> = Vec<String>.new()
        for (item in self) {
            elements.push(item.to_json())
        }
        return "[" + elements.join(",") + "]"
    }
}

# User-defined types
struct User { name<String>, age<Int> }
bind ToJson -> User {
    to_json(self<their<User>>)<String> -> {
        return "{\"name\":\"" + self.name + "\",\"age\":" + self.age.to_json() + "}"
    }
}

# Everything composes naturally!
```

## Classes Are Not Planned for Vyb

**Classes are NOT part of Vyb's design — now or in the future.**

Why?
1. **Aspects are strictly more powerful** — Everything classes do, aspects do better
2. **Simpler mental model** — Structs + Aspects + Functions = Complete
3. **Better with ownership** — Aspect bounds work naturally with `my`/`our`/`their` semantics
4. **Modern best practice** — Rust, Go, Swift all moving away from inheritance
5. **Prevents bad patterns** — Forces composition over inheritance

If you come from an OOP background and feel the urge to propose a class system, read this
document again. Then read `doc/TRAIT_SYSTEM_DESIGN.md`. Aspects + structs cover every
use case that classes address, without the drawbacks.

## The Vyb Way

```vyb
# Data = Structs
struct User {
    name<String>,
    email<String>,
    age<Int>
}

# Behavior = Functions (for simple cases)
validate_email(email<their<String>>)<Bool> -> {
    return email.contains("@")
}

# Polymorphism = Aspects (when needed)
aspect Validatable {
    validate(self<their<Self>>)<Bool> -> { }
}

bind Validatable -> User {
    validate(self<their<User>>)<Bool> -> {
        return self.age >= 18 && validate_email(&self.email)
    }
}

# Generic constraints = Aspect bounds
save_to_db<T<Validatable><ToJson>>(item<their<T>>)<Bool> -> {
    if (!item.validate()) {
        return false
    }
    db.insert(item.to_json())
    return true
}
```

**This is the complete toolkit. No classes needed.** 🚀

---

**TL;DR:**
- ✅ Structs = Data
- ✅ Aspects = Behavior Contracts
- ✅ Bind = Connect Data to Behavior
- ✅ Generics = Parameterize Over Types
- ✅ **Result: Everything you need, nothing you don't**

**No classes. By design. Forever.** 💪
