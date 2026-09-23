// The handle holds the region.  The second arm lends it and reaches End
// with the loan open.  The program selects only the first arm, which ends
// with the region owned, so no compiled step sees the open loan.  The
// walk at the first handle visits every arm, so it refuses the protocol.

#include <fixy/session/Handle.h>

#include <foundation/effects/Row.h>
#include <foundation/permissions/PermSet.h>

#include <cstddef>
#include <source_location>
#include <utility>

namespace {
namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {};
using Proto = s::Select<s::End, s::Send<s::Borrowed<int, Region>, s::End>>;
}  // namespace

int main() {
    auto head = s::detail::open_session_<Proto, Wire, s::check::Enforced, perm::PermSet<Region>>(
        Wire{}, std::source_location::current());
    auto at_end = std::move(head).select<0>([](Wire&, std::size_t) noexcept {});
    (void)std::move(at_end).close();
    return 0;
}
