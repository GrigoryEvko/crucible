// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::open refuses a first argument that is not an execution context.
// A test capability source permits IO and Block, but it is only the
// source.  The gate reads the row of a context, and a source has no row,
// so a caller must build a context that claims the two effects.

#include <crucible/Cipher.h>

int main() {
    auto cipher = ::crucible::Cipher::open(
        ::foundation::effects::testing::test(),
        ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{"/tmp/crucible_neg_open_non_context"}));
    return cipher.is_open() ? 0 : 1;
}
