// IsCapability admits a type only when IsInstanceOf says that it is a
// specialization of Capability.  This file tries to add a class of its own
// to that set: it writes an explicit specialization of IsInstanceOf, as it
// would for a variable template.  IsInstanceOf is a concept, and the
// template-id of a concept declares nothing.

#include <foundation/Quarantine.h>
#include <foundation/effects/Capability.h>
#include <foundation/reflect/Instance.h>

namespace {

struct Fake final {};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
template <>
inline constexpr bool foundation::reflect::IsInstanceOf<Fake, ^^foundation::effects::Capability> = true;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
