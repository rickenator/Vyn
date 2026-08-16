# Vyb Ownership Types: `mild<T>`

## Overview

`mild<T>` is Vyb's fourth ownership type, providing **mild references** to `our<T>` (shared ownership) objects. It solves the circular reference problem and enables patterns like observers, caches, and back-pointers in tree structures.

## The Four Ownership Types

| Type | Ownership | Copying | Nullability | Use Case |
|------|-----------|---------|-------------|----------|
| `my<T>` | Unique | Move only | Never null | Exclusive ownership |
| `our<T>` | Shared (ref-counted) | Clone via `our(x)` | Never null | Multiple owners |
| `their<T>` | Borrowed | Temporary | Never null | Function parameters |
| `mild<T>` | Mild (non-owning) | Clone via `soft(x)` | Can become null | Break cycles, observers |

## Why `mild<T>`?

### Problem: Circular References

```vyb
# Without mild: Memory leak!
struct Node {
    next: our<Node>,  # Strong reference
    prev: our<Node>   # Strong reference - CIRCULAR!
}
# Both nodes hold strong references to each other
# Reference counts never reach zero → memory leak
```

### Solution: Mild References

```vyb
# With mild: No leak!
struct Node {
    next: our<Node>,   # Strong reference (owns)
    prev: mild<Node>   # Mild reference (doesn't own, breaks cycle)
}
# When next is dropped, prev doesn't prevent cleanup
```

## Key Methods

### `grab() -> Option<our<T>>`

Attempts to upgrade the mild reference to a strong reference. Returns
`Some(our<T>)` (with `strong_count` incremented) while the target is live, and
`None` once the object has been released. The failed-upgrade path is therefore
type-safe and detectable with `match`/`select` (or the `.value` accessor) without
dereferencing a dangling handle.

```vyb
node<our<Node>> = get_node()
shadow<mild<Node>> = soft(node)

# Match on the upgrade result
result<Option<our<Node>>> = shadow.grab()
match (result) {
    Some(strong) -> println(strong.value)
    None -> println("Node no longer exists")
}
```

### `released() -> Bool`

Checks if the referenced object has been destroyed. Returns `true` if the object is dead, `false` if still alive.

```vyb
if (shadow.released()) {
    println("Object was released")
} else {
    println("Object is still alive")
}

# More idiomatically
if (!shadow.released()) {
    # Safe to try grab()
}
```

## Common Patterns

### 1. Tree with Parent Pointers

```vyb
struct TreeNode {
    value: Int,
    children: Vec<our<TreeNode>>,  # Own children
    parent: mild<TreeNode>         # Don't own parent
}

fn get_parent_value(node: our<TreeNode>) -> Int {
    if let parent = node.parent.grab() {
        return parent.value
    }
    return -1  # Root node or parent destroyed
}
```

### 2. Observer Pattern

```vyb
struct Subject {
    observers: Vec<mild<Observer>>
}

fn notify(subject: our<Subject>) -> Void {
    for (shadow in subject.observers) {
        if let observer = shadow.grab() {
            observer.update()  # Still alive
        }
        # Otherwise skip - observer was destroyed
    }

    # Optional: Clean up released observers
    subject.observers = subject.observers.filter(|o| !o.released())
}
```

### 3. Cache with Expiring Entries

```vyb
struct Cache {
    entries: Vec<mild<Entry>>
}

# Cache doesn't prevent entries from being freed
# When user drops all strong references to an entry,
# the cache's mild reference becomes invalid

fn get_valid_entries(cache: our<Cache>) -> Vec<our<Entry>> {
    result: Vec<our<Entry>> = Vec()
    for (shadow in cache.entries) {
        if let entry = shadow.grab() {
            result.push(entry)
        }
    }
    return result
}
```

### 4. Back-References in Doubly-Linked List

```vyb
struct ListNode {
    value: Int,
    next: our<ListNode>?,    # Strong reference (owns next)
    prev: mild<ListNode>     # Mild reference (doesn't own prev)
}

fn insert_after(node: our<ListNode>, new_node: our<ListNode>) -> Void {
    new_node.next = node.next
    new_node.prev = soft(node)
    node.next = our(new_node)
}
```

## Implementation Details

### Current Implementation Status

Vyb now has a minimal real runtime model for `our<T>` / `mild<T>`:

- `our(expr)` allocates the payload and a control block.
- `soft(ourValue)` increments `weak_count` and returns a `mild<T>` handle tied
  to the same control block.
- `mild<T>.released()` reads the control block release flag.
- `mild<T>.grab()` increments `strong_count` and returns an `our<T>` handle when
  the payload is live.
- When the last local strong owner in a scope is cleaned up, the payload is
  freed and the control block is marked released while weak handles remain.
- Returning a local `our<T>` or `mild<T>` transfers that local handle to the
  caller instead of cleaning it up before return.
- `our<T>` copy/assignment/parameter semantics are complete: every storage
  location holds its own strong ref, retained on shared copy and released on
  scope exit / overwrite. Full `my<T>` move semantics and a complete ownership
  transfer checker remain future work.
- Control-block cleanup is minimal and focused on current scope/return paths.

### Control Block Structure

`our<T>` objects maintain a control block with:
- **strong_count**: Number of `our<T>` references
- **weak_count**: Number of `mild<T>` references
- **object**: Pointer to the actual data

### Lifecycle Rules

1. **When `our<T>` strong_count reaches 0:**
   - Object is destroyed
   - Memory is freed
   - Control block is kept if `weak_count > 0`
   - Control block marks object as "released"

2. **When `mild<T>` is destroyed:**
   - `weak_count` is decremented
   - If `weak_count == 0` and object was already freed, control block is freed

3. **When `mild<T>.grab()` is called:**
   - If object is still alive, `strong_count++` and return `our<T>`
   - If object was released, return `nil`

4. **When `mild<T>.released()` is called:**
   - Check control block's "released" flag
   - Return `true` if object destroyed, `false` otherwise

## Comparison with Other Languages

| Language | Mild/Weak Reference Type | Upgrade Method | Check Method |
|----------|---------------------|----------------|--------------|
| **Vyb** | `mild<T>` | `grab() -> our<T>?` | `released() -> Bool` |
| C++ | `std::weak_ptr<T>` | `lock() -> shared_ptr<T>` | `expired() -> bool` |
| Rust | `Weak<T>` | `upgrade() -> Option<Rc<T>>` | `strong_count() == 0` |
| Swift | `weak var` | Automatic upgrade | Check `!= nil` |

## Best Practices

### ✅ Do Use `mild<T>` For:
- Parent pointers in tree structures
- Back-references in linked structures
- Observer/listener patterns
- Caches that don't own entries
- Breaking reference cycles

### ❌ Don't Use `mild<T>` For:
- Function parameters (use `their<T>` instead - faster, simpler)
- Short-lived references (use `their<T>`)
- When you need guaranteed validity (use `our<T>`)

### Performance Considerations
- `mild<T>` has overhead: control block must survive object destruction
- `grab()` requires atomic increment of strong count
- Use `their<T>` for temporary borrows (zero overhead)
- Use `mild<T>` only when you need to detect object destruction

## Example: Complete Tree Implementation

```vyb
struct TreeNode {
    value: Int,
    children: Vec<our<TreeNode>>,
    parent: mild<TreeNode>
}

fn create_node(value: Int, parent: mild<TreeNode>) -> our<TreeNode> {
    return our(TreeNode {
        value: value,
        children: Vec(),
        parent: parent
    })
}

fn add_child(parent: our<TreeNode>, value: Int) -> our<TreeNode> {
    child: our<TreeNode> = create_node(value, soft(parent))
    parent.children.push(child)
    return child
}

fn get_ancestors(node: our<TreeNode>) -> Vec<Int> {
    ancestors: Vec<Int> = Vec()
    current: mild<TreeNode> = node.parent

    while (!current.released()) {
        if let parent = current.grab() {
            ancestors.push(parent.value)
            current = parent.parent
        } else {
            break
        }
    }

    return ancestors
}

fn main() -> Int {
    root: our<TreeNode> = create_node(1, mild<TreeNode>())  # Root has no parent
    child1: our<TreeNode> = add_child(root, 2)
    child2: our<TreeNode> = add_child(child1, 3)

    ancestors: Vec<Int> = get_ancestors(child2)
    # Should return [2, 1]

    return 0
}
```

## Summary

`mild<T>` is Vyb's solution to circular references and the observer pattern. It provides:
- **Mild (non-owning) references** to `our<T>` objects
- **Safe access** via `grab()` that returns `our<T>?`
- **Lifecycle detection** via `released()`
- **Zero cost** when not used (no impact on `my<T>`, `our<T>`, or `their<T>`)

Use `mild<T>` when you need long-lived references that don't prevent cleanup, and use `their<T>` for temporary borrows with guaranteed validity.
