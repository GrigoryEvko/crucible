// Each gate of fn reads the claim and the strict pole of each axis.  The
// table was once the class template axis_traits, with a defined primary
// for the axes of the default row.  This file tries to give Axis::Size the
// claim of a right and a pole that grants it: it specializes that class
// template for the axis.  The table is a function, and no class template
// gives a row, so the specialization does not compile.

#include <fixy/Axis.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::axis_traits<fixy::Axis::Size> {
    static constexpr fixy::Shape shape = fixy::Shape::Lattice;
    static constexpr fixy::Discharge discharge = fixy::Discharge::TypeLevel;
    static constexpr fixy::Wrapper wrapper = fixy::Wrapper::None;
    static constexpr fixy::Claim claim = fixy::Claim::Right;
    using strict = fixy::pole::Unconstrained<fixy::Axis::Effect>;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
