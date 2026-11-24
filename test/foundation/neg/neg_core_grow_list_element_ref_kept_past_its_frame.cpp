// The body of the element door with_element of a GrowList keeps a Ref of
// the element in a variable that it captures by reference.  The Ref outlives
// the frame, so a growth or a move of the list, or a move of a child owner
// that the element holds (the child move of test_core_owner_probe.cpp), can
// end the storage that the Ref names.  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the body.

#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

namespace {

struct NodeBrand final {};

struct Node {
    using brand_type = NodeBrand;
    int label = 0;
};

}  // namespace

int main() {
    auto list = ::foundation::core::mint_grow_list<Node>(8);
    list.append(::foundation::effects::testing::test().alloc, Node{}).expect("the list is below its maximum");
    ::foundation::core::Option<::foundation::core::Ref<Node, NodeBrand>> kept{::foundation::core::none};
    bool const is_kept = list.with_element(
        0,
        [&kept](Node& first) noexcept {
            kept = ::foundation::core::some(::foundation::core::to(first));
            return true;
        },
        [] noexcept { return false; });
    return is_kept ? 0 : 1;
}
