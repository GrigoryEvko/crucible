// The doors of bench_reflect to the pinned rapidhash of Nicolas De Carli
// (utils/pins.txt, the MIT license).  rapidhash.h gives each function as an
// inline function, so each call of a loop below inlines its body with the
// size of the record as a constant, as the hash of the family inlines into
// the loop of its row.

#include "bench_reflect_references.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the pinned rapidhash is the reference of the hash of the Protocol family")

#include "rapidhash.h"

namespace bench_reflect_references {

std::uint64_t rapidhash_nano_16(std::byte const* first, std::size_t count) noexcept {
    constexpr std::size_t size = 16;
    std::uint64_t folded = 0;
    for (std::size_t index = 0; index < count; ++index) {
        folded ^= rapidhashNano(first + index * size, size);
    }
    return folded;
}

std::uint64_t rapidhash_88(std::byte const* first, std::size_t count) noexcept {
    constexpr std::size_t size = 88;
    std::uint64_t folded = 0;
    for (std::size_t index = 0; index < count; ++index) {
        folded ^= rapidhash(first + index * size, size);
    }
    return folded;
}

}  // namespace bench_reflect_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
