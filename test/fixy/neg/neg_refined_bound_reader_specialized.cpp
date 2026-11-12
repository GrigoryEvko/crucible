// The relation compares two bounds through the concept BoundAtMost, which
// no unit can specialize.  This file tries to forge in_range<0, 4096> ⇒
// bounded_above<10> through a variable template that reads a bound as a
// magnitude.  No such template exists, so the specialization does not
// compile.

#include <fixy/Predicates.h>

template <>
inline constexpr int fixy::refined::exact_bound_of<4096> = 1;

int main() { return 0; }
