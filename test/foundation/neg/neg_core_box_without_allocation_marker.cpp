// mint_box takes the allocation capability first, so each heap allocation
// shows at its call site.  The IO capability is not the allocation
// capability, and the gate of the mint refuses it.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

int main() { return ::foundation::core::mint_box<int>(::foundation::effects::IO{}, 3).get(); }
