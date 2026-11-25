// Negative-compile test. This file must fail to compile.
//
// A Link takes only a Ref of its own brand (foundation/core/Link.h).  A node
// of an arena of a second family has a different brand, so it does not go
// into a Link of the first family, and a walk of the first family reaches no
// node that the end of the second arena ends.  The second tag states the first
// one, so a node of the second arena can hold a Link of the first family.

#include <foundation/core/BumpArena.h>
#include <foundation/core/Link.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace {

struct[[= ::foundation::brand::arena_family_brand{}]] First final {};
struct[[= ::foundation::brand::arena_family_brand{}]][[= ::foundation::brand::outlived_by<First>{}]] Second final {};

struct Node {
    int value = 0;
    ::foundation::core::Link<Node, First> next{};
};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::BumpArena<First> first = ::foundation::core::mint_bump_arena<First>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    ::foundation::core::BumpArena<Second> second = ::foundation::core::mint_bump_arena<Second>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    const ::foundation::core::Ref<Node, First> head = first.make<Node>(alloc);
    const ::foundation::core::Ref<Node, Second> foreign = second.make<Node>(alloc);
    head->next = foreign;
    return head->value;
}
