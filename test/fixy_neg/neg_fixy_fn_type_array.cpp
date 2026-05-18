// fixy_neg: Type=int[4] rejects via IsAccepted's type-axis check.
//
// HS14 floor for FIXY-AUDIT-B11.  fixy::fn / mint_fn's IsAccepted gate
// requires `type_is_accepted_payload<Type>` — array types are
// excluded because array decay would corrupt the wrapper's copy ctor
// and break the round-trip with safety::fn::Fn<...>.
//
// Expected diagnostic: "IsAccepted" — the satisfaction-failure chain
// names the top-level concept that rejects the Type axis.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-axis pack — well-formed Grants — but Type=int[4] fails the
    // Type-axis well-formedness check inside IsAccepted.
    auto bad = fixy::mint_fn<int[4],
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>({});
    (void)bad;
    return 0;
}
