// ═══════════════════════════════════════════════════════════════════
// prop_monotonic_predicates.cpp — differential fuzzer for
// decide::weakly_increasing (foundation/contracts/Decide.h).
//
// weakly_increasing admits an equal adjacent pair and refuses a strict
// descent.  The TraceGraph CSR row offsets cite it, because a
// zero-length row repeats the offset of the row before it.
//
// The oracle is std::is_sorted, an iterator-based implementation from a
// different codebase than the index loop under test.  Four generator
// modes drive the directed cases: StrictlyIncreasing and WeaklyWithDups
// must pass, Regression must fail, and Random is decided by the oracle
// alone.  Signed int32_t reaches negative values and crossings from
// negative to positive.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <foundation/contracts/Decide.h>

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
    WeaklyWithDups = 1,
    Regression = 2,
    Random = 3,
};

struct SeqSpec {
    std::array<T, kMaxLen> xs{};
    uint32_t len = 0;
    Mode mode = Mode::Random;
    uint8_t pad[3]{};
};

// The oracle: non-decreasing order, by iterators.
[[nodiscard]] bool weak_oracle(std::span<const T> xs) noexcept {
    return std::is_sorted(xs.begin(), xs.end());
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    using ::foundation::decide::weakly_increasing;

    const Config cfg = parse_args(argc, argv, 2000000);

    return run(
        "monotonic_predicates", cfg,
        // ── Generator ──
        [](Rng& rng) noexcept -> SeqSpec {
            SeqSpec spec{};
            spec.mode = rng.pick<Mode>();
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
                    spec.xs[drop_at] = spec.xs[drop_at - 1u] - static_cast<T>(1u + rng.next_below(1000u));
                    break;
                }
                case Mode::Random: {
                    spec.len = rng.next_below(kMaxLen + 1u);
                    for (uint32_t i = 0; i < spec.len; ++i) {
                        spec.xs[i] = static_cast<T>(rng.next32());  // full range incl negatives
                    }
                    break;
                }
                default:
                    std::unreachable();  // pick returns an enumerator of Mode
            }
            return spec;
        },
        // ── Property: std differential and the construction ──
        [](const SeqSpec& spec) noexcept -> bool {
            const std::span<const T> view{spec.xs.data(), spec.len};
            const bool weak = weakly_increasing<T>(view);

            if (weak != weak_oracle(view)) return false;

            switch (spec.mode) {
                case Mode::StrictlyIncreasing:
                case Mode::WeaklyWithDups:
                    if (!weak) return false;  // built non-decreasing
                    break;
                case Mode::Regression:
                    if (weak) return false;  // a descent → weak false
                    break;
                case Mode::Random:
                    break;  // the oracle decides
                default:
                    std::unreachable();
            }
            return true;
        });
}
