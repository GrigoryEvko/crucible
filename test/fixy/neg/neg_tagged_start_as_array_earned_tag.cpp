// The checked lifetime start refuses a value under an earned tag.  Such a
// value keeps a trivial copy constructor, so it is an implicit-lifetime
// type, and the class carries the annotation no_start_over_bytes.
// start_as_array refuses it at its constraint.  A value started over bytes
// would carry a tag that no retag gave it.

#include <fixy/Tagged.h>
#include <foundation/Lifetime.h>

namespace tags = ::fixy::tags;

int main() {
    using Sanitized = fixy::Tagged<int, tags::source::Sanitized>;
    alignas(Sanitized) unsigned char bytes[sizeof(Sanitized)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Sanitized>(bytes, 1);
    return static_cast<int>(forged.size());
}
