// Negative compile test.  This file must fail to compile.
//
// The walk of foundation/Transfer.h trusts by_arguments{} only in a class of
// the base.  A class outside the base that states it gets no property from
// the mark, and the walk reads its member, which refers to a plain int.  So
// the class is not Sendable, and it is no payload of a Box channel.

#include <fixy/concurrent/PermissionedBoxChannel.h>

#include <foundation/Transfer.h>
#include <foundation/permissions/Permission.h>

namespace {
struct ParcelTag {};
template <class T>
struct[[= ::foundation::transfer::by_arguments{}]] OutsideOwner {
    int& shared;
};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, ParcelTag>>();
    return ::fixy::concurrent::box_channel_t<OutsideOwner<int>, 2, decltype(root)>::capacity() == 2 ? 0 : 1;
}
