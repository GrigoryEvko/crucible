#pragma once

// zeroize_bytes(object) writes zero to each byte of a trivially copyable
// object, with stores that the optimizer keeps.  A key, a tag or a state of a
// keyed hash ends with it, so no copy of a secret stays in memory that a later
// read can see.

#include <cstddef>
#include <type_traits>

namespace foundation::core {

// Writes zero to each byte of the object.  The writes are volatile, so the
// optimizer cannot drop a clear whose result no read uses.  The cast chain
// adds the volatile qualifier by implicit conversion and then narrows through
// a volatile void pointer, which keeps the qualifier all the way down without
// reinterpreting or casting away a qualifier.  A constant evaluation writes
// nothing, because its bytes never reach memory that a later read can see.
// Complexity: one write for each byte of T.
template <typename T>
    requires std::is_trivially_copyable_v<T>
constexpr void zeroize_bytes(T& object) noexcept {
    if !consteval {
        volatile T* const target = __builtin_addressof(object);
        volatile auto* const bytes = static_cast<volatile unsigned char*>(static_cast<volatile void*>(target));
        for (std::size_t index = 0; index < sizeof(T); ++index)
            bytes[index] = 0;
    }
}

}  // namespace foundation::core
