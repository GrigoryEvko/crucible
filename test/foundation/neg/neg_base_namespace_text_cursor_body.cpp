// The rule base-namespace of the quarantine plugin refuses the body of a
// function of the base that the base only declares in the header that a unit
// includes.  foundation/core/Text.h declares the cursor of a TextView, and
// foundation/core/Region.h defines it.  A unit that includes only Text.h can
// give the cursor a body with no bound.  The plugin makes the definition a
// compile error in each build.

#include <foundation/core/Text.h>

constexpr auto foundation::core::TextView::begin() const noexcept { return 0; }

int main() { return 0; }
