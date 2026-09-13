#include <crucible/safety/FpMode.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/algebra/lattices/AllLattices.h>

#include <type_traits>

namespace cs = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;
namespace ca = ::crucible::algebra;
namespace csd = ::crucible::safety::diag;

namespace {

// The concept is the structural contract every graded carrier owes: the
// grade, lattice and value types, the modality, and the two name accessors.
// Checking all eleven catches a refactor that drops one member from one alias.
using F01 = cs::FpRoundingPinned<cs::FpRounding::RoundToNearestEven, int>;
using F02 = cs::FpFtzPinned<cs::FpFtz::FlushToZero, int>;
using F03 = cs::FpContractPinned<cs::FpContract::Fast, int>;
using F04 = cs::FpTrapMaskPinned<cs::FpTrapMask::AllMasked, int>;
using F05 = cs::FpDenormalInputPinned<cs::FpDenormalInput::DenormalsAreZero, int>;
using F06 = cs::FpNanPolicyPinned<cs::FpNanPolicy::PropagateQuiet, int>;
using F07 = cs::FpInfPolicyPinned<cs::FpInfPolicy::PropagateInfinity, int>;
using F08 = cs::FpComplexLayoutPinned<cs::FpComplexLayout::Interleaved, int>;
using F09 = cs::FpLibmPolicyPinned<cs::FpLibmPolicy::ScalarLibm, int>;
using F10 = cs::FpReassociatePinned<cs::FpReassociate::Forbidden, int>;
using F11 = cs::FpConstantRoundingPinned<cs::FpConstantRounding::SameAsRuntime, int>;

static_assert(ca::GradedWrapper<F01>);
static_assert(ca::GradedWrapper<F02>);
static_assert(ca::GradedWrapper<F03>);
static_assert(ca::GradedWrapper<F04>);
static_assert(ca::GradedWrapper<F05>);
static_assert(ca::GradedWrapper<F06>);
static_assert(ca::GradedWrapper<F07>);
static_assert(ca::GradedWrapper<F08>);
static_assert(ca::GradedWrapper<F09>);
static_assert(ca::GradedWrapper<F10>);
static_assert(ca::GradedWrapper<F11>);

// The singleton sub-lattice element is empty for every mode, and the grade is
// held with [[no_unique_address]], so each wrapper is byte-equivalent to its
// payload. Every zero-cost claim about these wrappers rests on this block.
static_assert(sizeof(F01) == sizeof(int));
static_assert(sizeof(F02) == sizeof(int));
static_assert(sizeof(F03) == sizeof(int));
static_assert(sizeof(F04) == sizeof(int));
static_assert(sizeof(F05) == sizeof(int));
static_assert(sizeof(F06) == sizeof(int));
static_assert(sizeof(F07) == sizeof(int));
static_assert(sizeof(F08) == sizeof(int));
static_assert(sizeof(F09) == sizeof(int));
static_assert(sizeof(F10) == sizeof(int));
static_assert(sizeof(F11) == sizeof(int));

// Each per-axis wrapper folds a distinct salt into the row hash. Two
// sub-axes over the same payload must land in different federation cache
// slots, or results computed under one evaluation policy get served for a
// request made under another.
static_assert(csd::row_hash_contribution_v<F01> != csd::row_hash_contribution_v<F02>);
static_assert(csd::row_hash_contribution_v<F01> != csd::row_hash_contribution_v<F03>);
static_assert(csd::row_hash_contribution_v<F02> != csd::row_hash_contribution_v<F04>);
static_assert(csd::row_hash_contribution_v<F05> != csd::row_hash_contribution_v<F06>);
static_assert(csd::row_hash_contribution_v<F07> != csd::row_hash_contribution_v<F08>);
static_assert(csd::row_hash_contribution_v<F09> != csd::row_hash_contribution_v<F10>);
static_assert(csd::row_hash_contribution_v<F10> != csd::row_hash_contribution_v<F11>);
// The same separation within one axis: the mode's own value reaches the hash,
// so two rounding modes over one payload are two slots, not one.
static_assert(csd::row_hash_contribution_v<cs::FpRoundingPinned<cs::FpRounding::RoundToNearestEven, int>>
              != csd::row_hash_contribution_v<cs::FpRoundingPinned<cs::FpRounding::RoundToZero, int>>);
static_assert(csd::row_hash_contribution_v<cs::FpFtzPinned<cs::FpFtz::FlushToZero, int>>
              != csd::row_hash_contribution_v<cs::FpFtzPinned<cs::FpFtz::PreserveSubnormals, int>>);

// All eleven sit on one axis. Telling them apart is the row hash's job, above.
// A cross-cutting query over axes sees a single FpMode axis.
static_assert(cs::wrapper_dimension_v<F01> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F02> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F03> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F04> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F05> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F06> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F07> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F08> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F09> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F10> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F11> == cs::DimensionAxis::FpMode);

// The composite is bounded because every component chain is bounded, and it
// is not a semiring because there is no sum or product distinct from join and
// meet. The arity catches a sub-axis dropped from the alias.
static_assert(ca::Lattice<cal::FpModeProductLattice>);
static_assert(ca::BoundedLattice<cal::FpModeProductLattice>);
static_assert(!ca::Semiring<cal::FpModeProductLattice>);
static_assert(cal::FpModeProductLattice::arity == 11);

// Eleven wrappers stacked outer to inner, every layer collapsing, so the whole
// nest is still the size of the payload.
using Composite = cs::FpModeComposite<cs::FpRounding::RoundToNearestEven, cs::FpFtz::FlushToZero, cs::FpContract::Off,
                                      cs::FpTrapMask::AllMasked, cs::FpDenormalInput::HonorDenormals,
                                      cs::FpNanPolicy::PropagateQuiet, cs::FpInfPolicy::PropagateInfinity,
                                      cs::FpComplexLayout::Interleaved, cs::FpLibmPolicy::ScalarLibm,
                                      cs::FpReassociate::Forbidden, cs::FpConstantRounding::SameAsRuntime, int>;
static_assert(sizeof(Composite) == sizeof(int));

// Calling a mint inside a static_assert is what proves it is usable at
// compile time, which no separate trait would establish.
static_assert(cs::mint_fp_rounding<cs::FpRounding::RoundToNearestEven, int>(7).peek() == 7);
static_assert(cs::mint_fp_ftz<cs::FpFtz::FlushToZero, int>(42).peek() == 42);
static_assert(cs::mint_fp_contract<cs::FpContract::Fast, int>(13).peek() == 13);
static_assert(cs::mint_fp_trap_mask<cs::FpTrapMask::AllMasked, int>(0).peek() == 0);

}  // namespace

int main() {
    // Every mint driven with non-constant arguments, plus the full nest.
    // Nothing above this point leaves constant evaluation.
    cs::fp_mode_runtime_smoke_test();
    return 0;
}
