// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidCipherHead refuses a zero hash written as a literal.  Zero is the
// sentinel for "no commit yet": a head of zero is a step that cannot be
// told from a step before the first commit, it breaks the binary search
// of hash_at_step, and it names an object that is never in the store.
//
// The companion fixture refuses the zero that a default-constructed hash
// holds.  The two reach the same bytes from two different mistakes: this
// one forwards a zero that a caller wrote, and the other forwards a value
// that a caller never set.
//
// The mint runs the predicate at constant evaluation, where a violated
// precondition makes the expression not a constant, so the constexpr
// variable below is ill-formed.

#include <crucible/Cipher.h>
#include <crucible/Types.h>

int main() {
    constexpr crucible::ValidCipherHead bad =
        ::fixy::mint_refined<::fixy::non_zero>(crucible::ContentHash{uint64_t{0}});
    (void)bad;
    return 0;
}
