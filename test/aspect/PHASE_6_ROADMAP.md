# Phase 6: Generic Aspects - Roadmap

## Overview
Phase 6 extends the aspect system (Vyb's trait/interface system) to support:
1. Generic aspects with type parameters
2. Bounds on generic type parameters
3. Generic implementations with Self type resolution
4. Method monomorphization for generic implementations

## Current Status (October 2025)

### ✅ COMPLETED Steps

**Step 5: Aspect Bounds** ✅ (October 19, 2025)
- Parser: `<T<Display, Clone>>` nested angle bracket syntax
- Semantic: Bounds validation (must be aspects, not structs)
- Storage: Bounds in symbol table for all type parameters
- Method calls: Bounded parameters can call aspect methods
- Documentation: ASPECT_BOUNDS.md comprehensive guide
- Test: test_aspect_bounds.vyb validates all features

### ⏳ IN PROGRESS / TODO Steps

**Step 1: Self Type Resolution** (Partially Complete)
- ❌ Self in parameter types: `show(self<Self>)` → needs resolution
- ✅ Self in return types: Working in some contexts
- ⏳ Self in generic impls: `impl<T> Display for Box<T>` where Self = Box<T>

**Step 2: Method Body Monomorphization** (Future)
- Generate specialized method implementations for concrete types
- Cache monomorphized methods to avoid duplicates

**Step 6: Associated Types** (Future)
- Aspect-associated types like `trait Iterator { type Item; }`

**Step 7: Type Inference** (Future)
- Infer type arguments from call sites
- Generic constructor inference: `Box { value = x }` → `Box<T>`

## Phase 6 Architecture

### Current Aspect System Features

**Aspects** (Vyb's name for traits/interfaces):
```vyb
aspect Display {
    show(self<Self>)<String> -> { }
}
```

**Binds** (Vyb's name for trait implementations):
```vyb
bind Display -> Point {
    show(self<Self>)<String> -> { return "Point" }
}
```

**Generic Binds**:
```vyb
bind<T> Display -> Box<T> {
    show(self<Self>)<String> -> { return "Box" }
}
```

**Bounded Generic Binds** ✅:
```vyb
bind<T<Display>> Display -> Box<T> {
    show(self<Self>)<String> -> {
        self.value.show();  // ✅ Works! T has Display bound
        return "Box containing: ..."
    }
}
```

## Next Steps for Phase 6

### Priority 1: Fix Self Type Resolution (Step 1)
Currently blocking test_aspect_bounds.vyb with 6 errors.

**Problem**: `self<Self>` in bind methods doesn't resolve properly
```vyb
bind Display -> Point {
    show(self<Self>)<String> -> {  // Self not resolving
        return "Point"
    }
}
```

**Files to Modify**:
- `src/vre/semantic.cpp` - TraitImpl visitor
- Enhance parameter type resolution to substitute Self

### Priority 2: Generic Constructor Inference (Step 7)
Quality of life improvement, blocking 3 test errors.

**Problem**: Can't infer type arguments from constructor
```vyb
box<Box<Point>> = Box { value = point };  // Says "got Box" not "Box<Point>"
```

**Solution**: Infer from variable type annotation and field types

### Priority 3: Method Monomorphization (Step 2)
Generate specialized LLVM functions for generic bind methods.

**Current**: Generic binds parse but don't generate specialized code
**Goal**: `Box<Int>.show()` generates `Box_Int_show()` function

## Design Considerations

### Challenge 1: Self Type Complexity
`Self` in `impl<T> Display for Box<T>` is `Box<T>`, not `Box<Int>`.
- Need to distinguish between template types and instantiated types
- Self substitution happens in two phases:
  1. Semantic analysis: `Self -> Box<T>`
  2. Monomorphization: `T -> Int`, so `Box<T> -> Box<Int>`

### Challenge 2: Type Inference
For `box.show()`, need to:
1. Infer `box` has type `Box_Int`
2. Trace back to original: `Box<Int>`
3. Extract type args: `[Int]`
4. Find trait impl: `impl<T> Display for Box<T>` matches
5. Substitute: `T = Int`

**Solution**: Store metadata on monomorphized types:
```cpp
struct MonomorphizedTypeInfo {
    std::string originalTemplate;  // "Box"
    std::vector<TypeNode*> typeArguments;  // [Int]
    llvm::StructType* llvmType;  // Box_Int
};
```

### Challenge 3: Multiple Trait Impls
What if we have both:
```vyb
impl<T> Display for Box<T> { ... }
impl<T> From<T> for Box<T> { ... }
```

**Solution**: Method template key must include trait name:
- `"Display::show"` vs `"From::from"`
- Prevents collisions

## Future Enhancements (Post-Phase 6)

### Associated Types
```vyb
trait Iterator {
    type Item
    fn next(self<Self>)<Self.Item?>
}
```

### Type Constraints
```vyb
impl<T: Display> Show for Box<T> {
    # Only implement if T implements Display
}
```

### Generic Functions
```vyb
fn identity<T>(x<T>)<T> -> {
    return x
}
```

### Higher-Kinded Types
```vyb
trait Functor<F<_>> {
    fn map<A, B>(fa<F<A>>, f<A -> B>)<F<B>>
}
```

## Success Criteria

Phase 6 is complete when:
1. ✅ `Self` resolves correctly in generic impls
2. ✅ Generic method templates stored
3. ✅ Method calls trigger monomorphization
4. ✅ Specialized LLVM functions generated
5. ✅ Cache prevents duplicates
6. ✅ Test suite validates all cases
7. ✅ `trait From<T>` example works end-to-end

## Next Phase Preview

**Phase 7: Type Constraints**
- Add `T: Trait` bounds syntax
- Enforce constraints during instantiation
- Enable conditional impl selection

**Phase 8: Advanced Features**
- Associated types
- Default type parameters
- Generic functions
- Const generics (e.g., `Array<T, N>`)
