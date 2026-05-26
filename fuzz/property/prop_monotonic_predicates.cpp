// ═══════════════════════════════════════════════════════════════════
// prop_monotonic_predicates.cpp — paired differential / inter-predicate
// fuzzer for decide::strictly_increasing and decide::weakly_increasing
// (safety/Decide.h).
//
// These two span-quantified monotonicity predicates differ only in `<`
// vs `<=` at the consecutive-pair test, and that one-character choice
// is load-bearing: Cipher::store cites strictly_increasing (duplicate
// step_id breaks event-source idempotence), while Arena epoch chains
// and TraceGraph CSR row offsets cite weakly_increasing (zero-length
// records legitimately repeat an offset).  The doc warns "the wrong
// choice silently corrupts replay" — so the bug class this fuzzer
// targets is strict-vs-weak confusion, NOT a single predicate in
// isolation.
//
// Teeth come from two independent sources:
//
//   (1) A std-library differential.  weakly_increasing ≡
//       std::is_sorted (non-decreasing); strictly_increasing ≡
//       std::is_sorted AND std::adjacent_find == end (no equal pair).
//       The iterator-based std implementation is a different codebase
//       from the index loop under test, so a regression in either
//       Decide loop diverges from std.
//
//   (2) Inter-predicate algebraic laws — independent of HOW either
//       predicate is implemented, so they cannot both be co-broken the
//       same way:
//         * strict ⟹ weak                (strict is the stronger claim)
//         * no equal-adjacent pair ⟹ strict == weak
//         * equal-adjacent pair present but NO regression ⟹
//             weak && !strict             (equal-adjacency is exactly
//             the discriminator between the two)
//
// Four generator modes drive the directed cases: StrictlyIncreasing
// (both true), WeaklyWithDups (weak true, strict false), Regression
// (both false), Random (oracle + laws decide).  Signed int32_t exercises
// negative values and negative-to-positive crossings.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/safety/Decide.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace {

inline constexpr uint32_t kMaxLen = 64;

using T = int32_t;
using crucible::fuzz::prop::Rng;

enum class Mode : uint8_t {
    StrictlyIncreasing = 0,
    WeaklyWithDups     = 1,
    Regression         = 2,
    Random             = 3,
};

struct SeqSpec {
    std::array<T, kMaxLen> xs{};
    uint32_t len  = 0;
    Mode     mode = Mode::Random;
    uint8_t  pad[3]{};
};

// Independent std-library oracles (iterator-based, distinct from the
// predicate's index loop).
[[nodiscard]] bool weak_oracle(std::span<const T> xs) noexcept {
    return std::is_sorted(xs.begin(), xs.end());  // non-decreasing
}
[[nodiscard]] bool strict_oracle(std::span<const T> xs) noexcept {
    return std::is_sorted(xs.begin(), xs.end()) &&
           std::adjacent_find(xs.begin(), xs.end()) == xs.end();  // + no equal pair
}
[[nodiscard]] bool has_equal_adjacent(std::span<const T> xs) noexcept {
    return std::adjacent_find(xs.begin(), xs.end()) != xs.end();
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    using crucible::decide::strictly_increasing;
    using crucible::decide::weakly_increasing;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("monotonic_predicates", cfg,
        // ── Generator ──
        [](Rng& rng) noexcept -> SeqSpec {
            SeqSpec spec{};
            spec.mode = static_cast<Mode>(rng.next_below(4));
            // Bounded base value keeps accumulated sequences well inside
            // int32 range (start in [-10000, 9999]; max climb 64*1000).
            const T start = static_cast<T>(rng.next_below(20000u)) - 10000;
            switch (spec.mode) {
                case Mode::StrictlyIncreasing: {
                    spec.len = rng.next_below(kMaxLen + 1u);  // [0, kMaxLen]
                    T v = start;
                    for (uint32_t i = 0; i < spec.len; ++i) {
                        if (i != 0) v += static_cast<T>(1u + rng.next_below(1000u));  // strict step
                        spec.xs[i] = v;
                    }
                    break;
                }
                case Mode::WeaklyWithDups: {
                    spec.len = 2u + rng.next_below(kMaxLen - 1u);  // [2, kMaxLen]
                    const uint32_t dup_at = 1u + rng.next_below(spec.len - 1u);  // force step 0 here
                    T v = start;
                    for (uint32_t i = 0; i < spec.len; ++i) {
                        if (i != 0) {
                            const T step = (i == dup_at)
                                ? T{0}  // guarantees an equal-adjacent pair
                                : static_cast<T>(rng.next_below(1000u));  // >= 0, non-decreasing
                            v += step;
                        }
                        spec.xs[i] = v;
                    }
                    break;
                }
                case Mode::Regression: {
                    spec.len = 2u + rng.next_below(kMaxLen - 1u);  // [2, kMaxLen]
                    T v = start;
                    for (uint32_t i = 0; i < spec.len; ++i) {
                        if (i != 0) v += static_cast<T>(rng.next_below(1000u));
                        spec.xs[i] = v;
                    }
                    // Force one strict descent: drop a chosen element below
                    // its predecessor.
                    const uint32_t drop_at = 1u + rng.next_below(spec.len - 1u);
                    spec.xs[drop_at] = spec.xs[drop_at - 1u] -
                                       static_cast<T>(1u + rng.next_below(1000u));
                    break;
                }
                case Mode::Random: {
                    spec.len = rng.next_below(kMaxLen + 1u);
                    for (uint32_t i = 0; i < spec.len; ++i) {
                        spec.xs[i] = static_cast<T>(rng.next32());  // full range incl negatives
                    }
                    break;
                }
                default: std::unreachable();  // mode ∈ {0..3} by next_below(4)
            }
            return spec;
        },
        // ── Property: std differential + inter-predicate laws ──
        [](const SeqSpec& spec) noexcept -> bool {
            const std::span<const T> view{spec.xs.data(), spec.len};
            const bool strict = strictly_increasing<T>(view);
            const bool weak   = weakly_increasing<T>(view);

            // (1) std-library differential.
            if (strict != strict_oracle(view)) return false;
            if (weak   != weak_oracle(view))   return false;

            // (2) Inter-predicate algebraic laws (implementation-blind).
            if (strict && !weak) return false;                      // strict ⟹ weak
            const bool has_dup = has_equal_adjacent(view);
            if (!has_dup && (strict != weak)) return false;         // no dup ⟹ strict == weak
            if (weak && has_dup && strict) return false;            // dup + sorted ⟹ !strict

            // Construction-directed.
            switch (spec.mode) {
                case Mode::StrictlyIncreasing:
                    if (!strict) return false;                      // strictly built
                    break;
                case Mode::WeaklyWithDups:
                    if (!weak || strict) return false;              // weak-true, strict-false
                    break;
                case Mode::Regression:
                    if (weak) return false;                         // a descent → weak false
                    break;
                case Mode::Random:
                    break;                                          // oracles + laws decide
                default: std::unreachable();
            }
            return true;
        });
}
