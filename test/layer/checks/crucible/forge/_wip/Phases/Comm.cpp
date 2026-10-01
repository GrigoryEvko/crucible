// The compile-time checks of crucible/forge/_wip/Phases/Comm.h.

#include <crucible/forge/_wip/Phases/Comm.h>

namespace crucible::forge::_wip::phases::comm {

static_assert(sizeof(DeclaredFusedCommDecision) == sizeof(FusedCommDecision));
static_assert(std::is_trivially_copyable_v<FusedCommDecision>);

}  // namespace crucible::forge::_wip::phases::comm
