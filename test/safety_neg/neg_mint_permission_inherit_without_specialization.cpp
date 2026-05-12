#include <crucible/permissions/PermissionInherit.h>

namespace {

struct DeadTag {};
struct SurvivorTag {};

}  // namespace

auto bad = ::crucible::permissions::mint_permission_inherit<
    DeadTag, SurvivorTag>();
