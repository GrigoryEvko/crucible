// ═══════════════════════════════════════════════════════════════════
// prop_fractional_lattice.cpp — arithmetic-correctness fuzzer for
// FractionalLattice::add / mul (foundation/algebra/lattices/FractionalLattice.h).
//
// FractionalLattice is the ℚ∩[0,1] partial monoid backing SharedPermission
// <Tag> (CSL fractional permissions, O'Hearn 2007 / Boyland 2003).  Its
// add/mul are the ONLY runtime-valued lattice ops with genuinely non-
// trivial arithmetic (cross-product + gcd reduction) — every sister
// lattice is max/min (chain) or bitwise (boolean).  Sparse coverage once
// hid a real bug: add cast its __int128 numerator sum to int64 BEFORE
// gcd-reducing, so a sum at the inclusive MAX_SAFE_MAGNITUDE bound wrapped
// to INT64_MIN and hit std::gcd UB (simplify_wide now reduces in the wide
// type first).  This fuzzer is the regression net for the arithmetic.
//
// The carrier is the unit interval with a denominator of at most the
// bound.  add refuses a sum above one, and add and mul refuse a result
// whose reduced denominator is above the bound.  The fuzzer calls an
// operation only inside its domain.  test/foundation/test_lattice_closure.cpp
// holds the refusals, each in a child process.
//
// The oracle is an INDEPENDENT __int128 cross-multiplication: rather
// than recompute the reduced fraction (which would duplicate add/mul),
// it verifies the returned r == true_num/true_den by the equality
//     r.num · true_den == true_num · r.den      (all in __int128)
// where (true_num, true_den) is the UN-reduced sum/product.  That is a
// different computation from the code under test (which reduces via
// __int128 gcd then narrows), so a divergence is a genuine bug.  The
// products stay within signed __int128 (≤ 2^125 ≪ 2^127) for all
// well-formed inputs.  The domain test reduces only the denominator, with
// its own greatest common divisor.
//
// Per (a, b) it asserts, for each operation inside its domain:
//   * the result is in the carrier: non-negative, at most one, with a
//     positive denominator at most the bound;
//   * add/mul equal the true rational via the __int128 oracle;
//   * commutativity: add(a,b)==add(b,a), mul(a,b)==mul(b,a);
//   * identities: add(a, 0/1)==a, mul(a, 1/1)==a, mul(a, 0/1)==0/1.
//
// Four generator modes — Random (full [0,2^31] range), Boundary
// (num/den drawn from {0,1,2^30,2^31-1,2^31} to hit the overflow corner
// densely), Small (small denominators) and Dyadic (a power of two as the
// denominator, so the sums at the top of the bound stay in the domain) —
// so the boundary that broke is exercised heavily, not just sampled.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <fixy/Core.h>
#include <foundation/algebra/lattices/FractionalLattice.h>

#include <array>
#include <cstdint>

namespace {

using ::foundation::algebra::lattices::Rational;
using ::foundation::algebra::lattices::FractionalLattice;
using crucible::fuzz::prop::Rng;
using W = Rational::wide_signed;  // __int128

inline constexpr std::int64_t kMax = Rational::MAX_SAFE_MAGNITUDE;  // 2^31

enum class Mode : uint8_t {
    Random = 0,
    Boundary = 1,
    Small = 2,
    Dyadic = 3
};

struct PairSpec {
    Rational a{};
    Rational b{};
    Mode mode = Mode::Random;
    uint8_t pad[7]{};
};

// One share of the carrier: den in [1, 2^31], num in [0, den].
[[nodiscard]] Rational gen_random(Rng& rng) noexcept {
    const std::int64_t den = 1 + static_cast<std::int64_t>(rng.next_below(static_cast<uint32_t>(kMax)));  // [1, 2^31]
    const std::int64_t num = static_cast<std::int64_t>(rng.next_below(static_cast<uint32_t>(den) + 1u));  // [0, den]
    return Rational{num, den};
}
[[nodiscard]] std::int64_t boundary_value(Rng& rng) noexcept {
    constexpr std::array<std::int64_t, 5> corners{0, 1, std::int64_t{1} << 30, kMax - 1, kMax};
    return corners[rng.next_below(5u)];
}
[[nodiscard]] Rational gen_boundary(Rng& rng) noexcept {
    std::int64_t num = boundary_value(rng);
    std::int64_t den = boundary_value(rng);
    if (den == 0) den = 1;  // denominator must be positive + well-formed
    if (num > den) return Rational{den, num};
    return Rational{num, den};
}
[[nodiscard]] Rational gen_small(Rng& rng) noexcept {
    const std::int64_t den = 1 + static_cast<std::int64_t>(rng.next_below(64u));
    return Rational{static_cast<std::int64_t>(rng.next_below(static_cast<uint32_t>(den) + 1u)), den};
}
[[nodiscard]] Rational gen_dyadic(Rng& rng) noexcept {
    const std::int64_t den = std::int64_t{1} << rng.next_below(32u);  // [1, 2^31]
    const std::int64_t num = static_cast<std::int64_t>(rng.next_below(static_cast<uint32_t>(den) + 1u));
    return Rational{num, den};
}

// Independent oracle: r equals true_num/true_den (UN-reduced) iff the
// __int128 cross-product is balanced.  All inputs non-negative so no
// sign games; products ≤ 2^125 fit signed __int128.
[[nodiscard]] bool equals_fraction(Rational r, W true_num, W true_den) noexcept {
    return static_cast<W>(r.num) * true_den == true_num * static_cast<W>(r.den);
}

// True when true_num/true_den is a share of the carrier: at most one, with
// a reduced denominator at most the bound.  The reduction is a plain
// Euclid on the wide type, apart from the code under test.
[[nodiscard]] bool is_in_domain(W true_num, W true_den) noexcept {
    if (true_num > true_den) return false;
    W left = true_num;
    W right = true_den;
    while (right != 0) {
        const W rest = left % right;
        left = right;
        right = rest;
    }
    const W divisor = true_num == 0 ? true_den : left;
    return true_den / divisor <= kMax;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    const Config cfg = parse_args(::fixy::os::args(argc, argv), 2000000);

    return run(
        "fractional_lattice", cfg,
        // ── Generator ──
        [](Rng& rng) noexcept -> PairSpec {
            PairSpec spec{};
            spec.mode = rng.pick<Mode>();
            switch (spec.mode) {
                case Mode::Random:
                    spec.a = gen_random(rng);
                    spec.b = gen_random(rng);
                    break;
                case Mode::Boundary:
                    spec.a = gen_boundary(rng);
                    spec.b = gen_boundary(rng);
                    break;
                case Mode::Small:
                    spec.a = gen_small(rng);
                    spec.b = gen_small(rng);
                    break;
                case Mode::Dyadic:
                    spec.a = gen_dyadic(rng);
                    spec.b = gen_dyadic(rng);
                    break;
                default:
                    ::fixy::unreachable();
            }
            return spec;
        },
        // ── Property ──
        [](const PairSpec& spec) noexcept -> bool {
            const Rational a = spec.a;
            const Rational b = spec.b;
            // Sanity: generators only emit well-formed inputs.
            if (!a.is_well_formed() || !b.is_well_formed()) return false;

            const W ad = static_cast<W>(a.den);
            const W bd = static_cast<W>(b.den);
            const W an = static_cast<W>(a.num);
            const W bn = static_cast<W>(b.num);

            // ── add ──
            const W sum_num = an * bd + bn * ad;
            const W sum_den = ad * bd;
            if (FractionalLattice::is_sum_defined(a, b) != (sum_num <= sum_den)) return false;
            if (is_in_domain(sum_num, sum_den)) {
                const Rational s = FractionalLattice::add(a, b);
                if (!s.is_well_formed()) return false;
                // Oracle: s == (a.num·b.den + b.num·a.den) / (a.den·b.den).
                if (!equals_fraction(s, sum_num, sum_den)) return false;
                // Commutativity.
                if (!(FractionalLattice::add(a, b) == FractionalLattice::add(b, a))) return false;
            }

            // ── mul ──
            if (is_in_domain(an * bn, ad * bd)) {
                const Rational p = FractionalLattice::mul(a, b);
                if (!p.is_well_formed()) return false;
                // Oracle: p == (a.num·b.num) / (a.den·b.den).
                if (!equals_fraction(p, an * bn, ad * bd)) return false;
                if (!(FractionalLattice::mul(a, b) == FractionalLattice::mul(b, a))) return false;
            }

            // ── identities ──
            if (!(FractionalLattice::add(a, FractionalLattice::zero()) == a)) return false;
            if (!(FractionalLattice::mul(a, FractionalLattice::one()) == a)) return false;
            if (!(FractionalLattice::mul(a, FractionalLattice::zero()) == FractionalLattice::zero())) return false;

            return true;
        });
}
