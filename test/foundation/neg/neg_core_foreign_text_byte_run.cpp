// mint_foreign_text takes a run of char from a pointer and a count, or the
// View of a run of std::byte, whose View proves the run.  A pointer of
// unsigned char is neither, so the call does not compile, and
// mint_foreign_view gives the View of such a run.

#include <foundation/core/Region.h>

int main() {
    auto const text = ::foundation::core::mint_foreign_text(static_cast<unsigned char const*>(nullptr), 0);
    return static_cast<int>(text.characters().size());
}
