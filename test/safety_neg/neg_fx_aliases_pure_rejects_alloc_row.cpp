// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-G79 fixture — pins IsPure rejection on an alloc-bearing row.
// PureRow is the empty row; any non-empty row violates the
// `requires Subrow<R, PureRow>` constraint.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/effects/FxAliases.h>

template <typename R>
    requires ::crucible::effects::IsPure<R>
constexpr bool only_pure() noexcept { return true; }

int main() {
    // Row<Alloc> is not a Subrow of Row<>; substitution failure.
    using BadRow = ::crucible::effects::Row<
        ::crucible::effects::Effect::Alloc>;
    (void)only_pure<BadRow>();
    return 0;
}
