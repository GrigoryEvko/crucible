// A region holds the run of storage of another owner, an arena or the run of a
// frame, and its state releases nothing, so it owns no storage.  The body of a
// frame door that captures by reference an Option of a region could keep a
// region over storage that ends with the frame there, and the caller could
// read it after the frame.  The store check reads the members of a region, so
// find_if refuses the body (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).

#include <fixy/OwnedRegion.h>

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>

namespace {

struct RunTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct RunBrand final {};

}  // namespace

int main() {
    ::foundation::core::FixedArray<int, 3> const values{1, 2, 3};
    ::foundation::core::Option<::fixy::OwnedRegion<int, RunTag, RunBrand>> kept{::foundation::core::none};
    auto const found = ::foundation::core::find_if(
        values.view(), [&kept](int const& value) noexcept { return value == 2 && kept.is_none(); });
    return static_cast<int>(found.is_some());
}
