// The compile-time checks of fixy/session/Checkpoint.h.

#include <fixy/session/Checkpoint.h>

// The two traits are aliases, and the roster walk of
// foundation/contracts/ArmedRoster.h finds class templates only.  These two
// assertions read the two cells.
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_checkpoint_primitive>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_checkpoint_compliant>);
