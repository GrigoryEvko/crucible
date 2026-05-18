// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-017 audit fixture: pins the load-bearing name-coverage
// assertion in algebra/lattices/AllLattices.h.  The umbrella ships a
// `static_assert((HasLatticeName<Ls> && ...))` over every canonical
// lattice instantiation; this fixture WITNESSES that the assertion
// fires when ANY listed lattice lacks `name()`.
//
// Test-only lattice `UnnamedTestLattice` is a structurally-valid
// bounded chain lattice (satisfies the Lattice concept) but
// deliberately ships NO `name()` member.  Adding it to the
// `every_lattice_has_name<...>` pack via a parallel local instantiation
// MUST fire the fold-assertion with the [Lattice_Missing_Name]
// diagnostic.
//
// Without this fixture: the A3-017 coverage assertion could regress
// to a vacuous fold (e.g., `(HasLatticeName<Ls> || ...)`) and silently
// admit lattices without `name()`.  WITH this fixture: a regression
// flips this from "fails to compile" to "compiles" — the negative test
// inverts and the discipline is preserved.
//
// Expected diagnostic: "static assertion failed" / "static_assert" /
// "Lattice_Missing_Name" (the local fold-static_assert mirrors the
// umbrella's, fires on the missing-name lattice).

#include <crucible/algebra/Lattice.h>

#include <string_view>

namespace alg = crucible::algebra;

// Structurally-valid 2-element bounded chain lattice over `bool`,
// matching alg::detail::lattice_self_test::TrivialBoolLattice's
// algebraic shape EXCEPT that `name()` is omitted.
struct UnnamedTestLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return false; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return (!a) || b;  // false ⊑ true
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return a || b;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return a && b;
    }
    // NO `name()` — this is the test discipline.
};

// Confirm Lattice concept still holds — the lattice is otherwise valid.
static_assert(alg::Lattice<UnnamedTestLattice>);

// `lattice_name<UnnamedTestLattice>()` returns the sentinel.  Confirm
// the sentinel fallback path is real (this is part of A3-017's
// motivation — silent sentinel admission must be caught at fold time).
static_assert(alg::lattice_name<UnnamedTestLattice>() ==
              std::string_view{"<unnamed lattice>"},
    "Sentinel fallback path must remain — A3-017 fires WHEN this is "
    "true AND HasLatticeName<L> is false.");

// Local fold mirroring the umbrella's assertion shape.  Including
// UnnamedTestLattice in the pack MUST fail — `HasLatticeName` is false
// for it, the fold collapses to `false`, the static_assert fires.
template <typename... Ls>
[[nodiscard]] consteval bool every_lattice_has_name_local() noexcept {
    return (alg::HasLatticeName<Ls> && ...);
}

int main() {
    static_assert(every_lattice_has_name_local<UnnamedTestLattice>(),
        "fixy-A3-017: [Lattice_Missing_Name] UnnamedTestLattice lacks "
        "`name()` — the umbrella's name-coverage fold MUST reject this. "
        "If this assertion ever silently passes, the production fold-"
        "static_assert in AllLattices.h has regressed to a vacuous "
        "fold and lattices without name() can be added unnoticed.");
    return 0;
}
