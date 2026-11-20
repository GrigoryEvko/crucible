// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A message of a channel holds a WeakRef to the counter of its producer.  The
// producer can end before the consumer pops the message, and the consumer
// then reads a dead counter.  A value of a channel holds no borrow at any
// depth: it is SendableOwned (foundation/Transfer.h).

#include <fixy/Borrowed.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/permissions/Permission.h>

namespace {
struct CellTag {};

struct Message {
    ::fixy::WeakRef<int const> counter{};
    int sequence = 0;
};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, CellTag>>();
    return ::fixy::concurrent::channel_t<Message, 8, decltype(root)>::capacity() == 8 ? 0 : 1;
}
