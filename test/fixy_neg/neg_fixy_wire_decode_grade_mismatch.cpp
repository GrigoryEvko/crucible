// ── neg_fixy_wire_decode_grade_mismatch (FIXY-G6 HS14) ────────────────
//
// Pins (at compile time) that wire_decode<F2> on a buffer encoded by
// F1 (different vendor) RETURNS A FAILURE.  Construct the deliberate
// regression: assert the buffer round-trips even though it shouldn't,
// so the static_assert fires when the discipline is intact.  The
// rejection text "wire_decode rejects: grade mismatch" is embedded
// in the failure message so the neg-compile driver regex matches.

#include <crucible/fixy/Fixy.h>

#include <array>
#include <cstdint>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

using NvBinding = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Bg>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cg::vendor_nv,
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

using AmBinding = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Bg>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cg::vendor_am,                                      // CHANGED
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

consteval bool cross_binding_decode_succeeds() {
    constexpr std::size_t kSize = cf::wire_grade_size_v<NvBinding>;
    std::array<std::uint8_t, kSize> buf{};
    [[maybe_unused]] auto written = cf::wire_encode<NvBinding>(buf);
    auto r = cf::wire_decode<AmBinding>(buf);
    return r.has_value();
}

// THE DISCIPLINE BEING PINNED: wire_decode rejects: grade mismatch.
// The inverted assertion below INTENTIONALLY FAILS — the consteval
// helper above returns `false` (the cross-binding decode is rejected
// by GradeMismatch as the discipline requires), and we assert it
// returns `true`.  The compile error embeds the canonical phrase.
static_assert(cross_binding_decode_succeeds(),
    "wire_decode rejects: grade mismatch — encoder/decoder vendor bytes "
    "differ; the consteval-evaluated decoder must return "
    "WireGradeError::GradeMismatch.  Build red is the EXPECTED outcome "
    "of the HS14 fixture proving the discipline fires.");

}  // namespace

int main() { return 0; }
