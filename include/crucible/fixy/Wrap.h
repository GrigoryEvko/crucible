#pragma once

// Every value-level safety wrapper is surfaced under `fixy::wrap::` so
// a caller who includes only the fixy umbrella never has to descend
// into the safety tree to wrap a value.
//
// Linear, Secret and SharedPermission are also reachable through the
// token-mint umbrellas.  Both paths must name the same substrate
// symbol.  A translation unit that opens both namespaces compiles only
// while they agree, and divergence surfaces as a lookup error rather
// than as two distinct types.  The self-test at the end of this file
// pins the agreement.

#include <crucible/effects/_Computation.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/_Affine.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/IsBorrowedRef.h>
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/Saturated.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/Cyclic.h>
#include <crucible/safety/CyclicBuffer.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/_Pinned.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_RefinedAlgebra.h>
#include <crucible/fixy/wrap/Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/_SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/SwissTableBuffer.h>
#include <crucible/safety/SwmrReader.h>
#include <crucible/safety/SwmrWriter.h>
#include <crucible/safety/SignatureTraits.h>
#include <crucible/safety/GradedExtract.h>
#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Hw.h>
#include <crucible/safety/JoinPolicy.h>
#include <crucible/safety/CallShape.h>
#include <crucible/safety/ControlFlow.h>
#include <crucible/safety/GlobalState.h>
#include <crucible/safety/StackUse.h>
#include <crucible/safety/Stdio.h>
#include <crucible/safety/ThreadLocalRef.h>
#include <crucible/safety/SuspendBehavior.h>
#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Path.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/WeakRef.h>
#include <crucible/safety/Witness.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::wrap {

using ::crucible::safety::Linear;
using ::crucible::safety::mint_linear;
using ::crucible::safety::drop;

using ::crucible::safety::Affine;
using ::crucible::safety::mint_affine;
using ::crucible::safety::is_already_consume_disciplined;
using ::crucible::safety::is_already_consume_disciplined_v;

// The Refined family arrives through the granular sub-header included
// above rather than being re-declared here.  Foundational consumers
// include that sub-header directly, which keeps them clear of this
// umbrella's transitive pull on the arena-backed region wrapper.

using ::crucible::safety::SealedRefined;
using ::crucible::safety::mint_sealed_refined;

using ::crucible::safety::Tagged;
using ::crucible::safety::mint_tagged;

template <typename Source>
using Path = ::crucible::safety::Path<Source>;
using ::crucible::safety::PathTraversal;
using ::crucible::safety::PathTraversalError;
using ::crucible::safety::sanitize_path;
using ::crucible::safety::MAX_PATH_BYTES;

using ::crucible::safety::Secret;
using ::crucible::safety::mint_secret;

using ::crucible::safety::Monotonic;

using ::crucible::safety::AppendOnly;

using ::crucible::safety::Stale;

using ::crucible::safety::TimeOrdered;

using ::crucible::safety::Borrowed;
using ::crucible::safety::BorrowedRef;

using ::crucible::safety::WeakRef;

using ::crucible::safety::Saturated;

using ::crucible::safety::add_sat_checked;
using ::crucible::safety::sub_sat_checked;
using ::crucible::safety::mul_sat_checked;

// The substrate for SharedPermission lives in the permissions tree and
// is re-exported into the safety namespace, so it is reachable by the
// same qualified spelling as every other entry here.
using ::crucible::safety::SharedPermission;
using ::crucible::safety::mint_permission_share;

using ::crucible::safety::WriteOnce;

using ::crucible::safety::WriteOnceNonNull;

using ::crucible::safety::BoundedMonotonic;

using ::crucible::safety::OrderedAppendOnly;

using ::crucible::safety::AtomicMonotonic;

// Each band below exports the wrapper, its lattice and its tier enum.
// The short per-tier spellings are not re-exported here: the friendly
// ones live in the per-wrapper sub-namespaces aliased at the end of
// this file, and the abbreviated ones are substrate-private.

using ::crucible::safety::HotPath;
using ::crucible::safety::HotPathLattice;
using ::crucible::safety::HotPathTier_v;

using ::crucible::safety::DetSafe;
using ::crucible::safety::DetSafeLattice;
using ::crucible::safety::DetSafeTier_v;

using ::crucible::safety::NumericalTier;
using ::crucible::safety::Tolerance;
using ::crucible::safety::ToleranceLattice;

using ::crucible::safety::Vendor;
using ::crucible::safety::VendorLattice;
using ::crucible::safety::VendorBackend_v;

using ::crucible::safety::ResidencyHeat;
using ::crucible::safety::ResidencyHeatLattice;
using ::crucible::safety::ResidencyHeatTag_v;

using ::crucible::safety::CipherTier;
using ::crucible::safety::CipherTierLattice;
using ::crucible::safety::CipherTierTag_v;

using ::crucible::safety::AllocClass;
using ::crucible::safety::AllocClassLattice;
using ::crucible::safety::AllocClassTag_v;

using ::crucible::safety::Wait;
using ::crucible::safety::WaitLattice;
using ::crucible::safety::WaitStrategy_v;

using ::crucible::safety::MemOrder;
using ::crucible::safety::MemOrderLattice;
using ::crucible::safety::MemOrderTag_v;

using ::crucible::safety::Progress;
using ::crucible::safety::ProgressLattice;
using ::crucible::safety::ProgressClass_v;

using ::crucible::effects::Computation;

using ::crucible::safety::Consistency;
using ::crucible::safety::ConsistencyLattice;
using ::crucible::safety::Consistency_v;

using ::crucible::safety::OpaqueLifetime;
using ::crucible::safety::LifetimeLattice;
using ::crucible::safety::Lifetime_v;

using ::crucible::safety::Crash;
using ::crucible::safety::CrashLattice;
using ::crucible::safety::CrashClass_v;

using ::crucible::safety::Budgeted;
using ::crucible::safety::BitsBudget;
using ::crucible::safety::BitsBudgetLattice;
using ::crucible::safety::PeakBytes;
using ::crucible::safety::PeakBytesLattice;

using ::crucible::safety::EpochVersioned;
using ::crucible::safety::Epoch;
using ::crucible::safety::EpochLattice;
using ::crucible::safety::Generation;
using ::crucible::safety::GenerationLattice;

using ::crucible::safety::NumaPlacement;
using ::crucible::safety::AffinityLattice;
using ::crucible::safety::AffinityMask;
using ::crucible::safety::NumaNodeId;
using ::crucible::safety::NumaNodeLattice;

using ::crucible::safety::RecipeSpec;
using ::crucible::safety::RecipeFamily;
using ::crucible::safety::RecipeFamilyLattice;

using ::crucible::safety::Witness;
using ::crucible::safety::WitnessLattice;
using ::crucible::safety::Witness_v;
using ::crucible::safety::mint_witness;

template <typename T>
using Unwitnessed = ::crucible::safety::Witness<::crucible::safety::Witness_v::UNWITNESSED, T>;
template <typename T>
using TypeChecked = ::crucible::safety::Witness<::crucible::safety::Witness_v::TYPE_CHECKED, T>;
template <typename T>
using TestPassed = ::crucible::safety::Witness<::crucible::safety::Witness_v::TEST_PASSED, T>;
template <typename T>
using FormallyVerified = ::crucible::safety::Witness<::crucible::safety::Witness_v::FORMALLY_VERIFIED, T>;

using ::crucible::safety::Hw;
using ::crucible::safety::HwInstructionLattice;
using ::crucible::safety::HwInstruction_v;
using ::crucible::safety::mint_hw;

using ::crucible::safety::BarrierGuarded;
using ::crucible::safety::BarrierStrengthLattice;
using ::crucible::safety::BarrierStrength_v;
using ::crucible::safety::mint_barrier_guarded;

using ::crucible::safety::SimdWidthPinned;
using ::crucible::safety::SimdIsaLattice;
using ::crucible::safety::SimdIsa_v;
using ::crucible::safety::mint_simd_width_pinned;

using ::crucible::safety::JoinPolicy;
using ::crucible::safety::JoinPolicyLattice;
using ::crucible::safety::JoinPolicy_v;
using ::crucible::safety::mint_join_policy;

using ::crucible::safety::SuspendBehavior;
using ::crucible::safety::SuspendBehaviorLattice;
using ::crucible::safety::SuspendBehavior_v;
using ::crucible::safety::mint_suspend_behavior;

// The five effect-surface bands below break the naming pattern of the
// bands above: the wrapper carries a `Pinned` suffix while the tier
// enum keeps the bare axis name and drops the `_v` suffix.

using ::crucible::safety::CallShape;
using ::crucible::safety::CallShapeLattice;
using ::crucible::safety::CallShapePinned;
using ::crucible::safety::mint_call_shape;

using ::crucible::safety::ControlFlow;
using ::crucible::safety::ControlFlowLattice;
using ::crucible::safety::ControlFlowPinned;
using ::crucible::safety::mint_control_flow;

using ::crucible::safety::GlobalState;
using ::crucible::safety::GlobalStateLattice;
using ::crucible::safety::GlobalStatePinned;
using ::crucible::safety::mint_global_state;

using ::crucible::safety::StackUse;
using ::crucible::safety::StackUseLattice;
using ::crucible::safety::StackUsePinned;
using ::crucible::safety::mint_stack_use;

using ::crucible::safety::Stdio;
using ::crucible::safety::StdioLattice;
using ::crucible::safety::StdioPinned;
using ::crucible::safety::mint_stdio;

using ::crucible::safety::Pinned;
using ::crucible::safety::NonMovable;

using ::crucible::safety::ScopedView;
using ::crucible::safety::mint_view;
using ::crucible::safety::no_scoped_view_field_check;
using ::crucible::safety::extract::IsBorrowedRef;

using ::crucible::safety::ThreadLocalRef;
using ::crucible::safety::mint_thread_local_ref;

using ::crucible::safety::extract::SwmrReader;
using ::crucible::safety::extract::is_swmr_reader_function_v;
using ::crucible::safety::extract::swmr_reader_handle_value_t;
using ::crucible::safety::extract::swmr_reader_returned_value_t;
using ::crucible::safety::extract::swmr_reader_value_consistent_v;
using ::crucible::safety::extract::IsSwmrReader;
using ::crucible::safety::extract::is_swmr_reader_v;
using ::crucible::safety::extract::swmr_reader_value_t;

using ::crucible::safety::extract::SwmrWriter;
using ::crucible::safety::extract::is_swmr_writer_function_v;
using ::crucible::safety::extract::swmr_writer_handle_value_t;
using ::crucible::safety::extract::swmr_writer_published_value_t;
using ::crucible::safety::extract::swmr_writer_value_consistent_v;
using ::crucible::safety::extract::IsSwmrWriter;
using ::crucible::safety::extract::is_swmr_writer_v;
using ::crucible::safety::extract::swmr_writer_value_t;

using ::crucible::safety::extract::signature_traits;
using ::crucible::safety::extract::param_type_t;
using ::crucible::safety::extract::return_type_t;
using ::crucible::safety::extract::function_type_t;
using ::crucible::safety::extract::arity_v;
using ::crucible::safety::extract::is_noexcept_v;

using ::crucible::safety::extract::value_type_of_t;
using ::crucible::safety::extract::lattice_of_t;
using ::crucible::safety::extract::grade_of_t;
using ::crucible::safety::extract::graded_type_of_t;
using ::crucible::safety::extract::modality_of_v;
using ::crucible::safety::extract::is_graded_wrapper_v;
using ::crucible::safety::extract::is_graded_specialization_v;

using ::crucible::safety::OwnedRegion;

using ::crucible::safety::FixedArray;

using ::crucible::safety::Cyclic;

using ::crucible::safety::CyclicBuffer;

using ::crucible::safety::NotInherited;
using ::crucible::safety::assert_not_inherited;
using ::crucible::safety::FinalBy;

using ::crucible::safety::Bits;

using ::crucible::safety::SwissTableBuffer;

namespace ct {
using ::crucible::safety::ct::mask_from_bit;
using ::crucible::safety::ct::select;
using ::crucible::safety::ct::eq;
using ::crucible::safety::ct::less;
using ::crucible::safety::ct::is_zero;
using ::crucible::safety::ct::cswap;
}  // namespace ct

// A wrapper appears below only when the substrate gives it a per-tier
// sub-namespace of convenience aliases.  This list is shorter than the
// list of wrappers above for that reason.  The absence of a name here
// tells you nothing about whether this header exports the wrapper.

namespace hot_path = ::crucible::safety::hot_path;
namespace det_safe = ::crucible::safety::det_safe;
namespace numerical_tier = ::crucible::safety::numerical_tier;
namespace vendor = ::crucible::safety::vendor;
namespace residency_heat = ::crucible::safety::residency_heat;
namespace cipher_tier = ::crucible::safety::cipher_tier;
namespace alloc_class = ::crucible::safety::alloc_class;
namespace wait = ::crucible::safety::wait;
namespace mem_order = ::crucible::safety::mem_order;
namespace progress = ::crucible::safety::progress;
namespace consistency = ::crucible::safety::consistency;
namespace opaque_lifetime = ::crucible::safety::opaque_lifetime;
namespace crash = ::crucible::safety::crash;

}  // namespace crucible::fixy::wrap

#include <crucible/fixy/Fs.h>

namespace crucible::fixy::wrap::fs {

namespace open_mode = ::crucible::fixy::fs::open_mode;
namespace flag = ::crucible::fixy::fs::flag;
namespace sync_op = ::crucible::fixy::fs::sync_op;
namespace atomicity = ::crucible::fixy::fs::atomicity;

namespace grant = ::crucible::fixy::grant::fs;

template <typename Source>
using Path = ::crucible::fixy::fs::Path<Source>;
using ::crucible::fixy::fs::Dirfd;
using ::crucible::fixy::fs::open_dirfd;
using ::crucible::fixy::fs::CtxAdmitsIoBlock;
using ::crucible::fixy::fs::CtxFitsFileMint;
using ::crucible::fixy::fs::CtxFitsSync;
using ::crucible::fixy::fs::CtxFitsCommitAtomic;

using ::crucible::fixy::fs::mint_file;
using ::crucible::fixy::fs::sync;
using ::crucible::fixy::fs::commit_atomic;
using ::crucible::fixy::fs::read_only;
using ::crucible::fixy::fs::mint_durable_truncate_file;
using ::crucible::fixy::fs::mint_durable_append_file;

}  // namespace crucible::fixy::wrap::fs

#include <crucible/fixy/Mmap.h>

namespace crucible::fixy::wrap::mmap {

namespace prot = ::crucible::fixy::mmap::prot;
namespace share = ::crucible::fixy::mmap::share;
namespace advice = ::crucible::fixy::mmap::advice;

namespace grant = ::crucible::fixy::grant::mmap;

template <typename Tag, typename Prot, typename Share>
using OwnedMmap = ::crucible::fixy::mmap::OwnedMmap<Tag, Prot, Share>;

using ::crucible::fixy::mmap::CtxAdmitsIoBlock;
using ::crucible::fixy::mmap::CtxFitsMmapMint;
using ::crucible::fixy::mmap::CtxFitsAnonMmapMint;
using ::crucible::fixy::mmap::CtxFitsSafeAdvise;
using ::crucible::fixy::mmap::CtxFitsReleaseAwareAdvise;

using ::crucible::fixy::mmap::mint_mmap;
using ::crucible::fixy::mmap::mint_mmap_anon;
using ::crucible::fixy::mmap::advise;
using ::crucible::fixy::mmap::advise_release_aware;

}  // namespace crucible::fixy::wrap::mmap

#include <crucible/fixy/Io.h>

namespace crucible::fixy::wrap::io {

namespace engine = ::crucible::fixy::io::engine;
namespace zerocopy = ::crucible::fixy::io::zerocopy;
namespace ring_flag = ::crucible::fixy::io::ring_flag;

namespace grant = ::crucible::fixy::grant::io;

using IoUringRing = ::crucible::fixy::io::IoUringRing;

using ::crucible::fixy::io::CtxAdmitsIoBlock;
using ::crucible::fixy::io::CtxFitsIoUringMint;
using ::crucible::fixy::io::CtxFitsZerocopyMint;

using ::crucible::fixy::io::mint_io_uring_ring;
using ::crucible::fixy::io::mint_zerocopy_transfer;

}  // namespace crucible::fixy::wrap::io

#include <crucible/fixy/Cipher.h>

namespace crucible::fixy::wrap::cipher {

using ::crucible::fixy::cipher::IsCipherWarmWriterStance;
using ::crucible::fixy::cipher::IsCipherColdWriterStance;
using ::crucible::fixy::cipher::IsHeadAdvanceStance;

using ::crucible::fixy::cipher::engages_warm_writer_stance_v;
using ::crucible::fixy::cipher::engages_cold_writer_stance_v;
using ::crucible::fixy::cipher::engages_head_advance_stance_v;

using ::crucible::fixy::cipher::CipherWarmWriterStance;
using ::crucible::fixy::cipher::CipherColdWriterStance;
using ::crucible::fixy::cipher::HeadAdvanceStance;

using ::crucible::fixy::cipher::stance_pack_satisfies_warm_v;
using ::crucible::fixy::cipher::stance_pack_satisfies_cold_v;
using ::crucible::fixy::cipher::stance_pack_satisfies_head_v;

using ::crucible::fixy::cipher::pack;

}  // namespace crucible::fixy::wrap::cipher

#include <crucible/fixy/CipherDurable.h>

namespace crucible::fixy::wrap::cipher::durable {

using ::crucible::fixy::cipher::durable::warm_writer_stance;
using ::crucible::fixy::cipher::durable::cold_writer_stance;
using ::crucible::fixy::cipher::durable::head_advance_stance;

using ::crucible::fixy::cipher::durable::CipherDurableHandle;

using ::crucible::fixy::cipher::durable::CtxFitsWarmWriterMint;
using ::crucible::fixy::cipher::durable::CtxFitsColdWriterMint;
using ::crucible::fixy::cipher::durable::CtxFitsHeadAdvancerMint;

using ::crucible::fixy::cipher::durable::mint_warm_writer;
using ::crucible::fixy::cipher::durable::mint_cold_writer;
using ::crucible::fixy::cipher::durable::mint_head_advancer;

}  // namespace crucible::fixy::wrap::cipher::durable

// These asserts pin each alias to its substrate origin.  A name that
// resolves is not the same claim as a name that resolves to the right
// entity.  A using-declaration that names the wrong symbol still
// compiles at every call site that needs only the name.  This check
// fires before any caller compiles.

namespace crucible::fixy::wrap::self_test {

static_assert(std::is_same_v<::crucible::fixy::wrap::Linear<int>, ::crucible::safety::Linear<int>>,
              "fixy::wrap::Linear must alias safety::Linear — dual-export drift.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Refined<::crucible::safety::positive, int>,
                             ::crucible::safety::Refined<::crucible::safety::positive, int>>,
              "fixy::wrap::Refined must alias safety::Refined — dual-export drift.");
static_assert(std::is_same_v<::crucible::fixy::wrap::SealedRefined<::crucible::safety::positive, int>,
                             ::crucible::safety::SealedRefined<::crucible::safety::positive, int>>,
              "fixy::wrap::SealedRefined must alias safety::SealedRefined.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Tagged<int, ::crucible::safety::source::FromUser>,
                             ::crucible::safety::Tagged<int, ::crucible::safety::source::FromUser>>,
              "fixy::wrap::Tagged must alias safety::Tagged.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Secret<int>, ::crucible::safety::Secret<int>>,
              "fixy::wrap::Secret must alias safety::Secret.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::Monotonic<std::uint64_t>, ::crucible::safety::Monotonic<std::uint64_t>>,
    "fixy::wrap::Monotonic must alias safety::Monotonic.");
static_assert(std::is_same_v<::crucible::fixy::wrap::AppendOnly<int>, ::crucible::safety::AppendOnly<int>>,
              "fixy::wrap::AppendOnly must alias safety::AppendOnly.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Stale<int>, ::crucible::safety::Stale<int>>,
              "fixy::wrap::Stale must alias safety::Stale.");

static_assert(std::is_same_v<::crucible::fixy::wrap::LinearRefined<::crucible::safety::positive, int>,
                             ::crucible::safety::LinearRefined<::crucible::safety::positive, int>>,
              "fixy::wrap::LinearRefined must alias safety::LinearRefined.");
static_assert(std::is_same_v<::crucible::fixy::wrap::RefinedLinear<::crucible::safety::positive, int>,
                             ::crucible::safety::RefinedLinear<::crucible::safety::positive, int>>,
              "fixy::wrap::RefinedLinear must alias safety::RefinedLinear.");

static_assert(std::is_same_v<::crucible::fixy::wrap::NonZero<int>, ::crucible::safety::NonZero<int>>,
              "fixy::wrap::NonZero must alias safety::NonZero — dual-export drift.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::NonEmpty<std::span<int>>, ::crucible::safety::NonEmpty<std::span<int>>>,
    "fixy::wrap::NonEmpty must alias safety::NonEmpty — dual-export drift.");
static_assert(std::is_same_v<::crucible::fixy::wrap::NonEmptySpan<int>, ::crucible::safety::NonEmptySpan<int>>,
              "fixy::wrap::NonEmptySpan must alias safety::NonEmptySpan — dual-export drift.");

// Each parameterised alias below carries a witness at two argument
// values.  A single value would still pass if a later constraint on
// the alias rejected only part of the argument range.
static_assert(std::is_same_v<::crucible::fixy::wrap::MinLength<1, std::span<int>>,
                             ::crucible::safety::MinLength<1, std::span<int>>>,
              "fixy::wrap::MinLength<1, ...> must alias safety::MinLength<1, ...>.");
static_assert(std::is_same_v<::crucible::fixy::wrap::MinLength<8, std::span<int>>,
                             ::crucible::safety::MinLength<8, std::span<int>>>,
              "fixy::wrap::MinLength<8, ...> must alias safety::MinLength<8, ...> "
              "— witness propagation through parameter pack at a non-trivial N.");
static_assert(std::is_same_v<::crucible::fixy::wrap::MaxBounded<255u, unsigned int>,
                             ::crucible::safety::MaxBounded<255u, unsigned int>>,
              "fixy::wrap::MaxBounded<255u, ...> must alias safety::MaxBounded<255u, ...>.");
static_assert(std::is_same_v<::crucible::fixy::wrap::MaxBounded<128u, unsigned int>,
                             ::crucible::safety::MaxBounded<128u, unsigned int>>,
              "fixy::wrap::MaxBounded<128u, ...> must alias safety::MaxBounded<128u, ...> "
              "— witness propagation through parameter pack at a saturation-counter N.");

static_assert(std::is_same_v<::crucible::fixy::wrap::AlignedTo<64, int*>, ::crucible::safety::AlignedTo<64, int*>>,
              "fixy::wrap::AlignedTo<64, ...> must alias safety::AlignedTo<64, ...> "
              "— cache-line-aligned cardinality witness.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::AlignedTo<4096, void*>, ::crucible::safety::AlignedTo<4096, void*>>,
    "fixy::wrap::AlignedTo<4096, ...> must alias safety::AlignedTo<4096, ...> "
    "— page-aligned cardinality witness.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::WithinRange<0, 100, int>, ::crucible::safety::WithinRange<0, 100, int>>,
    "fixy::wrap::WithinRange<0, 100, ...> must alias safety::WithinRange<0, 100, ...> "
    "— closed-interval cardinality witness.");
static_assert(std::is_same_v<::crucible::fixy::wrap::WithinRange<-128, 127, std::int8_t>,
                             ::crucible::safety::WithinRange<-128, 127, std::int8_t>>,
              "fixy::wrap::WithinRange<-128, 127, ...> must alias safety::WithinRange<-128, 127, ...> "
              "— signed NTTP witness, covers the int8_t representable-range case.");

// SharedPermission is reachable through this namespace and through the
// permission umbrella.  Both paths must land on one substrate type.
struct WrapDualExportTag {};
static_assert(std::is_same_v<::crucible::fixy::wrap::SharedPermission<WrapDualExportTag>,
                             ::crucible::safety::SharedPermission<WrapDualExportTag>>,
              "fixy::wrap::SharedPermission must alias safety::SharedPermission "
              "— this dual-export must agree with the fixy::perm:: parallel path.");

static_assert(std::is_same_v<::crucible::fixy::wrap::WriteOnce<int>, ::crucible::safety::WriteOnce<int>>,
              "fixy::wrap::WriteOnce must alias safety::WriteOnce.");
static_assert(std::is_same_v<::crucible::fixy::wrap::AtomicMonotonic<std::uint64_t>,
                             ::crucible::safety::AtomicMonotonic<std::uint64_t>>,
              "fixy::wrap::AtomicMonotonic must alias safety::AtomicMonotonic.");

// The three predicates are lambdas, so there is no type name to
// compare.  For a stateless lambda, `&positive == &safety::positive`
// is not a constant expression.  A `decltype` comparison is the
// witness that both names denote one closure type.
static_assert(std::is_same_v<decltype(::crucible::fixy::wrap::positive), decltype(::crucible::safety::positive)>,
              "fixy::wrap::positive must alias safety::positive — predicate drift.");
static_assert(
    std::is_same_v<decltype(::crucible::fixy::wrap::non_negative), decltype(::crucible::safety::non_negative)>,
    "fixy::wrap::non_negative must alias safety::non_negative.");
static_assert(std::is_same_v<decltype(::crucible::fixy::wrap::non_null), decltype(::crucible::safety::non_null)>,
              "fixy::wrap::non_null must alias safety::non_null.");

// Each assert below spells a concrete grade, so the tier enum and the
// class template both take part in the comparison.  A default-grade
// spelling would leave the enum unchecked.

static_assert(std::is_same_v<::crucible::fixy::wrap::HotPath<::crucible::fixy::wrap::HotPathTier_v::Hot, int>,
                             ::crucible::safety::HotPath<::crucible::safety::HotPathTier_v::Hot, int>>,
              "fixy::wrap::HotPath must alias safety::HotPath.");
static_assert(std::is_same_v<::crucible::fixy::wrap::DetSafe<::crucible::fixy::wrap::DetSafeTier_v::Pure, int>,
                             ::crucible::safety::DetSafe<::crucible::safety::DetSafeTier_v::Pure, int>>,
              "fixy::wrap::DetSafe must alias safety::DetSafe.");
static_assert(std::is_same_v<::crucible::fixy::wrap::NumericalTier<::crucible::fixy::wrap::Tolerance::BITEXACT, int>,
                             ::crucible::safety::NumericalTier<::crucible::safety::Tolerance::BITEXACT, int>>,
              "fixy::wrap::NumericalTier must alias safety::NumericalTier.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Vendor<::crucible::fixy::wrap::VendorBackend_v::Portable, int>,
                             ::crucible::safety::Vendor<::crucible::safety::VendorBackend_v::Portable, int>>,
              "fixy::wrap::Vendor must alias safety::Vendor.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::ResidencyHeat<::crucible::fixy::wrap::ResidencyHeatTag_v::Hot, int>,
                   ::crucible::safety::ResidencyHeat<::crucible::safety::ResidencyHeatTag_v::Hot, int>>,
    "fixy::wrap::ResidencyHeat must alias safety::ResidencyHeat.");
static_assert(std::is_same_v<::crucible::fixy::wrap::CipherTier<::crucible::fixy::wrap::CipherTierTag_v::Hot, int>,
                             ::crucible::safety::CipherTier<::crucible::safety::CipherTierTag_v::Hot, int>>,
              "fixy::wrap::CipherTier must alias safety::CipherTier.");
static_assert(std::is_same_v<::crucible::fixy::wrap::AllocClass<::crucible::fixy::wrap::AllocClassTag_v::Arena, int>,
                             ::crucible::safety::AllocClass<::crucible::safety::AllocClassTag_v::Arena, int>>,
              "fixy::wrap::AllocClass must alias safety::AllocClass.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Wait<::crucible::fixy::wrap::WaitStrategy_v::SpinPause, int>,
                             ::crucible::safety::Wait<::crucible::safety::WaitStrategy_v::SpinPause, int>>,
              "fixy::wrap::Wait must alias safety::Wait.");
static_assert(std::is_same_v<::crucible::fixy::wrap::MemOrder<::crucible::fixy::wrap::MemOrderTag_v::AcqRel, int>,
                             ::crucible::safety::MemOrder<::crucible::safety::MemOrderTag_v::AcqRel, int>>,
              "fixy::wrap::MemOrder must alias safety::MemOrder.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Progress<::crucible::fixy::wrap::ProgressClass_v::Bounded, int>,
                             ::crucible::safety::Progress<::crucible::safety::ProgressClass_v::Bounded, int>>,
              "fixy::wrap::Progress must alias safety::Progress.");

static_assert(std::is_same_v<::crucible::fixy::wrap::Computation<::crucible::effects::Row<>, int>,
                             ::crucible::effects::Computation<::crucible::effects::Row<>, int>>,
              "fixy::wrap::Computation must alias effects::Computation.");

static_assert(std::is_same_v<::crucible::fixy::wrap::Consistency<::crucible::fixy::wrap::Consistency_v::STRONG, int>,
                             ::crucible::safety::Consistency<::crucible::safety::Consistency_v::STRONG, int>>,
              "fixy::wrap::Consistency must alias safety::Consistency.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::OpaqueLifetime<::crucible::fixy::wrap::Lifetime_v::PER_PROGRAM, int>,
                   ::crucible::safety::OpaqueLifetime<::crucible::safety::Lifetime_v::PER_PROGRAM, int>>,
    "fixy::wrap::OpaqueLifetime must alias safety::OpaqueLifetime.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Crash<::crucible::fixy::wrap::CrashClass_v::NoThrow, int>,
                             ::crucible::safety::Crash<::crucible::safety::CrashClass_v::NoThrow, int>>,
              "fixy::wrap::Crash must alias safety::Crash.");

static_assert(std::is_same_v<::crucible::fixy::wrap::Budgeted<int>, ::crucible::safety::Budgeted<int>>,
              "fixy::wrap::Budgeted must alias safety::Budgeted.");
static_assert(std::is_same_v<::crucible::fixy::wrap::EpochVersioned<int>, ::crucible::safety::EpochVersioned<int>>,
              "fixy::wrap::EpochVersioned must alias safety::EpochVersioned.");
static_assert(std::is_same_v<::crucible::fixy::wrap::NumaPlacement<int>, ::crucible::safety::NumaPlacement<int>>,
              "fixy::wrap::NumaPlacement must alias safety::NumaPlacement.");
static_assert(std::is_same_v<::crucible::fixy::wrap::RecipeSpec<int>, ::crucible::safety::RecipeSpec<int>>,
              "fixy::wrap::RecipeSpec must alias safety::RecipeSpec.");

// One cell per tier covers the whole lattice.
static_assert(std::is_same_v<::crucible::fixy::wrap::Witness<::crucible::fixy::wrap::Witness_v::UNWITNESSED, int>,
                             ::crucible::safety::Witness<::crucible::safety::Witness_v::UNWITNESSED, int>>,
              "fixy::wrap::Witness must alias safety::Witness.");
static_assert(std::is_same_v<::crucible::fixy::wrap::Witness<::crucible::fixy::wrap::Witness_v::TYPE_CHECKED, int>,
                             ::crucible::safety::Witness<::crucible::safety::Witness_v::TYPE_CHECKED, int>>,
              "fixy::wrap::Witness must alias safety::Witness (TYPE_CHECKED).");
static_assert(std::is_same_v<::crucible::fixy::wrap::Witness<::crucible::fixy::wrap::Witness_v::TEST_PASSED, int>,
                             ::crucible::safety::Witness<::crucible::safety::Witness_v::TEST_PASSED, int>>,
              "fixy::wrap::Witness must alias safety::Witness (TEST_PASSED).");
static_assert(std::is_same_v<::crucible::fixy::wrap::Witness<::crucible::fixy::wrap::Witness_v::FORMALLY_VERIFIED, int>,
                             ::crucible::safety::Witness<::crucible::safety::Witness_v::FORMALLY_VERIFIED, int>>,
              "fixy::wrap::Witness must alias safety::Witness (FORMALLY_VERIFIED).");

static_assert(std::is_same_v<::crucible::fixy::wrap::Unwitnessed<int>,
                             ::crucible::safety::Witness<::crucible::safety::Witness_v::UNWITNESSED, int>>,
              "fixy::wrap::Unwitnessed must alias Witness<UNWITNESSED, T>.");
static_assert(std::is_same_v<::crucible::fixy::wrap::TypeChecked<int>,
                             ::crucible::safety::Witness<::crucible::safety::Witness_v::TYPE_CHECKED, int>>,
              "fixy::wrap::TypeChecked must alias Witness<TYPE_CHECKED, T>.");
static_assert(std::is_same_v<::crucible::fixy::wrap::TestPassed<int>,
                             ::crucible::safety::Witness<::crucible::safety::Witness_v::TEST_PASSED, int>>,
              "fixy::wrap::TestPassed must alias Witness<TEST_PASSED, T>.");
static_assert(std::is_same_v<::crucible::fixy::wrap::FormallyVerified<int>,
                             ::crucible::safety::Witness<::crucible::safety::Witness_v::FORMALLY_VERIFIED, int>>,
              "fixy::wrap::FormallyVerified must alias Witness<FORMALLY_VERIFIED, T>.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_witness<::crucible::fixy::wrap::Witness_v::FORMALLY_VERIFIED, int, int>),
        decltype(&::crucible::safety::mint_witness<::crucible::safety::Witness_v::FORMALLY_VERIFIED, int, int>)>,
    "fixy::wrap::mint_witness must alias safety::mint_witness.");

static_assert(std::is_same_v<::crucible::fixy::wrap::Hw<::crucible::fixy::wrap::HwInstruction_v::Vectorizable, int>,
                             ::crucible::safety::Hw<::crucible::safety::HwInstruction_v::Vectorizable, int>>,
              "fixy::wrap::Hw must alias safety::Hw.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_hw<::crucible::fixy::wrap::HwInstruction_v::Vectorizable, int, int>),
        decltype(&::crucible::safety::mint_hw<::crucible::safety::HwInstruction_v::Vectorizable, int, int>)>,
    "fixy::wrap::mint_hw must alias safety::mint_hw.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::BarrierGuarded<::crucible::fixy::wrap::BarrierStrength_v::SeqCst, int>,
                   ::crucible::safety::BarrierGuarded<::crucible::safety::BarrierStrength_v::SeqCst, int>>,
    "fixy::wrap::BarrierGuarded must alias safety::BarrierGuarded.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_barrier_guarded<::crucible::fixy::wrap::BarrierStrength_v::SeqCst, int,
                                                               int>),
        decltype(&::crucible::safety::mint_barrier_guarded<::crucible::safety::BarrierStrength_v::SeqCst, int, int>)>,
    "fixy::wrap::mint_barrier_guarded must alias safety::mint_barrier_guarded.");

static_assert(std::is_same_v<::crucible::fixy::wrap::SimdWidthPinned<::crucible::fixy::wrap::SimdIsa_v::Avx512Bw, int>,
                             ::crucible::safety::SimdWidthPinned<::crucible::safety::SimdIsa_v::Avx512Bw, int>>,
              "fixy::wrap::SimdWidthPinned must alias safety::SimdWidthPinned.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_simd_width_pinned<::crucible::fixy::wrap::SimdIsa_v::Avx512Bw, int,
                                                                 int>),
        decltype(&::crucible::safety::mint_simd_width_pinned<::crucible::safety::SimdIsa_v::Avx512Bw, int, int>)>,
    "fixy::wrap::mint_simd_width_pinned must alias safety::mint_simd_width_pinned.");

static_assert(std::is_same_v<::crucible::fixy::wrap::JoinPolicy<::crucible::fixy::wrap::JoinPolicy_v::JOIN_ALL, int>,
                             ::crucible::safety::JoinPolicy<::crucible::safety::JoinPolicy_v::JOIN_ALL, int>>,
              "fixy::wrap::JoinPolicy must alias safety::JoinPolicy.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_join_policy<::crucible::fixy::wrap::JoinPolicy_v::JOIN_ALL, int, int>),
        decltype(&::crucible::safety::mint_join_policy<::crucible::safety::JoinPolicy_v::JOIN_ALL, int, int>)>,
    "fixy::wrap::mint_join_policy must alias safety::mint_join_policy.");

static_assert(std::is_same_v<
                  ::crucible::fixy::wrap::SuspendBehavior<::crucible::fixy::wrap::SuspendBehavior_v::KeepsTicking, int>,
                  ::crucible::safety::SuspendBehavior<::crucible::safety::SuspendBehavior_v::KeepsTicking, int>>,
              "fixy::wrap::SuspendBehavior must alias safety::SuspendBehavior.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::mint_suspend_behavior<
                                      ::crucible::fixy::wrap::SuspendBehavior_v::KeepsTicking, int, int>),
                             decltype(&::crucible::safety::mint_suspend_behavior<
                                      ::crucible::safety::SuspendBehavior_v::KeepsTicking, int, int>)>,
              "fixy::wrap::mint_suspend_behavior must alias safety::mint_suspend_behavior.");

static_assert(std::is_same_v<::crucible::fixy::wrap::CallShapePinned<::crucible::fixy::wrap::CallShape::Indirect, int>,
                             ::crucible::safety::CallShapePinned<::crucible::safety::CallShape::Indirect, int>>,
              "fixy::wrap::CallShapePinned must alias safety::CallShapePinned.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_call_shape<::crucible::fixy::wrap::CallShape::Indirect, int, int>),
        decltype(&::crucible::safety::mint_call_shape<::crucible::safety::CallShape::Indirect, int, int>)>,
    "fixy::wrap::mint_call_shape must alias safety::mint_call_shape.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::ControlFlowPinned<::crucible::fixy::wrap::ControlFlow::AbortOnly, int>,
                   ::crucible::safety::ControlFlowPinned<::crucible::safety::ControlFlow::AbortOnly, int>>,
    "fixy::wrap::ControlFlowPinned must alias safety::ControlFlowPinned.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_control_flow<::crucible::fixy::wrap::ControlFlow::AbortOnly, int, int>),
        decltype(&::crucible::safety::mint_control_flow<::crucible::safety::ControlFlow::AbortOnly, int, int>)>,
    "fixy::wrap::mint_control_flow must alias safety::mint_control_flow.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::GlobalStatePinned<::crucible::fixy::wrap::GlobalState::ConstGlobal, int>,
                   ::crucible::safety::GlobalStatePinned<::crucible::safety::GlobalState::ConstGlobal, int>>,
    "fixy::wrap::GlobalStatePinned must alias safety::GlobalStatePinned.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_global_state<::crucible::fixy::wrap::GlobalState::ConstGlobal, int,
                                                            int>),
        decltype(&::crucible::safety::mint_global_state<::crucible::safety::GlobalState::ConstGlobal, int, int>)>,
    "fixy::wrap::mint_global_state must alias safety::mint_global_state.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::StackUsePinned<::crucible::fixy::wrap::StackUse::BoundedByParam, int>,
                   ::crucible::safety::StackUsePinned<::crucible::safety::StackUse::BoundedByParam, int>>,
    "fixy::wrap::StackUsePinned must alias safety::StackUsePinned.");

static_assert(
    std::is_same_v<
        decltype(&::crucible::fixy::wrap::mint_stack_use<::crucible::fixy::wrap::StackUse::BoundedByParam, int, int>),
        decltype(&::crucible::safety::mint_stack_use<::crucible::safety::StackUse::BoundedByParam, int, int>)>,
    "fixy::wrap::mint_stack_use must alias safety::mint_stack_use.");

static_assert(std::is_same_v<::crucible::fixy::wrap::StdioPinned<::crucible::fixy::wrap::Stdio::BufferedWrite, int>,
                             ::crucible::safety::StdioPinned<::crucible::safety::Stdio::BufferedWrite, int>>,
              "fixy::wrap::StdioPinned must alias safety::StdioPinned.");

static_assert(std::is_same_v<
                  decltype(&::crucible::fixy::wrap::mint_stdio<::crucible::fixy::wrap::Stdio::BufferedWrite, int, int>),
                  decltype(&::crucible::safety::mint_stdio<::crucible::safety::Stdio::BufferedWrite, int, int>)>,
              "fixy::wrap::mint_stdio must alias safety::mint_stdio.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::ThreadLocalRef<int, int>, ::crucible::safety::ThreadLocalRef<int, int>>,
    "fixy::wrap::ThreadLocalRef must alias safety::ThreadLocalRef.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::mint_thread_local_ref<int, int>),
                             decltype(&::crucible::safety::mint_thread_local_ref<int, int>)>,
              "fixy::wrap::mint_thread_local_ref must alias safety::mint_thread_local_ref.");

static_assert(std::is_same_v<::crucible::fixy::wrap::Affine<int>, ::crucible::safety::Affine<int>>,
              "fixy::wrap::Affine must alias safety::Affine.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::mint_affine<int, int>),
                             decltype(&::crucible::safety::mint_affine<int, int>)>,
              "fixy::wrap::mint_affine must alias safety::mint_affine.");

static_assert(::crucible::fixy::wrap::is_already_consume_disciplined_v<int>
                  == ::crucible::safety::is_already_consume_disciplined_v<int>,
              "fixy::wrap::is_already_consume_disciplined_v must "
              "alias safety::is_already_consume_disciplined_v.");

struct WrapStructuralTag {};
static_assert(std::is_same_v<::crucible::fixy::wrap::Pinned<int>, ::crucible::safety::Pinned<int>>,
              "fixy::wrap::Pinned must alias safety::Pinned.");
static_assert(std::is_same_v<::crucible::fixy::wrap::NonMovable<int>, ::crucible::safety::NonMovable<int>>,
              "fixy::wrap::NonMovable must alias safety::NonMovable.");
static_assert(std::is_same_v<::crucible::fixy::wrap::ScopedView<int, WrapStructuralTag>,
                             ::crucible::safety::ScopedView<int, WrapStructuralTag>>,
              "fixy::wrap::ScopedView must alias safety::ScopedView.");
static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::mint_view<WrapStructuralTag, int>),
                             decltype(&::crucible::safety::mint_view<WrapStructuralTag, int>)>,
              "fixy::wrap::mint_view must alias safety::mint_view.");
static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::mint_sealed_refined<::crucible::safety::positive, int>),
                             decltype(&::crucible::safety::mint_sealed_refined<::crucible::safety::positive, int>)>,
              "fixy::wrap::mint_sealed_refined must alias safety::mint_sealed_refined.");
static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::mint_tagged<::crucible::safety::source::FromUser, int>),
                             decltype(&::crucible::safety::mint_tagged<::crucible::safety::source::FromUser, int>)>,
              "fixy::wrap::mint_tagged must alias safety::mint_tagged.");
static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::no_scoped_view_field_check<int>),
                             decltype(&::crucible::safety::no_scoped_view_field_check<int>)>,
              "fixy::wrap::no_scoped_view_field_check must alias safety::"
              "no_scoped_view_field_check.");
static_assert(::crucible::fixy::wrap::IsBorrowedRef<::crucible::safety::BorrowedRef<int>>,
              "fixy::wrap::IsBorrowedRef must accept safety::BorrowedRef<int>.");
static_assert(!::crucible::fixy::wrap::IsBorrowedRef<int>, "fixy::wrap::IsBorrowedRef must reject plain int.");

namespace swmr_reader_witness {
struct ReaderHandle {
    [[nodiscard]] int load() const noexcept { return 0; }
};
struct WriterHandle {
    void publish(int const&) noexcept {}
};
inline int read_int(ReaderHandle&&) noexcept { return 0; }
inline int read_from_int(int&&) noexcept { return 0; }
}  // namespace swmr_reader_witness

static_assert(::crucible::fixy::wrap::IsSwmrReader<swmr_reader_witness::ReaderHandle>,
              "fixy::wrap::IsSwmrReader must accept a handle with "
              "[[nodiscard]] T load() const noexcept.");
static_assert(!::crucible::fixy::wrap::IsSwmrReader<swmr_reader_witness::WriterHandle>,
              "fixy::wrap::IsSwmrReader must reject a writer handle "
              "(no load() — would match IsSwmrWriter instead).");
static_assert(!::crucible::fixy::wrap::IsSwmrReader<int>, "fixy::wrap::IsSwmrReader must reject plain int.");

static_assert(::crucible::fixy::wrap::is_swmr_reader_v<swmr_reader_witness::ReaderHandle>,
              "fixy::wrap::is_swmr_reader_v must agree with IsSwmrReader.");

static_assert(std::is_same_v<::crucible::fixy::wrap::swmr_reader_value_t<swmr_reader_witness::ReaderHandle>, int>,
              "fixy::wrap::swmr_reader_value_t must alias the handle's "
              "load() return type.");

static_assert(::crucible::fixy::wrap::SwmrReader<&swmr_reader_witness::read_int>,
              "fixy::wrap::SwmrReader must accept a function with one "
              "SWMR-reader-handle rvalue-ref parameter and a by-value non-region return.");
static_assert(!::crucible::fixy::wrap::SwmrReader<&swmr_reader_witness::read_from_int>,
              "fixy::wrap::SwmrReader must reject a function whose "
              "param 0 is not a SWMR reader handle (int&& fails the handle predicate).");

static_assert(::crucible::fixy::wrap::is_swmr_reader_function_v<&swmr_reader_witness::read_int>,
              "fixy::wrap::is_swmr_reader_function_v must agree with SwmrReader.");

static_assert(std::is_same_v<::crucible::fixy::wrap::swmr_reader_handle_value_t<&swmr_reader_witness::read_int>, int>,
              "swmr_reader_handle_value_t must extract handle payload type.");
static_assert(std::is_same_v<::crucible::fixy::wrap::swmr_reader_returned_value_t<&swmr_reader_witness::read_int>, int>,
              "swmr_reader_returned_value_t must extract function return type.");
static_assert(::crucible::fixy::wrap::swmr_reader_value_consistent_v<&swmr_reader_witness::read_int>,
              "swmr_reader_value_consistent_v must witness handle/return agreement.");

namespace swmr_writer_witness {
inline void publish_int(swmr_reader_witness::WriterHandle&&, int) noexcept {}
inline void publish_from_int(int&&, int) noexcept {}
inline void publish_by_lvalue_ref(swmr_reader_witness::WriterHandle&&, int&) noexcept {}
inline int publish_returns_int(swmr_reader_witness::WriterHandle&&, int) noexcept { return 0; }
}  // namespace swmr_writer_witness

static_assert(::crucible::fixy::wrap::IsSwmrWriter<swmr_reader_witness::WriterHandle>,
              "fixy::wrap::IsSwmrWriter must accept a handle with "
              "void publish(T const&) noexcept.");
static_assert(!::crucible::fixy::wrap::IsSwmrWriter<swmr_reader_witness::ReaderHandle>,
              "fixy::wrap::IsSwmrWriter must reject a reader handle "
              "(no publish() — would match IsSwmrReader instead).");
static_assert(!::crucible::fixy::wrap::IsSwmrWriter<int>, "fixy::wrap::IsSwmrWriter must reject plain int.");

static_assert(::crucible::fixy::wrap::is_swmr_writer_v<swmr_reader_witness::WriterHandle>,
              "fixy::wrap::is_swmr_writer_v must agree with IsSwmrWriter.");

static_assert(std::is_same_v<::crucible::fixy::wrap::swmr_writer_value_t<swmr_reader_witness::WriterHandle>, int>,
              "fixy::wrap::swmr_writer_value_t must alias the handle's "
              "publish() parameter type.");

static_assert(::crucible::fixy::wrap::SwmrWriter<&swmr_writer_witness::publish_int>,
              "fixy::wrap::SwmrWriter must accept a function with "
              "(WriterHandle&&, T) → void shape.");
static_assert(!::crucible::fixy::wrap::SwmrWriter<&swmr_writer_witness::publish_from_int>,
              "fixy::wrap::SwmrWriter must reject a function whose "
              "param 0 is not a SWMR writer handle.");
static_assert(!::crucible::fixy::wrap::SwmrWriter<&swmr_writer_witness::publish_by_lvalue_ref>,
              "fixy::wrap::SwmrWriter must reject a function whose "
              "param 1 is by lvalue reference (writer expects by-value).");
static_assert(!::crucible::fixy::wrap::SwmrWriter<&swmr_writer_witness::publish_returns_int>,
              "fixy::wrap::SwmrWriter must reject a function whose "
              "return is non-void (writer is publish-and-forget).");

static_assert(::crucible::fixy::wrap::is_swmr_writer_function_v<&swmr_writer_witness::publish_int>,
              "fixy::wrap::is_swmr_writer_function_v must agree with SwmrWriter.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::swmr_writer_handle_value_t<&swmr_writer_witness::publish_int>, int>,
    "swmr_writer_handle_value_t must extract handle payload type.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::swmr_writer_published_value_t<&swmr_writer_witness::publish_int>, int>,
    "swmr_writer_published_value_t must extract param 1 value type.");
static_assert(::crucible::fixy::wrap::swmr_writer_value_consistent_v<&swmr_writer_witness::publish_int>,
              "swmr_writer_value_consistent_v must witness handle/value agreement.");
static_assert(std::is_same_v<::crucible::fixy::wrap::OwnedRegion<int, WrapStructuralTag>,
                             ::crucible::safety::OwnedRegion<int, WrapStructuralTag>>,
              "fixy::wrap::OwnedRegion must alias safety::OwnedRegion.");
struct WrapFinalT final {};
static_assert(::crucible::fixy::wrap::NotInherited<WrapFinalT>,
              "fixy::wrap::NotInherited must alias safety::NotInherited concept.");
static_assert(::crucible::safety::NotInherited<WrapFinalT>, "NotInherited concept must accept a `final` class.");
static_assert(std::is_same_v<::crucible::fixy::wrap::FinalBy<WrapFinalT>, ::crucible::safety::FinalBy<WrapFinalT>>,
              "fixy::wrap::FinalBy must alias safety::FinalBy.");

// The sample has external linkage.  Its address is a valid template
// argument, and it names one entity in every translation unit that
// includes this header.  A static function would give each translation
// unit a different entity.
inline int sig_sample(int, double) noexcept { return 0; }

static_assert(std::is_same_v<::crucible::fixy::wrap::signature_traits<&sig_sample>,
                             ::crucible::safety::extract::signature_traits<&sig_sample>>,
              "fixy::wrap::signature_traits must alias safety::extract::signature_traits.");
static_assert(std::is_same_v<::crucible::fixy::wrap::param_type_t<&sig_sample, 0>,
                             ::crucible::safety::extract::param_type_t<&sig_sample, 0>>,
              "fixy::wrap::param_type_t must alias the substrate.");
static_assert(std::is_same_v<::crucible::fixy::wrap::return_type_t<&sig_sample>,
                             ::crucible::safety::extract::return_type_t<&sig_sample>>,
              "fixy::wrap::return_type_t must alias the substrate.");
static_assert(std::is_same_v<::crucible::fixy::wrap::function_type_t<&sig_sample>,
                             ::crucible::safety::extract::function_type_t<&sig_sample>>,
              "fixy::wrap::function_type_t must alias the substrate.");
static_assert(::crucible::fixy::wrap::arity_v<&sig_sample> == ::crucible::safety::extract::arity_v<&sig_sample>,
              "fixy::wrap::arity_v must alias the substrate.");
static_assert(::crucible::fixy::wrap::is_noexcept_v<&sig_sample>
                  == ::crucible::safety::extract::is_noexcept_v<&sig_sample>,
              "fixy::wrap::is_noexcept_v must alias the substrate.");
// The asserts above hold for any consistent pair of names.  These pin
// the sample's actual shape.  A re-export that reads a different
// function fails here, and so does a misspelled one.
static_assert(::crucible::fixy::wrap::arity_v<&sig_sample> == 2, "sig_sample has arity 2.");
static_assert(::crucible::fixy::wrap::is_noexcept_v<&sig_sample>, "sig_sample is noexcept.");
static_assert(std::is_same_v<::crucible::fixy::wrap::return_type_t<&sig_sample>, int>, "sig_sample returns int.");
static_assert(std::is_same_v<::crucible::fixy::wrap::param_type_t<&sig_sample, 0>, int>, "sig_sample param 0 is int.");
static_assert(std::is_same_v<::crucible::fixy::wrap::param_type_t<&sig_sample, 1>, double>,
              "sig_sample param 1 is double.");

// The extractors below carry a constraint on the graded-wrapper
// concept, and a hand-written witness type does not satisfy its
// forwarder clauses.  These checks use a wrapper that already conforms.
static_assert(std::is_same_v<::crucible::fixy::wrap::value_type_of_t<::crucible::safety::Linear<int>>,
                             ::crucible::safety::extract::value_type_of_t<::crucible::safety::Linear<int>>>,
              "fixy::wrap::value_type_of_t must alias the substrate.");
static_assert(std::is_same_v<::crucible::fixy::wrap::lattice_of_t<::crucible::safety::Linear<int>>,
                             ::crucible::safety::extract::lattice_of_t<::crucible::safety::Linear<int>>>,
              "fixy::wrap::lattice_of_t must alias the substrate.");
static_assert(std::is_same_v<::crucible::fixy::wrap::grade_of_t<::crucible::safety::Linear<int>>,
                             ::crucible::safety::extract::grade_of_t<::crucible::safety::Linear<int>>>,
              "fixy::wrap::grade_of_t must alias the substrate.");
static_assert(std::is_same_v<::crucible::fixy::wrap::graded_type_of_t<::crucible::safety::Linear<int>>,
                             ::crucible::safety::extract::graded_type_of_t<::crucible::safety::Linear<int>>>,
              "fixy::wrap::graded_type_of_t must alias the substrate.");
static_assert(::crucible::fixy::wrap::modality_of_v<::crucible::safety::Linear<int>>
                  == ::crucible::safety::extract::modality_of_v<::crucible::safety::Linear<int>>,
              "fixy::wrap::modality_of_v must alias the substrate.");
static_assert(::crucible::fixy::wrap::is_graded_wrapper_v<::crucible::safety::Linear<int>>
                  == ::crucible::safety::extract::is_graded_wrapper_v<::crucible::safety::Linear<int>>,
              "fixy::wrap::is_graded_wrapper_v must alias the substrate.");
static_assert(::crucible::fixy::wrap::is_graded_specialization_v<int>
                  == ::crucible::safety::extract::is_graded_specialization_v<int>,
              "fixy::wrap::is_graded_specialization_v must alias the substrate.");
static_assert(::crucible::fixy::wrap::is_graded_wrapper_v<::crucible::safety::Linear<int>>,
              "Linear<int> is a GradedWrapper.");
static_assert(!::crucible::fixy::wrap::is_graded_wrapper_v<int>, "int is not a GradedWrapper.");
static_assert(std::is_same_v<::crucible::fixy::wrap::value_type_of_t<::crucible::safety::Linear<int>>, int>,
              "Linear<int> user-facing value_type is int.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::ct::select<unsigned>),
                             decltype(&::crucible::safety::ct::select<unsigned>)>,
              "fixy::wrap::ct::select must alias safety::ct::select.");

struct WrapBorrowedParentT {};
static_assert(std::is_same_v<::crucible::fixy::wrap::Borrowed<int, WrapBorrowedParentT>,
                             ::crucible::safety::Borrowed<int, WrapBorrowedParentT>>,
              "fixy::wrap::Borrowed must alias safety::Borrowed.");
static_assert(std::is_same_v<::crucible::fixy::wrap::BorrowedRef<int>, ::crucible::safety::BorrowedRef<int>>,
              "fixy::wrap::BorrowedRef must alias safety::BorrowedRef.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::Saturated<std::uint64_t>, ::crucible::safety::Saturated<std::uint64_t>>,
    "fixy::wrap::Saturated must alias safety::Saturated.");

static_assert(std::is_same_v<::crucible::fixy::wrap::FixedArray<int, 8>, ::crucible::safety::FixedArray<int, 8>>,
              "fixy::wrap::FixedArray must alias safety::FixedArray.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::Cyclic<std::uint32_t, 8>, ::crucible::safety::Cyclic<std::uint32_t, 8>>,
    "fixy::wrap::Cyclic must alias safety::Cyclic.");

static_assert(std::is_same_v<::crucible::fixy::wrap::CyclicBuffer<int, 8>, ::crucible::safety::CyclicBuffer<int, 8>>,
              "fixy::wrap::CyclicBuffer must alias safety::CyclicBuffer.");

static_assert(std::is_same_v<::crucible::fixy::wrap::WeakRef<int>, ::crucible::safety::WeakRef<int>>,
              "fixy::wrap::WeakRef must alias safety::WeakRef.");

namespace bits_sentinel_detail {
enum class WrapBitsSentinelE : std::uint8_t {
    kA = 1,
    kB = 2
};
}  // namespace bits_sentinel_detail
static_assert(std::is_same_v<::crucible::fixy::wrap::Bits<bits_sentinel_detail::WrapBitsSentinelE>,
                             ::crucible::safety::Bits<bits_sentinel_detail::WrapBitsSentinelE>>,
              "fixy::wrap::Bits must alias safety::Bits.");

static_assert(
    std::is_same_v<::crucible::fixy::wrap::SwissTableBuffer<void*>, ::crucible::safety::SwissTableBuffer<void*>>,
    "fixy::wrap::SwissTableBuffer must alias safety::SwissTableBuffer.");

// A comparison of the two function-pointer values instead of their
// types trips `-Werror=tautological-compare`, because the compiler
// folds both sides to one address.  A `decltype` comparison carries
// the same claim past that diagnostic.
static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::add_sat_checked<std::uint64_t>),
                             decltype(&::crucible::safety::add_sat_checked<std::uint64_t>)>,
              "fixy::wrap::add_sat_checked must alias safety::add_sat_checked.");
static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::sub_sat_checked<std::uint64_t>),
                             decltype(&::crucible::safety::sub_sat_checked<std::uint64_t>)>,
              "fixy::wrap::sub_sat_checked must alias safety::sub_sat_checked.");
static_assert(std::is_same_v<decltype(&::crucible::fixy::wrap::mul_sat_checked<std::uint64_t>),
                             decltype(&::crucible::safety::mul_sat_checked<std::uint64_t>)>,
              "fixy::wrap::mul_sat_checked must alias safety::mul_sat_checked.");

static_assert(std::is_same_v<::crucible::fixy::wrap::hot_path::Hot<int>, ::crucible::safety::hot_path::Hot<int>>,
              "fixy::wrap::hot_path::Hot must alias safety::hot_path::Hot.");
static_assert(std::is_same_v<::crucible::fixy::wrap::det_safe::Pure<int>, ::crucible::safety::det_safe::Pure<int>>,
              "fixy::wrap::det_safe::Pure must alias safety::det_safe::Pure.");
static_assert(std::is_same_v<::crucible::fixy::wrap::numerical_tier::Bitexact<int>,
                             ::crucible::safety::numerical_tier::Bitexact<int>>,
              "fixy::wrap::numerical_tier::Bitexact must alias substrate.");
static_assert(std::is_same_v<::crucible::fixy::wrap::vendor::Nv<int>, ::crucible::safety::vendor::Nv<int>>,
              "fixy::wrap::vendor::Nv must alias safety::vendor::Nv.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::residency_heat::Hot<int>, ::crucible::safety::residency_heat::Hot<int>>,
    "fixy::wrap::residency_heat::Hot must alias substrate.");
static_assert(std::is_same_v<::crucible::fixy::wrap::cipher_tier::Hot<int>, ::crucible::safety::cipher_tier::Hot<int>>,
              "fixy::wrap::cipher_tier::Hot must alias safety::cipher_tier::Hot.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::alloc_class::Arena<int>, ::crucible::safety::alloc_class::Arena<int>>,
    "fixy::wrap::alloc_class::Arena must alias safety::alloc_class::Arena.");
static_assert(std::is_same_v<::crucible::fixy::wrap::wait::SpinPause<int>, ::crucible::safety::wait::SpinPause<int>>,
              "fixy::wrap::wait::SpinPause must alias safety::wait::SpinPause.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::mem_order::AcqRel<int>, ::crucible::safety::mem_order::AcqRel<int>>,
    "fixy::wrap::mem_order::AcqRel must alias safety::mem_order::AcqRel.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::progress::Terminating<int>, ::crucible::safety::progress::Terminating<int>>,
    "fixy::wrap::progress::Terminating must alias substrate.");
static_assert(
    std::is_same_v<::crucible::fixy::wrap::consistency::Strong<int>, ::crucible::safety::consistency::Strong<int>>,
    "fixy::wrap::consistency::Strong must alias safety::consistency::Strong.");
static_assert(std::is_same_v<::crucible::fixy::wrap::opaque_lifetime::PerFleet<int>,
                             ::crucible::safety::opaque_lifetime::PerFleet<int>>,
              "fixy::wrap::opaque_lifetime::PerFleet must alias substrate.");
static_assert(std::is_same_v<::crucible::fixy::wrap::crash::NoThrow<int>, ::crucible::safety::crash::NoThrow<int>>,
              "fixy::wrap::crash::NoThrow must alias safety::crash::NoThrow.");

}  // namespace crucible::fixy::wrap::self_test

namespace crucible::fixy::wrap {
// The asserts above compare types and build no value.  This function
// builds one value per shape, which exercises the aliases as
// expressions.
inline void runtime_smoke_test() noexcept {
    HotPath<HotPathTier_v::Hot, int> hot{42};
    (void)hot;

    Budgeted<int> b{};
    (void)b;

    unsigned const masked = ct::select<unsigned>(1u, 0xAAu, 0x55u);
    (void)masked;

    residency_heat::Hot<int> hot_int{1};
    residency_heat::Warm<int> warm_int{2};
    residency_heat::Cold<int> cold_int{3};
    (void)hot_int;
    (void)warm_int;
    (void)cold_int;
}
}  // namespace crucible::fixy::wrap
