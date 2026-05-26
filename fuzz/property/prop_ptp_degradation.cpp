// ═══════════════════════════════════════════════════════════════════
// prop_ptp_degradation.cpp — priority + signed-abs fuzzer for the PTP
// daemon-report degradation classifier (topology/Ptp.h
// ptp_degradation_reason).
//
// ptp_degradation_reason maps a ptp4l/phc2sys daemon report to the FIRST
// failing reason in a fixed priority order: daemon-down (ptp4l, then
// phc2sys), grandmaster-missing, servo-unlocked, excessive-offset, then
// excessive-skew.  Two parts are bug-prone: (1) the offset gate computes
// |offset_from_master_ns| with the unsigned-wrap idiom
// `offset<0 ? 0u - (uint64)offset : (uint64)offset` — correct only
// because it never negates a signed INT64_MIN (which is UB); (2) the
// priority order decides which reason wins when several conditions hold
// at once (a reordered check would mis-classify, e.g. report
// ExcessiveSkew while the grandmaster is actually missing → operator
// chases the wrong fault).  This is the only property test for the
// topology PTP surface; test_ptp pins hand-picked cases.
//
// The INDEPENDENT oracle re-derives the result two ways that share
// nothing with production: the absolute offset via a SIGNED 128-bit
// negate (`o < 0 ? -o : o`, where o is __int128 so INT64_MIN negates
// safely) compared to the threshold, and the priority as an explicit
// if-chain mirroring the documented order — so any reordering or any
// abs/comparison defect (e.g. >= vs >, or a wrong INT64_MIN abs) surfaces
// as a mismatch.  Reports are generated with all fields independent so
// MULTIPLE degradation conditions frequently hold together, exercising
// the first-wins priority; offsets are corner-biased to {INT64_MIN,
// INT64_MAX, 0, ±1, small} and the accepted bounds to small / near-max
// so the |offset| > max_offset and skew > max_skew boundaries fire
// densely against small thresholds.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/topology/Ptp.h>

#include <cstdint>
#include <limits>

namespace {

namespace ct = crucible::topology;
using crucible::fuzz::prop::Rng;

inline constexpr std::uint64_t kU64Max =
    std::numeric_limits<std::uint64_t>::max();

// 128-bit accumulators for the independent absolute-offset oracle.
// __extension__ silences -Wpedantic (matches fuzz/property/prop_checked_arith.cpp).
__extension__ using w_s = __int128;
__extension__ using w_u = unsigned __int128;

struct Spec {
    std::uint8_t ptp4l = 0;
    std::uint8_t phc2sys = 0;
    std::uint8_t grandmaster = 0;
    std::uint8_t servo = 0;          // 0..6 → PtpServoState
    std::int64_t offset = 0;
    std::uint64_t skew_bound = 1;    // ≥ 1 (Positive contract)
    std::uint64_t max_skew = 1;
    std::uint64_t max_offset = 1;
};

[[nodiscard]] std::int64_t gen_offset(Rng& rng) noexcept {
    switch (rng.next_below(7u)) {
        case 0: return std::numeric_limits<std::int64_t>::min();   // INT64_MIN abs edge
        case 1: return std::numeric_limits<std::int64_t>::max();
        case 2: return 0;
        case 3: return -1;
        case 4: return 1;
        case 5: return static_cast<std::int64_t>(rng.next_below(4000u)) - 2000;  // [-2000,1999]
        default: return static_cast<std::int64_t>(rng.next64());
    }
}

// Positive bound (≥ 1); biased small so the offset/skew boundaries fire.
[[nodiscard]] std::uint64_t gen_bound(Rng& rng) noexcept {
    switch (rng.next_below(5u)) {
        case 0: return 1u;
        case 1: return 1u + rng.next_below(2000u);
        case 2: return kU64Max;
        case 3: return kU64Max - rng.next_below(4u);
        default: return rng.next64() | 1u;   // nonzero
    }
}

[[nodiscard]] ct::PtpServoState servo_of(std::uint8_t raw) noexcept {
    return static_cast<ct::PtpServoState>(static_cast<std::uint8_t>(raw % 7u));
}

// Independent re-derivation of the priority classifier.
[[nodiscard]] ct::PtpDegradationReason oracle(const Spec& s) noexcept {
    using R = ct::PtpDegradationReason;
    if (s.ptp4l == 0u) return R::Ptp4lUnavailable;
    if (s.phc2sys == 0u) return R::Phc2sysUnavailable;
    if (s.grandmaster == 0u) return R::GrandmasterMissing;
    const ct::PtpServoState servo = servo_of(s.servo);
    if (servo != ct::PtpServoState::Slave && servo != ct::PtpServoState::Master) {
        return R::ServoUnlocked;
    }
    // |offset| via signed 128-bit negate — INT64_MIN negates safely.
    const w_s o = static_cast<w_s>(s.offset);
    const w_u abs_offset = o < 0 ? static_cast<w_u>(-o) : static_cast<w_u>(o);
    if (abs_offset > static_cast<w_u>(s.max_offset)) return R::ExcessiveOffset;
    if (s.skew_bound > s.max_skew) return R::ExcessiveSkew;
    return R::None;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("ptp_degradation", cfg,
        [](Rng& rng) noexcept -> Spec {
            return Spec{
                .ptp4l = static_cast<std::uint8_t>(rng.next_below(4u) == 0u ? 0u : 1u),
                .phc2sys = static_cast<std::uint8_t>(rng.next_below(4u) == 0u ? 0u : 1u),
                .grandmaster = static_cast<std::uint8_t>(rng.next_below(4u) == 0u ? 0u : 1u),
                .servo = static_cast<std::uint8_t>(rng.next_below(7u)),
                .offset = gen_offset(rng),
                .skew_bound = gen_bound(rng),
                .max_skew = gen_bound(rng),
                .max_offset = gen_bound(rng),
            };
        },
        [](const Spec& s) noexcept -> bool {
            const ct::PtpDaemonReport report{
                .ptp4l_running = s.ptp4l != 0u,
                .phc2sys_running = s.phc2sys != 0u,
                .grandmaster_present = s.grandmaster != 0u,
                .servo = servo_of(s.servo),
                .offset_from_master_ns = s.offset,
                .mean_path_delay_ns = ct::PositivePtpPathDelayNs{std::uint64_t{1}},
                .frequency_adjustment_ppb = 0,
                .skew_bound_ns = ct::PositivePtpSkewBoundNs{s.skew_bound},
                .max_accepted_skew_ns = ct::PositivePtpSkewBoundNs{s.max_skew},
                .max_accepted_offset_ns = ct::PositivePtpOffsetBoundNs{s.max_offset},
                .sequence = 0,
            };
            return ct::ptp_degradation_reason(report) == oracle(s);
        });
}
