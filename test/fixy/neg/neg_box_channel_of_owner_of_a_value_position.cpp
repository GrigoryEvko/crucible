// Negative compile test.  This file must fail to compile.
//
// by_arguments<1>{} names the position 1 as an owned type argument, but the
// argument at that position is a value.  The walk of foundation/Transfer.h
// stops the build when it reads such a statement.

#include <fixy/concurrent/PermissionedBoxChannel.h>

#include <foundation/Transfer.h>
#include <foundation/permissions/Permission.h>

namespace {
struct ParcelTag {};
template <class T, int Count>
struct[[= ::foundation::transfer::by_arguments<1>{}]] OwnerOfAValue {
    int value = Count;
};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, ParcelTag>>();
    return ::fixy::concurrent::box_channel_t<OwnerOfAValue<int, 1>, 2, decltype(root)>::capacity() == 2 ? 0 : 1;
}
