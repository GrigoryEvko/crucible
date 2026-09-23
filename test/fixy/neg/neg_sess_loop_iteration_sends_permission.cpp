// One iteration of this loop sends the region that the handle held at the
// loop entry, and receives nothing back.  The next iteration would start
// with a different permission set.  The walk at the first handle refuses
// the protocol before the step to the Continue is compiled.

#include <fixy/session/Handle.h>

#include <foundation/effects/Row.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>

#include <source_location>
#include <utility>

namespace {
namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {};
using Token = perm::Permission<Region>;
using Forever = s::Loop<s::Send<Token, s::Continue>>;
}  // namespace

int main() {
    auto head = s::detail::open_session_<Forever, Wire, s::check::Enforced, perm::PermSet<Region>>(
        Wire{}, std::source_location::current());
    auto next = std::move(head).send(perm::mint_permission_root<Region>(), [](Wire&, Token&& token) noexcept {
        perm::permission_drop(std::move(token));
    });
    std::move(next).detach(s::detach_reason::InfiniteLoopProtocol{});
    return 0;
}
