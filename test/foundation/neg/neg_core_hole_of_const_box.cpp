// The mint of a Hole from a Box ends the object of the Box, so it takes a
// Box that it can change.  A const Box is refused, also as an rvalue.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

int main() {
    foundation::core::Box<std::uint64_t> const box =
        foundation::core::mint_box<std::uint64_t>(foundation::effects::Alloc{}, std::uint64_t{1});
    auto hole = foundation::core::mint_hole(std::move(box));
    std::move(hole).consume();
    return 0;
}
