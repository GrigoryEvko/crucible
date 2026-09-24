// The checked lifetime start refuses a thread-name witness.  Its one
// constructor is user-provided and private, and its copy and move are
// deleted, so it is not an implicit-lifetime type, and start_as_array
// refuses it at its constraint.  A witness started over bytes would name a
// thread that nobody named.

#include <fixy/os/ThreadName.h>
#include <foundation/Lifetime.h>

int main() {
    using Witness = fixy::ThreadNamed<"forged">;
    alignas(Witness) unsigned char bytes[sizeof(Witness)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Witness>(bytes, 1);
    return static_cast<int>(forged.size());
}
