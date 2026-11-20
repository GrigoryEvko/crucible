// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A message of a channel holds a weak reference to the counter of its
// producer.  The producer can end before the consumer pops the message, and
// the consumer then reads a dead counter.  A value of a channel holds no
// borrow at any depth: it is SendableOwned (foundation/Transfer.h).
//
// The weak reference here is a word that states refers{}, as a reference of
// the base does.  It is trivially copyable, so the ring of the channel takes
// the message, and only the rule of SendableOwned refuses it.

#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/Transfer.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>

namespace {
struct CellTag {};

// The address of an object of T of the frame of the producer, as one word.
template <class T>
struct[[= ::foundation::transfer::refers{}]] WeakWord {
    std::uint64_t address = 0;
};

struct Message {
    WeakWord<int const> counter{};
    int sequence = 0;
};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, CellTag>>();
    return ::fixy::concurrent::channel_t<Message, 8, decltype(root)>::capacity() == 8 ? 0 : 1;
}
