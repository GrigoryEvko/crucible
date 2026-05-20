// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074n fixture #2 for fixy::source::federation::mint_self_signed_handshake
// (FederationPermission.h:631).  The mint carries
// `requires FederationOrgTag<Org>` = `std::is_class_v<Org> &&
// std::is_empty_v<Org>`.  A class type that carries a data member is a
// class but NOT empty, so it fails the `std::is_empty_v<Org>` half of the
// concept — the realistic "passed a stateful struct where an empty
// federation org tag is required" bug.
//
// This exercises the FIXY re-export specifically (the substrate-side
// equivalents live in test/safety_neg/, which gen-mint-inventory does not
// count toward the fixy umbrella's HS14 floor).
//
// Distinct mismatch class from
// neg_fixy_source_mint_self_signed_non_class_org.cpp (#1): there the Org
// is not a class at all (fails std::is_class_v); here it IS a class but
// non-empty (fails std::is_empty_v).
//
// Expected diagnostic: constraints not satisfied / FederationOrgTag /
// no matching function / is_empty.

#include <crucible/fixy/Source.h>

namespace fsrc = crucible::fixy::source::federation;

namespace neg_fixy_source_mint_self_signed_non_empty_org {
// A class, but NOT empty — carries a data member, so std::is_empty_v is
// false and FederationOrgTag rejects it.
struct StatefulOrg {
    int forbidden_state = 0;
};
}  // namespace neg_fixy_source_mint_self_signed_non_empty_org

int main() {
    [[maybe_unused]] auto handshake =
        fsrc::mint_self_signed_handshake<
            neg_fixy_source_mint_self_signed_non_empty_org::StatefulOrg>();
    return 0;
}
