// fixy_neg: §30.14 corpus rejects internal × IO-in-multi-effect-pack
// without declassify.
//
// fixy-H-18 second fixture (HS14 ≥2 floor): proves the
// `internal_io_without_declassify` corpus entry fires when the IO
// effect is one of many in a multi-effect grant::with<...> pack, not
// just when it stands alone.  Catches a regression where someone
// accidentally narrows `is_io_effect_grant` to "with<IO>" exact-match
// instead of the current "with<E...> contains IO" semantics.
//
// Reject sequence: IsAccepted → IsAcceptedDirect → NotInTheoryCorpus →  // fixy-A4-023: post-H-05 chain.
// `is_io_effect_grant<with<Alloc, IO>>::value` returns true → matches
// fires → IsAccepted concept fails.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace eff  = crucible::effects;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19-axis pack engaging:
    //   Security = as_internal
    //   Effect   = with<Alloc, IO>  (IO in a multi-effect pack)
    //   NO declassify<Policy>
    // Internal data leaks through I/O regardless of whether IO is the
    // sole effect or one of several in the pack — must reject.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with<eff::Effect::Alloc, eff::Effect::IO>,  // IO inside multi-effect pack
        gr::as_internal,
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
        strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>(42);
    (void)bad;
    return 0;
}
