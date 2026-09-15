// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// at_bottom(T) means "hold this value, graded at bottom".  Honouring
// that needs a grade the caller can set without touching the value,
// which the primary Graded template has and this specialization does
// not: here L::element_type is T, so the value IS the grade and the
// request could only be met by discarding the argument.
//
// The overload used to exist on all three specializations with two
// different meanings.  The primary forced the grade and kept the
// value, while this one asserted the value was already at bottom and
// aborted when it was not.  One call, two behaviours, chosen by a
// storage regime the public API hides.  The value below is the one
// the primary accepted in silence.
//
// The checked form this replaces is Graded{value, L::bottom()}, whose
// witness check is exactly that assertion.

#include <crucible/algebra/Graded.h>

#include <string_view>

struct ValueIsGradeLattice {
    using element_type = unsigned;
    static constexpr element_type bottom() noexcept { return 0; }
    static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
    static constexpr std::string_view name() noexcept { return "ValueIsGradeLattice"; }
};

using GradedOverElement =
    crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, ValueIsGradeLattice, unsigned>;

int main() {
    auto held = GradedOverElement::at_bottom(99u);
    (void)held;
    return 0;
}
