// CipherDurableHandle attests to a fact that it cannot see: the flags that
// opened its descriptor.  An OwnedFd is evidence of a descriptor that the
// caller owns, and not of those flags.  So the constructor that takes a
// descriptor is private, and only the three mints are its friends.  The
// compiler refuses a direct construction from a forged descriptor.

#include <crucible/cipher/CipherDurable.h>

#include <fixy/os/Fd.h>

// The descriptor that a forger gives.  The function has no definition,
// because the fixture compiles and never links.
::fixy::fs::OwnedFd&& forged_descriptor() noexcept;

int main() {
    [[maybe_unused]] auto forged =
        ::crucible::cipher::durable::CipherDurableHandle<::crucible::cipher::durable::warm_writer_stance>{
            forged_descriptor()};
    return 0;
}
