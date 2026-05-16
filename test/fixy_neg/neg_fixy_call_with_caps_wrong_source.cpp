// ── neg_fixy_call_with_caps_wrong_source (FIXY-G3 HS14) ───────────────
//
// Binding requires Effect=Row<Alloc, IO>; CapsPack carries only Block.
// Different shape of missing-capability — covers the Alloc-required
// path AND the IO-required path simultaneously.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

using FnPtr = void(*)(int);

void noop(int) noexcept {}

using AllocIoFn = cf::fn<FnPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Alloc, fx::Effect::IO>,
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

int main() {
    AllocIoFn bound{&noop};
    cf::CapsPack<fx::cap::Block> caps{};  // missing Alloc AND IO
    cf::call_with_caps<AllocIoFn>(bound, caps, 0);
    return 0;
}
