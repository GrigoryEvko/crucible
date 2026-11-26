// A unit fills a link slot of an object of one arena with a family Ref of
// another arena through the store of the slot.  The store is private to the
// frame, so the call does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

struct Node {
    std::uint32_t value = 0;
    fc::FrameLink<Node, TestFamily> next{};
};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::BumpArena<TestFamily> other =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::Ref<Node, TestFamily> const holder = arena.make<Node>(alloc);
    fc::Ref<Node, TestFamily> const outside = other.make<Node>(alloc);
    holder->next.set_(fc::some(outside));
    return 0;
}
