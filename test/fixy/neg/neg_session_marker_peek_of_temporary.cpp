// A borrow of the payload of a temporary marker dangles at the end of the
// full expression.  The rvalue overload of peek() is deleted.  Only the door
// of a hold builds a marker, so the marker comes from a function that packs
// it and returns it by value.

#include <fixy/session/Flow.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

auto packed() noexcept {
    auto [message, rest] = sess::mint_permission_hold(fp::mint_permission_root<Region>()).template pack<Region>(1);
    static_cast<void>(std::move(rest).into_permissions());
    return std::move(message);
}
}  // namespace

int main() {
    int const& dangling = packed().peek();
    return dangling;
}
