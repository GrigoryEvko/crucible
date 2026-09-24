// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 1 of 2 for fixy::mint_linear_view.
//
// The linear form hands its view over rather than sharing it, so it
// carries the state predicate further than the scoped form does and
// needs the same gate at least as much.  The clause is spelled on this
// factory rather than only inherited from the mint_view call inside it,
// so the refusal names the factory the call site wrote.
//
// Measured before CarrierDeclaresViewState:
// `requires { mint_linear_view<Tag>(c) }` answered TRUE for a carrier
// that declares no predicate, and the refusal then came from the inner
// call.
//
// Distinct mismatch class from
// neg_scoped_view_linear_tag_has_no_predicate.cpp (fixture 2): there the
// carrier declares a predicate for another tag; here it declares none.
//
// Expected diagnostic: no matching function for mint_linear_view, whose
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
    [[maybe_unused]] auto token = ::fixy::mint_linear_view<Ready>(carrier);
    return 0;
}
