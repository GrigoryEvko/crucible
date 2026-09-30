// IsCapability admits a type only when IsInstanceOf says that it is a
// specialization of Capability.  This file tries to add a class of its own
// to that set: it writes an explicit specialization of IsInstanceOf, as it
// would for a variable template.  IsInstanceOf is a concept, and the
// template-id of a concept declares nothing.

#include <foundation/effects/Capability.h>
#include <foundation/reflect/Instance.h>

namespace {

struct Fake final {};

}  // namespace

template <>
inline constexpr bool foundation::reflect::IsInstanceOf<Fake, ^^foundation::effects::Capability> = true;

int main() { return 0; }
