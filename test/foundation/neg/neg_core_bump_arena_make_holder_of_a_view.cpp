// A body of a frame door captures an arena by reference and makes an object
// that holds the View of the frame.  The position of the object is no borrow,
// so the body could keep it, and the caller would read the View after the list
// ends.  make refuses a type that holds a View (objects of an arena of
// foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Holder {
    ::foundation::core::View<int const> kept{};
};

void keep_the_view_of_a_frame(::foundation::core::BumpArena<Family>& arena,
                              ::foundation::core::Option<::foundation::core::ArenaPosition<Holder, Family>>& position) {
    auto list = ::foundation::core::mint_grow_list<int>(64);
    list.with_view([&arena, &position](::foundation::core::View<int const> run) noexcept {
        auto const made = arena.make<Holder>(::foundation::effects::testing::test().alloc, Holder{run});
        position = arena.position_of(made);
    });
}
}  // namespace

int main() {
    static_cast<void>(&keep_the_view_of_a_frame);
    return 0;
}
