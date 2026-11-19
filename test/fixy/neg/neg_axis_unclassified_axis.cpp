// The axis table gives each axis a row.  A value with no row would get a
// pole that no author gave it.
//
// A value one past the enum stands in for such a value.  No case of
// axis_record names it, so its row is unclassified, and an assertion that
// the table classifies it must not compile.

#include <fixy/Axis.h>

int main() {
    constexpr auto unclassified = static_cast<fixy::Axis>(fixy::axis_count);
    static_assert(fixy::AxisIsClassified<unclassified>, "an axis with no row in the axis table is classified");
    return 0;
}
