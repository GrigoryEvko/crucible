// ═══════════════════════════════════════════════════════════════════
// prop_hlc_update.cpp — causality/monotonicity fuzzer for the Hybrid
// Logical Clock update rule (canopy/Hlc.h).
//
// Hlc (Kulkarni-Demirbas-Madappa-Avva-Leone) timestamps every canopy
// gossip/consensus event so happens-before is recoverable across the
// fleet.  The whole point is: if event e causally precedes f, then
// hlc(e) < hlc(f).  That guarantee rests entirely on two PURE static
// functions — `Hlc::local_event(old, physical_now)` and
// `Hlc::recv_event(old, peer, physical_now)` — which take the physical
// time explicitly (no clock dependency), so they are deterministically
// fuzzable.  A wrong counter pick in any of recv_event's four cases, or
// a botched counter-rollover, silently breaks causality: two events get
// equal-or-inverted timestamps and the fleet's ordering corrupts.
//
// test_hlc.cpp pins ~6 hand-picked static_asserts (the three recv cases,
// a local advance/bump, one counter==MAX rollover) but never asserts the
// UNIVERSAL invariant across the domain, nor recv-side rollover, nor the
// absolute (UINT64_MAX, UINT32_MAX) ceiling.  This fuzzer closes that.
//
// HlcTimestamp's defaulted operator<=> is lexicographic (physical_ns,
// then counter) — exactly the HLC order — so domination is re-derived
// INDEPENDENTLY by direct comparison, a different computation from the
// case analysis under test.  Per (old, peer, physical_now), corner-
// biased to {0, 1, MAX-1, MAX} with a small-cluster mode that makes the
// three physical values collide often (exercising recv_event's equal-
// physical case densely), it asserts:
//
//   local_event(old, p):
//     * result >= old                         (monotonic — never regress)
//     * result > old   unless old == ceiling   (strict local advance)
//     * result.physical >= max(old.physical, p)
//   recv_event(old, peer, p):
//     * result >= old  AND  result >= peer     (dominates both — causality)
//     * result > old   unless old  == ceiling
//     * result > peer  unless peer == ceiling
//     * result.physical >= max(old.physical, peer.physical, p)
//   pack/unpack round-trips for any timestamp.
//
// The `>=` forms hold universally (even at the saturating ceiling, where
// result == input); the strict forms are guarded by the ceiling because
// bumped_ deliberately saturates (MAX,MAX)→(MAX,MAX).  All verified
// clean by hand-trace before shipping — the rule is correct; this is the
// full-domain regression net the spot static_asserts lack.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Hlc.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;
using cc::HlcTimestamp;

inline constexpr std::uint64_t kPMax = std::numeric_limits<std::uint64_t>::max();
inline constexpr std::uint32_t kCMax = std::numeric_limits<std::uint32_t>::max();
inline constexpr HlcTimestamp kCeiling{.physical_ns = kPMax, .counter = kCMax};

struct Spec {
    HlcTimestamp old_ts{};
    HlcTimestamp peer_ts{};
    std::uint64_t physical_now = 0;
};

[[nodiscard]] std::uint64_t gen_u64(Rng& rng) noexcept {
    switch (rng.next_below(6u)) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return kPMax;
        case 3: return kPMax - 1u;
        case 4: return rng.next_below(64u);  // small cluster → physical collisions
        default: return rng.next64();
    }
}

[[nodiscard]] std::uint32_t gen_u32(Rng& rng) noexcept {
    switch (rng.next_below(6u)) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return kCMax;
        case 3: return kCMax - 1u;
        case 4: return rng.next_below(64u);
        default: return rng.next32();
    }
}

[[nodiscard]] HlcTimestamp gen_ts(Rng& rng) noexcept {
    return HlcTimestamp{.physical_ns = gen_u64(rng), .counter = gen_u32(rng)};
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("hlc_update", cfg,
        [](Rng& rng) noexcept -> Spec {
            return Spec{gen_ts(rng), gen_ts(rng), gen_u64(rng)};
        },
        [](const Spec& spec) noexcept -> bool {
            const HlcTimestamp old_ts = spec.old_ts;
            const HlcTimestamp peer_ts = spec.peer_ts;
            const std::uint64_t now = spec.physical_now;

            // ── pack/unpack round-trip (low 32 bits unused, must restore) ──
            if (cc::detail::unpack_hlc_timestamp(
                    cc::detail::pack_hlc_timestamp(old_ts)) != old_ts) {
                return false;
            }
            if (cc::detail::unpack_hlc_timestamp(
                    cc::detail::pack_hlc_timestamp(peer_ts)) != peer_ts) {
                return false;
            }

            // ── local_event ──
            const HlcTimestamp le = cc::Hlc::local_event(old_ts, now);
            if (le < old_ts) return false;                       // monotonic
            if (old_ts != kCeiling && !(le > old_ts)) return false;  // strict
            if (le.physical_ns < std::max(old_ts.physical_ns, now)) return false;

            // ── recv_event ──
            const HlcTimestamp re = cc::Hlc::recv_event(old_ts, peer_ts, now);
            if (re < old_ts) return false;                        // dominates old
            if (re < peer_ts) return false;                       // dominates peer
            if (old_ts != kCeiling && !(re > old_ts)) return false;
            if (peer_ts != kCeiling && !(re > peer_ts)) return false;
            const std::uint64_t recv_floor =
                std::max({old_ts.physical_ns, peer_ts.physical_ns, now});
            if (re.physical_ns < recv_floor) return false;

            return true;
        });
}
