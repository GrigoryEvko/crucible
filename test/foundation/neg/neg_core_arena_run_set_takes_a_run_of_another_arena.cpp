// A set of runs of an arena takes a run of an arena of another family.  The
// brand of the run is not the brand of the set, so no overload of set<I> takes
// it.

#include <foundation/core/BumpArena.h>
#include <foundation/core/RunSet.h>

#include <cstdint>

struct[[= ::foundation::brand::arena_family_brand{}]] SetArena final {};
struct[[= ::foundation::brand::arena_family_brand{}]] OtherArena final {};

int main() {
    ::foundation::core::ArenaRunSet<std::uint16_t, SetArena, ::foundation::core::RunOf<int const, 0>> set{};
    ::foundation::core::ArenaRun<int const, OtherArena> other{};
    set.set<0>(other);
    return 0;
}
