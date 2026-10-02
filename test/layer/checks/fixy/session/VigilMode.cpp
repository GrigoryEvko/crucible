// The compile-time checks of fixy/session/VigilMode.h.

#include <fixy/session/VigilMode.h>

namespace fixy::session::vigil_mode {

static_assert(is_well_formed_v<ModeProtocol>);

static_assert(AtomicMachineCell<ModeCell>);

static_assert(std::is_same_v<typename ModeSessionHandle<>::resource_type, ModeCell&>);

}  // namespace fixy::session::vigil_mode
