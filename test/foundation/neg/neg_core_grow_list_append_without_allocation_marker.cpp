// An append to a GrowList takes the allocation capability first, so each
// growth shows at its call.  A different capability tag fails the gate.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

int main() {
    auto numbers = ::foundation::core::mint_grow_list<int>(4);
    numbers.append(::foundation::effects::IO{}, 1).expect("the list is below its maximum");
    return static_cast<int>(numbers.size());
}
