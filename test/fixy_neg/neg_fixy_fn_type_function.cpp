// fixy_neg: Type=void(int) (bare function type) rejects via IsAccepted.
//
// HS14 floor for FIXY-AUDIT-B11.  Bare function types (`void(int)`,
// `int()`, etc.) fail the Type-axis well-formedness check —
// `std::is_object_v<void(int)>` is false (functions are not objects in
// the C++ object model).  Function types must be wrapped as pointers
// (`int (*)(int)`) or callables (lambdas / std::function_ref) before
// reaching fixy::fn.
//
// Expected diagnostic: "IsAccepted" — concept satisfaction failure.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // The bare function type `void(int)` cannot be constructed — the
    // wrapper instantiation alone fires the static_assert without
    // needing an argument.  Reference the type via a typedef so the
    // template parameter is named.
    using BareFn = void(int);
    using Bad = fixy::fn<BareFn,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>, strict<D::Synchronization>>;
    (void)sizeof(Bad);  // force class-body instantiation
    return 0;
}
