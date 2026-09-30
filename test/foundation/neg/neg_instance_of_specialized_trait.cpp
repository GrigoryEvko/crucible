// IsCapability admits a type only when it is a specialization of
// Capability.  This file tries to add a class of its own to that set.  It
// specializes the variable template that the instance query once had,
// because a gate that read that variable admitted each class that a
// specialization marked.  The query is a concept, so the specialization
// has nothing to name.

#include <foundation/effects/Capability.h>
#include <foundation/reflect/Instance.h>

namespace {

struct Fake final {};

}  // namespace

template <>
inline constexpr bool foundation::reflect::is_instance_of_v<Fake, ^^foundation::effects::Capability> = true;

int main() { return 0; }
