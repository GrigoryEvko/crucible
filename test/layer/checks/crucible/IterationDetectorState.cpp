// The compile-time checks of crucible/IterationDetectorState.h.

#include <crucible/IterationDetectorState.h>

namespace crucible {

static_assert(::fixy::no_scoped_view_field_check<IterationDetector>(),
              "IterationDetector must not contain a ScopedView field. A view is a non-owning witness bounded "
              "by its carrier's lifetime, and storing one as a member defeats that bound.");

}  // namespace crucible
