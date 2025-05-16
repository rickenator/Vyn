#ifndef VYN_VRE_VALUE_HPP
#define VYN_VRE_VALUE_HPP

#include <string>
#include <vector>
#include <variant>
#include <memory> // For ::std::unique_ptr, ::std::shared_ptr if needed later
#include <cstdint> // For fixed-width integers
#include <unordered_map> // For ::std::unordered_map
#include <stdexcept> // For ::std::runtime_error

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
    OBJECT,     // Instance of a struct/class
    ARRAY,      // Dynamic array/vector
    // FUNCTION,   // Function pointer or closure (future)
    // NATIVE_POINTER, // Raw pointer for FFI or internal use (future)
};

// Forward declare VreValue for self-referential types if necessary (e.g. in VreObject or VreArray)
struct VreValue;

#if VYN_VRE_MEM_DEBUG
#include <iostream>
#include <atomic>
// #include <string> // For ::std::string (already included above)
#endif

struct VreObject {
    ::std::unordered_map< ::std::string, VreValue> fields;
    ::std::string type_name;
#if VYN_VRE_MEM_DEBUG
    static ::std::atomic<size_t> live_count; // Declaration
    VreObject(const ::std::unordered_map< ::std::string, VreValue>& f, const ::std::string& t)
        : fields(f), type_name(t) { ++live_count; ::std::cerr << "[VreObject] +1 (" << live_count << ") type=" << type_name << ::std::endl; }
    VreObject(const VreObject& other) : fields(other.fields), type_name(other.type_name) { ++live_count; ::std::cerr << "[VreObject] copy +1 (" << live_count << ") type=" << type_name << ::std::endl; }
    VreObject(VreObject&& other) noexcept : fields(::std::move(other.fields)), type_name(::std::move(other.type_name)) { ++live_count; ::std::cerr << "[VreObject] move +1 (" << live_count << ") type=" << type_name << ::std::endl; }
    ~VreObject() { --live_count; ::std::cerr << "[VreObject] -1 (" << live_count << ") type=" << type_name << ::std::endl; }
#else
    VreObject(const ::std::unordered_map< ::std::string, VreValue>& f, const ::std::string& t) : fields(f), type_name(t) {}
    VreObject(const VreObject& other) : fields(other.fields), type_name(other.type_name) {}
    VreObject(VreObject&& other) noexcept : fields(::std::move(other.fields)), type_name(::std::move(other.type_name)) {}
#endif
};
#if VYN_VRE_MEM_DEBUG
::std::atomic<size_t> VreObject::live_count{0};
#endif

struct VreArray {
    ::std::vector<VreValue> elements;
    ::std::string element_type_name;
#if VYN_VRE_MEM_DEBUG
    static ::std::atomic<size_t> live_count; // Declaration
    VreArray(const ::std::vector<VreValue>& elems, const ::std::string& t)
        : elements(elems), element_type_name(t) { ++live_count; ::std::cerr << "[VreArray] +1 (" << live_count << ") type=" << element_type_name << ::std::endl; }
    VreArray(const VreArray& other) : elements(other.elements), element_type_name(other.element_type_name) { ++live_count; ::std::cerr << "[VreArray] copy +1 (" << live_count << ") type=" << element_type_name << ::std::endl; }
    VreArray(VreArray&& other) noexcept : elements(::std::move(other.elements)), element_type_name(::std::move(other.element_type_name)) { ++live_count; ::std::cerr << "[VreArray] move +1 (" << live_count << ") type=" << element_type_name << ::std::endl; }
    ~VreArray() { --live_count; ::std::cerr << "[VreArray] -1 (" << live_count << ") type=" << element_type_name << ::std::endl; }
#else
    VreArray(const ::std::vector<VreValue>& elems, const ::std::string& t) : elements(elems), element_type_name(t) {}
    VreArray(const VreArray& other) : elements(other.elements), element_type_name(other.element_type_name) {}
    VreArray(VreArray&& other) noexcept : elements(::std::move(other.elements)), element_type_name(::std::move(other.element_type_name)) {}
#endif
};
#if VYN_VRE_MEM_DEBUG
::std::atomic<size_t> VreArray::live_count{0};
#endif

struct VreValue {
    VreValueType type;
    ::std::variant<
        ::std::monostate, // For NIL
        bool,           // For BOOLEAN
        int64_t,        // For INTEGER
        double,         // For FLOAT
        ::std::string,    // For STRING (heap-allocated)
        ::std::shared_ptr<VreObject>, // For OBJECT
        ::std::shared_ptr<VreArray>   // For ARRAY
    > data;

    // Constructors
    VreValue() : type(VreValueType::NIL), data(::std::monostate{}) {}
    explicit VreValue(bool val) : type(VreValueType::BOOLEAN), data(val) {}
    explicit VreValue(int64_t val) : type(VreValueType::INTEGER), data(val) {}
    explicit VreValue(double val) : type(VreValueType::FLOAT), data(val) {}
    explicit VreValue(const char* s) : type(VreValueType::STRING), data(::std::string(s)) {}
    explicit VreValue(::std::string s) : type(VreValueType::STRING), data(::std::move(s)) {}
    explicit VreValue(::std::shared_ptr<VreObject> obj) : type(VreValueType::OBJECT), data(::std::move(obj)) {}
    explicit VreValue(::std::shared_ptr<VreArray> arr) : type(VreValueType::ARRAY), data(::std::move(arr)) {}

    // Utility functions
    bool is_nil() const { return type == VreValueType::NIL; }
    bool is_boolean() const { return type == VreValueType::BOOLEAN; }
    bool is_integer() const { return type == VreValueType::INTEGER; }
    bool is_float() const { return type == VreValueType::FLOAT; }
    bool is_string() const { return type == VreValueType::STRING; }
    bool is_object() const { return type == VreValueType::OBJECT; }
    bool is_array() const { return type == VreValueType::ARRAY; }

    // Accessors (with type checking)
    bool as_boolean() const {
        if (!is_boolean()) throw ::std::runtime_error("VreValue: not a boolean");
        return ::std::get<bool>(data);
    }
    int64_t as_integer() const {
        if (!is_integer()) throw ::std::runtime_error("VreValue: not an integer");
        return ::std::get<int64_t>(data);
    }
    double as_float() const {
        if (!is_float()) throw ::std::runtime_error("VreValue: not a float");
        return ::std::get<double>(data);
    }
    const ::std::string& as_string() const {
        if (!is_string()) throw ::std::runtime_error("VreValue: not a string");
        return ::std::get<::std::string>(data);
    }
    ::std::shared_ptr<VreObject> as_object() const {
        if (!is_object()) throw ::std::runtime_error("VreValue: not an object");
        return ::std::get<::std::shared_ptr<VreObject>>(data);
    }
    ::std::shared_ptr<VreArray> as_array() const {
        if (!is_array()) throw ::std::runtime_error("VreValue: not an array");
        return ::std::get<::std::shared_ptr<VreArray>>(data);
    }
};

// --- Vyn Runtime Memory Management Model ---
//
// VreValue is the universal runtime value type for Vyn. It supports primitive types (bool, int64_t, double),
// heap-allocated types (std::string, VreObject, VreArray), and type info for dynamic dispatch.
//
// Memory Management:
// - Primitive types are stored directly in the variant and trivially copied.
// - Heap types (VreObject, VreArray, std::string) are managed via std::shared_ptr (for objects/arrays) and std::string (for strings).
//   - std::shared_ptr provides automatic reference counting and deallocation when the last reference is destroyed.
//   - This ensures safe, automatic memory management for interpreter and codegen backends.
// - If custom memory management or debugging is needed, you can add manual refcount fields or custom allocators later.
//
// Helper functions are provided for allocation of objects/arrays, and the VreValue API ensures type safety and correct ownership.
//
// Example usage:
//   auto obj = make_object("MyType");
//   obj->fields["x"] = VreValue(42);
//   VreValue v(obj); // v now holds a reference-counted object
//
// This model is robust for both interpreter and LLVM codegen, and can be extended for GC or custom allocators in the future.

// --- Debugging hooks for Vyn runtime memory management ---
//
// These hooks allow you to track allocations, reference count changes, and deallocations of VreObject and VreArray.
// They are useful for debugging memory leaks, double-frees, and lifetime issues in both interpreter and codegen backends.
//
// To enable, set VYN_VRE_MEM_DEBUG to 1. Hooks print to stderr by default.

// Helper functions for heap allocation of VreObject and VreArray
inline ::std::shared_ptr<VreObject> make_object(const ::std::string& type_name) {
    return ::std::make_shared<VreObject>(VreObject{/*fields=*/{}, type_name});
}
inline ::std::shared_ptr<VreArray> make_array(const ::std::string& elem_type) {
    return ::std::make_shared<VreArray>(VreArray{/*elements=*/{}, elem_type});
}

} // namespace vyn::vre

#endif // VYN_VRE_VALUE_HPP
