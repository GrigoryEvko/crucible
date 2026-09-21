// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 2 of 2 for fixy::mint_view.
//
// A carrier declares one predicate per state it can witness.  A tag it
// declares no predicate for is a state it cannot witness, and asking for
// a view of that state is a different mistake from asking a carrier that
// witnesses nothing: this carrier is viewable, just not as Closed.
//
// Measured before CarrierDeclaresViewState:
// `requires { mint_view<Closed>(c) }` answered TRUE for this carrier,
// because the tag reaches the contract predicate and not the signature.
// The pair is refused by the clause now, so a caller can ask which
// states a carrier serves and get an answer per tag.
//
// Distinct mismatch class from
// neg_scoped_view_carrier_declares_no_state.cpp (fixture 1): there no
// predicate existed at all; here one exists and the tag misses it.
//
// Expected diagnostic: no matching function for mint_view, whose
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
    [[maybe_unused]] auto view = ::fixy::mint_view<Closed>(carrier);
    return 0;
}
