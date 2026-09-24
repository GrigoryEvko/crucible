// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A read loan built from a byte would be a read right that no parked
// token waits for.  std::bit_cast needs a trivially copyable type, and
// ReadLoan has a user-provided move constructor, so it is not one.
//
// Expected diagnostic: no matching function for std::bit_cast, whose
// constraint asks for a trivially copyable target.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <bit>

namespace fp = ::foundation::permissions;

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto forged = std::bit_cast<fp::ReadLoan<Region>>(char{0});
    (void)forged;
    return 0;
}
