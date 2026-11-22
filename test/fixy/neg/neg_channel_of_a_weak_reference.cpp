// Negative compile test.  This file must fail to compile.
//
// A producer can push a value and end its frame before the consumer pops it
// on another thread.  A weak reference to an int of the frame of the producer
// then names a dead object when the consumer reads it.  So a value of a
// channel holds no borrow: it is SendableOwned (foundation/Transfer.h).
//
// The weak reference here is a word that states refers{}, as a reference of
// the base does.  It is trivially copyable, so the ring of the channel takes
// it, and only the rule of SendableOwned refuses it.

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
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, CellTag>>();
    return ::fixy::concurrent::channel_t<WeakWord<int const>, 8, decltype(root)>::capacity() == 8 ? 0 : 1;
}
