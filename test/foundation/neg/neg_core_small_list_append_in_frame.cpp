// The body of the frame door with_view appends to its SmallList.  The append
// can move the elements that the View borrows, from the inline slots to the
// heap, so the View would dangle.  The door carries the annotation
// borrow_match, and the rule borrow-match refuses a call of a member of the
// list that is not const in the body.

#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_small_list<int, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    list.append(alloc, 1).expect("the list is below its maximum");
    list.append(alloc, 2).expect("the list is below its maximum");
    std::size_t const count = list.with_view([&](::foundation::core::View<int const> view) noexcept {
        static_cast<void>(list.append(alloc, 3));
        return view.size();
    });
    return static_cast<int>(count);
}
