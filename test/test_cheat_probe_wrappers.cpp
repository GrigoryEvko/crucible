// Every cheat below is a type that looks like one of the wrappers but is
// not that wrapper.  Naming the probe is the whole assertion: the
// instantiation fails to compile the moment a detector starts admitting
// the type it is supposed to reject, so the build is the test.
//
// A new detector needs two things.  Wire the gate for its category, then
// register at least one cheat for that category here.  Without the first,
// every probe for the category is silently vacuous.

#include <crucible/safety/_CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/_DetSafe.h>
#include <crucible/safety/_NumericalTier.h>
#include <crucible/safety/_OpaqueLifetime.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/_RecipeSpec.h>
#include <crucible/safety/diag/CheatProbeGates.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

namespace diag = ::crucible::safety::diag;
namespace safety = ::crucible::safety;
namespace extract = ::crucible::safety::extract;

// Each struct below carries the storage silhouette of one wrapper while
// neither inheriting from it nor specializing it.  A detector keyed on
// shape would admit them.  Keyed on wrapper identity, it rejects them.

struct Cheat_NumericalTier_Lookalike {
    int value;
    extract::Tolerance tier_field;
};

struct Cheat_Consistency_Lookalike {
    int value;
    extract::Consistency_v level_field;
};

struct Cheat_OpaqueLifetime_Lookalike {
    int value;
    extract::Lifetime_v scope_field;
};

struct Cheat_DetSafe_Lookalike {
    int value;
    extract::DetSafeTier_v tier_field;
};

struct Cheat_CipherTier_Lookalike {
    int value;
    extract::CipherTierTag_v tier_field;
};

struct Cheat_ResidencyHeat_Lookalike {
    int value;
    extract::ResidencyHeatTag_v tier_field;
};

struct Cheat_Vendor_Lookalike {
    int value;
    extract::VendorBackend_v backend_field;
};

struct Cheat_Crash_Lookalike {
    int value;
    extract::CrashClass_v class_field;
};

struct Cheat_Budgeted_Lookalike {
    int value;
    std::uint64_t bits_field;
    std::uint64_t peak_field;
};

struct Cheat_EpochVersioned_Lookalike {
    int value;
    std::uint64_t epoch_field;
    std::uint64_t generation_field;
};

struct Cheat_NumaPlacement_Lookalike {
    int value;
    safety::NumaNodeId node_field;
    safety::AffinityMask aff_field;
};

struct Cheat_RecipeSpec_Lookalike {
    int value;
    safety::Tolerance tol_field;
    safety::RecipeFamily fam_field;
};

// The detectors strip cv and reference qualifiers before matching, and
// nothing else.  A pointer to a wrapper therefore keeps its pointer
// identity and falls through to the rejecting branch.

using NT_int_bitexact = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;
using Cn_int_strong = safety::Consistency<extract::Consistency_v::STRONG, int>;
using OL_int_fleet = safety::OpaqueLifetime<extract::Lifetime_v::PER_FLEET, int>;
using DS_int_pure = safety::DetSafe<extract::DetSafeTier_v::Pure, int>;
using CT_int_hot = safety::CipherTier<extract::CipherTierTag_v::Hot, int>;
using RH_int_hot = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Hot, int>;
using V_int_nv = safety::Vendor<extract::VendorBackend_v::NV, int>;
using C_int_no_throw = safety::Crash<extract::CrashClass_v::NoThrow, int>;
using B_int = safety::Budgeted<int>;
using EV_int = safety::EpochVersioned<int>;
using NP_int = safety::NumaPlacement<int>;
using RS_int = safety::RecipeSpec<int>;

using probe_NT_lookalike = diag::cheat_probe_type<Cheat_NumericalTier_Lookalike, diag::Category::NumericalTierMismatch>;
using probe_NT_pointer = diag::cheat_probe_type<NT_int_bitexact*, diag::Category::NumericalTierMismatch>;
using probe_NT_int = diag::cheat_probe_type<int, diag::Category::NumericalTierMismatch>;
// The sibling cheats are real wrappers that share a template shape with
// their target but are not it.
using probe_NT_sibling = diag::cheat_probe_type<Cn_int_strong, diag::Category::NumericalTierMismatch>;

using probe_Cn_lookalike = diag::cheat_probe_type<Cheat_Consistency_Lookalike, diag::Category::ConsistencyMismatch>;
using probe_Cn_pointer = diag::cheat_probe_type<Cn_int_strong*, diag::Category::ConsistencyMismatch>;
using probe_Cn_sibling = diag::cheat_probe_type<NT_int_bitexact, diag::Category::ConsistencyMismatch>;

using probe_OL_lookalike = diag::cheat_probe_type<Cheat_OpaqueLifetime_Lookalike, diag::Category::LifetimeViolation>;
using probe_OL_pointer = diag::cheat_probe_type<OL_int_fleet*, diag::Category::LifetimeViolation>;
using probe_OL_sibling = diag::cheat_probe_type<DS_int_pure, diag::Category::LifetimeViolation>;

using probe_DS_lookalike = diag::cheat_probe_type<Cheat_DetSafe_Lookalike, diag::Category::DetSafeLeak>;
using probe_DS_pointer = diag::cheat_probe_type<DS_int_pure*, diag::Category::DetSafeLeak>;
using probe_DS_sibling = diag::cheat_probe_type<OL_int_fleet, diag::Category::DetSafeLeak>;

using probe_CT_lookalike = diag::cheat_probe_type<Cheat_CipherTier_Lookalike, diag::Category::CipherTierViolation>;
using probe_CT_pointer = diag::cheat_probe_type<CT_int_hot*, diag::Category::CipherTierViolation>;
// These two take the same shape: a three-tier enum and an element type.
using probe_CT_sibling_RH = diag::cheat_probe_type<RH_int_hot, diag::Category::CipherTierViolation>;

using probe_RH_lookalike =
    diag::cheat_probe_type<Cheat_ResidencyHeat_Lookalike, diag::Category::ResidencyHeatViolation>;
using probe_RH_pointer = diag::cheat_probe_type<RH_int_hot*, diag::Category::ResidencyHeatViolation>;
using probe_RH_sibling_CT = diag::cheat_probe_type<CT_int_hot, diag::Category::ResidencyHeatViolation>;

using probe_V_lookalike = diag::cheat_probe_type<Cheat_Vendor_Lookalike, diag::Category::VendorBackendMismatch>;
using probe_V_pointer = diag::cheat_probe_type<V_int_nv*, diag::Category::VendorBackendMismatch>;
using probe_V_sibling = diag::cheat_probe_type<C_int_no_throw, diag::Category::VendorBackendMismatch>;

using probe_C_lookalike = diag::cheat_probe_type<Cheat_Crash_Lookalike, diag::Category::CrashClassMismatch>;
using probe_C_pointer = diag::cheat_probe_type<C_int_no_throw*, diag::Category::CrashClassMismatch>;
using probe_C_sibling = diag::cheat_probe_type<V_int_nv, diag::Category::CrashClassMismatch>;

using probe_B_lookalike = diag::cheat_probe_type<Cheat_Budgeted_Lookalike, diag::Category::BudgetExceeded>;
using probe_B_pointer = diag::cheat_probe_type<B_int*, diag::Category::BudgetExceeded>;
// These two are identical byte for byte: a pair of runtime 64-bit axes
// over one element.
using probe_B_sibling_EV = diag::cheat_probe_type<EV_int, diag::Category::BudgetExceeded>;

using probe_EV_lookalike = diag::cheat_probe_type<Cheat_EpochVersioned_Lookalike, diag::Category::EpochMismatch>;
using probe_EV_pointer = diag::cheat_probe_type<EV_int*, diag::Category::EpochMismatch>;
using probe_EV_sibling_B = diag::cheat_probe_type<B_int, diag::Category::EpochMismatch>;

using probe_NP_lookalike = diag::cheat_probe_type<Cheat_NumaPlacement_Lookalike, diag::Category::NumaPlacementMismatch>;
using probe_NP_pointer = diag::cheat_probe_type<NP_int*, diag::Category::NumaPlacementMismatch>;
using probe_NP_sibling = diag::cheat_probe_type<RS_int, diag::Category::NumaPlacementMismatch>;

using probe_RS_lookalike = diag::cheat_probe_type<Cheat_RecipeSpec_Lookalike, diag::Category::RecipeSpecMismatch>;
using probe_RS_pointer = diag::cheat_probe_type<RS_int*, diag::Category::RecipeSpecMismatch>;
// These two key on the same tolerance enum and share a sub-lattice.
using probe_RS_sibling_NT = diag::cheat_probe_type<NT_int_bitexact, diag::Category::RecipeSpecMismatch>;

// A probe whose gate is undefined does nothing at all.  Deleting a gate
// would therefore turn every cheat above it into a silent no-op, and
// these assertions are what stops that from passing unnoticed.

static_assert(diag::is_gate_defined_v<diag::Category::NumericalTierMismatch>);
static_assert(diag::is_gate_defined_v<diag::Category::ConsistencyMismatch>);
static_assert(diag::is_gate_defined_v<diag::Category::LifetimeViolation>);
static_assert(diag::is_gate_defined_v<diag::Category::DetSafeLeak>);
static_assert(diag::is_gate_defined_v<diag::Category::CipherTierViolation>);
static_assert(diag::is_gate_defined_v<diag::Category::ResidencyHeatViolation>);
static_assert(diag::is_gate_defined_v<diag::Category::VendorBackendMismatch>);
static_assert(diag::is_gate_defined_v<diag::Category::CrashClassMismatch>);
static_assert(diag::is_gate_defined_v<diag::Category::BudgetExceeded>);
static_assert(diag::is_gate_defined_v<diag::Category::EpochMismatch>);
static_assert(diag::is_gate_defined_v<diag::Category::NumaPlacementMismatch>);
static_assert(diag::is_gate_defined_v<diag::Category::RecipeSpecMismatch>);

// A gate wired to a predicate that rejects everything would also reject
// every cheat.  Admitting the real wrapper is what makes the rejections
// above mean something.

static_assert(diag::concept_gate<diag::Category::NumericalTierMismatch>::admits_type<NT_int_bitexact>);
static_assert(diag::concept_gate<diag::Category::ConsistencyMismatch>::admits_type<Cn_int_strong>);
static_assert(diag::concept_gate<diag::Category::LifetimeViolation>::admits_type<OL_int_fleet>);
static_assert(diag::concept_gate<diag::Category::DetSafeLeak>::admits_type<DS_int_pure>);
static_assert(diag::concept_gate<diag::Category::CipherTierViolation>::admits_type<CT_int_hot>);
static_assert(diag::concept_gate<diag::Category::ResidencyHeatViolation>::admits_type<RH_int_hot>);
static_assert(diag::concept_gate<diag::Category::VendorBackendMismatch>::admits_type<V_int_nv>);
static_assert(diag::concept_gate<diag::Category::CrashClassMismatch>::admits_type<C_int_no_throw>);
static_assert(diag::concept_gate<diag::Category::BudgetExceeded>::admits_type<B_int>);
static_assert(diag::concept_gate<diag::Category::EpochMismatch>::admits_type<EV_int>);
static_assert(diag::concept_gate<diag::Category::NumaPlacementMismatch>::admits_type<NP_int>);
static_assert(diag::concept_gate<diag::Category::RecipeSpecMismatch>::admits_type<RS_int>);

}  // namespace

// The claims are all made at compile time.  This body only reports.
int main() {
    std::fprintf(stderr, "test_cheat_probe_wrappers: 12 categories × 3 cheats = 36 "
                         "lookalike/pointer/sibling cheats locked.\n");
    return EXIT_SUCCESS;
}
