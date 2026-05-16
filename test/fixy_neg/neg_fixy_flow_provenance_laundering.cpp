// ── neg_fixy_flow_provenance_laundering (FIXY-G1 HS14) ────────────────
//
// Producer carries source::External; Consumer requires source::Internal.
// Identity channel rejects (no sanitizer step in the channel).

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cs = crucible::safety;

namespace {

using FnPtr = int(*)(int);

int noop(int x) noexcept { return x; }

template <typename S>
using SrcBind = cf::fn<FnPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cg::from_source<S>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
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

using ExternalProducer = SrcBind<cs::source::External>;
using InternalConsumer = SrcBind<cs::source::FromInternal>;

static_assert(!cf::FlowOk<ExternalProducer, cf::channel::Identity,
                          InternalConsumer>,
    "FlowMismatch: External → FromInternal under Identity channel must "
    "reject (no sanitize step — provenance laundering).");

int main() {
    ExternalProducer p{&noop};
    InternalConsumer c{&noop};
    auto f = cf::mint_flow(p, cf::channel::Identity{}, c);
    (void)f;
    return 0;
}
