// The compile-time checks of foundation/algebra/lattices/ToleranceLattice.h.

#include <foundation/algebra/lattices/ToleranceLattice.h>

namespace foundation::algebra::lattices {

namespace detail::tolerance_lattice_self_test {

static_assert(::foundation::reflect::enum_count<Tolerance> == 7,
              "Tolerance catalog diverged from {RELAXED, ULP_INT8, ULP_FP8, ULP_FP16, ULP_FP32, ULP_FP64, "
              "BITEXACT}.  Confirm intent and update the precision-budget callers.");

static_assert(verify_chain_lattice<ToleranceLattice>(), "ToleranceLattice: the chain order, the pinned grades or the "
                                                        "reflected names diverged from the Tolerance enumerator list.");

static_assert(!UnboundedLattice<ToleranceLattice>);
static_assert(!Semiring<ToleranceLattice>);

static_assert(ToleranceLattice::bottom() == Tolerance::RELAXED);
static_assert(ToleranceLattice::top() == Tolerance::BITEXACT);

static_assert(ToleranceLattice::join(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::BITEXACT,
              "join gives the strictest-wins reading on this chain, because the "
              "top is BITEXACT.  join(RELAXED, BITEXACT) returns BITEXACT, the "
              "tighter budget.");
static_assert(ToleranceLattice::meet(Tolerance::RELAXED, Tolerance::BITEXACT) == Tolerance::RELAXED,
              "meet gives the loosest floor, because the bottom is RELAXED.  A "
              "gate that admits any tolerance calls meet.");

static_assert(ToleranceLattice::name() == "ToleranceLattice");
static_assert(tolerance::RelaxedTier::name() == "ToleranceLattice::At<RELAXED>");
static_assert(tolerance::BitexactTier::name() == "ToleranceLattice::At<BITEXACT>");
static_assert(ToleranceLattice::At<static_cast<Tolerance>(255)>::name() == "ToleranceLattice::At<?>");

static_assert(tolerance::RelaxedTier::tier == Tolerance::RELAXED);
static_assert(tolerance::BitexactTier::tier == Tolerance::BITEXACT);

}  // namespace detail::tolerance_lattice_self_test

}  // namespace foundation::algebra::lattices
