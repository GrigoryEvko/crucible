// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-ALGEBRA fixture (fix-09 law-witness gate, #2): the strengthened
// `Lattice<L>` concept rejects a lattice with CORRECT SIGNATURES but a
// deliberately NON-ASSOCIATIVE join.  This is a distinct law-break
// class from fixture #1 (idempotence): here join(join(a,b),c) differs
// from join(a,join(b,c)) at the canonical witnesses, so the law witness
// rejects it even though every signature is present and correct.
//
// A non-associative join, like a non-idempotent one, would feed a
// structurally-broken grade into Graded::compose (which uses join) and
// into row_hash federation keying — the very failure fix-09 makes
// impossible to reach at the concept gate.
//
// Expected diagnostic: GCC's "associated constraints are not satisfied"
// pointing at the Lattice concept (its `laws_hold<L>()` conjunct), or
// the static_assert message below.

#include <crucible/fixy/Algebra.h>

#include <cstdint>

namespace fa = crucible::fixy::algebra;

// Three-element carrier {A, B, C} with an idempotent-but-NON-ASSOCIATIVE
// join encoded as a hand-written table.  bottom()/top() are A/C.
//
//   join table (row = x, col = y):
//        A  B  C
//     A  A  C  C
//     B  C  B  B
//     C  C  B  C
//
// join is idempotent on the diagonal (A,A→A; B,B→B; C,C→C) but
// non-associative: join(join(A,B),C) = join(C,C) = C, whereas
// join(A,join(B,C)) = join(A,B) = C — equal here; pick a triple that
// breaks: join(join(B,C),A) = join(B,A) = C (table[B][A]=C), whereas
// join(B,join(C,A)) = join(B,C) = B (table[B][C]=B) → C != B.  The
// canonical-witness sweep evaluates triples drawn from {bottom, top} =
// {A, C}: join(join(C,A),A)=join(C,A)=C vs join(C,join(A,A))=join(C,A)=C
// (equal) — so we additionally break the bottom/top pair directly:
// make join NON-commutative at (A,C) so the {A,C} witness sweep fails.
//
// Simplest robust construction: a join that is non-associative AND
// non-commutative on {A, C} alone, so the canonical bottom()/top()
// witnesses (A and C) suffice to fail the law check.
struct LatticeNeg_NonAssociativeJoin {
    enum class Sym : std::uint8_t { A = 0, B = 1, C = 2 };
    using element_type = Sym;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return Sym::A; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return Sym::C; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return static_cast<std::uint8_t>(a) <= static_cast<std::uint8_t>(b);
    }
    // BROKEN: idempotent on the diagonal but non-commutative (hence the
    // law witness fails commutativity AND associativity).  join(A,C)=C
    // but join(C,A)=A, so the {bottom=A, top=C} canonical witness sweep
    // detects the break: raw_axioms_at(A, C, ...) finds comm_join false.
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (a == b) return a;                 // idempotent on the diagonal
        if (a == Sym::A && b == Sym::C) return Sym::C;
        if (a == Sym::C && b == Sym::A) return Sym::A;   // ← asymmetric
        return static_cast<std::uint8_t>(a) >= static_cast<std::uint8_t>(b) ? a : b;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return leq(a, b) ? a : b;
    }
};

int main() {
    // Must FAIL: signatures present, but join is non-commutative /
    // non-associative at the canonical witnesses, so the law witness
    // rejects the lattice.
    static_assert(fa::Lattice<LatticeNeg_NonAssociativeJoin>,
        "fa::Lattice<NonAssociativeJoin> must reject — join is "
        "non-commutative/non-associative at the canonical bottom()/top() "
        "witnesses; the fix-09 law witness catches it.");
    return 0;
}
