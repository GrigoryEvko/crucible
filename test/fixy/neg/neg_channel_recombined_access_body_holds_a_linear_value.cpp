// The recombined access of a channel borrows its body and calls it, and a
// call is no consume.  A body that holds a linear token by value would end
// the token with the body, where no rule reads the end.  The gate of the
// access refuses such a body.

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

namespace {
struct LogTag {};
struct HeldTag {
    using permission_row = ::foundation::effects::Row<>;
};
auto log_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, LogTag>>(); }
}  // namespace

using Channel = c::channel_t<int, 8, decltype(log_root())>;

int main() {
    Channel channel{};
    auto held = perm::mint_permission_root<HeldTag>();
    auto whole = channel.with_recombined_access(
        log_root(), [kept = std::move(held)](Channel::RecombinedRing& /*ring*/) noexcept {});
    static_cast<void>(whole);
    return 0;
}
