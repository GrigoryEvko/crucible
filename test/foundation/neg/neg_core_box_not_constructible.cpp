// mint_box builds its object from its arguments.  An argument list that
// does not build the object fails the gate of the mint at the call, and
// not deep in the body.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

int main() { return ::foundation::core::mint_box<int>(::foundation::effects::Alloc{}, "seven").get(); }
