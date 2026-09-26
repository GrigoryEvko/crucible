// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A fusion pattern with no row in the shape table has no shape.  The table
// is consteval and its default arm is unreachable, so asking for the shape
// of a value outside the enum is no constant expression.

#include <crucible/forge/_wip/Phases/Comm.h>

namespace phase = crucible::forge::_wip::phases::comm;

inline constexpr auto kUnlistedPattern = static_cast<phase::CommFusionPattern>(99);

static_assert(!phase::comm_fusion_shape(kUnlistedPattern).lossy);
