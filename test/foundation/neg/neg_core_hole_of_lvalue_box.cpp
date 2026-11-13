// The mint of a Hole from a Box ends the object of the Box, so it takes a
// Box that the caller gives up.  A Box that the caller keeps is refused.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

int main() {
    foundation::core::Box<std::uint64_t> box =
        foundation::core::mint_box<std::uint64_t>(foundation::effects::Alloc{}, std::uint64_t{1});
    auto hole = foundation::core::mint_hole(box);
    std::move(hole).consume();
    return 0;
}
