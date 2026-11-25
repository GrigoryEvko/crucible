// A set of runs of an arena takes a View.  A View names no arena, so the set
// would keep the elements of an owner that can end before the arena.  The
// deleted overload of set<I> gives the reason.

#include <foundation/core/Array.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/RunSet.h>

#include <cstdint>

struct[[= ::foundation::brand::arena_family_brand{}]] SetArena final {};

int main() {
    ::foundation::core::ArenaRunSet<std::uint16_t, SetArena, ::foundation::core::RunOf<int const, 0>> set{};
    ::foundation::core::FixedArray<int, 2> const numbers{1, 2};
    set.set_count<0>(2);
    set.set<0>(::foundation::core::View<int const>{numbers.view()});
    return 0;
}
