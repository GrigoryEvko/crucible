// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-ALGEBRA fixture (fix-09 law-witness gate, #1): the strengthened
// `Lattice<L>` concept rejects a lattice that has CORRECT SIGNATURES
// (element_type + leq/join/meet + bottom/top) but a deliberately
// NON-IDEMPOTENT join.  Before fix-09, `Lattice<L>` was signature-only:
// such a substrate satisfied the concept and could feed a non-lattice
// into Graded::compose and into row_hash federation keying with no
// diagnostic.  After fix-09, the concept requires that the lattice's
// own canonical witnesses (bottom()/top()) satisfy the lattice axioms,
// so a join with `join(top, top) != top` fails the law witness.
//
// Expected diagnostic: GCC's "associated constraints are not satisfied"
// pointing at the Lattice concept (its `laws_hold<L>()` conjunct), or
// the static_assert message below.

#include <crucible/fixy/Algebra.h>

#include <cstdint>

namespace fa = crucible::fixy::algebra;

// A two-element chain {Lo ⊑ Hi} with a BROKEN join: join always
// returns Hi, so join(Hi, Hi) == Hi but join(Lo, Lo) == Hi != Lo —
// idempotence is violated.  All SIGNATURES are present and correct.
struct LatticeNeg_NonIdempotentJoin {
    enum class Tier : std::uint8_t { Lo = 0, Hi = 1 };
    using element_type = Tier;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return Tier::Lo; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return Tier::Hi; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return static_cast<std::uint8_t>(a) <= static_cast<std::uint8_t>(b);
    }
    // BROKEN: not idempotent — join(Lo, Lo) yields Hi, not Lo.
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept {
        return Tier::Hi;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return leq(a, b) ? a : b;
    }
};

int main() {
    // Must FAIL: signatures present, but the join is non-idempotent at
    // the canonical witnesses, so the law witness rejects the lattice.
    static_assert(fa::Lattice<LatticeNeg_NonIdempotentJoin>,
        "fa::Lattice<NonIdempotentJoin> must reject — join(Lo, Lo) == Hi "
        "violates idempotence; the fix-09 law witness catches it at the "
        "canonical bottom()/top() witnesses.");
    return 0;
}
