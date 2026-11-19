// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A push on one thread gives the value of a channel to a pop on another
// thread, so a value of a channel is Sendable (foundation/Transfer.h).  A raw
// pointer is not: the walk cannot see who else holds the object.

#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/permissions/Permission.h>

namespace {
struct CellTag {};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, CellTag>>();
    return ::fixy::concurrent::channel_t<int*, 8, decltype(root)>::capacity() == 8 ? 0 : 1;
}
