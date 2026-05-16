// ── neg_fixy_call_with_perm_wrong_tag (FIXY-G4 HS14) ──────────────────
//
// Binding declares lifetime_region<ArenaA>; calling with
// Permission<ArenaB> is a compile error — PermissionMatchesLifetime
// concept rejects.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cs = crucible::safety;

namespace {

struct ArenaA {};
struct ArenaB {};

inline constexpr ArenaA kArenaA{};

using FnPtr = void(*)(int);

void noop(int) noexcept {}

using ArenaAFn = cf::fn<FnPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cg::lifetime_region<kArenaA>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

int main() {
    ArenaAFn bound{&noop};
    // Wrong-tag perm: minted for ArenaB, but binding expects ArenaA.
    auto p = cs::mint_permission_root<ArenaB>();
    cf::call_with_perm<ArenaAFn>(bound, std::move(p), 0);
    return 0;
}
