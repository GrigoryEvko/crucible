// Every cheat below is a type that looks like one of the wrappers but is
// not that wrapper.  Each assertion states that the detector for the
// wrapper refuses the cheat, so the build fails the moment a detector
// starts to admit the type it must reject.
//
// A new detector needs at least one cheat here, and one assertion that
// the detector admits the real wrapper.  Without the second assertion, a
// detector that refuses every type would satisfy each rejection below.

#include <crucible/safety/_CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/_Crash.h>
#include <crucible/safety/_DetSafe.h>
#include <crucible/safety/_NumericalTier.h>
#include <crucible/safety/_OpaqueLifetime.h>
#include <crucible/safety/_ResidencyHeat.h>
#include <crucible/safety/_Vendor.h>
#include <crucible/safety/_Budgeted.h>
#include <crucible/safety/_EpochVersioned.h>
#include <crucible/safety/_NumaPlacement.h>
#include <crucible/safety/_RecipeSpec.h>

#include <crucible/safety/_IsBudgeted.h>
#include <crucible/safety/_IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/_IsCrash.h>
#include <crucible/safety/_IsDetSafe.h>
#include <crucible/safety/_IsEpochVersioned.h>
#include <crucible/safety/_IsNumaPlacement.h>
#include <crucible/safety/_IsNumericalTier.h>
#include <crucible/safety/_IsOpaqueLifetime.h>
#include <crucible/safety/_IsRecipeSpec.h>
#include <crucible/safety/_IsResidencyHeat.h>
#include <crucible/safety/_IsVendor.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

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

static_assert(!extract::is_numerical_tier_v<Cheat_NumericalTier_Lookalike>);
static_assert(!extract::is_numerical_tier_v<NT_int_bitexact*>);
static_assert(!extract::is_numerical_tier_v<int>);
// The sibling cheats are real wrappers that share a template shape with
// their target but are not it.
static_assert(!extract::is_numerical_tier_v<Cn_int_strong>);

static_assert(!extract::is_consistency_v<Cheat_Consistency_Lookalike>);
static_assert(!extract::is_consistency_v<Cn_int_strong*>);
static_assert(!extract::is_consistency_v<NT_int_bitexact>);

static_assert(!extract::is_opaque_lifetime_v<Cheat_OpaqueLifetime_Lookalike>);
static_assert(!extract::is_opaque_lifetime_v<OL_int_fleet*>);
static_assert(!extract::is_opaque_lifetime_v<DS_int_pure>);

static_assert(!extract::is_det_safe_v<Cheat_DetSafe_Lookalike>);
static_assert(!extract::is_det_safe_v<DS_int_pure*>);
static_assert(!extract::is_det_safe_v<OL_int_fleet>);

static_assert(!extract::is_cipher_tier_v<Cheat_CipherTier_Lookalike>);
static_assert(!extract::is_cipher_tier_v<CT_int_hot*>);
// These two take the same shape: a three-tier enum and an element type.
static_assert(!extract::is_cipher_tier_v<RH_int_hot>);

static_assert(!extract::is_residency_heat_v<Cheat_ResidencyHeat_Lookalike>);
static_assert(!extract::is_residency_heat_v<RH_int_hot*>);
static_assert(!extract::is_residency_heat_v<CT_int_hot>);

static_assert(!extract::is_vendor_v<Cheat_Vendor_Lookalike>);
static_assert(!extract::is_vendor_v<V_int_nv*>);
static_assert(!extract::is_vendor_v<C_int_no_throw>);

static_assert(!extract::is_crash_v<Cheat_Crash_Lookalike>);
static_assert(!extract::is_crash_v<C_int_no_throw*>);
static_assert(!extract::is_crash_v<V_int_nv>);

static_assert(!extract::is_budgeted_v<Cheat_Budgeted_Lookalike>);
static_assert(!extract::is_budgeted_v<B_int*>);
// These two are identical byte for byte: a pair of runtime 64-bit axes
// over one element.
static_assert(!extract::is_budgeted_v<EV_int>);

static_assert(!extract::is_epoch_versioned_v<Cheat_EpochVersioned_Lookalike>);
static_assert(!extract::is_epoch_versioned_v<EV_int*>);
static_assert(!extract::is_epoch_versioned_v<B_int>);

static_assert(!extract::is_numa_placement_v<Cheat_NumaPlacement_Lookalike>);
static_assert(!extract::is_numa_placement_v<NP_int*>);
static_assert(!extract::is_numa_placement_v<RS_int>);

static_assert(!extract::is_recipe_spec_v<Cheat_RecipeSpec_Lookalike>);
static_assert(!extract::is_recipe_spec_v<RS_int*>);
// These two key on the same tolerance enum and share a sub-lattice.
static_assert(!extract::is_recipe_spec_v<NT_int_bitexact>);

// A detector that rejects every type would also reject every cheat.
// Admitting the real wrapper is what makes the rejections above mean
// something.

static_assert(extract::is_numerical_tier_v<NT_int_bitexact>);
static_assert(extract::is_consistency_v<Cn_int_strong>);
static_assert(extract::is_opaque_lifetime_v<OL_int_fleet>);
static_assert(extract::is_det_safe_v<DS_int_pure>);
static_assert(extract::is_cipher_tier_v<CT_int_hot>);
static_assert(extract::is_residency_heat_v<RH_int_hot>);
static_assert(extract::is_vendor_v<V_int_nv>);
static_assert(extract::is_crash_v<C_int_no_throw>);
static_assert(extract::is_budgeted_v<B_int>);
static_assert(extract::is_epoch_versioned_v<EV_int>);
static_assert(extract::is_numa_placement_v<NP_int>);
static_assert(extract::is_recipe_spec_v<RS_int>);

}  // namespace

// The claims are all made at compile time.  This body only reports.
int main() {
    std::fprintf(stderr, "test_cheat_probe_wrappers: 12 detectors × 3 cheats = 36 "
                         "lookalike/pointer/sibling cheats locked.\n");
    return EXIT_SUCCESS;
}
