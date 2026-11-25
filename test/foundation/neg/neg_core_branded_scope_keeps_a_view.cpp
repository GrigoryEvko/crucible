// The body of a branded scope captures by reference an Option of a View, which
// the caller reads after the scope.  The body can keep the View of the scope
// there, and the View would outlive the brand that proves its indices.  The
// mint refuses a body that can store a borrow outside the scope
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Array.h>
#include <foundation/core/Branded.h>
#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::FixedArray<int, 3> const values{1, 2, 3};
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto const count = ::foundation::core::mint_branded_scope(
        values.view(), [&kept](auto view) noexcept { return view.size() + (kept.is_some() ? 1 : 0); });
    return static_cast<int>(count);
}
