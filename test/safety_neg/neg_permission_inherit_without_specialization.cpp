#include <crucible/permissions/PermissionInherit.h>

namespace {

struct DeadTag {};
struct SurvivorTag {};

}  // namespace

auto bad = ::crucible::permissions::permission_inherit<DeadTag, SurvivorTag>();
