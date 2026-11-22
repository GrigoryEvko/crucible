// Negative compile test.  This file must fail to compile.
//
// The object of a Box moves from the thread of a push to the thread of a
// pop, so the payload of a Box channel is Sendable (foundation/Transfer.h).
// A payload that refers to a plain int is not, because the thread of the push
// can still write that int.

#include <fixy/concurrent/PermissionedBoxChannel.h>

#include <foundation/permissions/Permission.h>

namespace {
struct ParcelTag {};
struct Parcel {
    int& shared;
};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, ParcelTag>>();
    return ::fixy::concurrent::box_channel_t<Parcel, 2, decltype(root)>::capacity() == 2 ? 0 : 1;
}
