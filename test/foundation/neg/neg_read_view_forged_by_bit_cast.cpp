// A read proof built from a byte.  The constructors of ReadView are
// user-provided, so the view is not trivially copyable, and std::bit_cast
// has no candidate.  A read proof comes from a Permission or a live
// share, and from nothing else.

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
    auto forged = std::bit_cast<fp::ReadView<Region>>(char{0});
    (void)forged;
    return 0;
}
