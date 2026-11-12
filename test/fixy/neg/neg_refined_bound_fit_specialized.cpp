// BoundFits holds its whole rule, so no unit can put a verdict in front
// of it.  A float holds no integer above 2^24 exactly, so a comparison
// with the bound 16777217 would round the bound down.  This file tries to
// admit that bound on a float through a function template that gives the
// verdict.  No such template exists, so the specialization does not
// compile.

#include <fixy/Refined.h>

template <>
consteval bool fixy::refined::bound_fits<float, 16777217>() noexcept {
    return true;
}

int main() { return 0; }
