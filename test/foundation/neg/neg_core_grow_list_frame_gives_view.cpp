// The body of the frame door with_view gives its View out.  The next append
// can move the storage that the View borrows, so the View would dangle.  The
// result of a frame door holds no borrow, so the call binds to the deleted
// overload.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::GrowList<int> list = ::foundation::core::mint_grow_list<int>(64);
    list.append(alloc, 1).expect("the list is below its maximum");
    list.with_view([](::foundation::core::View<int const> view) noexcept { return view; });
    return 0;
}
