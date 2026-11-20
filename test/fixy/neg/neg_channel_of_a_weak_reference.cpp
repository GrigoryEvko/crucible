// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A producer can push a value and end its frame before the consumer pops it
// on another thread.  A WeakRef to an int of the frame of the producer then
// names a dead object when the consumer reads it.  So a value of a channel
// holds no borrow: it is SendableOwned (foundation/Transfer.h).

#include <fixy/Borrowed.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/permissions/Permission.h>

namespace {
struct CellTag {};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, CellTag>>();
    return ::fixy::concurrent::channel_t<::fixy::WeakRef<int const>, 8, decltype(root)>::capacity() == 8 ? 0 : 1;
}
