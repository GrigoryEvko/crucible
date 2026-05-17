// fixy_neg: Type=volatile int rejects via IsAccepted's type-axis check.
//
// HS14 floor for FIXY-AUDIT-B11.  Top-level volatile-qualified types
// fail the Type-axis well-formedness check
// (`type_is_object_or_function<volatile int>` is false because
// `std::is_volatile_v<volatile int>` is true).  Volatile-qualified
// fields force every access through a memory barrier that the
// substrate's optimization-relevant aggregator (Fn<...>) does not
// model — `volatile` belongs on the storage primitive, not the
// discipline wrapper.
//
// Expected diagnostic: "IsAccepted" — concept satisfaction failure.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    auto bad = fixy::mint_fn<volatile int,
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
