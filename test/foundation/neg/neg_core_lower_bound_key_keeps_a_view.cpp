// The key function of find_lower_bound_by_key captures by reference an Option of
// a View, which the caller reads after the search.  The function can keep a
// borrow of an element there, with no View of the search to name the element.
// The search refuses a key function that can store a borrow outside it
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::FixedArray<int, 3> const values{1, 2, 3};
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto const position = ::foundation::core::find_lower_bound_by_key(
        values.view(), [&kept](int const& value) noexcept { return kept.is_some() ? 0 : value; }, 2);
    return static_cast<int>(position);
}
