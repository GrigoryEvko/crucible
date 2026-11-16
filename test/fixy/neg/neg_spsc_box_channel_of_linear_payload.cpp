// The end of a Box channel claims each word that its ring still holds, and
// the Box of each claim ends its object.  The end of a linear object drops
// it, so a Box channel holds no linear payload.  A Hole is linear.

#include <fixy/concurrent/PermissionedSpscBoxChannel.h>

#include <foundation/core/Ref.h>
#include <foundation/permissions/Permission.h>

namespace {
struct CreditTag {};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<::fixy::concurrent::spsc_tag::Whole<CreditTag>>();
    return ::fixy::concurrent::spsc_box_channel_t<::foundation::core::Hole<8, 8>, 2, decltype(root)>::capacity() == 2
             ? 0
             : 1;
}
