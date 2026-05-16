// ── neg_fixy_call_with_caps_missing_io (FIXY-G3 HS14) ─────────────────
//
// Binding declares Effect=Row<IO>; calling with a CapsPack that has
// NO IO atom must fail the caps_cover_v concept gate.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

using FnPtr = void(*)(int);

void noop(int) noexcept {}

using IoFn = cf::fn<FnPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::IO>,
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
    IoFn bound{&noop};
    // CapsPack carries Alloc only — missing IO required by Effect row.
    cf::CapsPack<fx::cap::Alloc> caps{};
    cf::call_with_caps<IoFn>(bound, caps, 0);
    return 0;
}
