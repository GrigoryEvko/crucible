// The compile-time checks of foundation/core/StaticText.h.

#include <foundation/core/StaticText.h>

namespace foundation::core {

// The site door gives the text of the file of a site, as the watch of
// fixy/session/Watch.h stores it, and the view reads it to its zero.
static_assert(detail::StaticTextDoor::of_site_file("checks/foundation/core/StaticText.cpp").view()
              == "checks/foundation/core/StaticText.cpp");
static_assert(detail::StaticTextDoor::of_site_file(__builtin_FILE()).view().size() != 0);

// The atomic cell of a StaticText reads its word protocol.
static_assert(detail::HasAtomicWord<StaticText>);

}  // namespace foundation::core
