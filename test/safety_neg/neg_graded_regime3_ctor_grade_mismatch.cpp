// HS14 negative-compile fixture (fix-13, regime-3):
//
// The Graded<M, L, T> derived-grade specialization (grade computed via
// L::grade_of(value), element_type != T) has a two-arg ctor that takes
// a witness grade, asserts it equals what L derives, then DISCARDS it.
// That guard MUST fire at consteval — where a vanilla foldable
// `this`-free `pre()` clause would have been silently skipped by the
// documented GCC 16.1.1 bypass family (or degraded to nothing under
// contract_evaluation_semantic=ignore).
//
// This fixture builds a minimal derived-grade lattice whose grade_of
// returns a container's size (a true partial order via <=).  A
// container of size 3 derives grade 3; the fixture constructs the
// two-arg ctor with a witness grade of 99 — NOT equivalent to 3
// (3 <= 99 holds but 99 <= 3 fails) — from a consteval/static_assert
// context, so the in-body contract_assert makes the call non-constant
// → compilation MUST fail.
//
// Expected: NON-zero exit with a contract/trap diagnostic.  A passing
// (zero-exit) compile is the bug this fixture guards against.

#include <cstddef>
#include <string_view>

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>

namespace {

using ::crucible::algebra::Graded;
using ::crucible::algebra::ModalityKind;

// A trivial container whose "grade" is its size.
struct SizedThing {
    std::size_t n{0};
    constexpr SizedThing() = default;
    constexpr explicit SizedThing(std::size_t k) noexcept : n{k} {}
    [[nodiscard]] constexpr std::size_t size() const noexcept { return n; }
    constexpr bool operator==(const SizedThing&) const = default;
};

// Derived-grade lattice: element_type (size_t) != T (SizedThing), so
// the regime-3 Graded specialization is selected.  leq is a true
// partial order (<=), so size 3 and grade 99 are NOT equivalent.
struct SizeGradeLattice {
    using element_type = std::size_t;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
    [[nodiscard]] static constexpr element_type grade_of(SizedThing const& c) noexcept { return c.size(); }
    [[nodiscard]] static constexpr std::string_view name() noexcept { return "SizeGradeLattice"; }
};

using GDerived =
    Graded<ModalityKind::Absolute, SizeGradeLattice, SizedThing>;

consteval std::size_t forge_mismatched_derived_grade() {
    // value derives grade 3; witness grade 99 is NOT equivalent.
    GDerived forged{SizedThing{3}, 99};
    return forged.peek().size();
}

static_assert(forge_mismatched_derived_grade() == 3,
              "fixture must fail to compile: regime-3 two-arg ctor "
              "grade-mismatch guard did not fire at consteval");

}  // namespace
