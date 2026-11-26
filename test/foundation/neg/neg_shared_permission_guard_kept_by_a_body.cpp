// A guard of a share holds the address of its pool, which it does not own.  Its
// mark by_arguments<0>{} states how it moves between threads, and the store
// check reads a share (shares{}) as no owner of storage, so it reads the
// address as a borrow.  The body of a frame door that captures by reference an
// Option of a guard could keep a guard there, and find_if refuses it
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/permissions/SharedPermission.h>

namespace {

struct ShareTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ShareBrand final {};

}  // namespace

int main() {
    ::foundation::core::FixedArray<int, 3> const values{1, 2, 3};
    ::foundation::core::Option<::foundation::permissions::SharedPermissionGuard<ShareTag, ShareBrand>> kept{
        ::foundation::core::none};
    auto const found = ::foundation::core::find_if(
        values.view(), [&kept](int const& value) noexcept { return value == 2 && kept.is_none(); });
    return static_cast<int>(found.is_some());
}
