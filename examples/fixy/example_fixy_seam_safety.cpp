// ── example_fixy_seam_safety (FIXY-G14 worked example — KEYSTONE) ─────
//
// Demonstrates that the SAME monotonic producer + concurrent consumer
// pair composes DIFFERENTLY across two channel shapes:
//
//   * Channel = `channel::Persist` (linearizes at serialize/
//     deserialize boundary): composition is SAFE — seam matcher
//     accepts.
//   * Channel = `channel::Identity` (no linearization): composition
//     is the gaps_010 manifestation — seam matcher REJECTS.
//
// Same components, different seam, different safety.  This is the
// canonical demonstration of "local soundness does not imply
// compositional soundness in graded effect systems" (Brookes-O'Hearn
// "Concurrent Separation Logic"; modal-CMM substructural reasoning).
//
// fixy now catches the bug fixy CLAIMED to catch since shipping.

#include <crucible/fixy/Fixy.h>

#include <cstdio>

namespace cf = crucible::fixy;
namespace ct = crucible::fixy::theory;
namespace cp = crucible::fixy::theory::pattern;
namespace cch = crucible::fixy::channel;
namespace ssf = crucible::safety::fn;
namespace ssrc = crucible::safety::source;
namespace strust = crucible::safety::trust;
namespace fx = crucible::effects;

namespace {

// Producer — monotonic-state append, Bg-context.
using MonotonicProducer = ssf::Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<fx::Effect::Bg>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Atomic,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Monotonic,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

// Consumer — concurrent reentrant reader.
using ReentrantConsumer = ssf::Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::Reentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

// Step 1 — both bindings per-component clear the substrate.
static_assert(::crucible::safety::fn::ValidComposition<MonotonicProducer>);
static_assert(::crucible::safety::fn::ValidComposition<ReentrantConsumer>);

// Step 2 — over Identity channel: gaps_010 seam fires.
static_assert(ct::seam_matches_v<
    cp::gaps_010_monotonic_concurrent_no_atomic,
    MonotonicProducer, cch::Identity, ReentrantConsumer>);

static_assert(ct::any_seam_pattern_matches_v<
    MonotonicProducer, cch::Identity, ReentrantConsumer>);

// Step 3 — over Persist channel: seam matcher passes (Persist inserts
// linearization at the serialize/deserialize boundary).
//
// gaps_010 seam is specialized only for Identity; Persist channel falls
// through to the false primary.
static_assert(!ct::seam_matches_v<
    cp::gaps_010_monotonic_concurrent_no_atomic,
    MonotonicProducer, cch::Persist, ReentrantConsumer>);

static_assert(!ct::any_seam_pattern_matches_v<
    MonotonicProducer, cch::Persist, ReentrantConsumer>);

}  // namespace

int main() {
    constexpr auto cite_identity =
        ct::which_seam_pattern_matches<MonotonicProducer,
                                       cch::Identity,
                                       ReentrantConsumer>();
    constexpr auto cite_persist =
        ct::which_seam_pattern_matches<MonotonicProducer,
                                       cch::Persist,
                                       ReentrantConsumer>();

    std::fprintf(stdout,
        "example_fixy_seam_safety:\n"
        "  MonotonicProducer through Identity to ReentrantConsumer:\n"
        "    seam cite = '%.*s' (gaps_010 — REJECTED)\n"
        "  MonotonicProducer through Persist to ReentrantConsumer:\n"
        "    seam cite = '%.*s' (empty — ACCEPTED, Persist linearizes)\n",
        static_cast<int>(cite_identity.size()), cite_identity.data(),
        static_cast<int>(cite_persist.size()), cite_persist.data());

    return 0;
}
