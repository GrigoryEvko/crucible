// try_receive_freeing of the consumer end of a lanes channel calls freed with
// the index of a lane, a local of the ring.  A callable that takes the index by
// reference and captures an Option of a View by reference could keep a borrow
// of that local there, and the caller could read it after the receive.  The
// end refuses such a callable (LentArmStoresNoBorrow of
// foundation/core/Choice.h).

#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel = ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::lanes, int, 8, UserTag,
                                                        UserBrand, ::fixy::concurrent::LaneCount{2}>;

[[maybe_unused]] bool receive_one(Channel::ConsumerEnd& consumer) noexcept {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    return consumer
        .try_receive_freeing([&kept](Channel::lane_index const& lane) noexcept {
            static_cast<void>(lane);
            static_cast<void>(kept.is_some());
        })
        .is_ok();
}

}  // namespace

int main() { return 0; }
