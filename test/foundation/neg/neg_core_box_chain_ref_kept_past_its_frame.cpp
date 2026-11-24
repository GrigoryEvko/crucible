// A chain of Boxes: each node holds an Option of the link to the next node.
// The arm of the frame door with_mut of that Option keeps a Ref of the link
// in a variable that it captures by reference.  The Ref outlives the frame,
// so a move of the next Box into the head through the Ref (the child move of
// test_core_owner_probe.cpp) ends the node that holds the link.  The store
// check of the door (StoresNoBorrowOutside of foundation/core/CaptureStore.h)
// refuses the arm.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

namespace {

struct ChainNode;

struct LinkBrand final {};

struct Link {
    using brand_type = LinkBrand;
    ::foundation::core::Box<ChainNode> box;
};

struct ChainNode {
    ::foundation::core::Option<Link> next{};
    int label = 0;
};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::Box<ChainNode> head = ::foundation::core::mint_box<ChainNode>(alloc);
    head->next = ::foundation::core::some(Link{::foundation::core::mint_box<ChainNode>(alloc)});
    ::foundation::core::Option<::foundation::core::Ref<Link, LinkBrand>> kept{::foundation::core::none};
    head->next.with_mut([&kept](auto link) noexcept { kept = ::foundation::core::some(::foundation::core::to(*link)); },
                        [] noexcept {});
    return kept.is_some() ? 0 : 1;
}
