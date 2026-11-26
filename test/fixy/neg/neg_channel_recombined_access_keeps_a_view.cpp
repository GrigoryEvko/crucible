// The recombined access of a channel lends its body a ring that lives in the
// frame of the access, and the claims of the roles end with that frame.  A body
// that captures an Option of a View by reference could keep a borrow of the
// access there, and the caller could read it after the claims ended.  The
// access refuses such a body (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).

#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <utility>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::wire, int, 8, UserTag, UserBrand>;

[[maybe_unused]] Channel::recombined_token access(Channel& channel, Channel::recombined_token&& token) noexcept {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    return channel.with_recombined_access(std::move(token), [&kept](Channel::RecombinedRing& ring) noexcept {
        static_cast<void>(ring);
        static_cast<void>(kept.is_some());
    });
}

}  // namespace

int main() { return 0; }
