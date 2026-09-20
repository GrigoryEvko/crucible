// The hand-written transcendentals, exercised with non-constant operands.
//
// fixy/fp/Polynomial.h pins every value this file pins, as a
// static_assert over a constant-folded call.  That is the stronger check
// and it is not the same check: a constant evaluation runs GCC's
// compile-time arithmetic, and a runtime call runs the host's FPU with
// whatever the optimizer did to the expression.  The two agreeing is the
// claim, and it is only a claim if both are made.
//
// Old spelling: test/test_fixy_v_095_box_muller_polynomial.cpp, which
// called fixy::fp::runtime_smoke_test() and asserted nothing about what
// came back.

#include <fixy/fp/Polynomial.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace fp = fixy::fp;

namespace {

// Reading through a volatile keeps the optimizer from folding the call
// into the constant the header already pinned.
[[nodiscard]] float opaque(float value) noexcept {
    volatile float sink = value;
    return sink;
}

[[nodiscard]] std::uint32_t word(float value) noexcept { return std::bit_cast<std::uint32_t>(value); }

[[nodiscard]] int report(const char* what, std::uint32_t got, std::uint32_t want) {
    if (got != want) {
        std::fprintf(stderr, "%s: got 0x%08X, want 0x%08X\n", what, got, want);
        return 1;
    }
    return 0;
}

[[nodiscard]] int identities_hold_at_runtime() {
    int failures = 0;
    failures += report("log_poly(1)", word(fp::log_poly(opaque(1.0f))), word(0.0f));
    failures += report("sin_poly(0)", word(fp::sin_poly(opaque(0.0f))), word(0.0f));
    failures += report("cos_poly(0)", word(fp::cos_poly(opaque(0.0f))), word(1.0f));
    failures += report("log_poly(2)", word(fp::log_poly(opaque(2.0f))), word(0x1.62e43p-1f));
    failures += report("sin_poly(pi/2)", word(fp::sin_poly(opaque(0x1.921FB6p+0f))), word(1.0f));
    failures += report("cos_poly(pi)", word(fp::cos_poly(opaque(0x1.921FB6p+1f))), word(-1.0f));
    return failures == 0 ? 0 : 1;
}

[[nodiscard]] int box_muller_pair_matches_the_pin() {
    // The two words come from the header, so this cell fails if the pin
    // moves, and fails differently if the runtime path disagrees with the
    // constant-folded one.
    using namespace fixy::fp::detail::polynomial_self_test;

    volatile std::uint32_t u1 = 0xDEADBEEFu;
    volatile std::uint32_t u2 = 0xCAFEBABEu;
    const auto pair = fp::box_muller_polynomial(u1, u2);

    int failures = 0;
    failures += report("box_muller .first", word(pair.first), kBoxMullerZ1);
    failures += report("box_muller .second", word(pair.second), kBoxMullerZ2);
    return failures == 0 ? 0 : 1;
}

// The quadrant is masked to two bits, so no argument can reach the
// std::unreachable() arm.  A runtime sweep is what says so for arguments
// a constant expression would not reach.
[[nodiscard]] int every_quadrant_is_in_range() {
    for (int step = -2000; step <= 2000; ++step) {
        const float x = opaque(static_cast<float>(step) * 0.37f);
        const auto rr = fp::reduce_quarter_pi(x);
        if (rr.quadrant < 0 || rr.quadrant > 3) {
            std::fprintf(stderr, "reduce_quarter_pi(%d * 0.37) reported quadrant %d, outside [0,3]\n", step,
                         rr.quadrant);
            return 1;
        }
        // Both consumers must return a finite number for every quadrant.
        const float s = fp::sin_poly(x);
        const float c = fp::cos_poly(x);
        if (std::isnan(s) || std::isnan(c)) {
            std::fprintf(stderr, "sin_poly/cos_poly returned NaN at %d * 0.37\n", step);
            return 1;
        }
    }
    return 0;
}

// box_muller feeds the logarithm a value derived from a raw word, and the
// added one is what keeps that argument above zero.  A raw zero is the
// case that would have produced log(0).
[[nodiscard]] int box_muller_survives_the_extreme_words() {
    const std::uint32_t words[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
    for (const std::uint32_t w1 : words) {
        for (const std::uint32_t w2 : words) {
            volatile std::uint32_t a = w1;
            volatile std::uint32_t b = w2;
            const auto pair = fp::box_muller_polynomial(a, b);
            if (std::isnan(pair.first) || std::isnan(pair.second)) {
                std::fprintf(stderr, "box_muller(0x%08X, 0x%08X) produced NaN\n", w1, w2);
                return 1;
            }
        }
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = identities_hold_at_runtime(); rc != 0) return rc;
    if (const int rc = box_muller_pair_matches_the_pin(); rc != 0) return rc;
    if (const int rc = every_quadrant_is_in_range(); rc != 0) return rc;
    if (const int rc = box_muller_survives_the_extreme_words(); rc != 0) return rc;
    return 0;
}
