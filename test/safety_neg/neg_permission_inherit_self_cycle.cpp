#include <crucible/permissions/PermissionInherit.h>

namespace {

struct PeerTag {};

}  // namespace

namespace crucible::permissions {

template <>
struct inherits_from<PeerTag, PeerTag> : std::true_type {};

}  // namespace crucible::permissions

auto bad = ::crucible::permissions::permission_inherit<PeerTag, PeerTag>();
