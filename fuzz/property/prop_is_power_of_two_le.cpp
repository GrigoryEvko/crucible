// ═══════════════════════════════════════════════════════════════════
// prop_is_power_of_two_le.cpp — differential-oracle fuzzer for
// decide::is_power_of_two_le (safety/Decide.h).
//
// is_power_of_two_le(x, bound) returns true iff x is a power of two AND
// 0 < x <= bound.  Unlike the other Decide predicates fuzzed so far, it
// has FIVE real production cites — SwissCtrl::kGroupWidth (SIMD group
// width), the KernelCache lock-free slot-table size, and the RecipePool
// / ExprPool / ExprPool-int-cache capacities — all capacity invariants
// for open-addressing / SIMD structures where a non-power-of-two size
// silently breaks the `x & (x-1)` mask arithmetic.  A regression here
// corrupts hash-table probing, so it is the highest-value Decide
// predicate to lock down with random differential coverage.
//
// The oracle uses std::popcount on the unsigned magnitude — a power of
// two has exactly one set bit — which is a completely different
// mechanism from the code-under-test's `(x & (x - 1)) == 0` Kernighan
// test.  Both are gated by the same `0 < x <= bound` bounds, but the
// power-of-two CORE is computed two different ways, so a divergence on
// any input is a genuine bug.
//
// Fuzzed over BOTH uint32_t and int32_t: the signed instantiation is
// where the only non-trivial logic lives — the `x <= 0` guard (which
// rejects zero and, for signed T, negatives, and prevents `x - 1`
// underflow UB at INT_MIN) and the `x > bound` branch with a possibly-
// NEGATIVE bound.  The hand-picked static_asserts barely exercise those
// paths; random signed inputs do.
//
// Four generator modes give teeth-by-construction:
//   * PowerInBound  — x a positive power of two, bound = T_MAX.  MUST
//     return true.
//   * PowerOverBound — x a positive power of two, bound in [0, x).
//     MUST return false (x > bound).
//   * NonPower      — x biased toward non-powers; oracle is ground
//     truth.
//   * Random        — full-range x and bound (incl. 0 / negatives for
//     signed); oracle is ground truth.
//
// PowerInBound (true) and PowerOverBound (false) are opposite-
// direction, so a vacuous oracle cannot satisfy the whole suite.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/safety/Decide.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

using crucible::fuzz::prop::Rng;

enum class Mode : uint8_t {
    PowerInBound  = 0,
    PowerOverBound = 1,
    NonPower      = 2,
    Random        = 3,
};

template <typename T>
struct PowSpec {
    T       x     = 0;
    T       bound = 0;
    Mode    mode  = Mode::Random;
    uint8_t pad[3]{};
};

// Independent oracle: same 0 < x <= bound gate, but the power-of-two
// core is popcount(|x|) == 1 (one set bit) rather than the
// (x & (x - 1)) == 0 Kernighan identity used by the predicate.
template <typename T>
[[nodiscard]] bool pow_oracle(T x, T bound) noexcept {
    if (x <= T{0}) return false;
    if (x > bound) return false;
    using U = std::make_unsigned_t<T>;
    return std::popcount(static_cast<U>(x)) == 1;
}

template <typename T>
[[nodiscard]] int run_pow(const crucible::fuzz::prop::Config& cfg,
                          const char* name) {
    using crucible::fuzz::prop::run;
    using crucible::decide::is_power_of_two_le;
    using U = std::make_unsigned_t<T>;
    // For unsigned T the top bit (k == width-1) is a valid positive
    // power of two; for signed T it is the sign bit, so cap at width-2
    // to keep generated powers positive.
    constexpr uint32_t width   = sizeof(T) * 8u;
    constexpr uint32_t max_pow = std::is_signed_v<T> ? (width - 2u) : (width - 1u);

    return run(name, cfg,
        // ── Generator ──
        [](Rng& rng) noexcept -> PowSpec<T> {
            PowSpec<T> spec{};
            spec.mode = static_cast<Mode>(rng.next_below(4));
            switch (spec.mode) {
                case Mode::PowerInBound: {
                    const uint32_t k = rng.next_below(max_pow + 1u);  // [0, max_pow]
                    spec.x     = static_cast<T>(static_cast<U>(1) << k);
                    spec.bound = std::numeric_limits<T>::max();  // x <= bound always
                    break;
                }
                case Mode::PowerOverBound: {
                    const uint32_t k = rng.next_below(max_pow + 1u);
                    spec.x     = static_cast<T>(static_cast<U>(1) << k);
                    // bound in [0, x) → strictly below x → x > bound.
                    spec.bound = static_cast<T>(rng.next_below(static_cast<uint32_t>(spec.x)));
                    break;
                }
                case Mode::NonPower: {
                    // Bias toward non-powers: OR two random values so
                    // multiple bits are usually set; oracle is truth.
                    spec.x     = static_cast<T>(rng.next32() | rng.next32());
                    spec.bound = std::numeric_limits<T>::max();
                    break;
                }
                case Mode::Random: {
                    spec.x     = static_cast<T>(rng.next32());  // full range incl 0 / negatives
                    spec.bound = static_cast<T>(rng.next32());
                    break;
                }
                default: std::unreachable();  // mode ∈ {0..3} by next_below(4)
            }
            return spec;
        },
        // ── Property: differential + construction-direction ──
        [](const PowSpec<T>& spec) noexcept -> bool {
            const bool cut   = is_power_of_two_le<T>(spec.x, spec.bound);
            const bool truth = pow_oracle<T>(spec.x, spec.bound);
            if (cut != truth) return false;  // universal differential

            switch (spec.mode) {
                case Mode::PowerInBound:
                    if (!cut) return false;  // positive power of two within bound
                    break;
                case Mode::PowerOverBound:
                    if (cut) return false;   // x > bound → must reject
                    break;
                case Mode::NonPower:
                case Mode::Random:
                    break;                   // oracle is ground truth
                default: std::unreachable();
            }
            return true;
        });
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    // Both instantiations run the full iteration budget; the signed
    // one exercises the x<=0 guard and negative-bound branch.
    const int r_u = run_pow<uint32_t>(cfg, "is_power_of_two_le<u32>");
    const int r_i = run_pow<int32_t>(cfg, "is_power_of_two_le<i32>");
    return (r_u != 0) ? r_u : r_i;
}
