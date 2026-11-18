// CipherDurableHandle attests to a fact that it cannot see: the flags that
// opened its descriptor.  A file handle is evidence of a descriptor that the
// caller owns, and not of those flags.  So the constructor that takes a file
// is private, and only the three mints are its friends.  The compiler
// refuses a direct construction from a forged file.

#include <crucible/cipher/CipherDurable.h>

// The file that a forger gives.  The function has no definition, because
// the fixture compiles and never links.
::crucible::cipher::durable::StanceFile<::crucible::cipher::durable::warm_writer_stance>&& forged_file() noexcept;

int main() {
    [[maybe_unused]] auto forged =
        ::crucible::cipher::durable::CipherDurableHandle<::crucible::cipher::durable::warm_writer_stance>{
            forged_file()};
    return 0;
}
