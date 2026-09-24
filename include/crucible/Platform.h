#pragma once

// The compiler floor, the attribute vocabulary and the invariant macros have
// one definition, in foundation/Platform.h. A translation unit that includes
// headers of the two trees then sees each macro one time, and no definition
// replaces another.

#include <foundation/Platform.h>
