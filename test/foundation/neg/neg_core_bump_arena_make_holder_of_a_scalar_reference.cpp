// A body of a frame door captures an arena by reference and makes an object
// that holds a reference to a scalar of the body.  The position of the object
// is no borrow, so the body could keep it, and the caller would read the scalar
// after the body ends.  make refuses a type with a reference member, also when
// the target is a scalar (objects of an arena of foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Holder {
    int const& kept;
};

void keep_a_scalar_of_a_frame(::foundation::core::BumpArena<Family>& arena,
                              ::foundation::core::Option<::foundation::core::ArenaPosition<Holder, Family>>& position) {
    auto list = ::foundation::core::mint_grow_list<int>(64);
    list.with_view([&arena, &position](::foundation::core::View<int const> run) noexcept {
        int const largest = run.size() > 1 ? 1 : 0;
        auto const made = arena.make<Holder>(::foundation::effects::testing::test().alloc, Holder{largest});
        position = arena.position_of(made);
    });
}
}  // namespace

int main() {
    static_cast<void>(&keep_a_scalar_of_a_frame);
    return 0;
}
