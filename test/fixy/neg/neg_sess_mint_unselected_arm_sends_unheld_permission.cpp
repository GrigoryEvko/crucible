// The second arm sends a region that the empty set of the public mint
// does not hold.  The program selects only the first arm, so no step of
// the second arm is ever compiled.  The mint walks every arm, so it
// refuses the protocol anyway.

#include <fixy/session/Handle.h>

#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace {
namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {};
using Proto = s::Select<s::End, s::Send<perm::Permission<Region>, s::End>>;
}  // namespace

int main() {
    auto head = s::mint_session_handle<Proto>(Wire{});
    auto at_end = std::move(head).select<0>([](Wire&, std::size_t) noexcept {});
    (void)std::move(at_end).close();
    return 0;
}
