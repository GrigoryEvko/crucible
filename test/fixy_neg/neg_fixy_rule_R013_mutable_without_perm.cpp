// ── neg_fixy_rule_R013_mutable_without_perm (FIXY-G4 HS14) ────────────
//
// Pin R013 via a static_assert: a binding with Mutation::Mutable +
// lifetime_region<Tag> is exactly the R013_requires_permission_v=true
// case.  The inverted static_assert below fires BECAUSE the discipline
// holds — the build red carries the "R013" / "requires Permission"
// phrases the neg-compile driver matches.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

struct MyArenaTag {};
inline constexpr MyArenaTag kArena{};

using FnPtr = void(*)(int);

using MutArenaBinding = cf::fn<FnPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cg::lifetime_region<kArena>,
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

// THE DISCIPLINE PINNED: R013 — mutable lifetime_region requires
// Permission at the call site.  The static_assert below asserts the
// INVERSE (that R013 does NOT fire for this binding) so the build red
// proves the rule was correctly detected by the substrate.
static_assert(!cf::R013_requires_permission_v<MutArenaBinding>,
    "R013 — mutable lifetime_region binding requires Permission at "
    "the call site.  This static_assert is INTENTIONALLY INVERTED; "
    "the build red carries the R013 phrase the neg-compile fixture "
    "regex matches.  When R013 fires correctly, this assert fails.");

}  // namespace

int main() { return 0; }
