// Negative compile test.  This file must fail to compile.
//
// A handle of a channel holds a ChannelBinding, which refers to the channel
// and owns nothing.  The channel can end before a consumer on another thread
// pops a message, so a message that carries a handle holds a borrow.  A value
// of a channel holds no borrow at any depth: it is SendableOwned
// (foundation/Transfer.h).  The handle itself still moves to another thread
// as a value of a body or a stage, because it is Sendable.

#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/permissions/Permission.h>

namespace {
struct InnerTag {};
struct ParcelTag {};
using InnerRoot = decltype(::foundation::permissions::mint_permission_root<
                           ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, InnerTag>>());
using Inner = ::fixy::concurrent::channel_t<int, 8, InnerRoot>;
// A message that carries a producer handle of a second channel.  A producer
// of a merge is no linear value, so a Box can drop the message.
struct Parcel {
    Inner::ProducerHandle producer;
};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, ParcelTag>>();
    return ::fixy::concurrent::box_channel_t<Parcel, 2, decltype(root)>::capacity() == 2 ? 0 : 1;
}
