// A Box in a Hole spends the Hole, so the mint takes a Hole that the caller
// gives up.  A Hole that the caller keeps is refused.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

int main() {
    auto hole = foundation::core::mint_hole<8, 8>(foundation::effects::Alloc{});
    foundation::core::Box<std::uint64_t> box = foundation::core::mint_box<std::uint64_t>(hole, std::uint64_t{1});
    return static_cast<int>(*box);
}
