// The mint of a Hole from the allocation door takes the allocation tag.  The
// IO tag is no allocation capability, so the gate of the mint refuses it.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    auto hole = foundation::core::mint_hole<8, 8>(foundation::effects::IO{});
    std::move(hole).consume();
    return 0;
}
