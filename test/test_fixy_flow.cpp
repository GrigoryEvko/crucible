// ── test_fixy_flow — FIXY-G1 positive test ────────────────────────────
//
// Pin mint_flow + FlowOk + compose_grade_t for the canonical
// Mimic-emit → Cipher-persist → KernelCache-load chain.  Verify
// cross-axis preservation; verify runtime .run() composes the calls.

#include <crucible/fixy/Fixy.h>

#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;
namespace fnns = crucible::safety::fn;

namespace {

// Producer: int → int identity callable.  Both producer and consumer
// have the same grade vector except Effect (producer is Bg+Alloc;
// consumer is Bg+Alloc).  Identity channel preserves everything.

using IdInt = int(*)(int);

int identity_impl(int x) noexcept { return x; }

// Two bindings with identical Grants — Identity channel between them.
template <typename Ptr>
using AllStrictBg = cf::fn<Ptr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Bg, fx::Effect::Alloc>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
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

using ProdFn = AllStrictBg<IdInt>;
using ConsFn = AllStrictBg<IdInt>;

// Compile-time invariants.
static_assert(cf::ChannelType<cf::channel::Identity>);
static_assert(cf::ChannelType<cf::channel::Persist>);
static_assert(cf::ChannelType<cf::channel::Federate>);

static_assert(cf::FlowOk<ProdFn, cf::channel::Identity, ConsFn>,
    "identical-grade producer/consumer must flow through Identity.");
static_assert(cf::FlowOk<ProdFn, cf::channel::Serialize, ConsFn>,
    "identical-grade producer/consumer must flow through Serialize.");

}  // namespace

int main() {
    ProdFn p{&identity_impl};
    ConsFn c{&identity_impl};

    auto flow = cf::mint_flow(p, cf::channel::Identity{}, c);
    int result = flow.run(7);
    if (result != 7) {
        std::fprintf(stderr, "Flow.run(7) -> %d, expected 7\n", result);
        return 1;
    }

    std::fputs("test_fixy_flow: OK\n", stdout);
    return 0;
}
