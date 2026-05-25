// FIXY-FOUND-048 HS14 fixture (1/2)
//
// MUST fail to compile: a Linear-outside-HotPath inversion is the
// archetypal §XVI violation.  HotPath has canonical_layer_index 0
// and Linear has canonical_layer_index 14; nesting Linear OUTSIDE
// HotPath inverts the §XVI order (14 > 0) and produces a different
// federation cache slot than the canonical nesting.  The
// `CanonicallyOrdered<>` concept must reject this at the
// instantiation site.
//
// Expected diagnostic substring matches:
//   * "static assertion failed" — the gate fires via static_assert
//   * "CanonicallyOrdered"      — concept name appears in the trace
//   * "false"                   — the predicate evaluates to false

#include <crucible/safety/diag/CanonicalOrder.h>

namespace co = crucible::safety::diag::canonical_order;
namespace cs = crucible::safety;

using BadStack = cs::Linear<cs::HotPath<cs::HotPathTier_v::Hot, int>>;

// Force concept evaluation at TU scope.
static_assert(co::CanonicallyOrdered<BadStack>,
    "FIXY-FOUND-048: Linear OUTSIDE HotPath inverts §XVI canonical "
    "wrapper-nesting order (Linear position 14 > HotPath position 0). "
    "This static_assert MUST fire — if it does not, the canonical-"
    "order gate has regressed.");
