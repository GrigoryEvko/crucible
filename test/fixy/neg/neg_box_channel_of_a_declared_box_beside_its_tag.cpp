// Negative compile test.  This file must fail to compile.
//
// A class that is only declared holds no property, so a Box of it is not
// Sendable (foundation/Transfer.h).  A type argument that names the same class
// is a tag, and a tag holds each property.  The tag beside the Box answers for
// the tag alone, so the parcel is not Sendable.

#include <fixy/concurrent/PermissionedBoxChannel.h>

#include <foundation/core/Ref.h>
#include <foundation/permissions/Permission.h>

namespace {
struct ParcelTag {};
class Cargo;
template <class Tag>
struct CargoLabel {
    int number = 0;
};
struct Parcel {
    ::foundation::core::Box<Cargo> cargo;
    CargoLabel<Cargo> label;
};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, ParcelTag>>();
    return ::fixy::concurrent::box_channel_t<Parcel, 2, decltype(root)>::capacity() == 2 ? 0 : 1;
}
