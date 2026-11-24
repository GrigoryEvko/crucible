// The body of the frame door with_view of a SmallList opens the frame door
// with_mut of the same list.  The move into the frame would end the elements
// that the View of with_view reads, and the ViewMut of with_mut would write
// them beside it.  The door carries the annotation borrow_match, and the rule
// borrow-match refuses the move of the list in the body.

#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_small_list<int, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    list.append(alloc, 1).expect("the list is below its maximum");
    std::size_t const count = list.with_view([&list](::foundation::core::View<int const> view) noexcept {
        auto back = ::foundation::core::with_mut(std::move(list), [](auto run) noexcept { static_cast<void>(run); });
        return view.size() + back.size();
    });
    return static_cast<int>(count);
}
