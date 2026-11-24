// The body of the frame door with_view gives its View out.  The next append
// can move the elements that the View borrows, so the View would dangle.  The
// result of a frame door holds no borrow, so the call binds to the deleted
// overload.

#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_small_list<int, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    list.append(alloc, 1).expect("the list is below its maximum");
    list.with_view([](::foundation::core::View<int const> view) noexcept { return view; });
    return 0;
}
