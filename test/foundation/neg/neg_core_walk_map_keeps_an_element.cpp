// The function of map of a walk keeps a Ref of its element in a variable that
// it captures by reference.  An element of a map, a filter or a flat map can
// be a value of the walk, which ends at the next element, so the kept Ref
// would name a value that no longer exists.  The store check of the adapter
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the
// function.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Walk.h>

int main() {
    ::foundation::core::FixedArray<int, 3> const values{1, 2, 3};
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    int const total =
        values.view()
            .walk()
            .map([&kept](int const& value) noexcept {
                kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(value)));
                return value;
            })
            .fold(0, [](int sum, int value) noexcept { return sum + value; });
    return total;
}
