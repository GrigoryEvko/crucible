// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-017 audit fixture (companion to neg_lattice_missing_name_-
// in_pack.cpp): pins the OPPOSITE side of the gate.  Where the
// companion proves the umbrella fold REJECTS a lattice without
// `name()`, this fixture proves the SENTINEL itself isn't silently
// promoted to a valid name — `lattice_name<L>()` returning the
// sentinel-string MUST NOT satisfy `HasLatticeName<L>`.
//
// The concept and the sentinel-fallback are TWO DIFFERENT mechanisms:
//
//   - HasLatticeName<L>   detects whether the member function exists.
//   - lattice_name<L>()   always returns either L::name() or the
//                         sentinel — the function is ALWAYS callable.
//
// A future bug could conflate them — e.g., "if lattice_name<L>()
// returns a non-empty string, treat HasLatticeName as true".  This
// fixture proves the concept rejects sentinel-only lattices: if the
// concept ever drifts to "any string return", the umbrella fold goes
// vacuously true on lattices that ship NO `name()` member, exactly
// the silent diagnostic-degradation that A3-017 forecloses.
//
// Expected diagnostic: "static assertion failed" / "static_assert" /
// "fixy-A3-017"

#include <crucible/algebra/Lattice.h>

#include <string_view>

namespace alg = crucible::algebra;

// Same UnnamedTestLattice shape as the companion fixture.
struct UnnamedTestLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return false; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return (!a) || b;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return a || b;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return a && b;
    }
};

int main() {
    // The lattice IS valid algebraically.
    static_assert(alg::Lattice<UnnamedTestLattice>);

    // `lattice_name<L>()` returns the sentinel — confirms the
    // fallback path activates.
    static_assert(alg::lattice_name<UnnamedTestLattice>() ==
                  std::string_view{"<unnamed lattice>"});

    // The load-bearing assertion: HasLatticeName MUST be false even
    // though lattice_name() returned a non-empty string.  Asserting
    // the opposite MUST fail to compile.  If a future refactor
    // conflates the two and HasLatticeName silently fires `true` on
    // sentinel-only lattices, the A3-017 fold-assertion in
    // AllLattices.h becomes vacuously true — silent regression.
    static_assert(alg::HasLatticeName<UnnamedTestLattice>,
        "fixy-A3-017: HasLatticeName MUST be false when the lattice "
        "ships no `name()` member, regardless of what lattice_name() "
        "returns from its sentinel fallback.  If this admits, the "
        "name-coverage fold loses its load-bearing rejection power.");
    return 0;
}
