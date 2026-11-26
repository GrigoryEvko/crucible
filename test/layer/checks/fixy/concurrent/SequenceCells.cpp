// The compile-time checks of fixy/concurrent/SequenceCells.h.

#include <fixy/concurrent/SequenceCells.h>

#include <foundation/core/CaptureStore.h>

namespace fixy::concurrent {

namespace detail::sequence_cells_checks {

struct Queue final {};
using Cells = SequenceCells<int, 8, Queue>;

// The cells state synchronized_by_proof{}, and the check of the mark holds on
// them: no door keeps a borrow that a caller gives it.
static_assert(::foundation::core::detail::synchronized_by_proof_check_v<Cells>);

}  // namespace detail::sequence_cells_checks

}  // namespace fixy::concurrent
