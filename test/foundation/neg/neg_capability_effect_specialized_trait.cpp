// CapMatchesCtx admits a capability when the row of the context holds the
// effect of the capability.  This file tries to give a Block capability
// the IO effect, so a context that owns IO and not Block would accept it.
// It specializes the variable template that the effect once was.  The
// effect comes from one function over reflections, so the specialization
// has nothing to name.

#include <foundation/effects/Capability.h>

template <>
inline constexpr foundation::effects::Effect foundation::effects::cap_of_v<
    foundation::effects::Capability<foundation::effects::Effect::Block, foundation::effects::Bg>> =
    foundation::effects::Effect::IO;

int main() { return 0; }
