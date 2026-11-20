// The compile-time checks of foundation/core/StaticText.h.

#include <foundation/core/StaticText.h>

namespace foundation::core {

// The site form gives the text of the file of a site, as the watch of
// fixy/session/Watch.h stores it, and the view reads it to its zero.
static_assert(StaticText{Site{"checks/foundation/core/StaticText.cpp"}}.view()
              == "checks/foundation/core/StaticText.cpp");
static_assert(StaticText{Site{}}.view().size() != 0);

// The atomic cell of a StaticText reads its word protocol.
static_assert(detail::HasAtomicWord<StaticText>);

}  // namespace foundation::core
