// The body of a walk of rows captures by reference an Option of a View, which
// the caller reads after the walk.  The body can keep a View of the elements of
// a row there, and the View would outlive the walk.  mint_strided_rows refuses a
// body that can store a borrow outside the walk (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Strided.h>
#include <foundation/core/StridedRows.h>

namespace {
struct RowsBrand final {};

[[maybe_unused]] bool walk(::foundation::core::StridedViewMut<int, 2, RowsBrand>& out,
                           ::foundation::core::StridedView<int const, 2> in) noexcept {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto const walked = ::foundation::core::mint_strided_rows(
        out,
        [&kept](auto const& row, auto target, auto source) noexcept {
            static_cast<void>(row);
            static_cast<void>(target);
            static_cast<void>(source);
            static_cast<void>(kept.is_some());
        },
        in);
    return walked.is_ok();
}
}  // namespace

int main() { return 0; }
