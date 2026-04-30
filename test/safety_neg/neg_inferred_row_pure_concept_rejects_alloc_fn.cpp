// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D10 fixture — pins IsPureFunction concept rejection.
// A function taking effects::Alloc has a non-empty inferred row,
// so a template constrained on `requires IsPureFunction<FnPtr>`
// must reject it at substitution time.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/effects/Capabilities.h>
#include <crucible/safety/InferredRow.h>

inline void f_with_alloc(crucible::effects::Alloc, int) noexcept {}

template <auto FnPtr>
    requires crucible::safety::extract::IsPureFunction<FnPtr>
inline void only_callable_when_pure() noexcept {}

int main() {
    only_callable_when_pure<&f_with_alloc>();
    return 0;
}
