// Each gate that asks for a capability reads IsCapability.  This file
// tries to add a class of its own to the capabilities: it writes an
// explicit specialization of IsCapability, as it would for a variable
// template.  IsCapability is a concept, and the template-id of a concept
// declares nothing.

#include <foundation/Quarantine.h>
#include <foundation/effects/Capability.h>

namespace {

struct Fake final {};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
template <>
inline constexpr bool foundation::effects::IsCapability<Fake> = true;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
