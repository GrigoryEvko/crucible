// The compile-time checks of fixy/session/VigilModeCell.h.

#include <fixy/session/VigilModeCell.h>

namespace fixy::session::vigil_mode {

static_assert(sizeof(ModeCell) == sizeof(std::atomic<Mode>));

}  // namespace fixy::session::vigil_mode
