// fixy_neg: Type=int& rejects via IsAccepted's type-axis check.
//
// HS14 floor for FIXY-AUDIT-B11.  Reference types fail the Type-axis
// well-formedness check (`type_is_accepted_payload<int&>` is false
// since `std::is_reference_v<int&>` is true).  Storing a reference as
// a value-semantic field would force the wrapper to lifetime-extend
// the referent, which the substrate's Fn<...> aggregator does not
// model.
//
// Expected diagnostic: "IsAccepted" — concept satisfaction failure.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    int slot = 0;
    // Type=int& triggers the Type-axis rejection.
    auto bad = fixy::mint_fn<int&,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>(slot);
    (void)bad;
    return 0;
}
