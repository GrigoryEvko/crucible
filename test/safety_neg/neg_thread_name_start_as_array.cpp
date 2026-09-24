// The checked lifetime start refuses an old-tree thread-name witness.  Its
// one constructor is user-provided and private, and its copy and move are
// deleted, so it is not an implicit-lifetime type, and start_as_array
// refuses it at its constraint.

#include <crucible/safety/ThreadName.h>
#include <foundation/Lifetime.h>

int main() {
    using Witness = ::crucible::safety::ThreadNamed<"forged">;
    alignas(Witness) unsigned char bytes[sizeof(Witness)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Witness>(bytes, 1);
    return static_cast<int>(forged.size());
}
