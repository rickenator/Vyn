#ifndef VYN_VRE_RUNTIME_TYPES_HPP
#define VYN_VRE_RUNTIME_TYPES_HPP

#include <vector>
#include <string>
#include <unordered_map> // For object fields if using map
#include <memory>      // For smart pointers

#include "vyn/vre/value.hpp" // VreValue is fundamental
#include "vyn/vre/memory.hpp" // For my<T>, etc.

namespace vyn::vre {

// Forward declarations
struct VreObject;
struct VreArray;
struct VreString; // If more complex than std::string in VreValue
struct VreSlice;
struct VreTraitObject;
struct VreFunction; // Or VreClosure

// Represents a Vyn struct/class instance at runtime
struct VreObject {
    // Option 1: Fields identified by name (more dynamic, REPL-friendly)
    // std::unordered_map<std::string, VreValue> fields;

    // Option 2: Fields as a vector (more performant if names are resolved to indices by compiler)
    std::vector<VreValue> fields_by_index;
    
    // TypeId type_id; // Link to static type information (defined in type_info.hpp perhaps)

    // Constructor, methods for field access, etc.
    // VreObject(TypeId id, size_t field_count) : type_id(id), fields_by_index(field_count) {}
};

// Represents a Vyn dynamic array at runtime
struct VreArray {
    std::vector<VreValue> elements;
    // Capacity, length are handled by std::vector
};

// Represents a Vyn string at runtime (if std::string in VreValue is not sufficient)
// struct VreString {
//     // Could wrap std::string or be custom for specific encoding/small-string-optimization etc.
//     std::string data;
// };

// Represents a Vyn slice (a view into an array or other contiguous memory)
struct VreSlice {
    VreValue* data_ptr; // Pointer to the first element
    size_t length;

    // Slices do not own their data
    VreSlice(VreValue* ptr = nullptr, size_t len = 0) : data_ptr(ptr), length(len) {}
};

// Represents a Vyn trait object for dynamic dispatch
struct VreTraitObject {
    my<void> instance_data; // Pointer to the actual object data (type-erased)
    void* vtable_ptr;        // Pointer to the virtual table for this trait implementation

    // The vtable would be a struct of function pointers
    // Example:
    // struct MyTraitVTable {
    //     RetType (*method1)(void* self, Arg1Type, Arg2Type);
    //     RetType (*method2)(void* self, Arg1Type);
    // };
};

// Represents a Vyn function or closure at runtime
struct VreFunction {
    // Could be a native function pointer, or a structure for closures
    // For closures, it would need to store:
    // 1. The function pointer
    // 2. Captured environment (e.g, a my<VreObject> or similar)
    
    // Using std::function as a placeholder for complex callable
    // std::function<VreValue(std::vector<VreValue>)> callable;
    
    // Or more low-level:
    // using NativeFuncPtr = VreValue (*)(/* args context */);
    // NativeFuncPtr ptr;
    // my<VreObject> captured_env; // If it's a closure
};


} // namespace vyn::vre

#endif // VYN_VRE_RUNTIME_TYPES_HPP
