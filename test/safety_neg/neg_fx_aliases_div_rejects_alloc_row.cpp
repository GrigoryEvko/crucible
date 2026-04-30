// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-G79 fixture — pins IsDiv rejection on a state-effect row.
// DivRow = Row<Block>.  Alloc is not a member, so Row<Alloc> fails
// the `requires Subrow<R, DivRow>` constraint.  Captures the F*
// distinction "may not terminate" (Div) vs "may modify state" (ST).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/effects/FxAliases.h>

template <typename R>
    requires ::crucible::effects::IsDiv<R>
constexpr bool only_div() noexcept { return true; }

int main() {
    // Alloc atom is in STRow but NOT in DivRow.
    using BadRow = ::crucible::effects::Row<
        ::crucible::effects::Effect::Alloc>;
    (void)only_div<BadRow>();
    return 0;
}
