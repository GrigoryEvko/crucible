// The less function of a search keeps a Ref of an element in a variable that
// it captures by reference.  An element that a sort or a search compares can
// be a copy in the scratch or on the stack of the door, which ends with the
// door, so the kept Ref would name an object that no longer exists.  The
// store check of the order (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the function.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Sort.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 3> const values{1, 2, 3};
    ::foundation::core::Option<::foundation::core::Ref<int const>> kept{::foundation::core::none};
    std::size_t const position =
        ::foundation::core::find_lower_bound(values.view(), 2, [&kept](int const& left, int const& right) noexcept {
            kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(left)));
            return left < right;
        });
    return static_cast<int>(position);
}
