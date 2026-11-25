// The visitor of a listing of a directory captures by reference an Option of a
// TextView, which the caller reads after the listing.  The visitor can keep the
// name of an entry there, but the name borrows the buffer of the door and lives
// only in its call.  for_each_entry refuses a visitor that can store a borrow
// outside the listing (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/os/Directory.h>

int main() {
    ::foundation::core::Option<::foundation::core::TextView> kept{::foundation::core::none};
    auto visit = [&kept](::foundation::core::CText name) noexcept {
        static_cast<void>(name);
        static_cast<void>(kept.is_some());
        return ::foundation::os::Visit::proceed;
    };
    auto const listed = ::foundation::os::for_each_entry(::foundation::core::CText{"/proc/self"}, visit);
    return static_cast<int>(listed.is_ok());
}
