// The alignment of a Hole is a power of two.  The gate of the mint refuses
// the alignment 12 before the allocation door can see it.

#include <foundation/core/Credit.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    auto hole = foundation::core::mint_hole<24, 12>(foundation::effects::Alloc{});
    std::move(hole).consume();
    return 0;
}
