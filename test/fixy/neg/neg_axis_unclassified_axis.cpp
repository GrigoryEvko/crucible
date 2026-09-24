// The axis table classifies every axis in one of two ways: a name on
// defaulted_axes, or a specialization of axis_traits.  A new enumerator
// that nobody classified would reach the defined primary and pick up a
// Lattice claim discharged at the type level that no author gave it.
//
// A value one past the enum stands in for that enumerator.  It reaches
// the primary and it is not on the roster, which is the exact shape of
// the failure, so asserting that the table accepts it must not compile.

#include <fixy/Axis.h>

int main() {
    constexpr auto unclassified = static_cast<fixy::Axis>(fixy::axis_count);
    static_assert(fixy::AxisIsClassified<unclassified>,
                  "an axis off the roster and off the specialization list is classified");
    return 0;
}
