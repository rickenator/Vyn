#ifndef VYN_VRE_VALUE_HPP
#define VYN_VRE_VALUE_HPP

#include <string>
#include <vector>
#include <variant>
#include <memory> // For std::unique_ptr, std::shared_ptr if needed later
#include <cstdint> // For fixed-width integers

// Forward declarations if needed for complex types
// namespace vyn::vre {
// class VreObject; // Example
// class VreArray;  // Example
// }

namespace vyn::vre {

// Enum to identify the type of value stored in VreValue
enum class VreValueType {
    NIL,        // Represents null or uninitialized
    BOOLEAN,
    INTEGER,    // i64
    FLOAT,      // f64
    STRING,     // Heap-allocated string
    // --- Potentially more complex types later ---
    // OBJECT,     // Instance of a struct/class
    // ARRAY,      // Dynamic array/vector
    // FUNCTION,   // Function pointer or closure
    // NATIVE_POINTER, // Raw pointer for FFI or internal use
};

// Forward declare VreValue for self-referential types if necessary (e.g. in VreObject or VreArray)
struct VreValue;

// Placeholder for future VreObject (representing struct/class instances)
// This would likely store a map of field names (strings) to VreValues
// Or a vector of VreValues if fields are accessed by index post-compilation
// struct VreObject {
//     // std::unordered_map<std::string, VreValue> fields;
//     // Or, if type information is available to map names to indices:
//     // std::vector<VreValue> fields;
//     // TypeId type_id; // To link back to a Vyn type definition
// };

// Placeholder for future VreArray
// struct VreArray {
//     std::vector<VreValue> elements;
// };


// Represents a runtime value in Vyn
// Using std::variant for type-safe union
struct VreValue {
    VreValueType type;
    std::variant<
        std::monostate, // For NIL
        bool,           // For BOOLEAN
        int64_t,        // For INTEGER
        double,         // For FLOAT
        std::string     // For STRING (heap-allocated)
        // std::unique_ptr<VreObject>, // For OBJECT - if using unique_ptr
        // std::unique_ptr<VreArray>   // For ARRAY - if using unique_ptr
    > data;

    // Constructors
    VreValue() : type(VreValueType::NIL), data(std::monostate{}) {}
    explicit VreValue(bool val) : type(VreValueType::BOOLEAN), data(val) {}
    explicit VreValue(int64_t val) : type(VreValueType::INTEGER), data(val) {}
    explicit VreValue(double val) : type(VreValueType::FLOAT), data(val) {}
    explicit VreValue(const char* s) : type(VreValueType::STRING), data(std::string(s)) {}
    explicit VreValue(std::string s) : type(VreValueType::STRING), data(std::move(s)) {}

    // Add constructors for VreObject, VreArray etc. as they are defined

    // Utility functions (examples)
    bool is_nil() const { return type == VreValueType::NIL; }
    bool is_boolean() const { return type == VreValueType::BOOLEAN; }
    // ... more type checkers

    // Accessors (with type checking)
    // Example:
    // bool as_boolean() const {
    //     if (!is_boolean()) { /* throw error or handle */ }
    //     return std::get<bool>(data);
    // }
    // ... more accessors

    // TODO: Consider how to handle memory for strings, objects, arrays.
    // std::string for STRING implies heap allocation.
    // VreObject and VreArray would also be heap-allocated, likely via smart pointers.
};

} // namespace vyn::vre

#endif // VYN_VRE_VALUE_HPP
