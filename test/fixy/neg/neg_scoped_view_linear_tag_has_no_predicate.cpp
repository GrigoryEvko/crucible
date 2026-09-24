// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 2 of 2 for fixy::mint_linear_view.
//
// A one-shot state proof is the token a holder hands over to make a
// transition, so a tag the carrier witnesses nothing about must not
// reach one.  This carrier witnesses Open and no other state.
//
// Measured before CarrierDeclaresViewState:
// `requires { mint_linear_view<Closed>(c) }` answered TRUE, because the
// tag reached only the contract predicate and never the signature.
//
// Distinct mismatch class from
// neg_scoped_view_linear_carrier_declares_no_state.cpp (fixture 1):
// there no predicate existed; here one exists and the tag misses it.
//
// Expected diagnostic: no matching function for mint_linear_view, whose
// candidate was discarded because CarrierDeclaresViewState is not
// satisfied for Closed.

#include <fixy/ScopedView.h>

namespace {
struct Open {};
struct Closed {};

struct CarrierOpenOnly {
    bool is_open = true;

    // Declared for Open, and for no other state.
    [[nodiscard]] friend constexpr bool view_ok(CarrierOpenOnly const& c, std::type_identity<Open>) noexcept {
        return c.is_open;
    }
};
}  // namespace

int main() {
    CarrierOpenOnly carrier{};
    // Open would be admitted.  Closed has no predicate on this carrier.
    [[maybe_unused]] auto token = ::fixy::mint_linear_view<Closed>(carrier);
    return 0;
}
