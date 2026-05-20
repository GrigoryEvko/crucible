// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074l fixture #2 for fixy::safety::mint_view (ScopedView.h:159).
// mint_view<Tag>(c) carries `pre(view_ok(c, std::type_identity<Tag>{}))`.
// FakeCarrier declares a view_ok hidden friend ONLY for state_tag::Open;
// minting a view for state_tag::Closed makes the contract predicate call
// `view_ok(c, type_identity<Closed>)`, which has no matching overload (the
// only candidate takes type_identity<Open>).
//
// Distinct mismatch class from neg_fixy_safety_mint_view_no_view_ok.cpp
// (#1): there view_ok was entirely absent; here it EXISTS but is wired for
// a different Tag — the realistic "state predicate registered for the
// wrong state" bug.
//
// Expected diagnostic: no matching function for call to view_ok(...,
// type_identity<Closed>) / required from mint_view.

#include <type_traits>

#include <crucible/fixy/Safety.h>

namespace fsy = crucible::fixy::safety;

namespace neg_fixy_safety_mint_view_wrong_tag {
namespace state_tag {
struct Open {};
struct Closed {};
}  // namespace state_tag

struct FakeCarrier {
    // view_ok wired ONLY for state_tag::Open.
    [[nodiscard]] friend constexpr bool
    view_ok(FakeCarrier const&, std::type_identity<state_tag::Open>) noexcept {
        return true;
    }
};
}  // namespace neg_fixy_safety_mint_view_wrong_tag

int main() {
    namespace ns = neg_fixy_safety_mint_view_wrong_tag;
    ns::FakeCarrier carrier{};

    // Closed has no view_ok overload — the pre-clause predicate is ill-formed.
    [[maybe_unused]] auto bad =
        fsy::mint_view<ns::state_tag::Closed>(carrier);
    return 0;
}
