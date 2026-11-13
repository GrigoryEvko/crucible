// An append to a GrowText takes the allocation capability first, so each
// growth shows at its call.  A different capability tag fails the gate.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

int main() {
    auto text = ::foundation::core::mint_grow_text(16);
    text.append(::foundation::effects::IO{}, "crucible").expect("the text is below its maximum");
    return static_cast<int>(text.size());
}
