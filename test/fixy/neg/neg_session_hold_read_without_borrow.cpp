// A read of a region the hold does not borrow.  read is gated by the set:
// a hold that owns Region, and does not borrow it, reads it through the
// token and not through a loan.  So a hold whose set has no
// BorrowedIn<Region> has no read of Region.

#include <fixy/session/Payload.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto owner = sess::mint_permission_hold(fp::mint_permission_root<Region>());
    auto back = std::move(owner).template read<Region>([](auto const&) noexcept {});
    (void)back;
    return 0;
}
