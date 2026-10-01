// The compile-time checks of crucible/mimic/_wip/am/network/Backend.h.

#include <crucible/mimic/_wip/am/network/Backend.h>

namespace crucible::mimic::_wip::am::network {

static_assert(!::crucible::mimic::_wip::network::network_backend_has_emit_path_v<vendor>,
              "has_emit_path is true for Am but this header ships no emit "
              "logic. Ship a vendor-specific Backend specialization here or "
              "set has_emit_path back to false.");

}  // namespace crucible::mimic::_wip::am::network
