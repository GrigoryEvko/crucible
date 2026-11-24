// The body of the frame door with_view of a SmallList keeps the View of the
// elements in a variable that it captures by reference.  The View outlives
// the frame, so a take_last of the list leaves it with a slot past the count
// (test_core_small_list_poison.cpp).  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the body.

#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

int main() {
    auto list = ::foundation::core::mint_small_list<std::uint64_t, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    list.append(::foundation::effects::testing::test().alloc, std::uint64_t{1}).expect("the list is below its maximum");
    ::foundation::core::View<std::uint64_t const> kept{};
    list.with_view([&kept](::foundation::core::View<std::uint64_t const> elements) noexcept { kept = elements; });
    return static_cast<int>(kept.size());
}
