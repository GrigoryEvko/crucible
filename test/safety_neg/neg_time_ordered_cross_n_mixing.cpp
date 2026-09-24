// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a TimeOrdered<T, N1> to a method expecting
// TimeOrdered<T, N2> for N1 ≠ N2.
//
// TimeOrdered<T, N, Tag> is a distinct class template per (N, Tag);
// the underlying Graded substrate's element_type also differs per N
// (HappensBeforeLattice<N>::element_type wraps std::array<uint64_t,
// N>).  Cross-N mixing must be a type-mismatch.
//
// Symmetric to neg_happens_before_cross_n_mixing.cpp but at the
// WRAPPER surface — pins that TimeOrdered preserves the per-N
// distinction the lattice already enforces.  Without this, a
// refactor that added an implicit converting constructor on
// TimeOrdered (e.g. for "interoperability") would let a 3-participant
// event flow into a 4-participant lattice-op position, breaking the
// causal-clock alignment.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection at the
// happens_before call site.

#include <crucible/safety/TimeOrdered.h>

using namespace crucible::safety;

int main() {
    using TO3 = TimeOrdered<int, 3>;
    using TO4 = TimeOrdered<int, 4>;

    TO3 evt_3p{};
    TO4 evt_4p{};

    // Should FAIL: TO4::happens_before takes TO4 const&; evt_3p is
    // TO3 — different class template instantiation, no implicit
    // conversion.
    return static_cast<int>(evt_4p.happens_before(evt_3p));
}
