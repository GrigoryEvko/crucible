// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-G79 fixture — pins IsST rejection on a context-tag-bearing
// row.  STRow = Row<Block, Alloc, IO>; Bg / Init / Test are dispatch
// hints, not state-effect atoms.  A Row<Bg> caller must rise to
// IsAll, never to IsST — captures the "context tag is not an effect"
// design rule from FxAliases.h's doc-block.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/effects/FxAliases.h>

template <typename R>
    requires ::crucible::effects::IsST<R>
constexpr bool only_st() noexcept { return true; }

int main() {
    // Bg is in AllRow but NOT in STRow.
    using BadRow = ::crucible::effects::Row<
        ::crucible::effects::Effect::Bg>;
    (void)only_st<BadRow>();
    return 0;
}
