// ═══════════════════════════════════════════════════════════════════
// prop_fractional_lattice.cpp — arithmetic-correctness fuzzer for
// FractionalLattice::add / mul (algebra/lattices/FractionalLattice.h).
//
// FractionalLattice is the ℚ∩[0,1] semiring backing SharedPermission
// <Tag> (CSL fractional permissions, O'Hearn 2007 / Boyland 2003).  Its
// add/mul are the ONLY runtime-valued lattice ops with genuinely non-
// trivial arithmetic (cross-product + gcd reduction) — every sister
// lattice is max/min (chain) or bitwise (boolean).  It shipped with
// only 5 hand-picked static_assert witnesses, and that sparse coverage
// hid a real bug: add cast its __int128 numerator sum to int64 BEFORE
// gcd-reducing, so a well-formed pair at the inclusive MAX_SAFE_MAGNITUDE
// bound (2^31/2^31 + 2^31/2^31, numerator sum 2^63) wrapped to INT64_MIN
// and hit std::gcd UB (fixed by simplify_wide in 17a26f7d).  This fuzzer
// is the regression net that sparse coverage lacked.
//
// The oracle is an INDEPENDENT __int128 cross-multiplication: rather
// than recompute the reduced fraction (which would duplicate add/mul),
// it verifies the returned r == true_num/true_den by the equality
//     r.num · true_den == true_num · r.den      (all in __int128)
// where (true_num, true_den) is the UN-reduced sum/product.  That is a
// different computation from the code under test (which reduces via
// __int128 gcd then narrows), so a divergence is a genuine bug.  The
// products stay within signed __int128 (≤ 2^125 ≪ 2^127) for all
// well-formed inputs.
//
// Per (a, b) it asserts:
//   * add/mul never UB or wrap — result is non-negative with a positive
//     denominator (pre-fix the wrapped numerator was NEGATIVE; this is
//     the cheap direct witness of the simplify_wide fix);
//   * add/mul equal the true rational via the __int128 oracle;
//   * commutativity: add(a,b)==add(b,a), mul(a,b)==mul(b,a);
//   * identities: add(a, 0/1)==a, mul(a, 1/1)==a, mul(a, 0/1)==0/1.
//
// Three generator modes — Random (full [0,2^31] range), Boundary
// (num/den drawn from {0,1,2^30,2^31-1,2^31} to hit the overflow corner
// densely), Small (small denominators) — so the boundary that broke is
// exercised heavily, not just sampled.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/algebra/lattices/FractionalLattice.h>

#include <array>
#include <cstdint>

namespace {

using crucible::algebra::lattices::Rational;
using crucible::algebra::lattices::FractionalLattice;
using crucible::fuzz::prop::Rng;
using W = Rational::wide_signed;  // __int128

inline constexpr std::int64_t kMax = Rational::MAX_SAFE_MAGNITUDE;  // 2^31

enum class Mode : uint8_t { Random = 0, Boundary = 1, Small = 2 };

struct PairSpec {
    Rational a{};
    Rational b{};
    Mode     mode = Mode::Random;
    uint8_t  pad[7]{};
};

// One well-formed component: num in [0, 2^31], den in [1, 2^31].
[[nodiscard]] Rational gen_random(Rng& rng) noexcept {
    const std::int64_t num = static_cast<std::int64_t>(
        rng.next_below(static_cast<uint32_t>(kMax) + 1u));        // [0, 2^31]
    const std::int64_t den = 1 + static_cast<std::int64_t>(
        rng.next_below(static_cast<uint32_t>(kMax)));             // [1, 2^31]
    return Rational{num, den};
}
[[nodiscard]] std::int64_t boundary_value(Rng& rng) noexcept {
    constexpr std::array<std::int64_t, 5> corners{
        0, 1, std::int64_t{1} << 30, kMax - 1, kMax};
    return corners[rng.next_below(5u)];
}
[[nodiscard]] Rational gen_boundary(Rng& rng) noexcept {
    const std::int64_t num = boundary_value(rng);
    std::int64_t den = boundary_value(rng);
    if (den == 0) den = 1;  // denominator must be positive + well-formed
    return Rational{num, den};
}
[[nodiscard]] Rational gen_small(Rng& rng) noexcept {
    return Rational{static_cast<std::int64_t>(rng.next_below(64u)),
                    1 + static_cast<std::int64_t>(rng.next_below(64u))};
}

// Independent oracle: r equals true_num/true_den (UN-reduced) iff the
// __int128 cross-product is balanced.  All inputs non-negative so no
// sign games; products ≤ 2^125 fit signed __int128.
[[nodiscard]] bool equals_fraction(Rational r, W true_num, W true_den) noexcept {
    return static_cast<W>(r.num) * true_den == true_num * static_cast<W>(r.den);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("fractional_lattice", cfg,
        // ── Generator ──
        [](Rng& rng) noexcept -> PairSpec {
            PairSpec spec{};
            spec.mode = static_cast<Mode>(rng.next_below(3));
            switch (spec.mode) {
                case Mode::Random:   spec.a = gen_random(rng);  spec.b = gen_random(rng);  break;
                case Mode::Boundary: spec.a = gen_boundary(rng); spec.b = gen_boundary(rng); break;
                case Mode::Small:    spec.a = gen_small(rng);    spec.b = gen_small(rng);    break;
                default: std::unreachable();
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
            const Rational s = FractionalLattice::add(a, b);
            // No-wrap witness: pre-fix the overflow produced a NEGATIVE
            // numerator; non-negative inputs must give a non-negative
            // result with a positive denominator.
            if (s.num < 0 || s.den <= 0) return false;
            // Oracle: s == (a.num·b.den + b.num·a.den) / (a.den·b.den).
            if (!equals_fraction(s, an * bd + bn * ad, ad * bd)) return false;
            // Commutativity.
            if (!(FractionalLattice::add(a, b) == FractionalLattice::add(b, a))) return false;

            // ── mul ──
            const Rational p = FractionalLattice::mul(a, b);
            if (p.num < 0 || p.den <= 0) return false;
            // Oracle: p == (a.num·b.num) / (a.den·b.den).
            if (!equals_fraction(p, an * bn, ad * bd)) return false;
            if (!(FractionalLattice::mul(a, b) == FractionalLattice::mul(b, a))) return false;

            // ── identities ──
            if (!(FractionalLattice::add(a, FractionalLattice::zero()) == a)) return false;
            if (!(FractionalLattice::mul(a, FractionalLattice::one()) == a)) return false;
            if (!(FractionalLattice::mul(a, FractionalLattice::zero())
                  == FractionalLattice::zero())) return false;

            return true;
        });
}
