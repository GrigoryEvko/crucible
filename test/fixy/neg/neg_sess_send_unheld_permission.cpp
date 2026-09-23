// A message that carries a Permission moves its region to the peer, so
// the sender must hold that region in its permission set.  This handle
// holds no permission, so the send is refused.  fixy/session/Payload.h
// computes what the payload takes, and the handle reads it.

#include <fixy/session/Handle.h>

#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {};
using Proto = s::Send<perm::Permission<Region>, s::End>;
}  // namespace

int main() {
    auto head = s::mint_session_handle<Proto>(Wire{});
    auto at_end = std::move(head).send(perm::mint_permission_root<Region>(),
                                       [](Wire&, perm::Permission<Region>&& token) noexcept {
                                           perm::permission_drop(std::move(token));
                                       });
    (void)std::move(at_end).close();
    return 0;
}
