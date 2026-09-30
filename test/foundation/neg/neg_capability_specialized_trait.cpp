// IsCapability admits a type only when it is a specialization of
// Capability.  This file tries to add a class of its own to that set.  It
// specializes the variable template that the concept once read, so a
// class of the caller would pass each gate that asks for a capability.
// The concept reads the instance query itself, so the specialization has
// nothing to name.

#include <foundation/effects/Capability.h>

namespace {

struct Fake final {};

}  // namespace

template <>
inline constexpr bool foundation::effects::is_capability_v<Fake> = true;

int main() { return 0; }
