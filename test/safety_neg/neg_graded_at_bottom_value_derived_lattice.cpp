// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The same refusal as the element-lattice case, for the other reason
// the grade cannot be set independently of the value: here the lattice
// derives the grade through grade_of, so holding an arbitrary value at
// bottom would need an inverse of grade_of, and none exists in
// general.
//
// The checked form this replaces is Graded{value, L::bottom()}, whose
// witness check asserts that the value already derives bottom.

#include <crucible/algebra/Graded.h>

#include <cstddef>
#include <string_view>

struct SizedContainer {
    std::size_t n{0};
    constexpr SizedContainer() = default;
    constexpr explicit SizedContainer(std::size_t k) noexcept : n{k} {}
    [[nodiscard]] constexpr std::size_t size() const noexcept { return n; }
    constexpr bool operator==(const SizedContainer&) const = default;
};

struct DerivedGradeLattice {
    using element_type = std::size_t;
    static constexpr element_type bottom() noexcept { return 0; }
    static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
    static constexpr element_type grade_of(SizedContainer const& c) noexcept { return c.size(); }
    static constexpr std::string_view name() noexcept { return "DerivedGradeLattice"; }
};

using GradedOverDerived =
    crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, DerivedGradeLattice, SizedContainer>;

int main() {
    auto held = GradedOverDerived::at_bottom(SizedContainer{3});
    (void)held;
    return 0;
}
