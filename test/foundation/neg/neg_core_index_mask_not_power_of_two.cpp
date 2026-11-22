// mask() of an Index keeps the low bits of a word, and these bits are below
// the count only when the count is a power of two.  An Index<6> of the mask
// of 7 would hold 7, past its count, so a count of another value has no mask.

#include <foundation/core/Index.h>

#include <cstddef>

[[nodiscard]] static std::size_t masked(std::size_t word) noexcept {
    return foundation::core::Index<6>::mask(word).raw();
}

int main() { return static_cast<int>(masked(std::size_t{7})); }
