// A pressure sample names a resource from the catalog.  A number cast to
// the enumeration that names no resource does not satisfy the mint's
// clause, so no sample can carry it.

#include <crucible/cntp/Backpressure.h>

int main() {
    namespace cntp = crucible::cntp;
    constexpr auto bogus = static_cast<::foundation::effects::ResourceKind>(static_cast<unsigned char>(0xff));
    auto pressure = cntp::mint_resource_pressure<bogus>(1);
    return pressure.has_value() ? 0 : 1;
}
