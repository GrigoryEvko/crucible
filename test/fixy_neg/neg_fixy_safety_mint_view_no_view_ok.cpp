// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074l fixture #1 for fixy::safety::mint_view (ScopedView.h:159).
// mint_view<Tag>(c) carries `pre(view_ok(c, std::type_identity<Tag>{}))`
// — view_ok is found by ADL on the Carrier (per-Carrier hidden friend;
// there is NO global default).  A Carrier that declares no view_ok at all
// makes the contract predicate ill-formed: name lookup + ADL find nothing,
// so `view_ok(...)` does not compile.
//
// Distinct mismatch class from neg_fixy_safety_mint_view_wrong_tag.cpp
// (#2): there a view_ok DOES exist but only for a different Tag; here the
// predicate's callee is entirely absent.
//
// Expected diagnostic: "view_ok" was not declared / no matching function
// for call to view_ok / required from mint_view.

#include <type_traits>

#include <crucible/fixy/Safety.h>

namespace fsy = crucible::fixy::safety;

namespace neg_fixy_safety_mint_view_no_view_ok {
struct SomeState {};
// No view_ok hidden friend, no ADL-reachable view_ok anywhere.
struct FakeCarrier {};
}  // namespace neg_fixy_safety_mint_view_no_view_ok

int main() {
    neg_fixy_safety_mint_view_no_view_ok::FakeCarrier carrier{};

    [[maybe_unused]] auto bad =
        fsy::mint_view<neg_fixy_safety_mint_view_no_view_ok::SomeState>(carrier);
    return 0;
}
