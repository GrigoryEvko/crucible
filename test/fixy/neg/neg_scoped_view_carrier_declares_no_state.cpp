// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 1 of 2 for fixy::mint_view.
//
// A carrier declares its state predicate as a view_ok reachable by
// argument-dependent lookup, and there is no default.  A carrier that
// declares none therefore fits no tag, and CarrierDeclaresViewState is
// the clause that says so.
//
// The refusal already existed, and it arrived from the wrong place.  The
// contract predicate inside mint_view named view_ok, so the carrier was
// rejected on that call, inside the header, after the call site had
// already chosen this factory.  Measured before the gate:
// `requires { mint_view<Tag>(c) }` answered TRUE for this carrier.  A
// caller dispatching on that answer picked a factory that cannot serve
// it.
//
// Distinct mismatch class from neg_scoped_view_tag_has_no_predicate.cpp
// (fixture 2): there the carrier declares a predicate, for another tag;
// here it declares none at all.
//
// Expected diagnostic: no matching function for mint_view, whose
// candidate was discarded because CarrierDeclaresViewState is not
// satisfied.

#include <fixy/ScopedView.h>

namespace {
struct Ready {};
// No view_ok hidden friend, and none reachable by ADL anywhere.
struct CarrierWithoutPredicate {};
}  // namespace

int main() {
    CarrierWithoutPredicate carrier{};
    [[maybe_unused]] auto view = ::fixy::mint_view<Ready>(carrier);
    return 0;
}
