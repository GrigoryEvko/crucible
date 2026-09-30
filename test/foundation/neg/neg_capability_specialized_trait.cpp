// Each gate that asks for a capability reads IsCapability.  This file
// tries to add a class of its own to the capabilities: it writes an
// explicit specialization of IsCapability, as it would for a variable
// template.  IsCapability is a concept, and the template-id of a concept
// declares nothing.

#include <foundation/effects/Capability.h>

namespace {

struct Fake final {};

}  // namespace

template <>
inline constexpr bool foundation::effects::IsCapability<Fake> = true;

int main() { return 0; }
