#pragma once

// The compiler floor, the attribute vocabulary and the invariant macros have
// one definition, in foundation/Platform.h. A translation unit that includes
// headers of the two trees then sees each macro one time, and no definition
// replaces another.
//
// Crucible code calls two debugger helpers by their crucible::detail names.
// The using declarations keep those names. Each one is the foundation entity,
// not a copy of it.

#include <foundation/Platform.h>

namespace crucible::detail {

using ::foundation::detail::breakpoint_if_debugging;
using ::foundation::detail::is_debugger_present;

}  // namespace crucible::detail
