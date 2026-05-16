// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// FIXY-C-STANCE-COMPOSE — `mint_fn_for_extra<Stance, ExtraGrants...>`
// composes Stance with ExtraGrants via `stance::compose_t`.  When
// ExtraGrants contain two tags engaging the same dim, the compose
// rejects with the static_assert in stance::compose's body.
//
// Probe: ExtraGrants = {grant::overflow_wrap, grant::overflow_saturate}.
// Both engage dim::Overflow; compose-time dim-collision rejection
// fires.

#include <crucible/fixy/Stance.h>

namespace cf = crucible::fixy;
namespace cg = crucible::fixy::grant;
namespace cs = crucible::fixy::stance;

void try_mint() {
    // mint_fn_for_extra<PureLinear, ExtraGrants...>(v) instantiates
    // compose_t<PureLinear, overflow_wrap, overflow_saturate> internally;
    // the dims_all_distinct gate fires.
    (void) cs::mint_fn_for_extra<cs::PureLinear,
        cg::overflow_wrap,
        cg::overflow_saturate>(int{42});
}

int main() { return 0; }
