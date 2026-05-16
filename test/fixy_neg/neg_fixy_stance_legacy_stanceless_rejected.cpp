// ── neg_fixy_stance_legacy_stanceless_rejected (FIXY-G13 HS14) ────────
//
// Pin backwards-compat opt-in: wire_decode_v1_under_v2_consumer<F, Tag>
// fails with StanceVersionMissing UNLESS the deployment opts in via
// `accept_legacy_stanceless<Tag> = true`.
//
// HS14: a constexpr decode of an empty (stanceless) buffer under
// BgWorkerTag without opt-in returns StanceVersionMissing.  An
// inverted static_assert claiming the decoder SUCCEEDED fires.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>

#include <array>

namespace cf = crucible::fixy;
namespace cs = crucible::fixy::stance;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

// BgWorkerTag has NOT been opted in for legacy stanceless load.
static_assert(!cs::accept_legacy_stanceless_v<cs::BgWorkerTag>);

using BgBinding = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cg::with<fx::Effect::Bg>,
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
    cf::accept_default_strict_for<cd::Staleness>>;

// Compile-time predicate: legacy decode WITHOUT opt-in must yield
// StanceVersionMissing.
[[maybe_unused]] consteval bool legacy_decode_returns_missing() noexcept {
    constexpr std::size_t kSize = cf::wire_grade_size_v<BgBinding>;
    std::array<std::uint8_t, kSize> buf{};
    // Populate with a valid V1 grade so wire_decode succeeds on its own
    // — encode produces a v1 buffer first.
    (void)cf::wire_encode<BgBinding>(buf);

    auto r = cs::wire_decode_v1_under_v2_consumer<BgBinding, cs::BgWorkerTag>(buf);
    // The decoder MUST reject with StanceVersionMissing without the
    // accept_legacy_stanceless<BgWorkerTag> opt-in.
    if (r.has_value()) return true;       // unexpected success — fixture should fire
    return r.error() != cf::WireGradeError::StanceVersionMissing;  // wrong error code → unexpected
}

// THE DISCIPLINE: legacy_decode_returns_missing() returns false (the
// decoder correctly fails with StanceVersionMissing).  An inverted
// static_assert pins build-red.
static_assert(legacy_decode_returns_missing(),
    "FIXY-G13 fixture: wire_decode_v1_under_v2_consumer<BgBinding, "
    "BgWorkerTag> on an unopted-in tag MUST return "
    "WireGradeError::StanceVersionMissing.  Build red on this inverted "
    "predicate is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
