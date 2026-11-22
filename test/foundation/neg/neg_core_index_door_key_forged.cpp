// An Index<N> proves its word below N, so at() of a FixedArray<T, N> reads
// with no check.  The base has no door that makes an Index from a word with
// no check: a word of the base layer takes check() after a compare, or mask()
// for a count that is a power of two.  The key of an unchecked door, which a
// unit of the base could reach, is gone.

#include <foundation/core/Index.h>

#include <cstddef>

[[nodiscard]] static std::size_t forged() noexcept {
    auto const key = foundation::core::detail::IndexDoor::key();
    return sizeof(key);
}

int main() { return static_cast<int>(forged()); }
