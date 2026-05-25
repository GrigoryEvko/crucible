// FIXY-FOUND-048 HS14 fixture (2/2)
//
// MUST fail to compile: same canonical layer used twice in a stack
// (e.g. HotPath<Hot, HotPath<Cold, T>>) is a §XVI defect — the
// canonical order requires STRICTLY increasing indices, so equal
// indices on the same layer reject.  This catches the "duplicate
// outer layer" mistake distinct from the inverted-order fixture.
//
// Expected diagnostic substring matches:
//   * "static assertion failed" — the gate fires via static_assert
//   * "CanonicallyOrdered"      — concept name appears in the trace

#include <crucible/safety/diag/CanonicalOrder.h>

namespace co = crucible::safety::diag::canonical_order;
namespace cs = crucible::safety;

using DuplicateStack = cs::HotPath<cs::HotPathTier_v::Hot,
                          cs::HotPath<cs::HotPathTier_v::Cold, int>>;

static_assert(co::CanonicallyOrdered<DuplicateStack>,
    "FIXY-FOUND-048: same canonical layer twice (HotPath ⊃ HotPath) "
    "violates §XVI strict-increase discipline.  This static_assert "
    "MUST fire — if it does not, the canonical-order gate accepts "
    "duplicate layers and the federation cache loses slot uniqueness.");
