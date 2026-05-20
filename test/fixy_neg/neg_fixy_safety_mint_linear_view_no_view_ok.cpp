// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074l fixture #3 for fixy::safety::mint_linear_view
// (ScopedView.h:198).  mint_linear_view<Tag>(c) carries the SAME
// `pre(view_ok(c, std::type_identity<Tag>{}))` contract as mint_view and
// also forwards to mint_view<Tag>(c) internally.  A Carrier that declares
// no view_ok makes the predicate ill-formed: name lookup + ADL find
// nothing.
//
// Distinct mismatch class from
// neg_fixy_safety_mint_linear_view_wrong_tag.cpp (#4): there a view_ok
// exists but only for a different Tag; here the predicate's callee is
// entirely absent.
//
// Expected diagnostic: "view_ok" was not declared / no matching function
// for call to view_ok / required from mint_linear_view (or the nested
// mint_view).

#include <type_traits>

#include <crucible/fixy/Safety.h>

namespace fsy = crucible::fixy::safety;

namespace neg_fixy_safety_mint_linear_view_no_view_ok {
struct SomeState {};
// No view_ok hidden friend, no ADL-reachable view_ok anywhere.
struct FakeCarrier {};
}  // namespace neg_fixy_safety_mint_linear_view_no_view_ok

int main() {
    namespace ns = neg_fixy_safety_mint_linear_view_no_view_ok;
    ns::FakeCarrier carrier{};

    [[maybe_unused]] auto bad =
        fsy::mint_linear_view<ns::SomeState>(carrier);
    return 0;
}
