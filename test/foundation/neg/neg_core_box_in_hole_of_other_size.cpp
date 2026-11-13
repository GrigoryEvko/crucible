// A Box builds in a Hole of the exact size of its object only, so the Box
// gives back the storage that the Hole holds.  A Hole of 16 bytes for an
// object of 8 bytes is refused.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

int main() {
    auto hole = foundation::core::mint_hole<16, 8>(foundation::effects::Alloc{});
    foundation::core::Box<std::uint64_t> box =
        foundation::core::mint_box<std::uint64_t>(std::move(hole), std::uint64_t{1});
    return static_cast<int>(*box);
}
