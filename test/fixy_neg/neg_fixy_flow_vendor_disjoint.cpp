// ── neg_fixy_flow_vendor_disjoint (FIXY-G1 HS14) ──────────────────────
//
// Producer is vendor_nv; Consumer accepts vendor_am only.  Federate
// channel disjoint rejection.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

using FnPtr = int(*)(int);

int noop(int x) noexcept { return x; }

template <typename V>
using VendorBind = cf::fn<FnPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    V,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

using NvBind = VendorBind<cg::vendor_nv>;
using AmBind = VendorBind<cg::vendor_am>;

static_assert(!cf::FlowOk<NvBind, cf::channel::Federate, AmBind>,
    "FlowMismatch: NV → AM under Federate must reject (vendor disjoint).");

int main() {
    NvBind p{&noop};
    AmBind c{&noop};
    auto f = cf::mint_flow(p, cf::channel::Federate{}, c);
    (void)f;
    return 0;
}
