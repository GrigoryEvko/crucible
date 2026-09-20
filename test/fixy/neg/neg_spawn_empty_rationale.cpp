// The rationale is the audit trail, so an empty one is rejected at
// instantiation, before any consumer of the atom sees it.  The size of a
// fixed-string non-type template parameter counts the trailing NUL, so an
// empty literal has size 1.

#include <fixy/os/Spawn.h>

int main() {
    [[maybe_unused]] fixy::atom::spawn::detach_with<""> unjustified{};
    return 0;
}
