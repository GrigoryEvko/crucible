// Negative compile test.  This file must fail to compile.
//
// A parcel owns an object of each element type (by_arguments{} of
// foundation/Transfer.h), so it moves to another thread only when each element
// type is Sendable.  The second element type refers to a plain int, which the
// thread of the push can still write, so the parcel is no payload of a Box
// channel.

#include <fixy/concurrent/PermissionedBoxChannel.h>

#include <foundation/Brand.h>
#include <foundation/core/ArenaFrame.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>

namespace {
struct ParcelTag {};
struct[[= ::foundation::brand::arena_family_brand{}]] ParcelFamily final {};
struct Shared {
    int& shared;
};
using Parcel = ::foundation::core::ArenaParcel<ParcelFamily, std::uint32_t, Shared>;
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, ParcelTag>>();
    return ::fixy::concurrent::box_channel_t<Parcel, 2, decltype(root)>::capacity() == 2 ? 0 : 1;
}
