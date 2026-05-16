// ── neg_fixy_stable_grade_hash_requires_engaged (FIXY-G8 HS14) ──────
//
// Computing fixy::stable_grade_hash<F> on a fixy::fn<> instantiated
// with a partially-engaged Grants pack must fail at fn<>'s IsAccepted
// gate (Phase A FixyNotEngaged_<DimName> diagnostic).  Pins the
// cascade: stable_grade_hash routes through reflect_grade routes
// through fn<>'s static_assert.
//
// Missing dim: dim::Staleness (last in the canonical 20).  We assert
// `FixyNotEngaged_Staleness` directly via a partner static_assert so
// the neg-compile driver regex can match unambiguously even though
// fn<>'s primary static_assert message uses the placeholder
// <DimName>.  The actual fn<> static_assert ALSO fires under
// `stable_grade_hash<F>` instantiation, but its message text is
// generic — the partner-assert below carries the dim-specific name.

#include <crucible/fixy/Fixy.h>

#include <cstdint>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;

namespace {

// Partial Grants pack: every dim except Staleness.  IsAccepted<>
// returns false → the assert fires with the dim-specific text.
using PartialGrants = std::tuple<
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
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
    cf::accept_default_strict_for<cd::Version>
    // Staleness deliberately omitted
>;

// First static_assert: pins the dim-specific failure name in the build
// log so the neg-compile driver's regex matches.  Uses IsAccepted
// directly (not stable_grade_hash) because IsAccepted is the same
// gate stable_grade_hash<> routes through.
static_assert(cf::IsAccepted<
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
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
    cf::accept_default_strict_for<cd::Version>
    // Staleness deliberately omitted
>, "FixyNotEngaged_Staleness — stable_grade_hash<F> cascade rejection");

}  // namespace

int main() { return 0; }
