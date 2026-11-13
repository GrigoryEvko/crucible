// The cold bodies of the growable owners of the Region family
// (foundation/core/Grow.h): the arithmetic of the next capacity, and the
// growth of a GrowText.

#include <foundation/core/Grow.h>

#include <foundation/core/Heap.h>

#include <cstddef>

namespace foundation::core {

namespace detail {

namespace {

// The bytes of the first storage of a growable owner: one cache line.
constexpr std::size_t kFirstStorageBytes = 64;

}  // namespace

std::size_t next_capacity_(std::size_t capacity, std::size_t needed, std::size_t maximum,
                           std::size_t element_bytes) noexcept {
    std::size_t next = 0;
    if (capacity == 0) {
        next = element_bytes < kFirstStorageBytes ? kFirstStorageBytes / element_bytes : 1;
    } else {
        next = capacity <= maximum / 2 ? capacity * 2 : maximum;
    }
    if (next < needed) next = needed;
    if (next > maximum) next = maximum;
    return next;
}

}  // namespace detail

void GrowText::grow_and_put_(char const* characters, std::size_t count) noexcept {
    std::size_t const needed = size_ + count;
    std::size_t const next = detail::next_capacity_(capacity_, needed, maximum_, 1);
    char* const target = static_cast<char*>(detail::heap_allocate_(next, 1));
    if (size_ != 0) {
        __builtin_memcpy(target, first_, size_);
    }
    __builtin_memcpy(target + size_, characters, count);
    if (first_ != nullptr) {
        detail::heap_release_(static_cast<void*>(first_), capacity_, 1);
    }
    first_ = target;
    size_ = needed;
    capacity_ = next;
}

}  // namespace foundation::core
