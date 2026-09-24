// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074n fixture #1 for fixy::source::federation::mint_self_signed_handshake
// (FederationPermission.h:631).  The mint carries
// `requires FederationOrgTag<Org>` = `std::is_class_v<Org> &&
// std::is_empty_v<Org>`.  Org is the explicit template parameter; a
// non-class type (int) fails `std::is_class_v<Org>`, so the requires-clause
// rejects it.
//
// This exercises the FIXY re-export specifically (the substrate-side
// equivalents live in test/safety_neg/, which gen-mint-inventory does not
// count toward the fixy umbrella's HS14 floor).
//
// Distinct mismatch class from
// neg_fixy_source_mint_self_signed_non_empty_org.cpp (#2): there the Org
// IS a class but carries a data member (fails std::is_empty_v); here the
// Org is not a class at all (fails std::is_class_v).
//
// Expected diagnostic: constraints not satisfied / FederationOrgTag /
// no matching function / is_class.

#include <crucible/fixy/Source.h>

namespace fsrc = crucible::fixy::source::federation;

int main() {
    // int is not a class type → FederationOrgTag<int> false → the
    // mint_self_signed_handshake<int> requires-clause rejects it.
    [[maybe_unused]] auto handshake = fsrc::mint_self_signed_handshake<int>();
    return 0;
}
