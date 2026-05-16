// ── test_fixy_seam (FIXY-G14 positive test — KEYSTONE) ────────────────
//
// Verify cross-binding §30.14 pattern detection at the producer-
// channel-consumer seam.  Each of the 5 seam patterns has a positive
// detection test below.  Non-flow bindings still match via
// `theory::matches_v` (intra-binding non-regression).
//
// Positive examples that should NOT fire any seam check:
//   * DefaultFn → any channel → DefaultFn — healthy baseline
//   * Producer/Consumer correctly aligned over Persist channel — safe
//
// References:
//   misc/16_05_2026_fixy.md §8 G14
//   fixy/theory/Seam.h

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

using ssf::Fn;

// ── 1. Healthy baseline — no seam pattern fires ───────────────────
using DefaultFn = Fn<int>;

static_assert(!ct::any_seam_pattern_matches_v<DefaultFn, cch::Identity,  DefaultFn>);
static_assert(!ct::any_seam_pattern_matches_v<DefaultFn, cch::Persist,   DefaultFn>);
static_assert(!ct::any_seam_pattern_matches_v<DefaultFn, cch::Serialize, DefaultFn>);
static_assert(!ct::any_seam_pattern_matches_v<DefaultFn, cch::Federate,  DefaultFn>);
static_assert(!ct::any_seam_pattern_matches_v<DefaultFn, cch::Reshard,   DefaultFn>);

// ── 2. gaps_010 seam — Monotonic + Reentrant over Identity ────────
//
// Producer carries Mutation=Monotonic + Reentrancy=NonReentrant
// (safe per-binding via substrate's M012 Atomic clearance).
// Consumer carries Mutation=Immutable + Reentrancy=Reentrant
// (safe per-binding).
// COMPOSITION over Identity exposes the race.

using GapsProducer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<fx::Effect::Bg>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Atomic,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Monotonic,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using GapsConsumer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::Reentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

static_assert(ct::seam_matches_v<
    cp::gaps_010_monotonic_concurrent_no_atomic,
    GapsProducer, cch::Identity, GapsConsumer>);

static_assert(ct::any_seam_pattern_matches_v<
    GapsProducer, cch::Identity, GapsConsumer>);

// Over PERSIST channel, NOT over Identity — seam pattern does NOT fire.
static_assert(!ct::seam_matches_v<
    cp::gaps_010_monotonic_concurrent_no_atomic,
    GapsProducer, cch::Persist, GapsConsumer>);

// which_seam_pattern_matches returns the correct cite.
static_assert(ct::which_seam_pattern_matches<GapsProducer, cch::Identity, GapsConsumer>()
              == ct::cite_gaps_010);

// ── 3. gaps_013 seam — External provenance × FromInternal under Federate ──
using TrustLaunderProducer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::External, strust::Unverified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using TrustLaunderConsumer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

static_assert(ct::seam_matches_v<
    cp::gaps_013_decimal_overflow_wrap,
    TrustLaunderProducer, cch::Federate, TrustLaunderConsumer>);

// Over Identity (same provenance preservation rules), the source check
// also fires — but the seam pattern 2 is specialized only for Federate.
static_assert(!ct::seam_matches_v<
    cp::gaps_013_decimal_overflow_wrap,
    TrustLaunderProducer, cch::Identity, TrustLaunderConsumer>);

// ── 4. krishnaswami_2017 seam — IO + Block under Persist ──────────
using IoProducer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<fx::Effect::IO>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using BlockConsumer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<fx::Effect::Block>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

static_assert(ct::seam_matches_v<
    cp::krishnaswami_2017_staleness_ct_channel,
    IoProducer, cch::Persist, BlockConsumer>);

// ── 5. krishnaswami_2014 seam — Capability under Federate ─────────
using CapProducer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Capability, fx::Row<>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using CapConsumer = DefaultFn;

static_assert(ct::seam_matches_v<
    cp::krishnaswami_2014_capability_replay,
    CapProducer, cch::Federate, CapConsumer>);

// Same Capability producer over Identity channel — NOT a seam violation
// (Identity preserves linearity per the substrate's Linear-modality
// machinery; only Federate fan-out triggers the duplication concern).
static_assert(!ct::seam_matches_v<
    cp::krishnaswami_2014_capability_replay,
    CapProducer, cch::Identity, CapConsumer>);

// ── 6. caires_pfenning_2010 seam — distinct protocols × Reshard ────
//
// Two producers with different session-typed protocols.  We synthesize
// distinct protocol_t types via the substrate's proto namespace.

struct ProtocolA {};
struct ProtocolB {};

using ProtoProducer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ProtocolA, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using ProtoConsumer = Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ProtocolB, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

static_assert(ct::seam_matches_v<
    cp::caires_pfenning_2010_implicit_flow,
    ProtoProducer, cch::Reshard, ProtoConsumer>);

// ── 7. Dashboard coverage assertion ───────────────────────────────
static_assert(ct::mechanically_detected_count_v == 6);

// Detection mode per pattern: the 5 §30.14 seam-matched patterns
// reach DetectionMode::Both; gaps_017 remains intra-only.
static_assert(ct::detection_mode_v<cp::gaps_010_monotonic_concurrent_no_atomic>
              == ct::DetectionMode::Both);
static_assert(ct::detection_mode_v<cp::gaps_013_decimal_overflow_wrap>
              == ct::DetectionMode::Both);
static_assert(ct::detection_mode_v<cp::krishnaswami_2017_staleness_ct_channel>
              == ct::DetectionMode::Both);
static_assert(ct::detection_mode_v<cp::krishnaswami_2014_capability_replay>
              == ct::DetectionMode::Both);
static_assert(ct::detection_mode_v<cp::caires_pfenning_2010_implicit_flow>
              == ct::DetectionMode::Both);
static_assert(ct::detection_mode_v<cp::gaps_017_capability_replay_session>
              == ct::DetectionMode::Intra);

// ── 8. Non-regression: intra matches still work post-G14 ──────────
//
// MonotonicBgAtomic (from Theory.h matcher_self_test) still fires the
// intra match for gaps_010 (matches_m012_shape).
static_assert(ct::matches<cp::gaps_010_monotonic_concurrent_no_atomic,
                          GapsProducer>::value);

}  // namespace

int main() {
    std::fputs("test_fixy_seam: OK\n", stdout);
    return 0;
}
