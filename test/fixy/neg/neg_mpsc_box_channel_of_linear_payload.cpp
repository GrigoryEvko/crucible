// The end of an MPSC Box channel claims each word that its ring still
// holds, and the Box of each claim ends its object.  The end of a linear
// object drops it, so an MPSC Box channel holds no linear payload.  A Hole
// is linear.

#include <fixy/concurrent/PermissionedMpscBoxChannel.h>

#include <foundation/core/Ref.h>
#include <foundation/permissions/Permission.h>

namespace {
struct CreditTag {};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, CreditTag>>();
    return ::fixy::concurrent::mpsc_box_channel_t<::foundation::core::Hole<8, 8>, 2, decltype(root)>::capacity() == 2
             ? 0
             : 1;
}
