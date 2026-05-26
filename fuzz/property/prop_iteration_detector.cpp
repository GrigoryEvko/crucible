// ═══════════════════════════════════════════════════════════════════
// prop_iteration_detector.cpp — black-box correctness fuzzer for
// IterationDetector (L6 Graphs).
//
// IterationDetector watches the background thread's stream of op
// schema-hashes and reports an iteration boundary once a K=5 signature
// matches twice (two-match warmup confirmation).  That boolean GATES
// COMPILED-mode activation: a FALSE POSITIVE compiles a non-repeating
// trace (→ guaranteed divergence on the next op); a FALSE NEGATIVE
// leaves the model stuck in RECORD mode forever.  It had only
// scenario-based unit coverage, no randomized fuzzer.
//
// The detection heuristic is bespoke, so re-implementing it as an
// oracle would be a tautology, not a test.  Instead this fuzzer asserts
// BLACK-BOX properties that need zero knowledge of the K-signature
// internals:
//
//   (P1) No false positive: an all-distinct (never-repeating) stream
//        must NEVER report a boundary — no K-window can recur, so no
//        match can complete twice.
//   (P2) Determinism (DetSafe): two detectors fed the same stream emit
//        identical boolean sequences.
//   (P3) True positive: a clean period-P (P>=K, distinct-within-period)
//        stream repeated >=4 times MUST report at least one boundary —
//        otherwise COMPILED mode could never engage.
//   (P4) Reset fidelity: after reset(), re-feeding the same stream
//        reproduces the from-fresh sequence (behavioral witness of the
//        six reset() POST invariants).
//   (universal) Signature-build phase (first K ops) never reports a
//        boundary — a detector cannot fire before its own signature
//        exists.
//
// P1 and P3 are opposite-direction, so a vacuous check cannot satisfy
// both: the fuzzer passing on BOTH the distinct and periodic modes is
// itself the proof that the detector genuinely distinguishes repeating
// from non-repeating input.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/IterationDetector.h>
#include <crucible/Types.h>

#include <array>
#include <cstdint>
#include <utility>

namespace {

inline constexpr uint32_t kK = crucible::IterationDetector::K;  // 5
inline constexpr uint32_t kMaxLen = 512;

enum class Mode : uint8_t { Distinct = 0, Periodic = 1, Random = 2 };

struct StreamSpec {
    std::array<uint64_t, kMaxLen> hashes{};
    uint32_t len = 0;
    Mode mode = Mode::Random;
    uint8_t pad[3]{};
};

// Feed the whole stream through one detector, recording per-op results.
void feed(const StreamSpec& spec, std::array<bool, kMaxLen>& out) noexcept {
    crucible::IterationDetector det{};
    for (uint32_t i = 0; i < spec.len; ++i) {
        out[i] = det.check(crucible::SchemaHash{spec.hashes[i]});
    }
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);

    return run("iteration_detector", cfg,
        // ── Generator: one of three stream shapes ──
        [](Rng& rng) noexcept -> StreamSpec {
            StreamSpec spec{};
            spec.mode = static_cast<Mode>(rng.next_below(3));
            switch (spec.mode) {
                case Mode::Distinct: {
                    // Strictly increasing, all-distinct, non-zero — no
                    // K-window ever recurs.
                    spec.len = kK + rng.next_below(kMaxLen - kK);
                    const uint64_t base = 1u + rng.next_below(1000u);
                    for (uint32_t i = 0; i < spec.len; ++i)
                        spec.hashes[i] = base + i;
                    break;
                }
                case Mode::Periodic: {
                    // Period P in [K, K+8], distinct WITHIN a period so the
                    // signature (first K) recurs only at period starts.
                    const uint32_t period = kK + rng.next_below(9u);
                    const uint32_t reps = 4u + rng.next_below(4u);   // [4,7]
                    const uint64_t base = 1u + rng.next_below(1000u);
                    spec.len = period * reps;                        // <= 13*7=91
                    for (uint32_t i = 0; i < spec.len; ++i)
                        spec.hashes[i] = base + (i % period);
                    break;
                }
                case Mode::Random: {
                    spec.len = rng.next_below(kMaxLen + 1u);          // [0, kMaxLen]
                    for (uint32_t i = 0; i < spec.len; ++i)
                        spec.hashes[i] = rng.next64();
                    break;
                }
                default: std::unreachable();  // mode ∈ {0,1,2} by next_below(3)
            }
            return spec;
        },
        // ── Property: feed + check (P1)-(P4) + universal ──
        [](const StreamSpec& spec) noexcept -> bool {
            std::array<bool, kMaxLen> r1{};
            feed(spec, r1);

            // Universal: signature-build phase (first min(K,len) ops) never fires.
            const uint32_t build_n = spec.len < kK ? spec.len : kK;
            for (uint32_t i = 0; i < build_n; ++i)
                if (r1[i]) return false;

            // (P2) Determinism: a second fresh detector matches.
            std::array<bool, kMaxLen> r2{};
            feed(spec, r2);
            for (uint32_t i = 0; i < spec.len; ++i)
                if (r1[i] != r2[i]) return false;

            // (P4) Reset fidelity: reset mid-stream-fed detector, re-feed,
            //      must reproduce the from-fresh sequence.
            {
                crucible::IterationDetector det{};
                // Dirty the detector with a prefix, then reset.
                for (uint32_t i = 0; i < spec.len; ++i)
                    (void)det.check(crucible::SchemaHash{spec.hashes[i]});
                det.reset();
                for (uint32_t i = 0; i < spec.len; ++i) {
                    const bool got = det.check(crucible::SchemaHash{spec.hashes[i]});
                    if (got != r1[i]) return false;
                }
            }

            // Mode-specific.
            if (spec.mode == Mode::Distinct) {
                // (P1) No false positive on a never-repeating stream.
                for (uint32_t i = 0; i < spec.len; ++i)
                    if (r1[i]) return false;
            } else if (spec.mode == Mode::Periodic) {
                // (P3) A clean repeating period must fire at least once.
                bool any = false;
                for (uint32_t i = 0; i < spec.len; ++i) any = any || r1[i];
                if (!any) return false;
            }
            return true;
        });
}
