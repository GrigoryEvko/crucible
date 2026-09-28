// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Mismatch class 1 of 2 for Expr::hash: a bare uint64_t cannot enter the
// type of that field, fixy::Tagged<uint64_t, hash_family::FamilyB>.
//
// The constructor of Tagged from its value is explicit and private, so
// mint_tagged is the one door, and it names the family at the call site.
// A bare integer names no family.  If this fixture compiles, the field is
// a bare uint64_t, and a process-local Family-B hash can reach a
// Family-A computation (a Cipher key, a merkle hash, a ContentHash) with
// no compile error.
//
// The fixture takes the type from the production field, so a change to
// Expr::hash changes what the fixture checks.
//
// Companion: neg_expr_hash_cross_family.cpp refuses a Family-A hash.

#include <crucible/Expr.h>

#include <cstdint>
#include <type_traits>

int main() {
    using ExprHash = std::remove_const_t<decltype(crucible::Expr::hash)>;

    ExprHash slot = ::fixy::mint_tagged<crucible::hash_family::FamilyB>(std::uint64_t{0xdeadbeefULL});

    // The compiler must reject this line: no assignment takes a bare uint64_t.
    slot = std::uint64_t{0x12345678ULL};
    return 0;
}
