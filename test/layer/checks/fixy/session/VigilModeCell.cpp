// The compile-time checks of fixy/session/VigilModeCell.h.

#include <fixy/session/VigilModeCell.h>

#include <type_traits>

namespace fixy::session::vigil_mode {

// The cell holds the mode and the identity of its writer, and no more.
static_assert(sizeof(ModeCell) <= 2 * sizeof(::foundation::core::Identity));
static_assert(!std::is_copy_constructible_v<ModeWriter>);
static_assert(!std::is_trivially_copyable_v<ModeWriter>);

}  // namespace fixy::session::vigil_mode
