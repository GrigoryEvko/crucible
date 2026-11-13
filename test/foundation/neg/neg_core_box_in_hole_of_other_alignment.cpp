// A Box builds in a Hole of the exact alignment of its object only.  A Hole
// at the alignment 8 for an object of 8 bytes at the alignment 4 is refused.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

struct TwoWords {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

int main() {
    auto hole = foundation::core::mint_hole<8, 8>(foundation::effects::Alloc{});
    foundation::core::Box<TwoWords> box = foundation::core::mint_box<TwoWords>(std::move(hole));
    return static_cast<int>(box->first);
}
