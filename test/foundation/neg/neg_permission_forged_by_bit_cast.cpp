// An exclusive token built from a byte.  A token with a trivial copy is
// trivially copyable, and std::bit_cast builds any such type from bytes
// with no constructor call, so the private mint key did not stop it.
// The move constructor of Permission is user-provided, so the token is
// not trivially copyable, and std::bit_cast has no candidate.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <bit>
#include <memory>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto forged = std::bit_cast<fp::Permission<Region>>(char{0});
    (void)forged;
    return 0;
}
