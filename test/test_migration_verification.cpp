// Each safety wrapper ships its own self-test, and each of those
// covers one wrapper alone.  Nothing else checks that the wrappers
// agree with each other.  This translation unit is the one place
// where they all meet, so it asserts the properties that only a
// cross-wrapper view can see:
//
//   - every wrapper exposes the same diagnostic surface, so a
//     forwarder that one wrapper loses and the others keep fails
//     here
//   - every wrapper preserves sizeof wherever its storage regime
//     promises it
//   - every wrapper instantiates as the inner type of every other
//   - wrapper nesting order selects the federation cache slot
//
// Wrapper-local behavior, such as construction, peek, consume and
// contracts, belongs to the per-wrapper self-tests.  This file does
// not repeat it.

#include <crucible/algebra/_GradedTrait.h>
#include <crucible/effects/_Computation.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_SealedRefined.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;

// A local predicate keeps these assertions independent of the
// predicates that the shared headers ship.
struct PositiveCheck {
    constexpr bool operator()(int x) const noexcept { return x > 0; }
};
inline constexpr PositiveCheck positive_local{};

struct VerificationTag {};

struct ResultTensor {
    int value;
};

// A wrapper whose grade is a single type-level point stores nothing
// of its own and collapses to the storage of T.  A wrapper that
// carries a runtime grade adds that grade to the layout.  Stale
// carries one 64-bit counter.  Each of the four product wrappers
// carries two 64-bit fields.  A composition that holds one of those
// therefore asserts a larger size.

static_assert(sizeof(Linear<int>) == sizeof(int));
static_assert(sizeof(Linear<long long>) == sizeof(long long));
static_assert(sizeof(Linear<void*>) == sizeof(void*));

static_assert(sizeof(Refined<positive_local, int>) == sizeof(int));

// SealedRefined shares Refined's substrate.  Sealing removes the
// rvalue extractor from the API and changes no storage.
static_assert(sizeof(SealedRefined<positive_local, int>) == sizeof(int));

static_assert(sizeof(Tagged<int, VerificationTag>) == sizeof(int));
static_assert(sizeof(Tagged<long, VerificationTag>) == sizeof(long));

static_assert(sizeof(Secret<int>) == sizeof(int));
static_assert(sizeof(Secret<long long>) == sizeof(long long));

static_assert(sizeof(NumericalTier<Tolerance::BITEXACT, int>) == sizeof(int));
static_assert(sizeof(NumericalTier<Tolerance::BITEXACT, double>) == sizeof(double));
static_assert(sizeof(NumericalTier<Tolerance::ULP_FP16, double>) == sizeof(double));
static_assert(sizeof(NumericalTier<Tolerance::RELAXED, long long>) == sizeof(long long));

static_assert(sizeof(Consistency<Consistency_v::STRONG, int>) == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::CAUSAL_PREFIX, double>) == sizeof(double));
static_assert(sizeof(Consistency<Consistency_v::EVENTUAL, long long>) == sizeof(long long));

static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET, int>) == sizeof(int));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_PROGRAM, double>) == sizeof(double));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_REQUEST, long long>) == sizeof(long long));

static_assert(sizeof(DetSafe<DetSafeTier_v::Pure, int>) == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, double>) == sizeof(double));
static_assert(sizeof(DetSafe<DetSafeTier_v::MonotonicClockRead, long long>) == sizeof(long long));

static_assert(sizeof(HotPath<HotPathTier_v::Hot, int>) == sizeof(int));
static_assert(sizeof(HotPath<HotPathTier_v::Warm, double>) == sizeof(double));
static_assert(sizeof(HotPath<HotPathTier_v::Cold, long long>) == sizeof(long long));

static_assert(sizeof(Wait<WaitStrategy_v::SpinPause, int>) == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::Park, double>) == sizeof(double));
static_assert(sizeof(Wait<WaitStrategy_v::Block, long long>) == sizeof(long long));

static_assert(sizeof(MemOrder<MemOrderTag_v::Relaxed, int>) == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::AcqRel, double>) == sizeof(double));
static_assert(sizeof(MemOrder<MemOrderTag_v::SeqCst, long long>) == sizeof(long long));

static_assert(sizeof(Progress<ProgressClass_v::Bounded, int>) == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::Productive, double>) == sizeof(double));
static_assert(sizeof(Progress<ProgressClass_v::MayDiverge, long long>) == sizeof(long long));

static_assert(sizeof(AllocClass<AllocClassTag_v::Stack, int>) == sizeof(int));
static_assert(sizeof(AllocClass<AllocClassTag_v::Arena, double>) == sizeof(double));
static_assert(sizeof(AllocClass<AllocClassTag_v::HugePage, long long>) == sizeof(long long));

static_assert(sizeof(CipherTier<CipherTierTag_v::Hot, int>) == sizeof(int));
static_assert(sizeof(CipherTier<CipherTierTag_v::Warm, double>) == sizeof(double));
static_assert(sizeof(CipherTier<CipherTierTag_v::Cold, long long>) == sizeof(long long));

static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Hot, int>) == sizeof(int));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Warm, double>) == sizeof(double));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Cold, long long>) == sizeof(long long));

// Vendor's lattice is a partial order and not a chain.  None is
// below every backend, Portable is above every backend, and two
// distinct middle backends are incomparable.  The wrapper still
// collapses because At<Backend> pins one point of that order.
static_assert(sizeof(Vendor<VendorBackend_v::Portable, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::NV, double>) == sizeof(double));
static_assert(sizeof(Vendor<VendorBackend_v::AMD, long long>) == sizeof(long long));
static_assert(sizeof(Vendor<VendorBackend_v::None, int>) == sizeof(int));

// Wrapper nesting order is load-bearing.  The row hash folds the
// stack from the outside in, so two stacks that hold the same
// wrappers in a different order land in different federation cache
// slots.  Each pair that follows pins one such order.

namespace cd = ::crucible::safety::diag;
namespace ce = ::crucible::effects;
using ce::Computation;
using ce::Effect;
using ce::Row;

using BgCarrier = Computation<Row<Effect::Bg>, int>;

static_assert(cd::row_hash_contribution_v<Stale<Tagged<int, VerificationTag>>>
              != cd::row_hash_contribution_v<Tagged<Stale<int>, VerificationTag>>);
static_assert(cd::row_hash_contribution_v<Refined<positive_local, Linear<int>>>
              != cd::row_hash_contribution_v<Linear<Refined<positive_local, int>>>);
static_assert(cd::row_hash_contribution_v<Secret<Tagged<int, VerificationTag>>>
              != cd::row_hash_contribution_v<Tagged<Secret<int>, VerificationTag>>);
static_assert(cd::row_hash_contribution_v<HotPath<HotPathTier_v::Hot, NumericalTier<Tolerance::BITEXACT, int>>>
              != cd::row_hash_contribution_v<NumericalTier<Tolerance::BITEXACT, HotPath<HotPathTier_v::Hot, int>>>);
static_assert(cd::row_hash_contribution_v<Vendor<VendorBackend_v::NV, CipherTier<CipherTierTag_v::Hot, int>>>
              != cd::row_hash_contribution_v<CipherTier<CipherTierTag_v::Hot, Vendor<VendorBackend_v::NV, int>>>);

using CanonicalFiveDeep =
    HotPath<HotPathTier_v::Hot,
            DetSafe<DetSafeTier_v::Pure,
                    NumericalTier<Tolerance::BITEXACT,
                                  Vendor<VendorBackend_v::NV, Computation<Row<Effect::Bg>, ResultTensor>>>>>;
using ShuffledFiveDeep = NumericalTier<
    Tolerance::BITEXACT,
    HotPath<HotPathTier_v::Hot,
            DetSafe<DetSafeTier_v::Pure, Vendor<VendorBackend_v::NV, Computation<Row<Effect::Bg>, ResultTensor>>>>>;

static_assert(cd::row_hash_contribution_v<CanonicalFiveDeep> != cd::row_hash_contribution_v<ShuffledFiveDeep>);

static_assert(cd::row_hash_contribution_v<HotPath<HotPathTier_v::Hot, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<DetSafe<DetSafeTier_v::Pure, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<NumericalTier<Tolerance::BITEXACT, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Vendor<VendorBackend_v::NV, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<ResidencyHeat<ResidencyHeatTag_v::Hot, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<CipherTier<CipherTierTag_v::Hot, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<AllocClass<AllocClassTag_v::Stack, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Wait<WaitStrategy_v::SpinPause, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<MemOrder<MemOrderTag_v::Relaxed, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Progress<ProgressClass_v::Terminating, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Stale<BgCarrier>> != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Tagged<BgCarrier, VerificationTag>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Refined<positive_local, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Secret<BgCarrier>> != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Linear<BgCarrier>> != cd::row_hash_contribution_v<BgCarrier>);

// A wrapper with no row-hash specialization falls back to the
// primary template, whose contribution is zero.  Two different
// wrappers then share one federation cache slot.  One cell for each
// wrapper makes such a regression name the wrapper.  Without those
// cells the only symptom is a coarse nesting failure.

static_assert(cd::row_hash_contribution_v<SealedRefined<positive_local, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<TimeOrdered<BgCarrier, 4>> != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Monotonic<std::uint64_t>> != cd::row_hash_contribution_v<std::uint64_t>);
static_assert(cd::row_hash_contribution_v<AppendOnly<int>> != cd::row_hash_contribution_v<int>);
static_assert(cd::row_hash_contribution_v<Consistency<Consistency_v::STRONG, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<OpaqueLifetime<Lifetime_v::PER_FLEET, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Crash<CrashClass_v::NoThrow, BgCarrier>>
              != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<Budgeted<BgCarrier>> != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<EpochVersioned<BgCarrier>> != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<NumaPlacement<BgCarrier>> != cd::row_hash_contribution_v<BgCarrier>);
static_assert(cd::row_hash_contribution_v<RecipeSpec<BgCarrier>> != cd::row_hash_contribution_v<BgCarrier>);

// The enum's underlying value takes part in the salt, so two tiers
// of one wrapper hash apart.
static_assert(cd::row_hash_contribution_v<Consistency<Consistency_v::STRONG, int>>
              != cd::row_hash_contribution_v<Consistency<Consistency_v::EVENTUAL, int>>);
static_assert(cd::row_hash_contribution_v<OpaqueLifetime<Lifetime_v::PER_FLEET, int>>
              != cd::row_hash_contribution_v<OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>>);
static_assert(cd::row_hash_contribution_v<Crash<CrashClass_v::NoThrow, int>>
              != cd::row_hash_contribution_v<Crash<CrashClass_v::Abort, int>>);

// A different clock width is a different carrier, so N takes part
// in the hash.
static_assert(cd::row_hash_contribution_v<TimeOrdered<int, 4>> != cd::row_hash_contribution_v<TimeOrdered<int, 8>>);

static_assert(cd::row_hash_contribution_v<CanonicalFiveDeep> == cd::row_hash_contribution_v<CanonicalFiveDeep>);
static_assert(cd::row_hash_contribution_v<Linear<BgCarrier>> == cd::row_hash_contribution_v<Linear<BgCarrier>>);

static_assert(sizeof(Crash<CrashClass_v::NoThrow, int>) == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::ErrorReturn, double>) == sizeof(double));
static_assert(sizeof(Crash<CrashClass_v::Throw, long long>) == sizeof(long long));
static_assert(sizeof(Crash<CrashClass_v::Abort, int>) == sizeof(int));

// Monotonic's grade is its value, so the substrate holds one cell
// instead of a value and a grade.
static_assert(sizeof(Monotonic<std::uint32_t>) == sizeof(std::uint32_t));
static_assert(sizeof(Monotonic<std::uint64_t>) == sizeof(std::uint64_t));

// SharedPermission is a proof token with no state of its own.  The
// fractional-share count lives in the pool that issues the token.
// An empty class is one byte alone and collapses to nothing inside
// a wrapper.
static_assert(sizeof(SharedPermission<VerificationTag>) == 1);

// The name comes from reflection.  The spelling of a primitive type
// depends on the surrounding context, so these match a suffix and
// not the whole string.

static_assert(Linear<int>::value_type_name().ends_with("int"));
static_assert(Refined<positive_local, int>::value_type_name().ends_with("int"));
static_assert(SealedRefined<positive_local, int>::value_type_name().ends_with("int"));
static_assert(Tagged<int, VerificationTag>::value_type_name().ends_with("int"));
static_assert(Secret<int>::value_type_name().ends_with("int"));
static_assert(NumericalTier<Tolerance::BITEXACT, int>::value_type_name().ends_with("int"));
static_assert(Consistency<Consistency_v::STRONG, int>::value_type_name().ends_with("int"));
static_assert(OpaqueLifetime<Lifetime_v::PER_FLEET, int>::value_type_name().ends_with("int"));
static_assert(DetSafe<DetSafeTier_v::Pure, int>::value_type_name().ends_with("int"));
static_assert(HotPath<HotPathTier_v::Hot, int>::value_type_name().ends_with("int"));
static_assert(Wait<WaitStrategy_v::SpinPause, int>::value_type_name().ends_with("int"));
static_assert(MemOrder<MemOrderTag_v::Relaxed, int>::value_type_name().ends_with("int"));
static_assert(Progress<ProgressClass_v::Bounded, int>::value_type_name().ends_with("int"));
static_assert(AllocClass<AllocClassTag_v::Stack, int>::value_type_name().ends_with("int"));
static_assert(CipherTier<CipherTierTag_v::Hot, int>::value_type_name().ends_with("int"));
static_assert(ResidencyHeat<ResidencyHeatTag_v::Hot, int>::value_type_name().ends_with("int"));
static_assert(Vendor<VendorBackend_v::Portable, int>::value_type_name().ends_with("int"));
static_assert(Crash<CrashClass_v::NoThrow, int>::value_type_name().ends_with("int"));
static_assert(Monotonic<std::uint64_t>::value_type_name().ends_with("uint64_t")
              || Monotonic<std::uint64_t>::value_type_name().ends_with("long unsigned int"));
static_assert(Stale<int>::value_type_name().ends_with("int"));
static_assert(TimeOrdered<int, 4>::value_type_name().ends_with("int"));
// SharedPermission's value type is the tag itself, so the name ends
// with the tag's own spelling.
static_assert(SharedPermission<VerificationTag>::value_type_name().ends_with("VerificationTag"));

// The assertion is not vacuous.  A refactor that moves graded_type
// back into a private section makes the name ill-formed here, and
// the static_assert then fails to compile.

static_assert(!std::is_void_v<typename Linear<int>::graded_type>);
static_assert(!std::is_void_v<typename Refined<positive_local, int>::graded_type>);
static_assert(!std::is_void_v<typename SealedRefined<positive_local, int>::graded_type>);
static_assert(!std::is_void_v<typename Tagged<int, VerificationTag>::graded_type>);
static_assert(!std::is_void_v<typename Secret<int>::graded_type>);
static_assert(!std::is_void_v<typename NumericalTier<Tolerance::BITEXACT, int>::graded_type>);
static_assert(!std::is_void_v<typename Consistency<Consistency_v::STRONG, int>::graded_type>);
static_assert(!std::is_void_v<typename OpaqueLifetime<Lifetime_v::PER_FLEET, int>::graded_type>);
static_assert(!std::is_void_v<typename DetSafe<DetSafeTier_v::Pure, int>::graded_type>);
static_assert(!std::is_void_v<typename HotPath<HotPathTier_v::Hot, int>::graded_type>);
static_assert(!std::is_void_v<typename Wait<WaitStrategy_v::SpinPause, int>::graded_type>);
static_assert(!std::is_void_v<typename MemOrder<MemOrderTag_v::Relaxed, int>::graded_type>);
static_assert(!std::is_void_v<typename Progress<ProgressClass_v::Bounded, int>::graded_type>);
static_assert(!std::is_void_v<typename AllocClass<AllocClassTag_v::Stack, int>::graded_type>);
static_assert(!std::is_void_v<typename CipherTier<CipherTierTag_v::Hot, int>::graded_type>);
static_assert(!std::is_void_v<typename ResidencyHeat<ResidencyHeatTag_v::Hot, int>::graded_type>);
static_assert(!std::is_void_v<typename Vendor<VendorBackend_v::Portable, int>::graded_type>);
static_assert(!std::is_void_v<typename Crash<CrashClass_v::NoThrow, int>::graded_type>);
static_assert(!std::is_void_v<typename Monotonic<std::uint64_t>::graded_type>);
static_assert(!std::is_void_v<typename AppendOnly<int>::graded_type>);
static_assert(!std::is_void_v<typename Stale<int>::graded_type>);
static_assert(!std::is_void_v<typename TimeOrdered<int, 4>::graded_type>);
static_assert(!std::is_void_v<typename SharedPermission<VerificationTag>::graded_type>);

// The concept folds the separate forwarder checks into one
// contract.  A wrapper that breaks a forwarder fails here, and the
// diagnostic names it.

using namespace ::crucible::algebra;

static_assert(GradedWrapper<Linear<int>>);
static_assert(GradedWrapper<Refined<positive_local, int>>);
static_assert(GradedWrapper<SealedRefined<positive_local, int>>);
static_assert(GradedWrapper<Tagged<int, VerificationTag>>);
static_assert(GradedWrapper<Secret<int>>);
static_assert(GradedWrapper<NumericalTier<Tolerance::BITEXACT, int>>);
static_assert(GradedWrapper<NumericalTier<Tolerance::ULP_FP16, double>>);
static_assert(GradedWrapper<Consistency<Consistency_v::STRONG, int>>);
static_assert(GradedWrapper<Consistency<Consistency_v::CAUSAL_PREFIX, double>>);
static_assert(GradedWrapper<OpaqueLifetime<Lifetime_v::PER_FLEET, int>>);
static_assert(GradedWrapper<OpaqueLifetime<Lifetime_v::PER_REQUEST, double>>);
static_assert(GradedWrapper<DetSafe<DetSafeTier_v::Pure, int>>);
static_assert(GradedWrapper<DetSafe<DetSafeTier_v::PhiloxRng, double>>);
static_assert(GradedWrapper<DetSafe<DetSafeTier_v::MonotonicClockRead, long long>>);
static_assert(GradedWrapper<HotPath<HotPathTier_v::Hot, int>>);
static_assert(GradedWrapper<HotPath<HotPathTier_v::Warm, double>>);
static_assert(GradedWrapper<HotPath<HotPathTier_v::Cold, long long>>);
static_assert(GradedWrapper<Wait<WaitStrategy_v::SpinPause, int>>);
static_assert(GradedWrapper<Wait<WaitStrategy_v::AcquireWait, double>>);
static_assert(GradedWrapper<Wait<WaitStrategy_v::Block, long long>>);
static_assert(GradedWrapper<MemOrder<MemOrderTag_v::Relaxed, int>>);
static_assert(GradedWrapper<MemOrder<MemOrderTag_v::AcqRel, double>>);
static_assert(GradedWrapper<MemOrder<MemOrderTag_v::SeqCst, long long>>);
static_assert(GradedWrapper<Progress<ProgressClass_v::Bounded, int>>);
static_assert(GradedWrapper<Progress<ProgressClass_v::Productive, double>>);
static_assert(GradedWrapper<Progress<ProgressClass_v::MayDiverge, long long>>);
static_assert(GradedWrapper<AllocClass<AllocClassTag_v::Stack, int>>);
static_assert(GradedWrapper<AllocClass<AllocClassTag_v::Arena, double>>);
static_assert(GradedWrapper<AllocClass<AllocClassTag_v::HugePage, long long>>);
static_assert(GradedWrapper<CipherTier<CipherTierTag_v::Hot, int>>);
static_assert(GradedWrapper<CipherTier<CipherTierTag_v::Warm, double>>);
static_assert(GradedWrapper<CipherTier<CipherTierTag_v::Cold, long long>>);
static_assert(GradedWrapper<ResidencyHeat<ResidencyHeatTag_v::Hot, int>>);
static_assert(GradedWrapper<ResidencyHeat<ResidencyHeatTag_v::Warm, double>>);
static_assert(GradedWrapper<ResidencyHeat<ResidencyHeatTag_v::Cold, long long>>);
static_assert(GradedWrapper<Vendor<VendorBackend_v::Portable, int>>);
static_assert(GradedWrapper<Vendor<VendorBackend_v::NV, double>>);
static_assert(GradedWrapper<Vendor<VendorBackend_v::AMD, long long>>);
static_assert(GradedWrapper<Vendor<VendorBackend_v::None, int>>);
static_assert(GradedWrapper<Crash<CrashClass_v::NoThrow, int>>);
static_assert(GradedWrapper<Crash<CrashClass_v::ErrorReturn, double>>);
static_assert(GradedWrapper<Crash<CrashClass_v::Throw, long long>>);
static_assert(GradedWrapper<Crash<CrashClass_v::Abort, int>>);
static_assert(GradedWrapper<Monotonic<std::uint64_t>>);
static_assert(GradedWrapper<AppendOnly<int>>);
static_assert(GradedWrapper<Stale<int>>);
static_assert(GradedWrapper<TimeOrdered<int, 4>>);
static_assert(GradedWrapper<SharedPermission<VerificationTag>>);

// A composition cell exercises only the outermost wrapper's
// conformance, which is independent of the inner type.  The four
// product wrappers therefore each need a bare assertion.
static_assert(GradedWrapper<Budgeted<int>>);
static_assert(GradedWrapper<EpochVersioned<int>>);
static_assert(GradedWrapper<NumaPlacement<int>>);
static_assert(GradedWrapper<RecipeSpec<int>>);

// The variable form tracks the concept, so two spot checks are
// enough.
static_assert(is_graded_wrapper_v<Linear<int>>);
static_assert(is_graded_wrapper_v<SharedPermission<VerificationTag>>);

static_assert(!is_graded_wrapper_v<int>);
static_assert(!is_graded_wrapper_v<void*>);
static_assert(!is_graded_wrapper_v<std::string_view>);

// The concept accepts any lattice_name() that returns a string
// view.  It does not require that view to equal the one the
// substrate returns, so a hand-written forwarder that answers
// wrongly still satisfies the concept.  This check closes that gap.
//
// A macro is the shorter spelling, but a template argument list
// contains commas that split the macro arguments, and `(W)::` is an
// old-style cast, which the build rejects.

template <typename W>
[[nodiscard]] consteval bool forwarders_actually_forward() noexcept {
    return W::value_type_name() == W::graded_type::value_type_name()
        && W::lattice_name() == W::graded_type::lattice_name();
}

static_assert(forwarders_actually_forward<Linear<int>>());
static_assert(forwarders_actually_forward<Refined<positive_local, int>>());
static_assert(forwarders_actually_forward<SealedRefined<positive_local, int>>());
static_assert(forwarders_actually_forward<Tagged<int, VerificationTag>>());
static_assert(forwarders_actually_forward<Secret<int>>());
static_assert(forwarders_actually_forward<NumericalTier<Tolerance::BITEXACT, int>>());
static_assert(forwarders_actually_forward<NumericalTier<Tolerance::ULP_FP16, double>>());
static_assert(forwarders_actually_forward<Consistency<Consistency_v::STRONG, int>>());
static_assert(forwarders_actually_forward<Consistency<Consistency_v::EVENTUAL, double>>());
static_assert(forwarders_actually_forward<OpaqueLifetime<Lifetime_v::PER_FLEET, int>>());
static_assert(forwarders_actually_forward<OpaqueLifetime<Lifetime_v::PER_REQUEST, double>>());
static_assert(forwarders_actually_forward<DetSafe<DetSafeTier_v::Pure, int>>());
static_assert(forwarders_actually_forward<DetSafe<DetSafeTier_v::MonotonicClockRead, double>>());
static_assert(forwarders_actually_forward<HotPath<HotPathTier_v::Hot, int>>());
static_assert(forwarders_actually_forward<HotPath<HotPathTier_v::Warm, double>>());
static_assert(forwarders_actually_forward<HotPath<HotPathTier_v::Cold, long long>>());
static_assert(forwarders_actually_forward<Wait<WaitStrategy_v::SpinPause, int>>());
static_assert(forwarders_actually_forward<Wait<WaitStrategy_v::AcquireWait, double>>());
static_assert(forwarders_actually_forward<Wait<WaitStrategy_v::Block, long long>>());
static_assert(forwarders_actually_forward<MemOrder<MemOrderTag_v::Relaxed, int>>());
static_assert(forwarders_actually_forward<MemOrder<MemOrderTag_v::AcqRel, double>>());
static_assert(forwarders_actually_forward<MemOrder<MemOrderTag_v::SeqCst, long long>>());
static_assert(forwarders_actually_forward<Progress<ProgressClass_v::Bounded, int>>());
static_assert(forwarders_actually_forward<Progress<ProgressClass_v::Productive, double>>());
static_assert(forwarders_actually_forward<Progress<ProgressClass_v::MayDiverge, long long>>());
static_assert(forwarders_actually_forward<AllocClass<AllocClassTag_v::Stack, int>>());
static_assert(forwarders_actually_forward<AllocClass<AllocClassTag_v::Arena, double>>());
static_assert(forwarders_actually_forward<AllocClass<AllocClassTag_v::HugePage, long long>>());
static_assert(forwarders_actually_forward<CipherTier<CipherTierTag_v::Hot, int>>());
static_assert(forwarders_actually_forward<CipherTier<CipherTierTag_v::Warm, double>>());
static_assert(forwarders_actually_forward<CipherTier<CipherTierTag_v::Cold, long long>>());
static_assert(forwarders_actually_forward<ResidencyHeat<ResidencyHeatTag_v::Hot, int>>());
static_assert(forwarders_actually_forward<ResidencyHeat<ResidencyHeatTag_v::Warm, double>>());
static_assert(forwarders_actually_forward<ResidencyHeat<ResidencyHeatTag_v::Cold, long long>>());
static_assert(forwarders_actually_forward<Vendor<VendorBackend_v::Portable, int>>());
static_assert(forwarders_actually_forward<Vendor<VendorBackend_v::NV, double>>());
static_assert(forwarders_actually_forward<Vendor<VendorBackend_v::AMD, long long>>());
static_assert(forwarders_actually_forward<Vendor<VendorBackend_v::None, int>>());
static_assert(forwarders_actually_forward<Crash<CrashClass_v::NoThrow, int>>());
static_assert(forwarders_actually_forward<Crash<CrashClass_v::ErrorReturn, double>>());
static_assert(forwarders_actually_forward<Crash<CrashClass_v::Throw, long long>>());
static_assert(forwarders_actually_forward<Crash<CrashClass_v::Abort, int>>());
static_assert(forwarders_actually_forward<Monotonic<std::uint64_t>>());
static_assert(forwarders_actually_forward<AppendOnly<int>>());
static_assert(forwarders_actually_forward<Stale<int>>());
static_assert(forwarders_actually_forward<TimeOrdered<int, 4>>());
static_assert(forwarders_actually_forward<SharedPermission<VerificationTag>>());

// These lattice names are hand-written literals and are stable, so
// they match in full.  The two predicate-derived names come from
// reflection and match a suffix instead.

static_assert(Linear<int>::lattice_name() == "QttSemiring::At<1>");
static_assert(Refined<positive_local, int>::lattice_name().ends_with("PositiveCheck"));
// SealedRefined shares Refined's lattice, so the two share a
// lattice name.  Class identity is what tells them apart.
static_assert(SealedRefined<positive_local, int>::lattice_name().ends_with("PositiveCheck"));
static_assert(Tagged<int, VerificationTag>::lattice_name().ends_with("VerificationTag"));
static_assert(Secret<int>::lattice_name() == "ConfLattice::At<Secret>");
static_assert(NumericalTier<Tolerance::BITEXACT, int>::lattice_name() == "ToleranceLattice::At<BITEXACT>");
static_assert(NumericalTier<Tolerance::ULP_FP16, double>::lattice_name() == "ToleranceLattice::At<ULP_FP16>");
static_assert(NumericalTier<Tolerance::RELAXED, long long>::lattice_name() == "ToleranceLattice::At<RELAXED>");
static_assert(Consistency<Consistency_v::STRONG, int>::lattice_name() == "ConsistencyLattice::At<STRONG>");
static_assert(Consistency<Consistency_v::CAUSAL_PREFIX, double>::lattice_name()
              == "ConsistencyLattice::At<CAUSAL_PREFIX>");
static_assert(Consistency<Consistency_v::EVENTUAL, long long>::lattice_name() == "ConsistencyLattice::At<EVENTUAL>");
static_assert(OpaqueLifetime<Lifetime_v::PER_FLEET, int>::lattice_name() == "LifetimeLattice::At<PER_FLEET>");
static_assert(OpaqueLifetime<Lifetime_v::PER_PROGRAM, double>::lattice_name() == "LifetimeLattice::At<PER_PROGRAM>");
static_assert(OpaqueLifetime<Lifetime_v::PER_REQUEST, long long>::lattice_name() == "LifetimeLattice::At<PER_REQUEST>");
static_assert(DetSafe<DetSafeTier_v::Pure, int>::lattice_name() == "DetSafeLattice::At<Pure>");
static_assert(DetSafe<DetSafeTier_v::PhiloxRng, double>::lattice_name() == "DetSafeLattice::At<PhiloxRng>");
static_assert(DetSafe<DetSafeTier_v::NonDeterministicSyscall, long long>::lattice_name()
              == "DetSafeLattice::At<NonDeterministicSyscall>");
static_assert(HotPath<HotPathTier_v::Hot, int>::lattice_name() == "HotPathLattice::At<Hot>");
static_assert(HotPath<HotPathTier_v::Warm, double>::lattice_name() == "HotPathLattice::At<Warm>");
static_assert(HotPath<HotPathTier_v::Cold, long long>::lattice_name() == "HotPathLattice::At<Cold>");
static_assert(Wait<WaitStrategy_v::SpinPause, int>::lattice_name() == "WaitLattice::At<SpinPause>");
static_assert(Wait<WaitStrategy_v::AcquireWait, double>::lattice_name() == "WaitLattice::At<AcquireWait>");
static_assert(Wait<WaitStrategy_v::Block, long long>::lattice_name() == "WaitLattice::At<Block>");
static_assert(MemOrder<MemOrderTag_v::Relaxed, int>::lattice_name() == "MemOrderLattice::At<Relaxed>");
static_assert(MemOrder<MemOrderTag_v::AcqRel, double>::lattice_name() == "MemOrderLattice::At<AcqRel>");
static_assert(MemOrder<MemOrderTag_v::SeqCst, long long>::lattice_name() == "MemOrderLattice::At<SeqCst>");
static_assert(Progress<ProgressClass_v::Bounded, int>::lattice_name() == "ProgressLattice::At<Bounded>");
static_assert(Progress<ProgressClass_v::Productive, double>::lattice_name() == "ProgressLattice::At<Productive>");
static_assert(Progress<ProgressClass_v::MayDiverge, long long>::lattice_name() == "ProgressLattice::At<MayDiverge>");
static_assert(AllocClass<AllocClassTag_v::Stack, int>::lattice_name() == "AllocClassLattice::At<Stack>");
static_assert(AllocClass<AllocClassTag_v::Arena, double>::lattice_name() == "AllocClassLattice::At<Arena>");
static_assert(AllocClass<AllocClassTag_v::HugePage, long long>::lattice_name() == "AllocClassLattice::At<HugePage>");
static_assert(CipherTier<CipherTierTag_v::Hot, int>::lattice_name() == "CipherTierLattice::At<Hot>");
static_assert(CipherTier<CipherTierTag_v::Warm, double>::lattice_name() == "CipherTierLattice::At<Warm>");
static_assert(CipherTier<CipherTierTag_v::Cold, long long>::lattice_name() == "CipherTierLattice::At<Cold>");
static_assert(ResidencyHeat<ResidencyHeatTag_v::Hot, int>::lattice_name() == "ResidencyHeatLattice::At<Hot>");
static_assert(ResidencyHeat<ResidencyHeatTag_v::Warm, double>::lattice_name() == "ResidencyHeatLattice::At<Warm>");
static_assert(ResidencyHeat<ResidencyHeatTag_v::Cold, long long>::lattice_name() == "ResidencyHeatLattice::At<Cold>");
static_assert(Vendor<VendorBackend_v::Portable, int>::lattice_name() == "VendorLattice::At<Portable>");
static_assert(Vendor<VendorBackend_v::NV, double>::lattice_name() == "VendorLattice::At<NV>");
static_assert(Vendor<VendorBackend_v::AMD, long long>::lattice_name() == "VendorLattice::At<AMD>");
static_assert(Vendor<VendorBackend_v::None, int>::lattice_name() == "VendorLattice::At<None>");
static_assert(Crash<CrashClass_v::NoThrow, int>::lattice_name() == "CrashLattice::At<NoThrow>");
static_assert(Crash<CrashClass_v::ErrorReturn, double>::lattice_name() == "CrashLattice::At<ErrorReturn>");
static_assert(Crash<CrashClass_v::Throw, long long>::lattice_name() == "CrashLattice::At<Throw>");
static_assert(Crash<CrashClass_v::Abort, int>::lattice_name() == "CrashLattice::At<Abort>");
static_assert(Monotonic<std::uint64_t>::lattice_name() == "MonotoneLattice");
static_assert(AppendOnly<int>::lattice_name() == "SeqPrefixLattice");
static_assert(Stale<int>::lattice_name() == "StalenessSemiring");
static_assert(TimeOrdered<int, 4>::lattice_name() == "HappensBeforeLattice");
static_assert(SharedPermission<VerificationTag>::lattice_name() == "FractionalLattice");

using TaggedLinear = Tagged<Linear<int>, VerificationTag>;
static_assert(sizeof(TaggedLinear) == sizeof(int));

// There is no Refined<P, Linear<T>> cell.  The predicate runs on
// the wrapped value, so such a composition needs a predicate that
// is invocable on Linear<T>.  No real predicate has that shape.

using SecretRefined = Secret<Refined<positive_local, int>>;
static_assert(sizeof(SecretRefined) == sizeof(Refined<positive_local, int>));
static_assert(sizeof(SecretRefined) == sizeof(int));

using TaggedStale = Tagged<Stale<int>, VerificationTag>;
// Stale's counter, and not Tagged, sets the size here.
static_assert(sizeof(TaggedStale) >= sizeof(Stale<int>));

using LinearSealed = Linear<SealedRefined<positive_local, int>>;
static_assert(sizeof(LinearSealed) == sizeof(int));

// SealedRefined<P, Linear<T>> is absent for the same reason.

using TaggedNumericalTier = Tagged<NumericalTier<Tolerance::BITEXACT, int>, VerificationTag>;
static_assert(sizeof(TaggedNumericalTier) == sizeof(int),
              "Tagged<NumericalTier<...>, Source> must EBO-collapse to sizeof(T) "
              "— two regime-1 wrappers stacked, both with empty grade, must "
              "preserve the underlying T's storage exactly.");

using NumericalTierOverLinear = NumericalTier<Tolerance::BITEXACT, Linear<int>>;
static_assert(sizeof(NumericalTierOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<NumericalTierOverLinear>,
              "NumericalTier<T_at, Linear<T>> must preserve Linear's "
              "move-only discipline.  If this fires, Linear's copy-deletion "
              "is no longer transitively visible through the NumericalTier "
              "wrapper — investigate the defaulted-copy regression.");
static_assert(std::is_move_constructible_v<NumericalTierOverLinear>);

using NumericalTierOverRefined = NumericalTier<Tolerance::ULP_FP32, Refined<positive_local, int>>;
static_assert(sizeof(NumericalTierOverRefined) == sizeof(int));

using TaggedConsistency = Tagged<Consistency<Consistency_v::STRONG, int>, VerificationTag>;
static_assert(sizeof(TaggedConsistency) == sizeof(int));

using ConsistencyOverNumerical = Consistency<Consistency_v::STRONG, NumericalTier<Tolerance::BITEXACT, int>>;
static_assert(sizeof(ConsistencyOverNumerical) == sizeof(int),
              "Consistency<...,NumericalTier<...,T>> must DOUBLE EBO-collapse "
              "to sizeof(T) — two regime-1 wrappers stacked, both with empty "
              "grade.");

using ConsistencyOverLinear = Consistency<Consistency_v::STRONG, Linear<int>>;
static_assert(sizeof(ConsistencyOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<ConsistencyOverLinear>,
              "Consistency<Level, Linear<T>> must preserve Linear's move-only "
              "discipline.  If this fires, Linear's copy-deletion is no longer "
              "transitively visible through the Consistency wrapper.");
static_assert(std::is_move_constructible_v<ConsistencyOverLinear>);

using TaggedOpaqueLifetime = Tagged<OpaqueLifetime<Lifetime_v::PER_FLEET, int>, VerificationTag>;
static_assert(sizeof(TaggedOpaqueLifetime) == sizeof(int));

using OpaqueLifetimeOverNumerical = OpaqueLifetime<Lifetime_v::PER_FLEET, NumericalTier<Tolerance::BITEXACT, int>>;
static_assert(sizeof(OpaqueLifetimeOverNumerical) == sizeof(int));

using OpaqueLifetimeOverLinear = OpaqueLifetime<Lifetime_v::PER_REQUEST, Linear<int>>;
static_assert(sizeof(OpaqueLifetimeOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<OpaqueLifetimeOverLinear>);
static_assert(std::is_move_constructible_v<OpaqueLifetimeOverLinear>);

// Depth is the point of this cell and of the deeper stacks that
// follow.  Collapse at two layers does not imply collapse at any
// depth, so those stacks add one wrapper at a time.
using TripleNested =
    OpaqueLifetime<Lifetime_v::PER_FLEET, Consistency<Consistency_v::STRONG, NumericalTier<Tolerance::BITEXACT, int>>>;
static_assert(sizeof(TripleNested) == sizeof(int), "TRIPLE-nested OpaqueLifetime<Consistency<NumericalTier<T>>> must "
                                                   "EBO-collapse all the way to sizeof(T) — three regime-1 wrappers "
                                                   "stacked over distinct lattices, all with empty grades.  If this "
                                                   "fires, one of the three wrapper layers stopped using the EBO-"
                                                   "friendly Graded substrate.");
static_assert(GradedWrapper<TripleNested>, "TRIPLE-nested wrapper must satisfy GradedWrapper at the outermost "
                                           "layer — proves the concept is compositional across distinct "
                                           "lattice types.");

using TaggedDetSafe = Tagged<DetSafe<DetSafeTier_v::Pure, int>, VerificationTag>;
static_assert(sizeof(TaggedDetSafe) == sizeof(int));

using DetSafeOverNumerical = DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>;
static_assert(sizeof(DetSafeOverNumerical) == sizeof(int));

// Both orders are pinned.  They share a layout but not a cache key,
// and a wrapper can regress its collapse in the outer position
// alone, or in the inner position alone.
using NumericalOverDetSafe = NumericalTier<Tolerance::BITEXACT, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(NumericalOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<NumericalOverDetSafe>);

using DetSafeOverLinear = DetSafe<DetSafeTier_v::Pure, Linear<int>>;
static_assert(sizeof(DetSafeOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<DetSafeOverLinear>);
static_assert(std::is_move_constructible_v<DetSafeOverLinear>);

using DetSafeOverRefined = DetSafe<DetSafeTier_v::Pure, Refined<positive_local, int>>;
static_assert(sizeof(DetSafeOverRefined) == sizeof(int));
static_assert(GradedWrapper<DetSafeOverRefined>);

using HotOverDetSafe = HotPath<HotPathTier_v::Hot, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(HotOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<HotOverDetSafe>);

using HotOverDetSafeOverNumerical =
    HotPath<HotPathTier_v::Hot, DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>>;
static_assert(sizeof(HotOverDetSafeOverNumerical) == sizeof(int));
static_assert(GradedWrapper<HotOverDetSafeOverNumerical>);

using TaggedHotPath = Tagged<HotPath<HotPathTier_v::Hot, int>, VerificationTag>;
static_assert(sizeof(TaggedHotPath) == sizeof(int));

using HotPathOverLinear = HotPath<HotPathTier_v::Hot, Linear<int>>;
static_assert(sizeof(HotPathOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<HotPathOverLinear>);
static_assert(std::is_move_constructible_v<HotPathOverLinear>);

using HotPathOverRefined = HotPath<HotPathTier_v::Hot, Refined<positive_local, int>>;
static_assert(sizeof(HotPathOverRefined) == sizeof(int));
static_assert(GradedWrapper<HotPathOverRefined>);

using HotPathOverStale = HotPath<HotPathTier_v::Hot, Stale<int>>;
static_assert(sizeof(HotPathOverStale) == sizeof(Stale<int>),
              "HotPath<Hot, Stale<T>> must EBO-collapse the HotPath grade only "
              "(Stale carries a runtime grade alongside T, regime-4).  If this "
              "fires, HotPath's regime-1 EBO discipline regressed when wrapping "
              "a non-empty-grade inner.");
static_assert(GradedWrapper<HotPathOverStale>);

using StaleOverHotPath = Stale<HotPath<HotPathTier_v::Hot, int>>;
static_assert(sizeof(StaleOverHotPath) == sizeof(Stale<int>),
              "Stale<HotPath<Hot, T>> must equal sizeof(Stale<T>) — HotPath's "
              "regime-1 EBO collapse means HotPath<Hot, T> is byte-equivalent "
              "to T at the inner layer, so Stale's storage shape is unchanged. "
              "If this fires, HotPath's regime-1 EBO discipline regressed when "
              "wrapped INSIDE a regime-4 wrapper.");
static_assert(GradedWrapper<StaleOverHotPath>);

using HotOverWait = HotPath<HotPathTier_v::Hot, Wait<WaitStrategy_v::SpinPause, int>>;
static_assert(sizeof(HotOverWait) == sizeof(int));
static_assert(GradedWrapper<HotOverWait>);

using WaitOverDetSafe = Wait<WaitStrategy_v::SpinPause, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(WaitOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<WaitOverDetSafe>);

using TaggedWait = Tagged<Wait<WaitStrategy_v::SpinPause, int>, VerificationTag>;
static_assert(sizeof(TaggedWait) == sizeof(int));

using WaitOverLinear = Wait<WaitStrategy_v::SpinPause, Linear<int>>;
static_assert(sizeof(WaitOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<WaitOverLinear>);
static_assert(std::is_move_constructible_v<WaitOverLinear>);

using WaitOverRefined = Wait<WaitStrategy_v::Park, Refined<positive_local, int>>;
static_assert(sizeof(WaitOverRefined) == sizeof(int));
static_assert(GradedWrapper<WaitOverRefined>);

using WaitOverStale = Wait<WaitStrategy_v::SpinPause, Stale<int>>;
static_assert(sizeof(WaitOverStale) == sizeof(Stale<int>),
              "Wait<SpinPause, Stale<T>> must EBO-collapse the Wait grade only "
              "(Stale carries a runtime grade alongside T, regime-4).  If this "
              "fires, Wait's regime-1 EBO discipline regressed when wrapping "
              "a non-empty-grade inner.");
static_assert(GradedWrapper<WaitOverStale>);

using StaleOverWait = Stale<Wait<WaitStrategy_v::SpinPause, int>>;
static_assert(sizeof(StaleOverWait) == sizeof(Stale<int>),
              "Stale<Wait<SpinPause, T>> must equal sizeof(Stale<T>) — Wait's "
              "regime-1 EBO collapse means Wait<SpinPause, T> is byte-equivalent "
              "to T at the inner layer, so Stale's storage shape is unchanged.");
static_assert(GradedWrapper<StaleOverWait>);

using HotOverWaitOverMemOrder =
    HotPath<HotPathTier_v::Hot, Wait<WaitStrategy_v::SpinPause, MemOrder<MemOrderTag_v::AcqRel, int>>>;
static_assert(sizeof(HotOverWaitOverMemOrder) == sizeof(int));
static_assert(GradedWrapper<HotOverWaitOverMemOrder>);

using MemOrderOverDetSafe = MemOrder<MemOrderTag_v::AcqRel, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(MemOrderOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<MemOrderOverDetSafe>);

using TaggedMemOrder = Tagged<MemOrder<MemOrderTag_v::Relaxed, int>, VerificationTag>;
static_assert(sizeof(TaggedMemOrder) == sizeof(int));

using MemOrderOverLinear = MemOrder<MemOrderTag_v::Relaxed, Linear<int>>;
static_assert(sizeof(MemOrderOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<MemOrderOverLinear>);
static_assert(std::is_move_constructible_v<MemOrderOverLinear>);

using MemOrderOverStale = MemOrder<MemOrderTag_v::Relaxed, Stale<int>>;
static_assert(sizeof(MemOrderOverStale) == sizeof(Stale<int>));
static_assert(GradedWrapper<MemOrderOverStale>);

using StaleOverMemOrder = Stale<MemOrder<MemOrderTag_v::Relaxed, int>>;
static_assert(sizeof(StaleOverMemOrder) == sizeof(Stale<int>));
static_assert(GradedWrapper<StaleOverMemOrder>);

using HotOverProgress = HotPath<HotPathTier_v::Hot, Progress<ProgressClass_v::Bounded, int>>;
static_assert(sizeof(HotOverProgress) == sizeof(int));
static_assert(GradedWrapper<HotOverProgress>);

using ProgressOverDetSafe = Progress<ProgressClass_v::Bounded, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(ProgressOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<ProgressOverDetSafe>);

using TaggedProgress = Tagged<Progress<ProgressClass_v::Bounded, int>, VerificationTag>;
static_assert(sizeof(TaggedProgress) == sizeof(int));

using ProgressOverLinear = Progress<ProgressClass_v::Bounded, Linear<int>>;
static_assert(sizeof(ProgressOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<ProgressOverLinear>);
static_assert(std::is_move_constructible_v<ProgressOverLinear>);

using ProgressOverStale = Progress<ProgressClass_v::Bounded, Stale<int>>;
static_assert(sizeof(ProgressOverStale) == sizeof(Stale<int>));
static_assert(GradedWrapper<ProgressOverStale>);

using StaleOverProgress = Stale<Progress<ProgressClass_v::Bounded, int>>;
static_assert(sizeof(StaleOverProgress) == sizeof(Stale<int>));
static_assert(GradedWrapper<StaleOverProgress>);

using HotOverWaitOverAllocClass =
    HotPath<HotPathTier_v::Hot, Wait<WaitStrategy_v::SpinPause, AllocClass<AllocClassTag_v::Stack, int>>>;
static_assert(sizeof(HotOverWaitOverAllocClass) == sizeof(int));
static_assert(GradedWrapper<HotOverWaitOverAllocClass>);

using AllocClassOverDetSafe = AllocClass<AllocClassTag_v::Arena, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(AllocClassOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<AllocClassOverDetSafe>);

using TaggedAllocClass = Tagged<AllocClass<AllocClassTag_v::Stack, int>, VerificationTag>;
static_assert(sizeof(TaggedAllocClass) == sizeof(int));

using AllocClassOverLinear = AllocClass<AllocClassTag_v::Arena, Linear<int>>;
static_assert(sizeof(AllocClassOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<AllocClassOverLinear>);
static_assert(std::is_move_constructible_v<AllocClassOverLinear>);

using AllocClassOverStale = AllocClass<AllocClassTag_v::Arena, Stale<int>>;
static_assert(sizeof(AllocClassOverStale) == sizeof(Stale<int>));
static_assert(GradedWrapper<AllocClassOverStale>);

using StaleOverAllocClass = Stale<AllocClass<AllocClassTag_v::Arena, int>>;
static_assert(sizeof(StaleOverAllocClass) == sizeof(Stale<int>));
static_assert(GradedWrapper<StaleOverAllocClass>);

using HotOverCipherTier = HotPath<HotPathTier_v::Warm, CipherTier<CipherTierTag_v::Hot, int>>;
static_assert(sizeof(HotOverCipherTier) == sizeof(int));
static_assert(GradedWrapper<HotOverCipherTier>);

using CipherTierOverDetSafe = CipherTier<CipherTierTag_v::Hot, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(CipherTierOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<CipherTierOverDetSafe>);

using TaggedCipherTier = Tagged<CipherTier<CipherTierTag_v::Warm, int>, VerificationTag>;
static_assert(sizeof(TaggedCipherTier) == sizeof(int));

using CipherTierOverLinear = CipherTier<CipherTierTag_v::Cold, Linear<int>>;
static_assert(sizeof(CipherTierOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<CipherTierOverLinear>,
              "CipherTier<Tier, Linear<T>> must preserve Linear's move-only "
              "discipline.  If this fires, Linear's copy-deletion is no longer "
              "transitively visible through the CipherTier wrapper.");
static_assert(std::is_move_constructible_v<CipherTierOverLinear>);

using CipherTierOverStale = CipherTier<CipherTierTag_v::Hot, Stale<int>>;
static_assert(sizeof(CipherTierOverStale) == sizeof(Stale<int>),
              "CipherTier<Hot, Stale<T>> must EBO-collapse the CipherTier "
              "grade only (Stale carries a runtime grade alongside T, "
              "regime-4).  If this fires, CipherTier's regime-1 EBO discipline "
              "regressed when wrapping a non-empty-grade inner.");
static_assert(GradedWrapper<CipherTierOverStale>);

using StaleOverCipherTier = Stale<CipherTier<CipherTierTag_v::Hot, int>>;
static_assert(sizeof(StaleOverCipherTier) == sizeof(Stale<int>),
              "Stale<CipherTier<Hot, T>> must equal sizeof(Stale<T>) — "
              "CipherTier's regime-1 EBO collapse means CipherTier<Hot, T> is "
              "byte-equivalent to T at the inner layer, so Stale's storage "
              "shape is unchanged.");
static_assert(GradedWrapper<StaleOverCipherTier>);

using AllocClassOverCipherTier = AllocClass<AllocClassTag_v::Stack, CipherTier<CipherTierTag_v::Hot, int>>;
static_assert(sizeof(AllocClassOverCipherTier) == sizeof(int),
              "AllocClass<Stack, CipherTier<Hot, T>> must EBO-collapse both "
              "regime-1 wrappers to sizeof(T).  If this fires, one of the two "
              "wrappers (AllocClass or CipherTier) regressed its EBO "
              "discipline when stacked with a sister chain wrapper.");
static_assert(GradedWrapper<AllocClassOverCipherTier>);

using CipherTierOverAllocClass = CipherTier<CipherTierTag_v::Hot, AllocClass<AllocClassTag_v::Stack, int>>;
static_assert(sizeof(CipherTierOverAllocClass) == sizeof(int),
              "CipherTier<Hot, AllocClass<Stack, T>> must EBO-collapse both "
              "regime-1 wrappers to sizeof(T) — order-symmetric to the "
              "AllocClassOverCipherTier cell above.");
static_assert(GradedWrapper<CipherTierOverAllocClass>);

using HotOverCipherTierOverResidencyHeat =
    HotPath<HotPathTier_v::Hot, CipherTier<CipherTierTag_v::Warm, ResidencyHeat<ResidencyHeatTag_v::Hot, int>>>;
static_assert(sizeof(HotOverCipherTierOverResidencyHeat) == sizeof(int),
              "HotPath ⊃ CipherTier ⊃ ResidencyHeat triple must EBO-collapse "
              "all three regime-1 wrappers to sizeof(T).  If this fires, one "
              "of the three orthogonal tier axes (execution-budget / storage-"
              "residency / cache-heat) regressed its EBO discipline.");
static_assert(GradedWrapper<HotOverCipherTierOverResidencyHeat>);

using ResidencyHeatOverDetSafe = ResidencyHeat<ResidencyHeatTag_v::Hot, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(ResidencyHeatOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<ResidencyHeatOverDetSafe>);

using TaggedResidencyHeat = Tagged<ResidencyHeat<ResidencyHeatTag_v::Warm, int>, VerificationTag>;
static_assert(sizeof(TaggedResidencyHeat) == sizeof(int));

using ResidencyHeatOverLinear = ResidencyHeat<ResidencyHeatTag_v::Cold, Linear<int>>;
static_assert(sizeof(ResidencyHeatOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<ResidencyHeatOverLinear>,
              "ResidencyHeat<Tier, Linear<T>> must preserve Linear's move-only "
              "discipline.  If this fires, Linear's copy-deletion is no longer "
              "transitively visible through the ResidencyHeat wrapper.");
static_assert(std::is_move_constructible_v<ResidencyHeatOverLinear>);

using ResidencyHeatOverStale = ResidencyHeat<ResidencyHeatTag_v::Hot, Stale<int>>;
static_assert(sizeof(ResidencyHeatOverStale) == sizeof(Stale<int>),
              "ResidencyHeat<Hot, Stale<T>> must EBO-collapse the "
              "ResidencyHeat grade only (Stale carries a runtime grade "
              "alongside T, regime-4).");
static_assert(GradedWrapper<ResidencyHeatOverStale>);

using StaleOverResidencyHeat = Stale<ResidencyHeat<ResidencyHeatTag_v::Hot, int>>;
static_assert(sizeof(StaleOverResidencyHeat) == sizeof(Stale<int>),
              "Stale<ResidencyHeat<Hot, T>> must equal sizeof(Stale<T>) — "
              "ResidencyHeat's regime-1 EBO collapse means ResidencyHeat<Hot, "
              "T> is byte-equivalent to T at the inner layer.");
static_assert(GradedWrapper<StaleOverResidencyHeat>);

using HotOverVendorOverDetSafe =
    HotPath<HotPathTier_v::Hot, Vendor<VendorBackend_v::Portable, DetSafe<DetSafeTier_v::Pure, int>>>;
static_assert(sizeof(HotOverVendorOverDetSafe) == sizeof(int),
              "HotPath ⊃ Vendor ⊃ DetSafe triple must EBO-collapse all three "
              "regime-1 wrappers to sizeof(T).  If this fires, the partial-"
              "order Vendor wrapper regressed its EBO discipline when nested "
              "with chain wrappers.");
static_assert(GradedWrapper<HotOverVendorOverDetSafe>);

using VendorOverDetSafe = Vendor<VendorBackend_v::NV, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(VendorOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<VendorOverDetSafe>);

using TaggedVendor = Tagged<Vendor<VendorBackend_v::NV, int>, VerificationTag>;
static_assert(sizeof(TaggedVendor) == sizeof(int));

using VendorOverLinear = Vendor<VendorBackend_v::Portable, Linear<int>>;
static_assert(sizeof(VendorOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<VendorOverLinear>,
              "Vendor<Backend, Linear<T>> must preserve Linear's move-only "
              "discipline.  If this fires, Linear's copy-deletion is no longer "
              "transitively visible through the Vendor wrapper.");
static_assert(std::is_move_constructible_v<VendorOverLinear>);

using VendorOverStale = Vendor<VendorBackend_v::NV, Stale<int>>;
static_assert(sizeof(VendorOverStale) == sizeof(Stale<int>),
              "Vendor<NV, Stale<T>> must EBO-collapse the Vendor grade only "
              "(Stale carries a runtime grade alongside T, regime-4).");
static_assert(GradedWrapper<VendorOverStale>);

using StaleOverVendor = Stale<Vendor<VendorBackend_v::NV, int>>;
static_assert(sizeof(StaleOverVendor) == sizeof(Stale<int>),
              "Stale<Vendor<NV, T>> must equal sizeof(Stale<T>) — Vendor's "
              "regime-1 EBO collapse means Vendor<NV, T> is byte-equivalent "
              "to T at the inner layer.");
static_assert(GradedWrapper<StaleOverVendor>);

using VendorPortableOverCipherTierHot = Vendor<VendorBackend_v::Portable, CipherTier<CipherTierTag_v::Hot, int>>;
static_assert(sizeof(VendorPortableOverCipherTierHot) == sizeof(int),
              "Vendor<Portable, CipherTier<Hot, T>> must EBO-collapse despite "
              "the lattice-shape divergence.  If this fires, the partial-order "
              "wrapper failed to compose with a chain-wrapper inner — would "
              "indicate a regime-1 EBO discipline regression specific to "
              "non-chain lattices.");
static_assert(GradedWrapper<VendorPortableOverCipherTierHot>);

using HotOverCrashOverDetSafe =
    HotPath<HotPathTier_v::Hot, Crash<CrashClass_v::NoThrow, DetSafe<DetSafeTier_v::Pure, int>>>;
static_assert(sizeof(HotOverCrashOverDetSafe) == sizeof(int),
              "HotPath ⊃ Crash ⊃ DetSafe triple must EBO-collapse all three "
              "regime-1 wrappers to sizeof(T).");
static_assert(GradedWrapper<HotOverCrashOverDetSafe>);

using CrashOverDetSafe = Crash<CrashClass_v::NoThrow, DetSafe<DetSafeTier_v::Pure, int>>;
static_assert(sizeof(CrashOverDetSafe) == sizeof(int));
static_assert(GradedWrapper<CrashOverDetSafe>);

using TaggedCrash = Tagged<Crash<CrashClass_v::NoThrow, int>, VerificationTag>;
static_assert(sizeof(TaggedCrash) == sizeof(int));

using CrashOverLinear = Crash<CrashClass_v::Abort, Linear<int>>;
static_assert(sizeof(CrashOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<CrashOverLinear>,
              "Crash<Class, Linear<T>> must preserve Linear's move-only "
              "discipline.");
static_assert(std::is_move_constructible_v<CrashOverLinear>);

using CrashOverStale = Crash<CrashClass_v::NoThrow, Stale<int>>;
static_assert(sizeof(CrashOverStale) == sizeof(Stale<int>),
              "Crash<NoThrow, Stale<T>> must EBO-collapse the Crash grade only "
              "(Stale carries a runtime grade alongside T, regime-4).");
static_assert(GradedWrapper<CrashOverStale>);

using StaleOverCrash = Stale<Crash<CrashClass_v::NoThrow, int>>;
static_assert(sizeof(StaleOverCrash) == sizeof(Stale<int>),
              "Stale<Crash<NoThrow, T>> must equal sizeof(Stale<T>) — Crash's "
              "regime-1 EBO collapse means Crash<NoThrow, T> is byte-equivalent "
              "to T at the inner layer.");
static_assert(GradedWrapper<StaleOverCrash>);

using CrashOverVendor = Crash<CrashClass_v::NoThrow, Vendor<VendorBackend_v::NV, int>>;
static_assert(sizeof(CrashOverVendor) == sizeof(int), "Crash<NoThrow, Vendor<NV, T>> must EBO-collapse — chain Crash "
                                                      "composing with partial-order Vendor at the inner layer.");
static_assert(GradedWrapper<CrashOverVendor>);

// Budgeted's grade is two runtime fields and not a type-level
// point.  The cells that follow check that a collapsing wrapper
// outside it leaves that grade alone.

using CrashOverBudgeted = Crash<CrashClass_v::NoThrow, Budgeted<int>>;
static_assert(sizeof(CrashOverBudgeted) == sizeof(Budgeted<int>),
              "Crash<NoThrow, Budgeted<T>> must EBO-collapse the Crash grade "
              "only — the inner Budgeted carries its 16-byte regime-4 grade "
              "unchanged.");
static_assert(GradedWrapper<CrashOverBudgeted>);

using HotPathOverBudgeted = HotPath<HotPathTier_v::Hot, Budgeted<int>>;
static_assert(sizeof(HotPathOverBudgeted) == sizeof(Budgeted<int>),
              "HotPath<Hot, Budgeted<T>> must EBO-collapse HotPath only.");
static_assert(GradedWrapper<HotPathOverBudgeted>);

using TaggedBudgeted = Tagged<Budgeted<int>, VerificationTag>;
static_assert(sizeof(TaggedBudgeted) == sizeof(Budgeted<int>), "Tagged<Budgeted<T>, Source> must EBO-collapse Tagged.");

// The bound adds 8 because Stale's counter is one 64-bit field.
using StaleOverBudgeted = Stale<Budgeted<int>>;
static_assert(sizeof(StaleOverBudgeted) >= sizeof(Budgeted<int>) + 8,
              "Stale<Budgeted<T>> must carry both grades — REGIME-4 ⊃ REGIME-4 "
              "is the sole non-EBO composition cell in this harness.");
static_assert(GradedWrapper<StaleOverBudgeted>);

static_assert(Budgeted<int>::lattice_name().size() > 0);
static_assert(Budgeted<double>::value_type_name().ends_with("double"));

using CrashOverEpochVersioned = Crash<CrashClass_v::NoThrow, EpochVersioned<int>>;
static_assert(sizeof(CrashOverEpochVersioned) == sizeof(EpochVersioned<int>),
              "Crash<NoThrow, EpochVersioned<T>> must EBO-collapse the Crash "
              "grade only — the inner EpochVersioned carries its 16-byte "
              "regime-4 grade unchanged.");
static_assert(GradedWrapper<CrashOverEpochVersioned>);

using TaggedEpochVersioned = Tagged<EpochVersioned<int>, VerificationTag>;
static_assert(sizeof(TaggedEpochVersioned) == sizeof(EpochVersioned<int>),
              "Tagged<EpochVersioned<T>, Source> must EBO-collapse Tagged.");

// The bound adds 16 because each product grade is two 64-bit
// fields.
using EpochVersionedOverBudgeted = EpochVersioned<Budgeted<int>>;
static_assert(sizeof(EpochVersionedOverBudgeted) >= sizeof(Budgeted<int>) + 16,
              "EpochVersioned<Budgeted<T>> must carry both regime-4 grades — "
              "the FIRST product-on-product cell.  Both inner Budgeted and "
              "outer EpochVersioned grades are visible; no EBO collapse "
              "happens between two non-empty grades.");
static_assert(GradedWrapper<EpochVersionedOverBudgeted>);

// Order does not change the total.  Each layer adds its own grade.
using BudgetedOverEpochVersioned = Budgeted<EpochVersioned<int>>;
static_assert(sizeof(BudgetedOverEpochVersioned) >= sizeof(EpochVersioned<int>) + 16,
              "Budgeted<EpochVersioned<T>> must carry both regime-4 grades.");
static_assert(GradedWrapper<BudgetedOverEpochVersioned>);

static_assert(EpochVersioned<int>::lattice_name().size() > 0);
static_assert(EpochVersioned<double>::value_type_name().ends_with("double"));

// NumaPlacement's product mixes a partial order with a boolean
// lattice.  Budgeted and EpochVersioned each compose two chains.

using CrashOverNumaPlacement = Crash<CrashClass_v::NoThrow, NumaPlacement<int>>;
static_assert(sizeof(CrashOverNumaPlacement) == sizeof(NumaPlacement<int>),
              "Crash<NoThrow, NumaPlacement<T>> must EBO-collapse the Crash "
              "grade only — the inner NumaPlacement carries its regime-4 grade "
              "unchanged.");
static_assert(GradedWrapper<CrashOverNumaPlacement>);

using TaggedNumaPlacement = Tagged<NumaPlacement<int>, VerificationTag>;
static_assert(sizeof(TaggedNumaPlacement) == sizeof(NumaPlacement<int>),
              "Tagged<NumaPlacement<T>, Source> must EBO-collapse Tagged.");

using TripleProductStack = NumaPlacement<EpochVersioned<Budgeted<int>>>;
static_assert(sizeof(TripleProductStack) >= sizeof(Budgeted<int>) + 16  // EpochVersioned grade
                                                + 16,  // NumaPlacement grade
              "Triple-product wrapper stack must carry all three regime-4 "
              "grades — sole instance of three non-empty grades stacked.  "
              "Total layout dominated by alignment padding between the "
              "various uint64-aligned components.");
static_assert(GradedWrapper<TripleProductStack>);

static_assert(NumaPlacement<int>::lattice_name().size() > 0);
static_assert(NumaPlacement<double>::value_type_name().ends_with("double"));

// RecipeSpec's product mixes a chain with a partial order.

using CrashOverRecipeSpec = Crash<CrashClass_v::NoThrow, RecipeSpec<int>>;
static_assert(sizeof(CrashOverRecipeSpec) == sizeof(RecipeSpec<int>),
              "Crash<NoThrow, RecipeSpec<T>> must EBO-collapse the Crash grade.");
static_assert(GradedWrapper<CrashOverRecipeSpec>);

using TaggedRecipeSpec = Tagged<RecipeSpec<int>, VerificationTag>;
static_assert(sizeof(TaggedRecipeSpec) == sizeof(RecipeSpec<int>));

using QuadProductStack = RecipeSpec<NumaPlacement<EpochVersioned<Budgeted<int>>>>;
static_assert(sizeof(QuadProductStack) >= sizeof(Budgeted<int>) + 16 + 16,
              "Four-product wrapper stack must carry all four regime-4 grades.");
static_assert(GradedWrapper<QuadProductStack>);

static_assert(RecipeSpec<int>::lattice_name().size() > 0);
static_assert(RecipeSpec<double>::value_type_name().ends_with("double"));

// Each product wrapper's own header asserts that its two axes are
// distinct types.  This is the only place where all of the wrappers
// are in scope at once, so the cross-wrapper pairs are asserted
// here.  Passing one axis where another is expected must not
// compile.
static_assert(!std::is_same_v<crucible::safety::BitsBudget, crucible::safety::Epoch>);
static_assert(!std::is_same_v<crucible::safety::BitsBudget, crucible::safety::Generation>);
static_assert(!std::is_same_v<crucible::safety::PeakBytes, crucible::safety::Epoch>);
static_assert(!std::is_same_v<crucible::safety::PeakBytes, crucible::safety::Generation>);

static_assert(!std::is_same_v<crucible::safety::AffinityMask, crucible::safety::BitsBudget>);
static_assert(!std::is_same_v<crucible::safety::AffinityMask, crucible::safety::PeakBytes>);
static_assert(!std::is_same_v<crucible::safety::AffinityMask, crucible::safety::Epoch>);
static_assert(!std::is_same_v<crucible::safety::AffinityMask, crucible::safety::Generation>);

// NumaNodeId is an enumeration over one byte and cannot collide
// with a 64-bit struct.  These three assertions are defensive.
static_assert(!std::is_same_v<crucible::safety::NumaNodeId, crucible::safety::AffinityMask>);
static_assert(!std::is_same_v<crucible::safety::NumaNodeId, crucible::safety::BitsBudget>);
static_assert(!std::is_same_v<crucible::safety::NumaNodeId, crucible::safety::Epoch>);

static_assert(!std::is_same_v<crucible::safety::Tolerance, crucible::safety::RecipeFamily>);

static_assert(!std::is_same_v<crucible::safety::Tolerance, crucible::safety::BitsBudget>);
static_assert(!std::is_same_v<crucible::safety::Tolerance, crucible::safety::PeakBytes>);
static_assert(!std::is_same_v<crucible::safety::Tolerance, crucible::safety::Epoch>);
static_assert(!std::is_same_v<crucible::safety::Tolerance, crucible::safety::Generation>);
static_assert(!std::is_same_v<crucible::safety::Tolerance, crucible::safety::NumaNodeId>);
static_assert(!std::is_same_v<crucible::safety::Tolerance, crucible::safety::AffinityMask>);

static_assert(!std::is_same_v<crucible::safety::RecipeFamily, crucible::safety::BitsBudget>);
static_assert(!std::is_same_v<crucible::safety::RecipeFamily, crucible::safety::PeakBytes>);
static_assert(!std::is_same_v<crucible::safety::RecipeFamily, crucible::safety::Epoch>);
static_assert(!std::is_same_v<crucible::safety::RecipeFamily, crucible::safety::Generation>);
static_assert(!std::is_same_v<crucible::safety::RecipeFamily, crucible::safety::NumaNodeId>);
static_assert(!std::is_same_v<crucible::safety::RecipeFamily, crucible::safety::AffinityMask>);

using ThreeHotsAtTop =
    HotPath<HotPathTier_v::Hot, CipherTier<CipherTierTag_v::Warm, ResidencyHeat<ResidencyHeatTag_v::Hot, int>>>;
static_assert(sizeof(ThreeHotsAtTop) == sizeof(int), "Three Hot-at-top tier axes (HotPath / CipherTier / "
                                                     "ResidencyHeat) compose orthogonally — each carrying a "
                                                     "structurally-identical but SEMANTICALLY DISTINCT lattice. "
                                                     "EBO collapse must succeed for all three despite the shape "
                                                     "similarity.  If this fires, cross-lattice identity collapsed.");
static_assert(GradedWrapper<ThreeHotsAtTop>);

using QuadrupleNested = OpaqueLifetime<
    Lifetime_v::PER_FLEET,
    Consistency<Consistency_v::STRONG, DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>>>;
static_assert(sizeof(QuadrupleNested) == sizeof(int),
              "QUADRUPLE-nested OpaqueLifetime<Consistency<DetSafe<NumericalTier<T>>>>"
              " must EBO-collapse to sizeof(T) — four regime-1 wrappers over "
              "four DISTINCT lattices.  If this fires, one of the four wrapper "
              "layers stopped using the EBO-friendly Graded substrate.");
static_assert(GradedWrapper<QuadrupleNested>);

using QuintupleNested =
    HotPath<HotPathTier_v::Hot,
            OpaqueLifetime<Lifetime_v::PER_FLEET,
                           Consistency<Consistency_v::STRONG,
                                       DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>>>>;
static_assert(sizeof(QuintupleNested) == sizeof(int), "QUINTUPLE-nested HotPath<OpaqueLifetime<Consistency<DetSafe<"
                                                      "NumericalTier<T>>>>> must EBO-collapse to sizeof(T) — five "
                                                      "regime-1 wrappers over five DISTINCT lattices.  If this fires, "
                                                      "one of the five wrapper layers stopped using the EBO-friendly "
                                                      "Graded substrate.");
static_assert(GradedWrapper<QuintupleNested>);

using SextupleNested =
    HotPath<HotPathTier_v::Hot,
            Wait<WaitStrategy_v::SpinPause,
                 OpaqueLifetime<Lifetime_v::PER_FLEET,
                                Consistency<Consistency_v::STRONG,
                                            DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>>>>>;
static_assert(sizeof(SextupleNested) == sizeof(int), "SEXTUPLE-nested HotPath<Wait<OpaqueLifetime<Consistency<"
                                                     "DetSafe<NumericalTier<T>>>>>> must EBO-collapse to sizeof(T) "
                                                     "— six regime-1 wrappers over six DISTINCT lattices.  If this "
                                                     "fires, one of the six wrapper layers stopped using the EBO-"
                                                     "friendly Graded substrate.");
static_assert(GradedWrapper<SextupleNested>);

using SeptupleNested = HotPath<
    HotPathTier_v::Hot,
    Wait<WaitStrategy_v::SpinPause,
         MemOrder<MemOrderTag_v::AcqRel,
                  OpaqueLifetime<Lifetime_v::PER_FLEET,
                                 Consistency<Consistency_v::STRONG,
                                             DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>>>>>>;
static_assert(sizeof(SeptupleNested) == sizeof(int), "SEPTUPLE-nested HotPath<Wait<MemOrder<OpaqueLifetime<"
                                                     "Consistency<DetSafe<NumericalTier<T>>>>>>> must EBO-collapse "
                                                     "to sizeof(T) — seven regime-1 wrappers over seven DISTINCT "
                                                     "lattices.  If this fires, one of the seven wrapper layers "
                                                     "stopped using the EBO-friendly Graded substrate.");
static_assert(GradedWrapper<SeptupleNested>);

using OctupleNested = HotPath<
    HotPathTier_v::Hot,
    Wait<WaitStrategy_v::SpinPause,
         MemOrder<MemOrderTag_v::AcqRel,
                  Progress<ProgressClass_v::Bounded,
                           OpaqueLifetime<
                               Lifetime_v::PER_FLEET,
                               Consistency<Consistency_v::STRONG,
                                           DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>>>>>>>;
static_assert(sizeof(OctupleNested) == sizeof(int), "OCTUPLE-nested HotPath<Wait<MemOrder<Progress<OpaqueLifetime<"
                                                    "Consistency<DetSafe<NumericalTier<T>>>>>>>> must EBO-collapse "
                                                    "to sizeof(T) — EIGHT regime-1 wrappers over EIGHT DISTINCT "
                                                    "lattices.  This is the Month-2 first-pass close — if this "
                                                    "fires, one of the eight wrapper layers stopped using the EBO-"
                                                    "friendly Graded substrate.");
static_assert(GradedWrapper<OctupleNested>);

using NonupleNested = HotPath<
    HotPathTier_v::Hot,
    Wait<
        WaitStrategy_v::SpinPause,
        MemOrder<MemOrderTag_v::AcqRel,
                 AllocClass<AllocClassTag_v::Stack,
                            Progress<ProgressClass_v::Bounded,
                                     OpaqueLifetime<Lifetime_v::PER_FLEET,
                                                    Consistency<Consistency_v::STRONG,
                                                                DetSafe<DetSafeTier_v::Pure,
                                                                        NumericalTier<Tolerance::BITEXACT, int>>>>>>>>>;
static_assert(sizeof(NonupleNested) == sizeof(int), "NONUPLE-nested HotPath<Wait<MemOrder<AllocClass<Progress<"
                                                    "OpaqueLifetime<Consistency<DetSafe<NumericalTier<T>>>>>>>>> "
                                                    "must EBO-collapse to sizeof(T) — NINE regime-1 wrappers over "
                                                    "NINE DISTINCT lattices.  If this fires, one of the nine "
                                                    "wrapper layers stopped using the EBO-friendly Graded substrate.");
static_assert(GradedWrapper<NonupleNested>);

using DecupleNested = HotPath<
    HotPathTier_v::Hot,
    Wait<WaitStrategy_v::SpinPause,
         MemOrder<MemOrderTag_v::AcqRel,
                  AllocClass<
                      AllocClassTag_v::Stack,
                      CipherTier<
                          CipherTierTag_v::Hot,
                          Progress<ProgressClass_v::Bounded,
                                   OpaqueLifetime<Lifetime_v::PER_FLEET,
                                                  Consistency<Consistency_v::STRONG,
                                                              DetSafe<DetSafeTier_v::Pure,
                                                                      NumericalTier<Tolerance::BITEXACT, int>>>>>>>>>>;
static_assert(sizeof(DecupleNested) == sizeof(int), "DECUPLE-nested HotPath<Wait<MemOrder<AllocClass<CipherTier<"
                                                    "Progress<OpaqueLifetime<Consistency<DetSafe<NumericalTier<T>"
                                                    ">>>>>>>>>> must EBO-collapse to sizeof(T) — TEN regime-1 "
                                                    "wrappers over TEN DISTINCT lattices.  If this fires, one of "
                                                    "the ten wrapper layers (most likely the just-shipped CipherTier) "
                                                    "stopped using the EBO-friendly Graded substrate.");
static_assert(GradedWrapper<DecupleNested>);

using UndecupleNested = HotPath<
    HotPathTier_v::Hot,
    Wait<WaitStrategy_v::SpinPause,
         MemOrder<
             MemOrderTag_v::AcqRel,
             AllocClass<
                 AllocClassTag_v::Stack,
                 CipherTier<CipherTierTag_v::Hot,
                            ResidencyHeat<ResidencyHeatTag_v::Hot,
                                          Progress<ProgressClass_v::Bounded,
                                                   OpaqueLifetime<Lifetime_v::PER_FLEET,
                                                                  Consistency<Consistency_v::STRONG,
                                                                              DetSafe<DetSafeTier_v::Pure,
                                                                                      NumericalTier<Tolerance::BITEXACT,
                                                                                                    int>>>>>>>>>>>;
static_assert(sizeof(UndecupleNested) == sizeof(int), "UNDECUPLE-nested HotPath<Wait<MemOrder<AllocClass<CipherTier<"
                                                      "ResidencyHeat<Progress<OpaqueLifetime<Consistency<DetSafe<"
                                                      "NumericalTier<T>>>>>>>>>>> must EBO-collapse to sizeof(T) — "
                                                      "ELEVEN regime-1 wrappers over ELEVEN DISTINCT lattices.  If "
                                                      "this fires, one of the eleven wrapper layers (most likely the "
                                                      "just-shipped ResidencyHeat) stopped using the EBO-friendly "
                                                      "Graded substrate.");
static_assert(GradedWrapper<UndecupleNested>);

using DuodecupleNested = HotPath<
    HotPathTier_v::Hot,
    Wait<WaitStrategy_v::SpinPause,
         MemOrder<
             MemOrderTag_v::AcqRel,
             AllocClass<
                 AllocClassTag_v::Stack,
                 CipherTier<
                     CipherTierTag_v::Hot,
                     ResidencyHeat<ResidencyHeatTag_v::Hot,
                                   Vendor<VendorBackend_v::NV,
                                          Progress<ProgressClass_v::Bounded,
                                                   OpaqueLifetime<Lifetime_v::PER_FLEET,
                                                                  Consistency<Consistency_v::STRONG,
                                                                              DetSafe<DetSafeTier_v::Pure,
                                                                                      NumericalTier<Tolerance::BITEXACT,
                                                                                                    int>>>>>>>>>>>>;
static_assert(sizeof(DuodecupleNested) == sizeof(int), "DUODECUPLE-nested HotPath<Wait<MemOrder<AllocClass<CipherTier<"
                                                       "ResidencyHeat<Vendor<Progress<OpaqueLifetime<Consistency<"
                                                       "DetSafe<NumericalTier<T>>>>>>>>>>>> must EBO-collapse to "
                                                       "sizeof(T) — TWELVE regime-1 wrappers over TWELVE DISTINCT "
                                                       "lattices, ELEVEN chain-shaped + ONE partial-order-shaped.  "
                                                       "If this fires, either (a) Vendor's regime-1 At<> singleton "
                                                       "EBO discipline regressed, or (b) the partial-order substrate "
                                                       "leaked non-empty grade bytes into the wrapper layout — both "
                                                       "would defeat the wrapper-nesting universal-vocabulary thesis.");
static_assert(GradedWrapper<DuodecupleNested>);

using TredecupleNested = HotPath<
    HotPathTier_v::Hot,
    Wait<WaitStrategy_v::SpinPause,
         MemOrder<
             MemOrderTag_v::AcqRel,
             AllocClass<AllocClassTag_v::Stack,
                        CipherTier<CipherTierTag_v::Hot,
                                   ResidencyHeat<
                                       ResidencyHeatTag_v::Hot,
                                       Vendor<VendorBackend_v::NV,
                                              Crash<CrashClass_v::NoThrow,
                                                    Progress<ProgressClass_v::Bounded,
                                                             OpaqueLifetime<
                                                                 Lifetime_v::PER_FLEET,
                                                                 Consistency<Consistency_v::STRONG,
                                                                             DetSafe<DetSafeTier_v::Pure,
                                                                                     NumericalTier<Tolerance::BITEXACT,
                                                                                                   int>>>>>>>>>>>>>;
static_assert(sizeof(TredecupleNested) == sizeof(int), "TREDECUPLE-nested HotPath<Wait<MemOrder<AllocClass<CipherTier<"
                                                       "ResidencyHeat<Vendor<Crash<Progress<OpaqueLifetime<Consistency<"
                                                       "DetSafe<NumericalTier<T>>>>>>>>>>>>> must EBO-collapse to "
                                                       "sizeof(T) — THIRTEEN regime-1 wrappers over THIRTEEN DISTINCT "
                                                       "lattices.");
static_assert(GradedWrapper<TredecupleNested>);

using QuattuordecupleNested = HotPath<
    HotPathTier_v::Hot,
    Wait<WaitStrategy_v::SpinPause,
         MemOrder<
             MemOrderTag_v::AcqRel,
             AllocClass<
                 AllocClassTag_v::Stack,
                 CipherTier<CipherTierTag_v::Hot,
                            ResidencyHeat<
                                ResidencyHeatTag_v::Hot,
                                Vendor<VendorBackend_v::NV,
                                       Crash<CrashClass_v::NoThrow,
                                             Progress<ProgressClass_v::Bounded,
                                                      OpaqueLifetime<
                                                          Lifetime_v::PER_FLEET,
                                                          Consistency<Consistency_v::STRONG,
                                                                      DetSafe<DetSafeTier_v::Pure,
                                                                              NumericalTier<Tolerance::BITEXACT,
                                                                                            Budgeted<int>>>>>>>>>>>>>>;
static_assert(sizeof(QuattuordecupleNested) == sizeof(Budgeted<int>),
              "QUATTUORDECUPLE-nested wrapper stack must EBO-collapse the "
              "outer thirteen regime-1 wrappers and carry only the inner "
              "Budgeted's regime-4 grade at the layout root.  If this fires, "
              "one of the chain/partial-order wrappers regressed regime-1 "
              "EBO collapse, OR Budgeted's layout invariant changed.");
static_assert(GradedWrapper<QuattuordecupleNested>);

// The per-wrapper self-tests cover this behavior in depth.  These
// smoke checks repeat it to confirm that the wrappers still behave
// the same way when one translation unit compiles all of them
// together.

void runtime_smoke_linear() {
    Linear<int> x{42};
    if (x.peek() != 42) std::abort();
    int y = std::move(x).consume();
    if (y != 42) std::abort();
}

void runtime_smoke_refined() {
    Refined<positive_local, int> r{7};
    if (r.value() != 7) std::abort();
    int v = std::move(r).into();
    if (v != 7) std::abort();
}

void runtime_smoke_sealed_refined() {
    SealedRefined<positive_local, int> s{42};
    if (s.value() != 42) std::abort();
    // Sealing removes into(), so there is nothing more to call here.

    Refined<positive_local, int> r{7};
    SealedRefined<positive_local, int> sealed_from_r{std::move(r)};
    if (sealed_from_r.value() != 7) std::abort();
}

void runtime_smoke_tagged() {
    Tagged<int, VerificationTag> t{99};
    if (t.value() != 99) std::abort();
    int v = std::move(t).into();
    if (v != 99) std::abort();
}

void runtime_smoke_secret() {
    Secret<int> s{0xCAFE};
    // declassify accepts only a policy derived from the shipped
    // policy base.  A local struct is rejected.
    int v = std::move(s).declassify<secret_policy::AuditedLogging>();
    if (v != 0xCAFE) std::abort();
}

void runtime_smoke_numerical_tier() {
    NumericalTier<Tolerance::BITEXACT, int> bx{42};
    if (bx.peek() != 42) std::abort();

    auto fp16 = bx.relax<Tolerance::ULP_FP16>();  // const& form
    if (fp16.peek() != 42) std::abort();
    if (fp16.tier != Tolerance::ULP_FP16) std::abort();

    auto relaxed = std::move(fp16).relax<Tolerance::RELAXED>();  // && form
    if (relaxed.peek() != 42) std::abort();

    static_assert(NumericalTier<Tolerance::BITEXACT, int>::satisfies<Tolerance::ULP_FP16>);
    static_assert(!NumericalTier<Tolerance::ULP_FP16, int>::satisfies<Tolerance::BITEXACT>);

    NumericalTier<Tolerance::BITEXACT, int> a{10};
    NumericalTier<Tolerance::BITEXACT, int> b{20};
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();
    a.swap(b);
    if (a.peek() != 20 || b.peek() != 100) std::abort();

    NumericalTier<Tolerance::BITEXACT, int> eq_a{42};
    NumericalTier<Tolerance::BITEXACT, int> eq_b{42};
    NumericalTier<Tolerance::BITEXACT, int> eq_c{43};
    if (!(eq_a == eq_b)) std::abort();
    if (eq_a == eq_c) std::abort();

    Tagged<NumericalTier<Tolerance::BITEXACT, int>, VerificationTag> tagged_bx{
        NumericalTier<Tolerance::BITEXACT, int>{77}};
    auto unwrapped_bx = std::move(tagged_bx).into();
    if (unwrapped_bx.peek() != 77) std::abort();
}

void runtime_smoke_consistency() {
    Consistency<Consistency_v::STRONG, int> strong{42};
    if (strong.peek() != 42) std::abort();

    auto causal = strong.relax<Consistency_v::CAUSAL_PREFIX>();
    if (causal.peek() != 42) std::abort();
    if (causal.level != Consistency_v::CAUSAL_PREFIX) std::abort();

    auto eventual = std::move(causal).relax<Consistency_v::EVENTUAL>();
    if (eventual.peek() != 42) std::abort();

    static_assert(Consistency<Consistency_v::STRONG, int>::satisfies<Consistency_v::CAUSAL_PREFIX>);
    static_assert(!Consistency<Consistency_v::EVENTUAL, int>::satisfies<Consistency_v::STRONG>);

    Consistency<Consistency_v::STRONG, int> a{10};
    Consistency<Consistency_v::STRONG, int> b{20};
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();
    a.swap(b);
    if (a.peek() != 20 || b.peek() != 100) std::abort();

    Consistency<Consistency_v::STRONG, int> eq_a{42};
    Consistency<Consistency_v::STRONG, int> eq_b{42};
    Consistency<Consistency_v::STRONG, int> eq_c{43};
    if (!(eq_a == eq_b)) std::abort();
    if (eq_a == eq_c) std::abort();

    ConsistencyOverNumerical nested{NumericalTier<Tolerance::BITEXACT, int>{55}};
    auto inner_after_consume = std::move(nested).consume();
    if (inner_after_consume.peek() != 55) std::abort();
}

void runtime_smoke_opaque_lifetime() {
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> fleet{42};
    if (fleet.peek() != 42) std::abort();

    auto program = fleet.relax<Lifetime_v::PER_PROGRAM>();
    if (program.peek() != 42) std::abort();
    if (program.scope != Lifetime_v::PER_PROGRAM) std::abort();

    auto request = std::move(program).relax<Lifetime_v::PER_REQUEST>();
    if (request.peek() != 42) std::abort();

    static_assert(OpaqueLifetime<Lifetime_v::PER_FLEET, int>::satisfies<Lifetime_v::PER_REQUEST>);
    static_assert(!OpaqueLifetime<Lifetime_v::PER_REQUEST, int>::satisfies<Lifetime_v::PER_FLEET>);

    OpaqueLifetime<Lifetime_v::PER_FLEET, int> a{10};
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> b{20};
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();
    a.swap(b);
    if (a.peek() != 20 || b.peek() != 100) std::abort();

    OpaqueLifetime<Lifetime_v::PER_FLEET, int> eq_a{42};
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> eq_b{42};
    OpaqueLifetime<Lifetime_v::PER_FLEET, int> eq_c{43};
    if (!(eq_a == eq_b)) std::abort();
    if (eq_a == eq_c) std::abort();

    TripleNested triple{Consistency<Consistency_v::STRONG, NumericalTier<Tolerance::BITEXACT, int>>{
        NumericalTier<Tolerance::BITEXACT, int>{77}}};
    auto consistency_layer = std::move(triple).consume();  // peel OpaqueLifetime
    auto numerical_layer = std::move(consistency_layer).consume();  // peel Consistency
    if (numerical_layer.peek() != 77) std::abort();
}

void runtime_smoke_det_safe() {
    DetSafe<DetSafeTier_v::Pure, int> pure{42};
    if (pure.peek() != 42) std::abort();

    auto philox = pure.relax<DetSafeTier_v::PhiloxRng>();
    if (philox.peek() != 42) std::abort();
    if (philox.tier != DetSafeTier_v::PhiloxRng) std::abort();

    auto mono = std::move(philox).relax<DetSafeTier_v::MonotonicClockRead>();
    if (mono.peek() != 42) std::abort();

    static_assert(DetSafe<DetSafeTier_v::Pure, int>::satisfies<DetSafeTier_v::PhiloxRng>);
    static_assert(!DetSafe<DetSafeTier_v::MonotonicClockRead, int>::satisfies<DetSafeTier_v::PhiloxRng>);

    DetSafe<DetSafeTier_v::Pure, int> a{10};
    DetSafe<DetSafeTier_v::Pure, int> b{20};
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();
    a.swap(b);
    if (a.peek() != 20 || b.peek() != 100) std::abort();

    DetSafe<DetSafeTier_v::Pure, int> eq_a{42};
    DetSafe<DetSafeTier_v::Pure, int> eq_b{42};
    DetSafe<DetSafeTier_v::Pure, int> eq_c{43};
    if (!(eq_a == eq_b)) std::abort();
    if (eq_a == eq_c) std::abort();

    QuadrupleNested quad{
        Consistency<Consistency_v::STRONG, DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>>{
            DetSafe<DetSafeTier_v::Pure, NumericalTier<Tolerance::BITEXACT, int>>{
                NumericalTier<Tolerance::BITEXACT, int>{99}}}};
    auto layer3 = std::move(quad).consume();  // peel OpaqueLifetime
    auto layer2 = std::move(layer3).consume();  // peel Consistency
    auto layer1 = std::move(layer2).consume();  // peel DetSafe
    if (layer1.peek() != 99) std::abort();  // bare NumericalTier value
}

void runtime_smoke_monotonic() {
    Monotonic<std::uint64_t> m{10};
    if (m.get() != 10) std::abort();
    m.advance(20);
    if (m.get() != 20) std::abort();
    if (!m.try_advance(20)) std::abort();  // leq holds for equal
    if (m.try_advance(15)) std::abort();  // backward fails
}

void runtime_smoke_append_only() {
    AppendOnly<int> a{};
    a.append(1);
    a.append(2);
    a.append(3);
    if (a.size() != 3) std::abort();
    if (a[0] != 1 || a[1] != 2 || a[2] != 3) std::abort();
}

// The token carries no state, so the pool is what a runtime check
// can observe.
void runtime_smoke_shared_permission() {
    auto exclusive = mint_permission_root<VerificationTag>();
    SharedPermissionPool<VerificationTag> pool{std::move(exclusive)};

    auto guard1 = pool.lend();
    if (!guard1) std::abort();
    [[maybe_unused]] SharedPermission<VerificationTag> tok = guard1->token();
    if (pool.outstanding() != 1) std::abort();

    guard1.reset();
    if (pool.outstanding() != 0) std::abort();

    // No shares are outstanding, so the upgrade must succeed.
    auto upgraded = pool.try_upgrade();
    if (!upgraded) std::abort();
    pool.deposit_exclusive(std::move(*upgraded));
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_migration_verification:\n");

    runtime_smoke_linear();
    std::fprintf(stderr, "  linear:      OK\n");
    runtime_smoke_refined();
    std::fprintf(stderr, "  refined:           OK\n");
    runtime_smoke_sealed_refined();
    std::fprintf(stderr, "  sealed_refined:    OK\n");
    runtime_smoke_tagged();
    std::fprintf(stderr, "  tagged:      OK\n");
    runtime_smoke_secret();
    std::fprintf(stderr, "  secret:      OK\n");
    runtime_smoke_numerical_tier();
    std::fprintf(stderr, "  numerical_tier:    OK\n");
    runtime_smoke_consistency();
    std::fprintf(stderr, "  consistency:       OK\n");
    runtime_smoke_opaque_lifetime();
    std::fprintf(stderr, "  opaque_lifetime:   OK\n");
    runtime_smoke_det_safe();
    std::fprintf(stderr, "  det_safe:          OK\n");
    runtime_smoke_monotonic();
    std::fprintf(stderr, "  monotonic:   OK\n");
    runtime_smoke_append_only();
    std::fprintf(stderr, "  append_only:       OK\n");
    runtime_smoke_shared_permission();
    std::fprintf(stderr, "  shared_permission: OK\n");

    std::fprintf(stderr, "\nALL PASSED — 14 migrated wrappers verified uniformly "
                         "(13 Graded-backed + 1 façade)\n");
    return EXIT_SUCCESS;
}
