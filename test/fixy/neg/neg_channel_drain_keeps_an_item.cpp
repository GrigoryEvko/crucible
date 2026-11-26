// The drain of a channel gives each item to its drop as a temporary of the
// door.  A drop that takes the item by reference and captures an Option of a
// View by reference could keep a borrow of that temporary there, and the
// caller could read it after the drain.  The drain refuses such a drop
// (LentArmStoresNoBorrow of foundation/core/Choice.h).

#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::wire, int, 8, UserTag, UserBrand>;

[[maybe_unused]] void drain_each(Channel& channel, Channel::DrainClaim& claim) noexcept {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    channel.drain(claim, [&kept](int const& item) noexcept {
        static_cast<void>(item);
        static_cast<void>(kept.is_some());
    });
}

}  // namespace

int main() { return 0; }
