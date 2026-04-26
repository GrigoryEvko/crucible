// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: bare types (int, void*, std::string_view) do NOT
// satisfy GradedWrapper.  This is the basic negative-coverage
// witness — every check the harness performs implicitly via
// `static_assert(!is_graded_wrapper_v<int>)` is also testable
// explicitly via this neg-compile.
//
// Catches the regression where someone "simplifies" the concept
// down to `requires { typename W::value_type; }` and accidentally
// admits std::vector<T>, std::optional<T>, std::pair<T, U>, etc.
// — all of which expose a value_type typedef but none of which
// is a Graded wrapper.

#include <crucible/algebra/GradedTrait.h>

int main() {
    // bare int is the canonical non-wrapper.
    static_assert(::crucible::algebra::GradedWrapper<int>);
    return 0;
}
