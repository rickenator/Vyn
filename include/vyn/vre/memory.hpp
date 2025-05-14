#ifndef VYN_VRE_MEMORY_HPP
#define VYN_VRE_MEMORY_HPP

#include <memory> // For std::unique_ptr, std::shared_ptr
#include <cstddef> // For size_t

namespace vyn::vre {

// Forward declaration
struct VreValue;

// Basic heap allocation (could be replaced by a custom allocator later)
void* allocate_raw(size_t size);
void deallocate_raw(void* ptr);

// Vyn's my<T> for unique ownership of heap-allocated values.
// T would typically be a VreValue or a specific runtime type like VreObject.
template<typename T>
using my = std::unique_ptr<T>; // Using std::unique_ptr as a starting point

// Vyn's our<T> for shared ownership of heap-allocated values (e.g., via ARC).
template<typename T>
using our = std::shared_ptr<T>; // Using std::shared_ptr as a starting point for shared ownership

// Vyn's their<T> for non-owning, borrowed references to values.
// This represents a raw pointer, indicating a borrow that does not affect lifetime.
// It can be null and can be reassigned. For non-nullable borrows, C++ references (T&) are typically used directly.
template<typename T>
using their = T*; // Using raw pointer for non-owning borrows

// --- Potential future additions ---
// class Allocator {
// public:
//     virtual ~Allocator() = default;
//     virtual void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;
//     virtual void deallocate(void* ptr, size_t size) = 0;
// };

// class DefaultAllocator : public Allocator {
// public:
//     void* allocate(size_t size, size_t alignment) override;
//     void deallocate(void* ptr, size_t size) override;
// };

// Functions for creating Boxed values
// template<typename T, typename... Args>
// my<T> make_my(Args&&... args) { // Renamed from make_box
//     // Potentially use custom allocation here
//     return std::make_unique<T>(std::forward<Args>(args)...);
// }

} // namespace vyn::vre

#endif // VYN_VRE_MEMORY_HPP
