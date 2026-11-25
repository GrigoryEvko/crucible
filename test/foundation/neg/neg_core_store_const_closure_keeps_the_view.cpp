// The body of the frame door with_view of a SmallList captures by const
// reference a closure that captures a variable of the caller by a reference
// that is not const.  C++ const does not go through the reference member of
// the closure, so the body calls the closure, and the closure keeps the View
// of the frame in the variable.  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) reads the
// reference member of the const closure as writable, so it refuses the body.

#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/SmallList.h>

#include <cstddef>

int main() {
    auto list = ::foundation::core::mint_small_list<int, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto const keeper = [&kept](::foundation::core::View<int const> elements) noexcept {
        kept = ::foundation::core::some(elements);
        return elements.size();
    };
    auto const& reader = keeper;
    auto const count =
        list.with_view([&reader](::foundation::core::View<int const> elements) noexcept { return reader(elements); });
    return count == 0 && kept.is_some() ? 0 : 1;
}
