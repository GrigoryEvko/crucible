// make builds in a BumpArena a class that holds a linear end as a member.
// The class is linear by its member, and the arena ends no object, so the end
// would end with the arena, with no consume.  make refuses a type that holds
// a linear value (CanMakeInArena).

#include <foundation/Linear.h>
#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};

struct Holder {
    End end;
    int count = 0;
};
}  // namespace

int main() {
    ::foundation::core::BumpArena<Family> arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    auto const holder =
        arena.make<Holder>(::foundation::effects::testing::test().alloc, Holder{.end = End{7}, .count = 1});
    return holder->count;
}
