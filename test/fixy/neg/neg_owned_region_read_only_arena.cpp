// The arena door of a region asks the arena for a run that it can write.
// An arena that gives a read-only run, a View<T const>, does not meet the
// door, so the region is not built.

#include <fixy/OwnedRegion.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace {

struct Cache {
    using permission_row = ::foundation::effects::Row<>;
};

// An arena whose run is read-only.
class ReadOnlyArena {
public:
    template <typename T>
    [[nodiscard]] ::foundation::core::View<T const> alloc_array(::foundation::effects::Alloc, std::size_t) noexcept {
        return ::foundation::core::View<T const>{};
    }
};

}  // namespace

int main() {
    ReadOnlyArena arena{};
    auto region = ::fixy::mint_owned_region<int>(::foundation::effects::testing::test().alloc, arena, std::size_t{2},
                                                 ::foundation::permissions::mint_permission_root<Cache>());
    auto token = std::move(region).release();
    (void)token;
    return 0;
}
