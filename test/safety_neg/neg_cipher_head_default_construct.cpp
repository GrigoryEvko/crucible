// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidCipherHead refuses a default-constructed hash.  The default of a
// strong hash is zero, and zero is the sentinel for "no commit yet".  A
// caller that forgets to set a local hash and forwards it would otherwise
// advance HEAD to that sentinel.
//
// The companion fixture refuses a zero written as a literal.  This one is
// the value a caller never set.
//
// The mint runs the predicate at constant evaluation, where a violated
// precondition makes the expression not a constant, so the constexpr
// variable below is ill-formed.

#include <crucible/Cipher.h>
#include <crucible/Types.h>

int main() {
    constexpr crucible::ContentHash empty{};
    constexpr crucible::ValidCipherHead bad = ::fixy::mint_refined<::fixy::non_zero>(empty);
    (void)bad;
    return 0;
}
