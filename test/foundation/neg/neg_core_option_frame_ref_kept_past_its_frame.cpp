// The arm of the frame door with_mut of an Option of a Box keeps a Ref of
// the object of the Box in a variable that it captures by reference.  The
// Ref outlives the frame, so a new payload of the Option, or a move of a
// child owner that the object holds (the child move of
// test_core_owner_probe.cpp), can end the object that the Ref names.  The
// store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the arm.

#include <foundation/core/Choice.h>
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
    ::foundation::core::Option<::foundation::core::Box<Node>> held =
        ::foundation::core::some(::foundation::core::mint_box<Node>(::foundation::effects::testing::test().alloc));
    ::foundation::core::Option<::foundation::core::Ref<Node, NodeBrand>> kept{::foundation::core::none};
    held.with_mut([&kept](auto box) noexcept { kept = ::foundation::core::some(::foundation::core::to(**box)); },
                  [] noexcept {});
    return kept.is_some() ? 0 : 1;
}
