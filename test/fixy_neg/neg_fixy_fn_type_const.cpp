// fixy_neg: Type=const int rejects via IsAccepted's type-axis check.
//
// HS14 floor for FIXY-AUDIT-B11.  Top-level const-qualified types fail
// the Type-axis well-formedness check (`type_is_accepted_payload<
// const int>` is false because `std::is_const_v<const int>` is true).
// Storing a `const int` as the wrapper's field silently deletes the
// copy-assignment operator and breaks downstream wrappers that depend
// on assignability.
//
// Expected diagnostic: "IsAccepted" — concept satisfaction failure.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    auto bad = fixy::mint_fn<const int,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>>(42);
    (void)bad;
    return 0;
}
