// A read proof built from nothing.  ReadView.h once named a host type
// as a friend, so any program that defined that type got the private
// default constructor, and with it a read proof of a region it did not
// hold.  The friend is gone.  A read proof comes from a Permission or a
// live share, and the definition below reaches a private constructor.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

namespace foundation::permissions::host {
struct BorrowIssuer {
    static ReadView<Region> issue() noexcept { return ReadView<Region>{}; }
};
}  // namespace foundation::permissions::host

int main() {
    auto proof = ::foundation::permissions::host::BorrowIssuer::issue();
    (void)proof;
    return 0;
}
