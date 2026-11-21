// The carrier census of the row hash.  test_row_hash_wrappers.cpp
// includes this header for its checks.  The fixture
// neg_row_hash_census_undisposed_carrier declares a class with no row, and
// then it includes this header.  The fixture then compiles only the census,
// and not the other checks of the test.

#pragma once

#include <fixy/Aliases.h>
#include <fixy/Atom.h>
#include <fixy/atoms/Barrier.h>
#include <fixy/atoms/Ctrl.h>
#include <fixy/atoms/Dispatch.h>
#include <fixy/atoms/Fp.h>
#include <fixy/atoms/Global.h>
#include <fixy/atoms/Hw.h>
#include <fixy/atoms/Observe.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Regime.h>
#include <fixy/atoms/Scope.h>
#include <fixy/atoms/Simd.h>
#include <fixy/atoms/Stack.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/atoms/Sync.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/Axis.h>
#include <fixy/Bands.h>
#include <fixy/Bits.h>
#include <fixy/Budgeted.h>
#include <fixy/CanonicalOrder.h>
#include <fixy/channels/Endpoint.h>
#include <fixy/channels/LazyEstablishedChannel.h>
#include <fixy/channels/StageEndpointBridge.h>
#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/channels/SwmrSession.h>
#include <fixy/Checked.h>
#include <fixy/Collision.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <fixy/concurrent/ChaseLevDeque.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/LaneRing.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/PayloadRow.h>
#include <fixy/concurrent/PermissionFork.h>
#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/RingValue.h>
#include <fixy/concurrent/Spawn.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/concurrent/StageShape.h>
#include <fixy/os/Topology.h>
#include <fixy/concurrent/WorkingSet.h>
#include <fixy/ConstantTime.h>
#include <fixy/Corpus.h>
#include <fixy/Ctx.h>
#include <fixy/Cyclic.h>
#include <fixy/CyclicBuffer.h>
#include <fixy/Elementwise.h>
#include <fixy/Reductions.h>
#include <fixy/EpochVersioned.h>
#include <fixy/Fn.h>
#include <fixy/fp/Canonicalize.h>
#include <fixy/fp/Polynomial.h>
#include <fixy/GradedFacade.h>
#include <fixy/handle/Once.h>
#include <fixy/handle/PublishOnce.h>
#include <fixy/handle/Published.h>
#include <fixy/Insights.h>
#include <fixy/Machine.h>
#include <fixy/Mutation.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Futex.h>
#include <fixy/os/Io.h>
#include <fixy/os/Loader.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/NumaPlacement.h>
#include <fixy/os/Sched.h>
#include <fixy/os/SchedClass.h>
#include <fixy/os/Socket.h>
#include <fixy/os/SpinBudget.h>
#include <fixy/os/SpinLock.h>
#include <fixy/os/ThreadName.h>
#include <fixy/os/Time.h>
#include <fixy/os/OwnedMmap.h>
#include <fixy/OwnedRegion.h>
#include <fixy/os/Path.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Reject.h>
#include <fixy/Role.h>
#include <fixy/Saturate.h>
#include <fixy/Saturated.h>
#include <fixy/ScopedView.h>
#include <fixy/Secret.h>
#include <fixy/session/Checkpoint.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Handle.h>
#include <fixy/session/MachineBridge.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Stepping.h>
#include <fixy/session/VigilMode.h>
#include <fixy/session/Witnessed.h>
#include <fixy/SharedRegion.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/Throws.h>
#include <foundation/AlignedBytes.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>
#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/EnumValuePins.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/LifetimeLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/algebra/lattices/PinningRequirementLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/ResidencyHeatLattice.h>
#include <foundation/algebra/lattices/SchedulerPolicyLattice.h>
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/TrustLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>
#include <foundation/algebra/Modality.h>
#include <foundation/Brand.h>
#include <foundation/contracts/Armed.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/CallRef.h>
#include <foundation/core/Immortal.h>
#include <foundation/core/Seat.h>
#include <foundation/diag/Catalog.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/diag/Insights.h>
#include <foundation/diag/JsonEmitter.h>
#include <foundation/diag/RowHash.h>
#include <foundation/diag/RowMismatch.h>
#include <foundation/diag/Runtime.h>
#include <foundation/diag/Tag.h>
#include <foundation/diag/tags/Aborts.h>
#include <foundation/diag/tags/Rows.h>
#include <foundation/diag/tags/Tiers.h>
#include <foundation/diag/tags/Wrappers.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Concurrent.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Resources.h>
#include <foundation/effects/Row.h>
#include <foundation/NoObject.h>
#include <foundation/permissions/Fwd.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/permissions/SharedPermission.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/reflect/Enumerate.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>
#include <foundation/reflect/Instance.h>
#include <foundation/reflect/Signature.h>
#include <foundation/Simd.h>
#include <foundation/SwissTableBuffer.h>
#include <foundation/ThreadLocalRef.h>

// The build writes this header from the tree: it includes each public
// header of include/fixy and include/foundation (test/fixy/CMakeLists.txt).
#include <census_public_headers.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <tuple>
#include <vector>

// ---------------------------------------------------------------------
// Every carrier in the tree is off the zero slot, or says why it is not.
//
// Zero is what the primary template answers for a type that carries no
// row, and it is also the value the live kernel-cache lookup passes as
// its row.  So a carrier that folds to zero does not merely share a slot
// with another carrier.  It shares the most-used slot in the cache with
// every bare payload in the tree, and the probe that finds it returns on
// row equality with no second check.  Before this census, some sixty
// carriers sat there: session handles, permissions, stages, pipelines,
// contexts, capabilities, and the mutation carriers beside the two that
// happened to be graded.
//
// The roster is reflected out of the namespaces that hold carriers, so a
// carrier added to one of them is in the census the moment it is
// declared.  Every roster member needs exactly one disposition: a
// witness instance whose row hash is not zero, or a stated reason why no
// row belongs to it.  There is no third outcome.  A member with no
// disposition, with two, or with a witness that folds to zero counts as
// unproven, and the proven count then falls short of the roster.  An
// entry that names nothing in the roster is stale, and a stale table is
// how this census would rot, so it reddens too.
//
// The namespace list is held to the same rule one level up.  Every
// namespace under fixy and foundation that declares a class is either
// censused or named as vocabulary with a reason.  A new namespace is
// therefore a red build until someone decides which it is.
//
// The census sees what this translation unit includes.  The include list
// above names the headers whose types the cells below name.  The build
// also writes census_public_headers.h from the tree, and that header
// includes each public header of both layers.  A hand-kept list can miss
// a header, and the census then never sees a decorator, a door or a query
// that it declares.  With the list from the tree, a header that a later
// commit adds joins the census at once, and a
// carrier in it with no disposition fails the census
// (neg_row_hash_census_undisposed_carrier).
//
// The census stands outside the unnamed namespace, because its witness
// tags reach a row hash, and a stable id refuses a type with internal
// linkage.

namespace census {

namespace fa = ::foundation::algebra;
namespace fe = ::foundation::effects;
namespace fp = ::foundation::permissions;

struct CarrierWitness {
    std::meta::info entity;
    std::meta::info witness;
};

struct StatedZero {
    std::meta::info entity;
    std::string_view reason;
};

struct Verdict {
    std::size_t roster = 0;
    std::size_t proven = 0;
    std::size_t stale = 0;
};

[[nodiscard]] consteval bool is_roster_member(std::meta::info member) {
    if (std::meta::is_class_template(member)) return true;
    return std::meta::is_type(member) && !std::meta::is_type_alias(member) && std::meta::is_class_type(member)
        && std::meta::has_identifier(member);
}

// A witness must be an instance of the member it stands for, so that a
// table row cannot prove one carrier with the hash of another.
[[nodiscard]] consteval bool witness_stands_for(std::meta::info witness, std::meta::info entity) {
    std::meta::info const dealiased = std::meta::dealias(witness);
    if (std::meta::is_class_template(entity)) {
        return std::meta::has_template_arguments(dealiased) && std::meta::template_of(dealiased) == entity;
    }
    return dealiased == entity;
}

// The row hash of each witness, in table order.  It is the one step that
// needs a splice, so it is the one step that is an expansion.
template <auto const& Carriers>
[[nodiscard]] consteval auto witness_hashes() {
    std::array<std::uint64_t, std::size(Carriers)> hashes{};
    std::size_t index = 0;
    template for (constexpr CarrierWitness row : Carriers) {
        hashes[index++] = ::foundation::diag::row_hash_contribution_v<typename[:row.witness:]>;
    }
    return hashes;
}

// The position of `entity` in the `count` entities at `entities`, or
// `count` when no entity is equal.  The read goes through a pointer, because
// a read through a vector subscript or a vector iterator costs three times
// as much in a constant evaluation.  O(count).
[[nodiscard]] consteval std::size_t position_of(std::meta::info const* entities, std::size_t count,
                                                std::meta::info entity) {
    for (std::size_t index = 0; index < count; ++index)
        if (entities[index] == entity) return index;
    return count;
}

// Each table row finds its member in the roster one time.  The member then
// keeps the number of its rows and the verdict of its last row.  A member is
// proven when it has one row and that row proves it.  A row that finds no
// member is stale.  The cost is O(rows * roster) comparisons.
template <auto const& Namespaces, auto const& Carriers, auto const& Zeros>
[[nodiscard]] consteval Verdict run() {
    Verdict verdict{};
    auto const hashes = witness_hashes<Carriers>();
    std::vector<std::meta::info> roster;
    for (std::meta::info ns : Namespaces) {
        std::vector<std::meta::info> const members = std::meta::members_of(ns, std::meta::access_context::current());
        std::meta::info const* member = members.data();
        for (std::size_t index = 0; index < members.size(); ++index)
            if (is_roster_member(member[index])) roster.push_back(member[index]);
    }
    verdict.roster = roster.size();
    std::vector<std::size_t> dispositions(roster.size());
    std::vector<std::uint8_t> last_row_proves(roster.size());
    std::size_t* disposition = dispositions.data();
    std::uint8_t* proves = last_row_proves.data();
    for (std::size_t row = 0; row < std::size(Carriers); ++row) {
        std::size_t const index = position_of(roster.data(), roster.size(), Carriers[row].entity);
        if (index == roster.size()) {
            ++verdict.stale;
            continue;
        }
        ++disposition[index];
        proves[index] = hashes[row] != 0 && witness_stands_for(Carriers[row].witness, Carriers[row].entity) ? 1 : 0;
    }
    for (StatedZero const& zero : Zeros) {
        std::size_t const index = position_of(roster.data(), roster.size(), zero.entity);
        if (index == roster.size()) {
            ++verdict.stale;
            continue;
        }
        ++disposition[index];
        proves[index] = zero.reason.empty() ? 0 : 1;
    }
    for (std::size_t index = 0; index < roster.size(); ++index)
        if (disposition[index] == 1 && proves[index] == 1) ++verdict.proven;
    return verdict;
}

// ── The stand-in roster ──────────────────────────────────────────────
//
// The census is only worth its green if it can go red.  This roster is
// small enough to know the answer for: two carriers that fold, one whose
// witness folds to zero, one with no disposition at all, and one table
// row that names something outside the roster.  The verdict must count
// exactly two proven members and one stale row.

namespace standin {

struct GradedProbe {
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    using lattice_type = fa::lattices::DetSafeLattice::At<fa::lattices::DetSafeTier::Pure>;
    using value_type = int;
};

struct discipline_identity;

struct DisciplineProbe {
    using row_discipline = discipline_identity;
    using row_payload = int;
};

template <typename T>
struct SilentCarrier {};

struct Undisposed {};

}  // namespace standin

inline constexpr std::meta::info kStandinNamespaces[] = {^^standin};

inline constexpr CarrierWitness kStandinCarriers[] = {
    {^^standin::GradedProbe, ^^standin::GradedProbe},
    {^^standin::DisciplineProbe, ^^standin::DisciplineProbe},
    {^^standin::SilentCarrier, ^^standin::SilentCarrier<int>},
    {^^::fixy::Bits, ^^int},
};

inline constexpr StatedZero kStandinZeros[] = {
    {^^standin::discipline_identity, "an identity, declared and never defined"},
};

inline constexpr Verdict kStandinVerdict = run<kStandinNamespaces, kStandinCarriers, kStandinZeros>();

static_assert(kStandinVerdict.roster == 5, "the stand-in roster did not reflect the five classes it declares");
static_assert(kStandinVerdict.proven == 3,
              "the census proved a member it must not.  Of five stand-in members, the graded probe, the "
              "discipline probe and the reasoned identity are proven; a carrier whose witness folds to zero "
              "and a carrier with no disposition must both stay unproven");
static_assert(kStandinVerdict.stale == 1, "the census did not count the one table row that names a type outside "
                                          "the stand-in roster");

// ── The carrier namespaces ───────────────────────────────────────────

inline constexpr std::meta::info kCensusNamespaces[] = {
    ^^::foundation,
    ^^::foundation::core,
    ^^::foundation::algebra,
    ^^::foundation::permissions,
    // The classes of the Simd facade are in its target namespace, whose name
    // follows the target (THE TARGET NAMESPACE of foundation/Platform.h).
    std::meta::parent_of(^^::foundation::simd::vec),
    ^^::foundation::effects,
    ^^::foundation::effects::resource,
    ^^::fixy,
    ^^::fixy::session,
    ^^::fixy::session::vigil_mode,
    ^^::fixy::session::witness,
    ^^::fixy::concurrent,
    ^^::fixy::channels,
    ^^::fixy::channels::swmr_session,
    ^^::fixy::handle,
    ^^::fixy::io,
    ^^::fixy::fs,
    ^^::fixy::os,
    ^^::fixy::mmap,
    ^^::fixy::numa,
    ^^::fixy::net,
    ^^::fixy::perf,
    ^^::fixy::fp,
    ^^::fixy::sched,
    ^^::fixy::spin,
    ^^::fixy::time,
    ^^::fixy::topology,
    ^^::fixy::observe,
};

// Namespaces that declare classes and hold no carrier.  Each holds the
// vocabulary a grade is spelled in, or the machinery that reads grades,
// and none of their types is a value that crosses a kernel signature.
struct StatedVocabulary {
    std::meta::info ns;
    std::string_view reason;
};

inline constexpr std::string_view kGradeVocabulary =
    "grade vocabulary: the atoms, poles, tags and lattice points a grade is spelled in, never a value";
inline constexpr std::string_view kMachinery =
    "machinery that computes over grades at compile time; nothing here is a value in a signature";

inline constexpr StatedVocabulary kVocabularyNamespaces[] = {
    {^^::foundation::core::unit, "the units and the dimensions of a Quantity: tags that a type names at compile "
                                 "time, and none of them is a value"},
    {^^::foundation::core::side, "the roles of the arguments of a call: empty tags that a Side names at compile "
                                 "time, and none of them is a value"},
    // The element ops are in the target namespace of their header, whose name
    // follows the target (THE TARGET NAMESPACE of foundation/Platform.h).
    {std::meta::parent_of(^^::foundation::core::elementwise::Sum),
     "the ops of one element of the pointwise walk: empty classes that a walk calls, and none of them is a value "
     "in a kernel signature"},
    {^^::foundation::os, "the raw system call primitive, its argument words, the kernel floor, the owner of a "
                         "guarded map and the reservations of the page source: words and memory of the kernel "
                         "interface that carry no grade"},
    {^^::foundation::os::side, "the role of the offset of a range of the page source: an empty tag that a Side "
                               "names at compile time, and no value"},
    {^^::foundation::alloc, "the layout of an allocation: a count of bytes and an alignment that the allocation door "
                            "takes, words of a request that carry no grade"},
    {^^::foundation::alloc::side, "the roles of the bases and the lengths of a window record: empty tags that a Side "
                                  "names at compile time, and no value"},
    {^^::foundation::heap, "the typed testing door of the allocator: the scope of the heap of a thread and its "
                           "blocks, which live in the body of the scope and cross no kernel signature"},
    {^^::foundation::heap::heap_tag, "the tag of the permission of the owner of a heap: an empty class that a "
                                     "Permission names at compile time, and no value"},
    {^^::fixy::tags::source, kGradeVocabulary},
    {^^::fixy::tags::trust, kGradeVocabulary},
    {^^::fixy::tags::access, kGradeVocabulary},
    {^^::fixy::tags::version, kGradeVocabulary},
    {^^::fixy::tags::vessel_trust, kGradeVocabulary},
    {^^::fixy::tags::secret_policy, kGradeVocabulary},
    {^^::fixy::pole, kGradeVocabulary},
    {^^::fixy::pole::pred, kGradeVocabulary},
    {^^::fixy::pole::proto, kGradeVocabulary},
    {^^::fixy::pole::stale, kGradeVocabulary},
    {^^::fixy::atom, kGradeVocabulary},
    {^^::fixy::atom::barrier, kGradeVocabulary},
    {^^::fixy::atom::ctrl, kGradeVocabulary},
    {^^::fixy::atom::dispatch, kGradeVocabulary},
    {^^::fixy::atom::fp, kGradeVocabulary},
    {^^::fixy::atom::global, kGradeVocabulary},
    {^^::fixy::atom::hw, kGradeVocabulary},
    {^^::fixy::atom::observe, kGradeVocabulary},
    {^^::fixy::atom::io, kGradeVocabulary},
    {^^::fixy::atom::fs, kGradeVocabulary},
    {^^::fixy::atom::mmap, kGradeVocabulary},
    {^^::fixy::atom::leak, kGradeVocabulary},
    {^^::fixy::atom::regime, kGradeVocabulary},
    {^^::fixy::atom::scope, kGradeVocabulary},
    {^^::fixy::atom::session, kGradeVocabulary},
    {^^::fixy::atom::simd, kGradeVocabulary},
    {^^::fixy::atom::stack, kGradeVocabulary},
    {^^::fixy::atom::stdio, kGradeVocabulary},
    {^^::fixy::atom::stdio::streams, kGradeVocabulary},
    {^^::fixy::atom::sync, kGradeVocabulary},
    {^^::fixy::atom::syscall, kGradeVocabulary},
    {^^::fixy::atom::spawn, kGradeVocabulary},
    {^^::fixy::io::engine, kGradeVocabulary},
    {^^::fixy::io::zerocopy, kGradeVocabulary},
    {^^::fixy::io::ring_flag, kGradeVocabulary},
    {^^::fixy::io::side, "the roles of the two offsets of a transfer: empty tags that a Side names at compile time, "
                         "and none of them is a value"},
    {^^::fixy::fs::open_mode, kGradeVocabulary},
    {^^::fixy::fs::flag, kGradeVocabulary},
    {^^::fixy::fs::sync_op, kGradeVocabulary},
    {^^::fixy::fs::atomicity, kGradeVocabulary},
    {^^::fixy::net::socket_kind, kGradeVocabulary},
    {^^::fixy::mmap::prot, kGradeVocabulary},
    {^^::fixy::mmap::share, kGradeVocabulary},
    {^^::fixy::mmap::advice, kGradeVocabulary},
    {^^::fixy::concurrent::channel_tag, kGradeVocabulary},
    {^^::fixy::spawn, "the holder of the parallel-for fan-out: static members only, and no object of it exists"},
    {^^::fixy::thread, "the options and the name of a thread that the thread door starts: settings of the door, which "
                       "carry no claim to fold"},
    {^^::fixy::futex, "the word, the deadline and the ends of a wait of the futex door: words of the kernel interface "
                      "that carry no claim to fold"},
    {^^::fixy::signal, "the alternate signal stack of a thread that the signal door sets: process state of the door, "
                       "which carries no claim to fold"},
    {^^::fixy::process, "the ids of a process and of a thread, and the child of a test with the report of its end: "
                        "words of the kernel interface that carry no claim to fold"},
    {^^::fixy::env, "the switches of the environment that the environment door reads one time at the start: "
                    "settings of the process, which carry no claim to fold"},
    {^^::fixy::check, "the test language: the source of the inputs of a property and its runner, which only a test "
                      "holds, and which carry no claim to fold"},
    {^^::fixy::os::kind, "the kinds of a handle: empty tags that a Handle names at compile time, and none of them "
                         "is a value"},
    {^^::fixy::sanitize::path_traversal, kGradeVocabulary},
    {^^::fixy::session::check, "abandonment policies, a property of the build and not of the claim"},
    {^^::fixy::session::side, "the roles of the two tiers of a persistence event: empty tags that a Side names at "
                              "compile time, and none of them is a value"},
    {^^::fixy::session::detach_reason, kGradeVocabulary},
    {^^::fixy::session::position, "protocol positions that a view names; a view carries no claim to fold"},
    {^^::fixy::session::watch, "the runtime watch over live sessions: its records are process state, not a claim"},
    {^^::fixy::session::global, "global types, their labels and their reductions: the projection reads them at "
                                "compile time, and none of them is a value in a signature"},
    {^^::fixy::session::config, "configurations of the reduction semantics: a model of a running system that the "
                                "compiler reduces, and never a value in a signature"},
    {^^::fixy::session::projection_failure, "the reasons that a projection refuses, as types that a diagnostic names"},
    {^^::fixy::session::transition, "the protocol algebra: registrations, graph nodes and verdicts computed "
                                    "at compile time; nothing here is a value in a signature"},
    {^^::fixy::refined, kMachinery},
    {^^::fixy::refined_algebra, "refinement predicate combinators, which are grade vocabulary"},
    {^^::fixy::collision, kMachinery},
    {^^::fixy::corpus, kMachinery},
    {^^::fixy::canonical_order, kMachinery},
    {^^::fixy::canonical_order::layer, "the names of the layers of the canonical wrapper order: a position is read "
                                       "from each name at compile time, and none of them is a value"},
    {^^::fixy::federation, "the words, the handshake and the replay window of the federation door, and the tag of "
                           "the local cipher: runtime values and a tag that carry no grade; the peer token that the "
                           "door admits is the carrier, and it folds"},
    {^^::fixy::federation::policy, "the lists of organizations that a deployment admits, read at compile time"},
    {^^::fixy::row_discipline, "discipline identities, declared and never defined; they name claims"},
    {^^::fixy::refined::row_discipline, "discipline identities, declared and never defined; they name claims"},
    {^^::foundation::algebra::modality, kGradeVocabulary},
    {^^::foundation::algebra::lattices, kGradeVocabulary},
    {^^::foundation::algebra::lattices::counter_tags, kGradeVocabulary},
    {^^::foundation::algebra::net, "interaction nets, rule tables and their verdicts, which the compiler checks.  "
                                   "None of them is a value in a signature"},
    {^^::foundation::effects::cap, kGradeVocabulary},
    {^^::foundation::effects::ctx_cap, kGradeVocabulary},
    {^^::foundation::effects::host, "the owners a context is minted for, never passed as values"},
    {^^::foundation::effects::testing, "the test context's witness, never passed as a value"},
    {^^::foundation::effects::row_discipline, "discipline identities, declared and never defined"},
    {^^::foundation::permissions::tag, kGradeVocabulary},
    {^^::foundation::permissions::row_discipline, "discipline identities, declared and never defined"},
    {^^::foundation::brand, "brands, which are identities of instances and never fold"},
    {^^::foundation::decide, "the interval that the precondition predicates read: a pair of bounds that carries no "
                             "claim to fold"},
    {^^::foundation::diag, "the row-hash fold and the diagnostic surface themselves"},
    {^^::foundation::diag::row_discipline, "discipline identities, declared and never defined"},
    {^^::foundation::diag::side, "the roles of the two texts of a violation: empty tags that a Side names at compile "
                                 "time, and none of them is a value"},
    {^^::foundation::fail_closed, kMachinery},
    {^^::foundation::contracts, "armed cells and the roster verdict, which are gate machinery"},
    {^^::foundation::lifetime, "the annotation that refuses a lifetime start over bytes, read at compile time"},
    {^^::foundation::linear, "the annotations of the linear marker and its doors, read at compile time"},
    {^^::foundation::transfer, "the annotations of the two transfer properties and the names of the two properties, "
                               "read at compile time"},
    {^^::foundation::closure, "the facts of a callable class, the policy of a door and the verdict of the check, "
                              "read at compile time"},
    {^^::foundation::reflect, kMachinery},
};

[[nodiscard]] consteval bool is_skipped_namespace(std::meta::info ns) {
    if (!std::meta::has_identifier(ns)) return true;
    std::string_view const name = std::meta::identifier_of(ns);
    return name == "detail" || name.ends_with("_self_test") || name.ends_with("_test");
}

// Add `ns` to `out` when it declares a class, and then each namespace
// below it that does, in pre-order.  The walk reads the members of each
// namespace one time.  The cost is O(members) over the walked namespaces.
consteval void collect_namespaces(std::meta::info ns, std::vector<std::meta::info>& out) {
    std::vector<std::meta::info> const members = std::meta::members_of(ns, std::meta::access_context::current());
    std::meta::info const* member = members.data();
    bool declares_a_class = false;
    std::vector<std::meta::info> children;
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (is_roster_member(member[index])) {
            declares_a_class = true;
            continue;
        }
        if (!std::meta::is_namespace(member[index]) || std::meta::is_namespace_alias(member[index])) continue;
        if (!is_skipped_namespace(member[index])) children.push_back(member[index]);
    }
    if (declares_a_class) out.push_back(ns);
    std::meta::info const* child = children.data();
    for (std::size_t index = 0; index < children.size(); ++index)
        collect_namespaces(child[index], out);
}

struct NamespaceVerdict {
    std::size_t declaring = 0;
    std::size_t disposed = 0;
    std::size_t stale = 0;
};

// Each table row finds its namespace in the declaring list one time, as
// the carrier census does.  A namespace is disposed when it has one row.
// A vocabulary row with no reason gives no disposition.  A row that finds
// no declaring namespace is stale.
[[nodiscard]] consteval NamespaceVerdict namespace_verdict() {
    std::vector<std::meta::info> declaring;
    for (std::meta::info root : {^^::foundation, ^^::fixy})
        collect_namespaces(root, declaring);
    NamespaceVerdict verdict{};
    verdict.declaring = declaring.size();
    std::vector<std::size_t> dispositions(declaring.size());
    std::size_t* disposition = dispositions.data();
    for (std::meta::info censused : kCensusNamespaces) {
        std::size_t const index = position_of(declaring.data(), declaring.size(), censused);
        if (index == declaring.size()) {
            ++verdict.stale;
            continue;
        }
        ++disposition[index];
    }
    for (StatedVocabulary const& vocabulary : kVocabularyNamespaces) {
        std::size_t const index = position_of(declaring.data(), declaring.size(), vocabulary.ns);
        if (index == declaring.size()) {
            ++verdict.stale;
            continue;
        }
        if (!vocabulary.reason.empty()) ++disposition[index];
    }
    for (std::size_t index = 0; index < declaring.size(); ++index)
        if (disposition[index] == 1) ++verdict.disposed;
    return verdict;
}

inline constexpr NamespaceVerdict kNamespaceVerdict = namespace_verdict();

static_assert(kNamespaceVerdict.declaring > 0, "no namespace under fixy or foundation declares a class, so the "
                                               "namespace census proves nothing");
static_assert(kNamespaceVerdict.disposed == kNamespaceVerdict.declaring,
              "a namespace under fixy or foundation declares a class and is neither censused nor named as "
              "vocabulary, or is named twice.  Add it to kCensusNamespaces if it holds carriers, or to "
              "kVocabularyNamespaces with the reason it holds none.");
static_assert(kNamespaceVerdict.stale == 0, "a namespace listed in the census tables no longer declares a class, "
                                            "or no longer exists.  Remove the stale row.");

// ── Witnesses ────────────────────────────────────────────────────────

struct PureRegionTag {
    using permission_row = fe::Row<>;
};

struct IoRegionTag {
    using permission_row = fe::Row<fe::Effect::IO>;
};

struct SplitSiteName {};

struct MachineState {};

struct SelfRole {};

struct PeerRole {};

struct LazyChannelWire : ::foundation::Pinned<LazyChannelWire> {};

// The tags and the two brands of the single-writer many-reader witness.
struct SwmrWriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct SwmrReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct SwmrReaderBrand {};
struct SwmrWriterBrand {};
// The tags and the brand of the witnesses of the three forms of the ring
// channel.  A channel names its form, its tag and the brand of its root, and
// the lanes form names the count of its lanes.
struct SpscWitnessTag {};
struct MpscWitnessTag {};
struct LanesWitnessTag {};
struct ChannelWitnessBrand {};
using SpscWitness = ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::wire, int, 8,
                                                            SpscWitnessTag, ChannelWitnessBrand>;
using MpscWitness = ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::merge, int, 8,
                                                            MpscWitnessTag, ChannelWitnessBrand>;
using LanesWitness =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::lanes, int, 8, LanesWitnessTag,
                                            ChannelWitnessBrand, ::fixy::concurrent::LaneCount{2}>;
struct SpscBoxWitnessTag {};
using SpscBoxWitness = ::fixy::concurrent::PermissionedBoxChannel<::fixy::concurrent::ChannelForm::wire, int, 8,
                                                                  SpscBoxWitnessTag, ChannelWitnessBrand>;
struct MpscBoxWitnessTag {};
using MpscBoxWitness = ::fixy::concurrent::PermissionedBoxChannel<::fixy::concurrent::ChannelForm::merge, int, 8,
                                                                  MpscBoxWitnessTag, ChannelWitnessBrand>;
struct PureRegionBrand {};
using SwmrWitness =
    ::fixy::channels::swmr_session::SwmrSession<int, SwmrWriterTag, SwmrReaderTag, SwmrReaderBrand, SwmrWriterBrand>;

using PureDet = fa::lattices::DetSafeLattice::At<fa::lattices::DetSafeTier::Pure>;
using ViewInt = ::foundation::core::View<int>;

// The pin proof and the placement proof.  Each is named and never built,
// because each one comes only from its mint.
using CpuPinnedWitness =
    ::fixy::CpuPinned<::fixy::AffinityMask::single(0), ::fixy::PinningPosture::PinnedExplicit, int>;
struct NumaProbeRegion final {};
using NumaPlacementWitness = ::fixy::NumaPlacement<NumaProbeRegion, ::fixy::mmap::prot::WriteCopy, PureRegionBrand>;
using NumaPolicyWitness = ::fixy::NumaPolicyPlacement<::fixy::NumaPolicy::interleave, NumaProbeRegion,
                                                      ::fixy::mmap::prot::WriteCopy, PureRegionBrand>;

// The decorators of a session handle, each over one plain handle at End.
using PlainHandle = ::fixy::session::SessionHandle<::fixy::session::End, int>;
using CrashWatchedHandle =
    ::fixy::session::CrashWatched<PlainHandle, SelfRole, PeerRole, ::fixy::session::ReliableSet<>,
                                  fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg>>>;
using RecordedHandle = ::fixy::session::Recorded<PlainHandle>;
using RecordedCrashWatchedHandle = ::fixy::session::Recorded<CrashWatchedHandle>;
using CheckpointAtEnd = ::fixy::session::CheckpointHandle<
    PlainHandle, ::fixy::session::End, void,
    ::fixy::session::CheckpointFrame<::fixy::session::End, ::fixy::session::End, void>>;

// The three stage shapes and the two pipeline shapes, each over the
// witness stage bodies of fixy/concurrent/Stage.h.  Naming a type builds
// no stage, so no handle and no channel is made here.
namespace stage_fakes = ::fixy::concurrent::detail::stage_witness;
using StageWitness = ::fixy::concurrent::Stage<&stage_fakes::stage_pass_through, ::fixy::HotFgCtx>;
using MpmcStageWitness = ::fixy::concurrent::MpmcStage<&stage_fakes::stage_fan_in_two, ::fixy::HotFgCtx>;
using SwmrStageWitness = ::fixy::concurrent::SwmrStage<&stage_fakes::stage_swmr_publish, ::fixy::HotFgCtx>;
using PipelineWitness = ::fixy::concurrent::Pipeline<StageWitness>;
using PipelineDagWitness = ::fixy::concurrent::PipelineDag<
    ::fixy::concurrent::StageGraph<::fixy::concurrent::StagePack<StageWitness, StageWitness>,
                                   ::fixy::concurrent::EdgePack<::fixy::concurrent::StageEdge<0, 1>>>>;

inline constexpr CarrierWitness kCarriers[] = {
    {^^fa::Graded, ^^fa::Graded<fa::ModalityKind::Absolute, PureDet, int>},

    {^^fp::Permission, ^^fp::Permission<PureRegionTag>},
    {^^fp::SharedPermission, ^^fp::SharedPermission<PureRegionTag>},
    {^^fp::SharedPermissionGuard, ^^fp::SharedPermissionGuard<PureRegionTag, ::foundation::brand::DefaultBrand>},
    {^^fp::SharedPermissionPool, ^^fp::SharedPermissionPool<PureRegionTag, ::foundation::brand::DefaultBrand>},
    {^^fp::PermSet, ^^fp::PermSet<PureRegionTag>},
    {^^fp::ReadView, ^^fp::ReadView<PureRegionTag>},
    {^^fp::WriteView, ^^fp::WriteView<PureRegionTag, PureRegionBrand>},
    {^^fp::ReadLoan, ^^fp::ReadLoan<PureRegionTag>},
    {^^fp::LentPermission, ^^fp::LentPermission<PureRegionTag>},

    {^^fe::Bg, ^^fe::Bg},
    {^^fe::Init, ^^fe::Init},
    {^^fe::Test, ^^fe::Test},
    {^^fe::Row, ^^fe::Row<>},
    {^^fe::ExecCtx, ^^fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>},
    {^^fe::Computation, ^^fe::Computation<fe::Row<>, int>},
    {^^fe::Capability, ^^fe::Capability<fe::Effect::Alloc, fe::Bg>},
    {^^fe::ConcurrentRow, ^^fe::ConcurrentRow<fe::resource::SmBudget<32>>},

    // Each resource tag folds its own identity, so a concurrent row that
    // names one axis takes a slot apart from a row that names another.
    {^^fe::resource::SmBudget, ^^fe::resource::SmBudget<1>},
    {^^fe::resource::WarpSchedulerSlots, ^^fe::resource::WarpSchedulerSlots<1>},
    {^^fe::resource::RegistersPerWarp, ^^fe::resource::RegistersPerWarp<1>},
    {^^fe::resource::SmemBytes, ^^fe::resource::SmemBytes<1>},
    {^^fe::resource::L2Bytes, ^^fe::resource::L2Bytes<1>},
    {^^fe::resource::HbmBytes, ^^fe::resource::HbmBytes<1>},
    {^^fe::resource::HbmBandwidth, ^^fe::resource::HbmBandwidth<1>},
    {^^fe::resource::NvlinkBandwidth, ^^fe::resource::NvlinkBandwidth<1>},
    {^^fe::resource::PcieBandwidth, ^^fe::resource::PcieBandwidth<1>},
    {^^fe::resource::NicQueueBudget, ^^fe::resource::NicQueueBudget<1>},
    {^^fe::resource::NicRingDepth, ^^fe::resource::NicRingDepth<1>},
    {^^fe::resource::NicQp, ^^fe::resource::NicQp<1>},
    {^^fe::resource::NicCq, ^^fe::resource::NicCq<1>},
    {^^fe::resource::NicMr, ^^fe::resource::NicMr<1>},
    {^^fe::resource::SwitchEgressBw, ^^fe::resource::SwitchEgressBw<1>},
    {^^fe::resource::SwitchBufferCells, ^^fe::resource::SwitchBufferCells<1>},
    {^^fe::resource::TcamEntries, ^^fe::resource::TcamEntries<1>},
    {^^fe::resource::CpuCoreBudget, ^^fe::resource::CpuCoreBudget<1>},
    {^^fe::resource::LlcBytes, ^^fe::resource::LlcBytes<1>},
    {^^fe::resource::PowerWatts, ^^fe::resource::PowerWatts<1>},
    {^^fe::resource::ThermalCelsius, ^^fe::resource::ThermalCelsius<1>},
    {^^fe::resource::RackPowerKw, ^^fe::resource::RackPowerKw<1>},
    {^^fe::resource::CarbonGramsPerKwh, ^^fe::resource::CarbonGramsPerKwh<1>},

    {^^::fixy::OwnedRegion, ^^::fixy::OwnedRegion<int, PureRegionTag>},
    {^^::fixy::fn, ^^::fixy::role::PureCopy<int>},
    {^^::fixy::graded_facade, ^^::fixy::graded_facade<fa::ModalityKind::Absolute, PureDet, int>},
    {^^::fixy::WriteOnce, ^^::fixy::WriteOnce<int>},
    {^^::fixy::WriteOnceNonNull, ^^::fixy::WriteOnceNonNull<int*>},
    {^^::fixy::AppendOnly, ^^::fixy::AppendOnly<int>},
    {^^::fixy::OrderedAppendOnly, ^^::fixy::OrderedAppendOnly<int>},
    {^^::fixy::Monotonic, ^^::fixy::Monotonic<std::uint64_t>},
    {^^::fixy::BoundedMonotonic, ^^::fixy::BoundedMonotonic<std::uint32_t, 1024U>},
    {^^::fixy::WideMonotonic, ^^::fixy::WideMonotonic<1>},
    {^^::fixy::AtomicMonotonic, ^^::fixy::AtomicMonotonic<std::uint64_t>},
    {^^::fixy::Qtt, ^^::fixy::Linear<int>},
    {^^::fixy::Refinement, ^^::fixy::Refined<::fixy::positive, int>},
    {^^::fixy::Saturated, ^^::fixy::Saturated<int>},
    {^^::fixy::Secret, ^^::fixy::Secret<int>},
    {^^::fixy::Stale, ^^::fixy::Stale<int>},
    {^^::fixy::EpochVersioned, ^^::fixy::EpochVersioned<int>},
    {^^::fixy::Budgeted, ^^::fixy::Budgeted<int>},
    {^^::fixy::Tagged, ^^::fixy::Tagged<int, ::fixy::tags::trust::Verified>},
    {^^::fixy::CpuPinned, ^^CpuPinnedWitness},
    {^^::fixy::NumaPlacement, ^^NumaPlacementWitness},
    {^^::fixy::NumaPolicyPlacement, ^^NumaPolicyWitness},
    {^^::fixy::SchedClass, ^^::fixy::sched_class::Batch<int>},
    {^^::fixy::ThreadNamed, ^^::fixy::ThreadNamed<"census">},
    {^^::fixy::Machine, ^^::fixy::Machine<MachineState>},
    {^^::fixy::ClockSource, ^^::fixy::MonotonicClockBytes<int>},
    {^^::fixy::OwnedMmap, ^^::fixy::detail::SmokeOwnedMmap},
    {^^::fixy::Disjoint, ^^::fixy::Disjoint<PureRegionTag, ::foundation::brand::DefaultBrand, SplitSiteName, 2>},
    {^^::fixy::SharedRegion, ^^::fixy::SharedRegion<int, PureRegionTag>},
    {^^::fixy::ScopedView, ^^::fixy::ScopedView<::fixy::detail::sv_test_carrier, ::fixy::detail::sv_test_tag>},
    {^^::fixy::SharedRead, ^^::fixy::SharedRead<int, PureRegionTag>},
    {^^::fixy::session::Witnessed,
     ^^::fixy::session::Witnessed<ViewInt, ::fixy::session::witness::UnderRow<PureRegionTag>>},

    {^^::fixy::session::SessionHandle, ^^::fixy::session::SessionHandle<::fixy::session::End, int>},
    {^^::fixy::session::SessionFromMachine, ^^::fixy::session::SessionFromMachine<MachineState, ::fixy::session::End>},
    {^^::fixy::session::CrashWatched, ^^CrashWatchedHandle},
    {^^::fixy::session::Recorded, ^^RecordedHandle},
    {^^::fixy::session::CheckpointHandle, ^^CheckpointAtEnd},

    {^^::fixy::concurrent::ChaseLevDeque, ^^::fixy::concurrent::ChaseLevDeque<int, 8>},
    {^^::fixy::concurrent::MpscRing, ^^::fixy::concurrent::MpscRing<int, 8>},
    {^^::fixy::concurrent::SpscRing, ^^::fixy::concurrent::SpscRing<int, 8>},
    {^^::fixy::concurrent::LaneRing, ^^::fixy::concurrent::LaneRing<int, 8, ::fixy::concurrent::LaneCount{2}>},
    {^^::fixy::concurrent::PermissionedChannel, ^^SpscWitness},
    {^^::fixy::concurrent::PermissionedBoxChannel, ^^SpscBoxWitness},
    {^^::fixy::concurrent::AtomicSnapshot, ^^::fixy::concurrent::AtomicSnapshot<int>},
    {^^::fixy::channels::swmr_session::SwmrSession, ^^SwmrWitness},
    {^^::fixy::concurrent::Stage, ^^StageWitness},
    {^^::fixy::concurrent::MpmcStage, ^^MpmcStageWitness},
    {^^::fixy::concurrent::SwmrStage, ^^SwmrStageWitness},
    {^^::fixy::concurrent::Pipeline, ^^PipelineWitness},
    {^^::fixy::concurrent::PipelineDag, ^^PipelineDagWitness},

    {^^::fixy::handle::Lazy, ^^::fixy::handle::Lazy<int>},
    {^^::fixy::handle::PublishOnce, ^^::fixy::handle::PublishOnce<int>},
    {^^::fixy::handle::PublishSlot, ^^::fixy::handle::PublishSlot<int>},
    {^^::fixy::channels::LazyEstablishedChannel,
     ^^::fixy::channels::LazyEstablishedChannel<::fixy::session::End, LazyChannelWire>},

    {^^::fixy::sched::SchedPriority, ^^::fixy::sched::SchedPriority<0>},
    {^^::fixy::spin::Gate, ^^::fixy::spin::SpinLock<PureRegionTag>},
    {^^::fixy::spin::GateGuard, ^^::fixy::spin::SpinGuard<PureRegionTag>},
};

// ── Stated zeros ─────────────────────────────────────────────────────

inline constexpr std::string_view kMetafunction =
    "a compile-time metafunction: it answers a question about types and is never instantiated as a value";
inline constexpr std::string_view kPasskey =
    "a passkey: it exists only inside the one mint that may construct it and never outlives the call";
inline constexpr std::string_view kVocabulary =
    "grade vocabulary: it is the grade another carrier is spelled at, not a carrier of one";
inline constexpr std::string_view kProtocol =
    "a protocol combinator: it is the grade a session handle steps through, and the handle folds it";
inline constexpr std::string_view kPayload = "a bare payload: it holds a value and makes no claim about it";
inline constexpr std::string_view kDescriptor =
    "a single non-template runtime descriptor or cell: it makes one fixed claim, is never a template "
    "argument of a kernel signature, and has no tier to separate";
inline constexpr std::string_view kFactory =
    "a stateless factory: it hands out readings and holds nothing a key could discriminate";
inline constexpr std::string_view kGraphShape =
    "the type-level shape of a stage graph; the pipeline that runs it folds the shape as its identity";
inline constexpr std::string_view kMessageMarker =
    "a message marker of a session protocol: it names the permission one message moves, it is the payload of a "
    "Send or a Recv, and the handle that steps through the protocol folds it";
inline constexpr std::string_view kQuery =
    "a question as one type, so that a predicate of one argument can hold an armed cell: it has no member, and it "
    "is never a value";
inline constexpr std::string_view kVerdictType =
    "the reason of a subtype verdict as a type, which a diagnostic names: it is never a value in a signature";
inline constexpr std::string_view kProjectionModel =
    "a type of the projection of a global type or of its typing context: the projection computes it at compile "
    "time, and it is never a value in a signature";
inline constexpr std::string_view kDoor =
    "a door of a session mint: it has static members only, no object of it exists, and it is never a value";
inline constexpr std::string_view kModeMessage =
    "a message of the mode protocol of a Vigil: it holds no value, it is the payload of a Send, and the handle "
    "that steps through the protocol folds it";
inline constexpr std::string_view kDescriptorDoor =
    "a door of a descriptor mint: it has static members only, no object of it exists, and it is never a value";

inline constexpr StatedZero kZeros[] = {
    {^^::foundation::Pinned, "a CRTP marker base: it forbids moves on its deriver and is never a value"},
    {^^::foundation::NoObject,
     "a CRTP base of a class that holds static members only: no object of its deriver exists, so it is never a "
     "value"},
    {^^::foundation::ChannelBinding,
     "a handle's binding to its channel: a member of a handle, never a kernel signature argument"},
    {^^::foundation::BindingKey,
     "the key of a binding to a channel: it holds nothing, only the channel builds it, and it is never a kernel "
     "signature argument"},
    {^^::foundation::ChannelIdentity,
     "the identity of a channel instance, which a pipeline mint compares: never a kernel signature argument"},
    {^^::foundation::EndpointClaim,
     "the claim of one role of a channel: a member of a channel, never a kernel signature argument"},
    {^^::foundation::simd::vec, kPayload},
    {^^::foundation::simd::mask, kPayload},
    {^^::foundation::AlignedBytes,
     "an owned block of bytes at an alignment of the run: it holds storage and makes no claim about the bytes"},
    {^^::foundation::UnwrittenBytes,
     "an owned block of bytes that no write reached: it holds storage, gives no read, and makes no claim about the "
     "bytes"},
    {^^::foundation::ByteWindow,
     "a window of a block: the offset of its first byte and its count of bytes, and it makes no claim about the "
     "bytes"},
    {^^::foundation::SwissTableBuffer,
     "the one allocation of an open-addressing table: it holds storage and makes no claim about the slots"},
    {^^::foundation::ThreadLocalRef,
     "a stateless handle onto a cell of the thread: it holds nothing, and the cell it names is process state, not a claim"},
    {^^::foundation::open_extension,
     "the open mark of a template: the value of an annotation, and never a value in a kernel signature"},
    {^^::foundation::core::Option, kPayload},
    {^^::foundation::core::NoValue, "the marker of an empty Option: it holds nothing, and no Option holds it"},
    {^^::foundation::core::Site,
     "the place of a call in a report: the argument of a fatal exit, never a value in a kernel signature"},
    {^^::foundation::core::Fmt,
     "the text of a report and its place: the argument of a fatal exit, never a value in a kernel signature"},
    {^^::foundation::core::niche, kMetafunction},
    {^^::foundation::core::niche_key,
     "the proof that the storage of an Option reads a niche: it lives for one call, and holds nothing"},
    {^^::foundation::core::OptionCursor,
     "the position of a loop over an Option: it lives inside the loop, and it is never a value in a signature"},
    {^^::foundation::core::OwnerStateMark,
     "the marker of an owner state: a member type that opts a state in to the owner core, and never a value"},
    {^^::foundation::core::OwnerHomes,
     "the list of the home namespaces of a roster of owner states: it lives at compile time, and holds nothing"},
    {^^::foundation::core::OwnerCore,
     "the protected base of an owner of storage: it holds the state of its owner, and is never a value of its own"},
    {^^::foundation::core::Box,
     "an owned allocation of one object: it holds storage and makes no claim about what the object holds"},
    {^^::foundation::core::Hole,
     "the owned storage of one object with no object in it: it holds storage and makes no claim about a value"},
    {^^::foundation::core::Immortal,
     "the storage of one object of the whole process: it holds storage, makes no claim about the object, and is "
     "never a value in a kernel signature"},
    {^^::foundation::core::Seat,
     "the holder of the value of a loop: it holds its payload from its mint to its take, makes no claim about the "
     "payload, and is never a value in a kernel signature"},
    {^^::foundation::core::BoxWord,
     "the object of a Box in a ring cell: one address that its authority claims, and never a value in a kernel "
     "signature"},
    {^^::foundation::core::box_word_key,
     "the proof that a member of the authority calls a door of a box word: it lives for one call, and holds nothing"},
    {^^::foundation::core::BumpArena,
     "the blocks of an arena: it holds storage and makes no claim about the objects in it"},
    {^^::foundation::core::Ref,
     "a borrow of one object: it owns nothing and makes no claim about what the object holds"},
    {^^::fixy::handle::published_key,
     "the proof that mint_published builds a Published: it lives for one call, and holds nothing"},
    {^^::fixy::handle::PublishedObject,
     "a borrow of one object that its stage published: it owns nothing and makes no claim about what the object "
     "holds"},
    {^^::foundation::core::Link,
     "an optional borrow of one object: it owns nothing and makes no claim about what the object holds"},
    {^^::foundation::core::LinkCursor,
     "the position of a loop over a Link: it lives inside the loop, and it is never a value in a signature"},
    {^^::foundation::core::RefMut,
     "the unique borrow of one object in a frame door: it owns nothing and makes no claim about what the object "
     "holds"},
    {^^::foundation::core::CallRef,
     "a borrow of one callable: it owns nothing, it lives in a frame, and it is never a value in a kernel signature"},
    {^^::foundation::core::Address,
     "the address of one object for an alignment query: it changes from run to run, so it is never a value in a "
     "kernel signature"},
    {^^::foundation::core::AddressKey,
     "the key of an address for a lookup: it changes from run to run, so it is never a value in a kernel "
     "signature"},
    {^^::foundation::core::RunAddress,
     "the address of a run of data: it changes from run to run, so it is never a value in a kernel signature"},
    {^^::foundation::core::DataAddress,
     "the address of a run of data and its device: the address changes from run to run, so it is never a value in "
     "a kernel signature"},
    {^^::foundation::core::Identity,
     "the run-time identity of one brand instance: it changes from run to run, so it is never a value in a kernel "
     "signature"},
    {^^::foundation::core::Alloc, kGradeVocabulary},
    {^^::foundation::core::Atomic,
     "a cell that two threads share: its value is process state, and the cell is never a kernel signature argument"},
    {^^::foundation::core::CasRefusal, kPayload},
    {^^::foundation::core::SoleWriter,
     "the proof of the one writer of a cell: a token with no bytes, which makes no claim about a value"},
    {^^::foundation::core::sole_writer_key, kPasskey},
    {^^::foundation::core::atomic_word, kMetafunction},
    {^^::foundation::core::atomic_word_key,
     "the proof that a cell makes a value from a word: it lives for one call, and holds nothing"},
    {^^::foundation::core::Result,
     "the value or the error of a partial operation: the result of a call, never a value in a kernel signature"},
    {^^::foundation::core::Err, "the mark of an error: it converts to a Result and makes no claim about the error"},
    {^^::foundation::core::closed_tag, "the annotation of a class that holds the tag of a sum: it holds nothing"},
    {^^::foundation::core::derive_protocol,
     "the annotation of a class that takes equal, order, hash and its codec from its parts: it holds nothing"},
    {^^::foundation::core::local_part,
     "the annotation of a member whose value is local to one process: it holds nothing"},
    {^^::foundation::core::pad_part, "the annotation of a member of explicit padding: it holds nothing"},
    {^^::foundation::core::DecodeFault,
     "the error of a decode of an image: a reason and a byte offset, and never a value in a kernel signature"},
    {^^::foundation::core::Unit, "the value of a Result that holds no value: it holds nothing"},
    {^^::foundation::core::OneOf,
     "one value of a closed set of types: the result of a call, never a value in a kernel signature"},
    {^^::foundation::core::LengthMismatch,
     "the error of an operation of two runs of different counts, such as a copy or try_zip: it holds nothing"},
    {^^::foundation::core::TreeStep, "one combine of the plan of a tree or of a scan: the nodes of its two operands, "
                                     "and it makes no claim about their values"},
    {^^::foundation::core::Comparator, "one compare-exchange of a sorting network: two positions, and it makes no "
                                       "claim about the elements"},
    {^^::foundation::core::Ascending, "the order of a sort when the caller gives none: it holds nothing"},
    {^^::foundation::core::KeyOrder,
     "the order of a sort by the key of each element: it holds the projection to the key, the argument of a sort, "
     "and it is never a value in a kernel signature"},
    {^^::foundation::core::CacheLine, "a layout wrapper: it aligns its cell and makes no claim about the cell"},
    {^^::foundation::core::Tally, kDescriptor},
    {^^::foundation::core::Counter, "the writer of a count, a tag in the type of a cell: it holds nothing"},
    {^^::foundation::core::View,
     "a borrow of a run of elements: it owns nothing and makes no claim about what the elements hold"},
    {^^::foundation::core::ViewCursor,
     "the position of a loop over a View: it lives inside the loop, and it is never a value in a signature"},
    {^^::foundation::core::Walk,
     "a lazy chain of steps over a borrowed run: it owns nothing, it lives in the expression of its chain, and it "
     "is never a value in a kernel signature"},
    {^^::foundation::core::RunSet,
     "a borrow of some runs of elements and their counts: it owns nothing and makes no claim about what the "
     "elements hold"},
    {^^::foundation::core::RunOf, "the element type and the slot of one run of a RunSet: it holds no value"},
    {^^::foundation::core::TextView,
     "a borrow of a run of characters: it owns nothing and makes no claim about what the characters hold"},
    {^^::foundation::core::CText,
     "a borrow of a run of characters with a terminating zero: it owns nothing and makes no claim about what the "
     "characters hold"},
    {^^::foundation::core::StaticText,
     "the name of a run of characters with static storage and a terminating zero: it owns nothing and makes no "
     "claim about what the characters hold"},
    {^^::foundation::core::FixedText,
     "an owned buffer of characters: it holds storage and makes no claim about what the characters hold"},
    {^^::foundation::core::ParseError,
     "the error of a parse() of a text: a kind and a position in the text, and never a value in a kernel "
     "signature"},
    {^^::foundation::core::Hex,
     "an argument of a report: an unsigned value and the count of its digits, and never a value in a kernel "
     "signature"},
    {^^::foundation::core::HexBytes,
     "an argument of a report: a borrow of the bytes that it writes, and never a value in a kernel signature"},
    {^^::fixy::os::LineReader,
     "the reader of the lines of standard input: it owns a buffer of characters and makes no claim about what a "
     "line holds, and it is never a value in a kernel signature"},
    {^^::fixy::os::LineTooLong,
     "the error of a line longer than the buffer of its reader: it holds nothing, and it is never a value in a "
     "kernel signature"},
    {^^::fixy::mmap::SharedWords,
     "a borrow of the words of a shared mapping that another writer stores: it owns nothing and makes no claim "
     "about what the words hold"},
    {^^::foundation::core::Fixed,
     "an argument of a report: the bits of a float and the layout of its text, and never a value in a kernel "
     "signature"},
    {^^::foundation::core::Padded,
     "an argument of a report: a text or an integer and the width of its column, and never a value in a kernel "
     "signature"},
    {^^::foundation::core::Record,
     "a product of values: it holds its parts and makes no claim about them, and a binding gives out each part"},
    {^^::foundation::core::Index,
     "the proof of an index below a count: a number whose bound is its type, and it makes no claim about an element"},
    {^^::foundation::core::StaticIndex, "the proof of a constant index: it holds nothing, and the index is its type"},
    {^^::foundation::core::IndexCursor,
     "the position of a loop over the indexes of a count: it lives inside the loop, and it is never a value in a "
     "signature"},
    {^^::foundation::core::IndexRange,
     "the loop over the indexes of a count: it holds nothing, and it is never a value in a signature"},
    {^^::foundation::core::FixedArray,
     "an inline array of elements: it holds storage and makes no claim about what the elements hold"},
    {^^::foundation::core::HeapArray,
     "an array of elements on the heap: it holds storage and makes no claim about what the elements hold"},
    {^^::foundation::core::WriteLease,
     "the write-only lease of the fresh elements of one mint: it lives in the frame of the mint, and it is never a "
     "value in a kernel signature"},
    {^^::foundation::core::MaskedArray,
     "an array of a power-of-two count of elements on the heap: it holds storage and makes no claim about what the "
     "elements hold"},
    {^^::foundation::core::FixedList,
     "an inline list of at most N elements: it holds storage and makes no claim about what the elements hold"},
    {^^::foundation::core::GrowList,
     "a list of elements on the heap that grows: it holds storage and makes no claim about what the elements hold"},
    {^^::foundation::core::WindowStack,
     "an inline stack of elements that lends windows: it holds storage and makes no claim about what the elements "
     "hold"},
    {^^::foundation::core::WindowLoan,
     "the loan of one window of a window stack: it lives in the scope of its loan, and it is never a value in a "
     "kernel signature"},
    {^^::foundation::core::BitSet,
     "an inline set of the indexes below a count: it holds bits and makes no claim about what the indexes name"},
    {^^::foundation::core::HeapBitSet,
     "a set of the ids of one domain on the heap: it holds bits and makes no claim about what the ids name"},
    {^^::foundation::core::BitCursor,
     "the position of a walk over the indexes of a bit set: it lives inside the loop, and it is never a value in a "
     "signature"},
    {^^::foundation::core::PackedWord,
     "one unsigned word of bit fields: it holds bits and makes no claim about what the fields name"},
    {^^::foundation::core::BitField,
     "the tag and the width of one field of a packed word: it holds no value, and it is never a value in a "
     "signature"},
    {^^::foundation::core::SmallList,
     "a list of elements inline and then on the heap: it holds storage and makes no claim about what the elements "
     "hold"},
    {^^::foundation::core::SlotMap,
     "a table of values behind generational keys on the heap: it holds storage and makes no claim about what the "
     "values hold"},
    {^^::foundation::core::SlotWalk,
     "a walk over the values of a slot map: it borrows the map, lives inside the loop, and is never a value in a "
     "signature"},
    {^^::foundation::core::SlotCursor,
     "the position of a walk over the values of a slot map: it lives inside the loop, and it is never a value in a "
     "signature"},
    {^^::foundation::core::GrowText,
     "a text on the heap that grows: it holds characters and makes no claim about them"},
    {^^::foundation::core::HeapText,
     "the characters of a text on the heap after its last append: it holds characters and makes no claim about "
     "them"},
    {^^::foundation::core::BrandedIndex,
     "the proof of an index below the count of one branded scope: it lives inside the scope, and it is never a value "
     "in a kernel signature"},
    {^^::foundation::core::BrandedView,
     "the View of one branded scope: it owns nothing, and it lives inside the body of the scope"},
    {^^::foundation::core::StridedView,
     "a borrow of the elements of a tensor at their strides in one run: it owns nothing and makes no claim about "
     "what the elements hold"},
    {^^::foundation::core::StridedIndex,
     "the coordinates of an index below the sizes of one strided scope: it lives inside the scope, and it is never a "
     "value in a kernel signature"},
    {^^::foundation::core::BrandedStridedView,
     "the strided view of one strided scope: it owns nothing, and it lives inside the body of the scope"},
    {^^::foundation::core::ShapeMismatch,
     "the error of a walk over strided views of different sizes: it holds nothing"},
    {^^::foundation::core::ViewMut,
     "the unique borrow of the run of an owner in a frame door: it owns nothing, it lives inside the body of the "
     "frame, and it makes no claim about what the elements hold"},
    {^^::foundation::core::ViewMutChunks,
     "the chunks of a split of a unique borrow: it owns nothing, it lives inside the body of the frame, and it makes "
     "no claim about what the elements hold"},
    {^^::foundation::core::StridedViewMut,
     "the unique borrow of the elements of the run of a frame at their strides: it owns nothing, it lives inside the "
     "body of the frame, and it makes no claim about what the elements hold"},
    // The walk of rows of foundation/core/StridedRows.h.
    {^^::foundation::core::RowViewBrand, "the brand of one view of one walk of rows: an empty tag, never a value"},
    {^^::foundation::core::RowIndex,
     "the number of a row of one walk of rows: it lives inside the body of the walk, and it is never a value in a "
     "kernel signature"},
    {^^::foundation::core::RowCount,
     "the count of the positions of a row of one walk of rows: it lives inside the body of the walk, and it is never "
     "a value in a kernel signature"},
    {^^::foundation::core::RowStep,
     "a position of a row of one walk of rows: it lives inside the body of the walk, and it is never a value in a "
     "kernel signature"},
    {^^::foundation::core::RowOffset,
     "the offset of a row in one view of a walk of rows: it lives inside the body of the walk, and it is never a "
     "value in a kernel signature"},
    {^^::foundation::core::RowSteps,
     "the counted loop over the positions of a row of one walk of rows: it lives inside the body of the walk"},
    {^^::foundation::core::RowView,
     "a view of one walk of rows: it owns nothing, it lives inside the body of the walk, and it makes no claim about "
     "what the elements hold"},
    {^^::foundation::core::StridedRow,
     "one row of a walk of rows: it reads the offsets of the row loop, lives for one call of the body, and is never "
     "a value in a kernel signature"},
    {^^::foundation::core::Full,
     "the error of a push into a full owner: it holds nothing, or the linear value of the push, and makes no claim"},
    // The FP mode.
    {^^::foundation::core::FpMode,
     "the three settings of the FP control register: a template argument of a kernel context, which states it in "
     "its type, and no value of a kernel signature"},
    // The Scalar family.
    {^^::foundation::core::WideProduct, kPayload},
    {^^::foundation::core::Bounds,
     "a proof that a range has lo <= hi and no NaN: the argument of clamp, never a value in a kernel signature"},
    {^^::foundation::core::Quantity,
     "a number with a unit: its unit is a part of its type, and it makes no claim that a grade carries"},
    {^^::foundation::core::Id, "a number whose kind is its tag: an id, which makes no claim that a grade carries"},
    {^^::foundation::core::Side,
     "a value in a role: the role is a part of its type, and it makes no claim that a grade carries"},
    {^^::foundation::core::Digest, "a hash whose kind is its tag: a value, which makes no claim that a grade carries"},
    {^^::foundation::core::Hash256, "the 32 bytes of a keyed hash: a value, which makes no claim that a grade carries"},
    {^^::foundation::core::KeyedHashKeyBytes,
     "the 32 bytes of the key of a keyed hash: a value, which makes no claim that a grade carries"},
    {^^::foundation::core::StorageFloatOf,
     "the bits of a value of a storage format of a float: a number, which makes no claim that a grade carries"},
    {^^::foundation::core::Philox4x32,
     "the counter-based generator: a holder of static functions and constants, never a value in a kernel signature"},
    {^^::foundation::core::SurfaceVerdict,
     "the answer of the protocol check of a family type: a text of a constant evaluation, never a value in a "
     "kernel signature"},
    {^^::foundation::core::DereferenceProtocol,
     "a grant of the protocol check: an argument of a template, and never a value in a kernel signature"},
    {^^::foundation::core::CursorProtocol,
     "a grant of the protocol check: an argument of a template, and never a value in a kernel signature"},

    {^^fa::is_graded_specialization, kMetafunction},
    {^^fa::graded_modality, kMetafunction},
    {^^fa::grade_key, kPasskey},
    {^^fa::graded_from_call_t,
     "the tag that selects the keyed door of Graded that builds the value in place: it holds no value, and the door "
     "takes a grade_key beside it"},

    {^^fp::can_split_into, kMetafunction},
    {^^fp::can_split_into_pack, kMetafunction},
    {^^fp::has_split_authoring_witness, kMetafunction},
    {^^fp::has_split_pack_authoring_witness, kMetafunction},
    {^^fp::perm_mint_key, kPasskey},
    {^^fp::scope_mint_key, kPasskey},
    {^^fp::federation_admission_key, kPasskey},
    {^^fp::FederationAdmission, "the verifier of federation handshakes: it holds the local key and the replay window, "
                                "the peer tokens that it admits fold, and it is never a value in a signature"},
    {^^::fixy::concurrent::PermissionForkThreads,
     "the holder of the fork body: it has static members only, no object of it exists, and it is never a value"},

    {^^fe::canonical_row, kMetafunction},
    {^^fe::EffectRowLattice, "the lattice over effect rows: a grade, where the row it grades is what folds"},
    {^^fe::is_cap_type, kMetafunction},
    {^^fe::cap_permitted_row, kMetafunction},
    {^^fe::cap_mint_key, kPasskey},
    {^^::fixy::observe::TimingSink,
     "a store of counts of the counter of the core: a body writes it with no context, so it carries no row, and a "
     "read of it takes the tag of Nondet"},
    {^^fe::KernelCall,
     "the token of one call of a kernel: run_kernel makes it and the kernel takes it by reference, so it is never a "
     "value, and the context that it names holds the row"},
    {^^fe::ResourceTagDescriptor, kPayload},
    {^^fe::concurrent_row_value, kMetafunction},
    {^^fe::concurrent_row_descriptors, kMetafunction},
    {^^fe::EffectMask,
     "a set of effect atoms read at run time, from a sample or from bytes on the wire: its type names no row, and bits_from_row projects a row into it"},

    {^^::fixy::AxisRecord,
     "the row of one axis in the table of fixy/Axis.h: it exists only at compile time, and it is never a value"},
    {^^::fixy::Bits, kPayload},
    {^^::fixy::is_writeonce, kMetafunction},
    {^^::fixy::is_writeoncenonnull, kMetafunction},
    {^^::fixy::is_already_linear, kMetafunction},
    {^^::fixy::is_already_consume_disciplined, kMetafunction},
    {^^::fixy::IsPositive, kVocabulary},
    {^^::fixy::IsNonNegative, kVocabulary},
    {^^::fixy::IsNonZero, kVocabulary},
    {^^::fixy::IsZero, kVocabulary},
    {^^::fixy::IsNonNull, kVocabulary},
    {^^::fixy::IsPowerOfTwo, kVocabulary},
    {^^::fixy::IsNonEmpty, kVocabulary},
    {^^::fixy::Aligned, kVocabulary},
    {^^::fixy::InRange, kVocabulary},
    {^^::fixy::BoundedAbove, kVocabulary},
    {^^::fixy::LengthGe, kVocabulary},
    {^^::fixy::ExactSize, kVocabulary},
    {^^::fixy::BoundedBelow, kVocabulary},
    {^^::fixy::DivisibleBy, kVocabulary},
    {^^::fixy::retag_policy, kMetafunction},
    {^^::fixy::Cyclic, kPayload},
    {^^::fixy::CyclicBuffer, kPayload},
    {^^::fixy::WideStart,
     "the start of a WideMonotonic: a value below the start limit that the count takes at its mint or reset, and the "
     "count folds its claim"},
    {^^::fixy::duplicate_atom_on, kMetafunction},
    {^^::fixy::malformed_atom, kMetafunction},
    {^^::fixy::unholdable_payload, kMetafunction},
    {^^::fixy::is_fn, kMetafunction},
    {^^::fixy::PathTraversal, kVocabulary},
    {^^::fixy::Unsplit, kVocabulary},
    {^^::fixy::Slice, kVocabulary},
    {^^::fixy::SplitParts, "a return bundle of a Disjoint witness and its shards, destructured at the call "
                           "site; each member folds on its own"},
    {^^::fixy::split_parts_for_, kMetafunction},
    {^^::fixy::VersionSource, "the owner of the versions of one kind of value: only a context that owns Init mints "
                              "it, it stays beside the values it stamps, and it is never a template argument of a "
                              "kernel signature"},
    {^^::fixy::VersionStamp, "a proof of one version that a source issued: it goes by reference into the "
                             "constructor of an EpochVersioned, which folds the version, and it is never a value "
                             "in a signature"},
    {^^::fixy::BudgetAuthority, "the owner of the budgets of a process: only a context that owns Init mints it, "
                                "and it is never a template argument of a kernel signature"},
    {^^::fixy::BudgetStamp, "a proof of one budget that an authority granted: the constructor of a Budgeted "
                            "spends it and folds the budget, and it is never a value in a signature"},
    {^^::fixy::SequenceSource, "the owner of the sequence of one producer: only a context that owns Init mints it, "
                               "it stays beside the values it grades, and it is never a template argument of a "
                               "kernel signature"},
    {^^::fixy::SequenceStamp, "a proof of one position that a source issued: it goes by reference into a door of "
                              "Stale, which folds the grade, and it is never a value in a signature"},

    {^^::fixy::session::Send, kProtocol},
    {^^::fixy::session::Recv, kProtocol},
    {^^::fixy::session::Select, kProtocol},
    {^^::fixy::session::Sender, kProtocol},
    {^^::fixy::session::AnonymousPeer, kProtocol},
    {^^::fixy::session::Offer, kProtocol},
    {^^::fixy::session::Loop, kProtocol},
    {^^::fixy::session::Continue, kProtocol},
    {^^::fixy::session::End, kProtocol},
    {^^::fixy::session::VendorPinned, kProtocol},
    {^^::fixy::session::Named, kProtocol},
    {^^::fixy::session::Crash,
     "the crash label of a session protocol: no endpoint sends it, an Offer receives it when a peer crashes, and "
     "the handle that steps through the protocol folds it"},
    {^^::fixy::session::PeerMsg,
     "a message of a projected local protocol: it names a peer and a label beside its payload, it is the payload "
     "of a Send or a Recv, and the handle that steps through the protocol folds it"},
    {^^::fixy::session::Labelled,
     "a message of the binary view of a projected protocol: it names a label beside its payload, it is the "
     "payload of a Send or a Recv, and the handle that steps through the protocol folds it"},
    {^^::fixy::session::SessionHandleBase,
     "the base every session handle derives from: it is never held by value, and the handle folds.  It "
     "publishes the Stepping modality without a resource, so hashing it directly is a hard error"},
    {^^::fixy::session::LentOut, kVocabulary},
    {^^::fixy::session::BorrowedIn, kVocabulary},
    {^^::fixy::session::Transferable, kMessageMarker},
    {^^::fixy::session::Returned, kMessageMarker},
    {^^::fixy::session::Borrowed, kMessageMarker},
    {^^::fixy::session::Released, kMessageMarker},
    {^^::fixy::session::DelegatedSession, kMessageMarker},
    {^^::fixy::session::Delegate, kProtocol},
    {^^::fixy::session::Accept, kProtocol},
    {^^::fixy::session::HandleFactory,
     "the builder of every session handle: it has static members only, no object of it exists, and it is never a "
     "value"},
    {^^::fixy::session::StoppedSend,
     "the result of a send with a stop test, taken apart at once by its caller: the handle in it folds on its own, "
     "and the payload that it can hold back is a bare payload"},
    {^^::fixy::session::TestChannelEnds,
     "the two ends of a test channel, taken apart at once by a structured binding of the test: each handle in it "
     "folds on its own"},
    {^^::fixy::session::SessionMintDoor,
     "the door of the session mints to the handle factory: it has static members only, no object of it exists, "
     "and it is never a value"},
    {^^::fixy::session::DelegationDoor,
     "the door of the delegation mint: it has static members only, no object of it exists, and it is never a value"},
    {^^::fixy::session::CheckpointDoor,
     "the door of the checkpoint mint: it has static members only, no object of it exists, and it is never a value"},
    {^^::fixy::session::HandleKey,
     "the passkey of every handle constructor: only the handle factory makes one, for one call, and it is never "
     "stored or a template argument of a kernel signature"},
    {^^::fixy::session::SessionOpenKey,
     "the passkey of the builders that open a session: only the door of the mints makes one, for one call, and it "
     "is never stored or a template argument of a kernel signature"},
    {^^::fixy::session::MintSite,
     "the place of the call of a session mint: a constant that the handle keeps for its reports, never a value in "
     "a kernel signature"},
    {^^::fixy::session::DelegationKey,
     "the passkey of the DelegatedSession constructor: only the delegation door makes one, for one call, and it is "
     "never stored or a template argument of a kernel signature"},
    {^^::fixy::session::SharedReader,
     "a reader's share of a pool that a message carries: it is the payload of a Send or a Recv, and the handle "
     "that steps through the protocol folds it"},
    {^^::fixy::session::DeclassifyOnSend,
     "a message carrier of a classified value: it is the payload of a Send or a Recv, and the handle that steps "
     "through the protocol folds it"},
    {^^::fixy::session::CTPayload,
     "a message carrier of a constant-time value: it is the payload of a Send or a Recv, and the handle that steps "
     "through the protocol folds it"},
    {^^::fixy::session::constant_time_value,
     "an annotation that marks a class as a constant-time value; it is never a value in a signature"},
    {^^::fixy::session::ContentAddressed,
     "a message carrier of a value that the recipient can already hold by its content: it is the payload of a Send "
     "or a Recv, and the handle that steps through the protocol folds it"},
    {^^::fixy::session::PermHold,
     "the hold of the tokens that one session endpoint owns: it stays on the thread of that endpoint beside the "
     "handle, and it is never a template argument of a kernel signature"},
    {^^::fixy::session::HoldFactory,
     "the builder of every hold and of each of its transitions: it has static members only, no object of it "
     "exists, and it is never a value"},
    {^^::fixy::session::HoldSlots,
     "the slots of the tokens of one permission set: a session handle or a hold keeps them in a private member on "
     "the thread of its endpoint, and they are never a template argument of a kernel signature"},
    {^^::fixy::session::is_permission_classified, kMetafunction},
    {^^::fixy::session::payload_perm_delta, kMetafunction},
    {^^::fixy::session::no_label_t,
     "the trying write of a local choice: it writes nothing and holds nothing, and it is a transport argument, never "
     "a value in a kernel signature"},
    {^^::fixy::session::is_plain_payload, kMetafunction},
    {^^::fixy::session::protocol_delivered_regions, kMetafunction},
    {^^::fixy::session::MoveOnlyResource,
     "an empty member that deletes the copy of a session Resource: it holds nothing, makes no claim, and is "
     "never a value in a signature"},
    {^^::fixy::session::side_relation,
     "a registration of the registry of the side relations: it names a concept at compile time, and it is "
     "never a value in a signature"},
    {^^::fixy::session::pole_table,
     "a registration of the registry of the pole tables: it names a class template at compile time, and it is "
     "never a value in a signature"},
    {^^::fixy::session::MachineLease,
     "the Resource of one view of a machine bridge: it reaches the state of the machine of the bridge for the "
     "span of the view, makes no claim, and is never a value in a signature"},

    {^^::fixy::session::SubtypeQuery, kQuery},
    {^^::fixy::session::is_sync_subtype, kMetafunction},
    {^^::fixy::session::SubtypeOk, kVerdictType},
    {^^::fixy::session::SubtypeRejection, kVerdictType},
    {^^::fixy::session::AsyncSubtypeQuery, kQuery},
    {^^::fixy::session::is_async_subtype, kMetafunction},

    {^^::fixy::session::Stop, kProtocol},
    {^^::fixy::session::ReliableSet, kVocabulary},
    {^^::fixy::session::CrashCoverage, kQuery},

    {^^::fixy::session::Commit, kProtocol},
    {^^::fixy::session::Roll, kProtocol},
    {^^::fixy::session::Abort, kProtocol},
    {^^::fixy::session::CheckpointPair, kQuery},
    {^^::fixy::session::CheckpointFrame, kVocabulary},

    {^^::fixy::session::Queued, kProjectionModel},
    {^^::fixy::session::OutQueue, kProjectionModel},
    {^^::fixy::session::Projected, kProjectionModel},
    {^^::fixy::session::NotProjectable, kProjectionModel},
    {^^::fixy::session::EveryRoleReliable, kVocabulary},
    {^^::fixy::session::is_projection_failure, kMetafunction},
    {^^::fixy::session::RoleState, kProjectionModel},
    {^^::fixy::session::TypingContext, kProjectionModel},
    {^^::fixy::session::ContextFault,
     "the fault of the marked system of a typing context: it holds a reflection, so it is a value of a constant "
     "evaluation only, and it is never a value in a signature"},
    {^^::fixy::session::CrashLiveness, kQuery},
    {^^::fixy::session::is_crash_live_by_construction, kMetafunction},
    {^^::fixy::session::is_live_by_construction, kMetafunction},
    {^^::fixy::session::Implementability, kQuery},
    {^^::fixy::session::is_implementable_on, kMetafunction},

    {^^::fixy::session::CrashWitness, kPayload},
    {^^::fixy::session::CrashReporter,
     "the one author of the reports of one crash cell: it holds a pointer to the cell, a report consumes it, and "
     "it is never a template argument of a kernel signature"},
    {^^::fixy::session::CrashWriter,
     "the one writer of the message count of one pair crash cell: it holds a pointer to the cell, a crash mint "
     "consumes it, and it is never a template argument of a kernel signature"},
    {^^::fixy::session::CrashCellCore, kDescriptor},
    {^^::fixy::session::CrashCell,
     "the crash cell of one pair of roles: its type names the pair so that a mint takes the cell of its own pair, "
     "and it is never a template argument of a kernel signature"},
    {^^::fixy::session::NoThirdRole,
     "the receiver of a crash session that sends to no third role: it names no cell, and it is a template argument "
     "of a crash writer only"},
    {^^::fixy::session::CrashSession, kQuery},
    {^^::fixy::session::CrashSend,
     "the result of a crash-watched send, destructured at the call site: the handle in it folds on its own, and "
     "the payload that it can hold back is a bare payload"},
    {^^::fixy::session::CrashSessionDoor, kDoor},

    {^^::fixy::session::SessionTagId, kPayload},
    {^^::fixy::session::RoleTagId, kPayload},
    {^^::fixy::session::SchemaHash, kPayload},
    {^^::fixy::session::PayloadHash, kPayload},
    {^^::fixy::session::RecoveryPathHash, kPayload},
    {^^::fixy::session::StateHash, kPayload},
    {^^::fixy::session::InnerPermSetHash, kPayload},
    {^^::fixy::session::LabelWord, kPayload},
    {^^::fixy::session::StepId, kPayload},
    {^^::fixy::session::CipherEventOrderTag,
     "the tag of the place of a persistence event: an incomplete class that an Id names, and never a value"},
    {^^::fixy::session::SessionEvent,
     "one record of a session event log: it holds the fields of one step, and it makes no claim about a value"},
    {^^::fixy::session::SessionEventLog, kDescriptor},
    {^^::fixy::session::SessionEventWriter,
     "the one writer of one session event log: it holds a pointer to the log, a recorder mint consumes it, and it "
     "is never a template argument of a kernel signature"},
    {^^::fixy::session::EventLogDecodeFailure, kPayload},
    {^^::fixy::session::StepIdKeyFn,
     "the stateless key that orders an event log: it reads the step of an event and holds nothing"},
    {^^::fixy::session::StepIdLess,
     "the stateless order of two steps of an event log: it compares two steps and holds nothing"},

    {^^::fixy::session::RecordingDoor, kDoor},

    {^^::fixy::session::vigil_mode::mode_tag, kVocabulary},
    {^^::fixy::session::vigil_mode::ModeRecordingToCompiled, kModeMessage},
    {^^::fixy::session::vigil_mode::ModeCompiledToRecording, kModeMessage},
    {^^::fixy::session::vigil_mode::ModeCell, kDescriptor},
    {^^::fixy::session::vigil_mode::ModeWriter,
     "the one writer of a mode cell: it holds the identity of its mint, a write of the cell takes it by "
     "reference, and it is never a template argument of a kernel signature"},
    {^^::fixy::session::vigil_mode::ModeLease,
     "the Resource of a session over the mode cell: it holds the address of the cell and the lent writer, the "
     "handle that holds it folds its protocol, and it is never a template argument of a kernel signature"},

    {^^::fixy::concurrent::payload_row, kMetafunction},
    {^^::fixy::concurrent::payload_row_under, kMetafunction},
    {^^::fixy::concurrent::payload_family_rule,
     "a rule that a layer gives the payload row walk at compile time: it names a family and the arguments that "
     "the walk reads, and it is never a value in a signature"},
    {^^::fixy::concurrent::StageArity, kMetafunction},
    {^^::fixy::topology::Topology, "a measured description of the host, read by the scheduler and never a "
                                   "template argument of a kernel signature"},
    {^^::fixy::concurrent::WorkBudget, kPayload},
    {^^::fixy::concurrent::HostFacts, "the facts of a host that the parallelism rule reads, from the topology or "
                                      "from a test, and never a template argument of a kernel signature"},
    {^^::fixy::concurrent::ParallelismDecision, kPayload},
    {^^::fixy::concurrent::ParallelismRule,
     "the parallelism rule: it has static members only, no object of it exists, and it is never a value"},
    {^^::fixy::concurrent::TreeSumParts, "a return bundle of a parallel tree sum and its region of slots, destructured "
                                         "at the call site; each member folds on its own"},
    {^^::fixy::concurrent::TreeReduceParts, "a return bundle of a parallel tree reduce and its region of slots, "
                                            "destructured at the call site; each member folds on its own"},
    {^^::fixy::concurrent::ProcessStageCpuClaims,
     "the type of the one table of claimed CPUs of the process: an object of libfixy, and never a value in a "
     "signature"},
    {^^::fixy::concurrent::StageCpuClaims,
     "the table of the CPUs that the pinned stages of the live pipeline runs hold: an object of libfixy, and never a "
     "value in a signature"},
    {^^::fixy::concurrent::StagePack, kGraphShape},
    {^^::fixy::concurrent::EdgePack, kGraphShape},
    {^^::fixy::concurrent::StageEdge, kGraphShape},
    {^^::fixy::concurrent::StageGraph, kGraphShape},
    {^^::fixy::concurrent::StageGraphCheck,
     "the record of the check of a stage graph: a constant evaluation computes it, it is the argument of the concept "
     "that a refusal prints, and it is never a value in a signature"},
    {^^::fixy::concurrent::SendsOn,
     "an end of a process of a loop: the door of the loop reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::ReceivesOn,
     "an end of a process of a loop: the door of the loop reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::InputsOf,
     "an end of a process of a loop: the door of the loop reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::OutputsOf,
     "an end of a process of a loop: the door of the loop reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::StageEdges,
     "an end of a process of a loop: the door of the loop reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::WritesLock,
     "an end of a process of a loop: the door of the loop reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::ReadsLock,
     "an end of a process of a loop: the door of the loop reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::LoopProcess,
     "a process of a loop and its ends: the door of the loop reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::StageLoop,
     "the type-level shape of a loop of processes: its door reads it at compile time, and it is never a value"},
    {^^::fixy::concurrent::is_stage_inline_safe, kMetafunction},
    {^^::fixy::concurrent::ProducerRoster,
     "the count of the live producer handles of one channel and the end of its stream: a member of the channel that "
     "folds, and never a value in a signature"},

    {^^::fixy::channels::handle_for, kMetafunction},
    {^^::fixy::channels::bridged_pole_protocols, kMetafunction},
    {^^::fixy::channels::swmr_session::swmr_pole_protocols, kMetafunction},
    {^^::fixy::channels::log_pole_protocols, kMetafunction},
    {^^::fixy::channels::Endpoint,
     "a builder that binds a channel handle to a context once: a stage mint or a session mint consumes it, the "
     "stage or the session handle that comes out folds, and an endpoint is never a template argument of a kernel "
     "signature"},
    {^^::fixy::channels::EndpointPack,
     "a list of endpoint types that the stage gate reads at compile time: it has no member, and it is never a value"},
    {^^::fixy::channels::EndpointDoor,
     "the door of the endpoint mint: it has static members only, no object of it exists, and it is never a value"},

    {^^::fixy::handle::Once, kDescriptor},
    {^^::fixy::handle::OneShotFlag, kDescriptor},
    {^^::fixy::handle::PublishCommitCell,
     "a publication counter: its Tag names one pipeline and its WriteAuth one stage, it holds a count and not a "
     "value, and it is never a template argument of a kernel signature"},

    {^^::fixy::io::IoUringRing, kDescriptor},
    {^^::fixy::io::IoRingDoor, kDescriptorDoor},
    {^^::fixy::os::Handle, kDescriptor},
    {^^::fixy::os::HandleRef, kDescriptor},
    {^^::fixy::os::Args, "the arguments of main: a view of argv, which carries no claim to fold"},
    {^^::fixy::os::ArgsCursor, "the cursor of the arguments of main, which carries no claim to fold"},
    {^^::fixy::os::Library, kDescriptor},
    {^^::fixy::os::LibraryFunction,
     "a function of a loaded library: the address of a function of a foreign library, which carries no claim to "
     "fold, and it is never a template argument of a kernel signature"},
    {^^::fixy::os::LoaderDiagnostic, kPayload},
    {^^::fixy::net::ForeignSocketRef, kDescriptor},
    {^^::fixy::net::SocketIdentityTag,
     "the tag of the identity of a socket: an incomplete class that an Id names, and never a value"},
    {^^::fixy::fs::FileDoor, kDescriptorDoor},
    {^^::fixy::fs::TemporaryDirectory, kDescriptor},
    {^^::fixy::fs::TemporaryDirectoryDoor, kDescriptorDoor},
    {^^::fixy::fs::SweepReport, kPayload},
    {^^::fixy::numa::PageNodeCensus, kPayload},
    {^^::fixy::NumaNodeMask, kPayload},
    {^^::fixy::numa::NodeAllocationCounters, kPayload},
    {^^::fixy::numa::NumaQueryError, kPayload},
    {^^::fixy::numa::ThreadMemoryPolicy, kPayload},
    {^^::fixy::net::SocketDoor, kDescriptorDoor},
    {^^::fixy::net::NoTimestampMessage, kPayload},
    {^^::fixy::net::ShortTimestampMessage, kPayload},
    {^^::fixy::net::ReceiveStamps, kPayload},
    {^^::fixy::net::TimestampedReceive, kPayload},
    {^^::fixy::perf::PerfEventDoor, kDescriptorDoor},
    {^^::fixy::perf::Event, kPayload},
    {^^::fixy::perf::Task, kPayload},
    {^^::fixy::time::PtpDeviceDoor, kDescriptorDoor},
    {^^::fixy::time::PtpClockCaps, kPayload},
    {^^::fixy::fp::CanonicalizeRecipeSpec, kPayload},
    {^^::fixy::fp::ReduceResult, kPayload},

    {^^::fixy::concurrent::EventCount, kDescriptor},
    {^^::fixy::concurrent::EventWait,
     "the entry of one waiter in an event count: it lives in one frame, its destructor leaves the count, and it is "
     "never a template argument of a kernel signature"},
    {^^::fixy::concurrent::Spin, "a wait policy: a tag that names how a consumer waits, and it is never a value"},
    {^^::fixy::concurrent::SpinThenPark,
     "a wait policy: a tag that names the spin budget of a consumer before its park, and it is never a value"},
    {^^::fixy::concurrent::SequenceCells,
     "the cells of a bounded queue with a turn word in each cell: a part of a ring, whose own row folds the claims, "
     "and it is never a template argument of a kernel signature"},
    {^^::fixy::concurrent::CellTicketTag,
     "the tag of the id of a ticket of the cells of a queue: it holds no value and makes no claim"},
    {^^::fixy::concurrent::SpscProducerRole,
     "the role of the proof of the one producer of an SPSC ring: a tag that names a side, and it is never a value"},
    {^^::fixy::concurrent::SpscConsumerRole,
     "the role of the proof of the one consumer of an SPSC ring: a tag that names a side, and it is never a value"},
    {^^::foundation::SoleWriterProof, kPasskey},
    {^^::fixy::spin::UnwitnessedSpinLock, kDescriptor},
    {^^::fixy::spin::UnwitnessedBlockingLock, kDescriptor},
    {^^::fixy::spin::try_acquire_t, "the tag that selects the guard that tries a gate one time: it holds no value and "
                                    "makes no claim"},
    {^^::fixy::time::ClockReader, kFactory},
    {^^::fixy::time::TscReader, kFactory},
    {^^::fixy::time::PtpClockReader, kFactory},
    {^^::fixy::time::BoundedSleeper, kFactory},
    {^^::fixy::time::TickRate, kPayload},
    {^^::fixy::time::SpinDeadline,
     "the end of one spin as a value of the counter: it gives only has_passed(), it makes no claim, and it is never a "
     "template argument of a kernel signature"},
    {^^::fixy::sched::SchedProofDoor,
     "a door of the scheduling proof mints: it has static members only, no object of it exists, and it is never a "
     "value"},
    {^^::fixy::sched::PriorAffinityDoor,
     "the door of a prior affinity mask: it has static members only, no object of it exists, and it is never a "
     "value"},
    {^^::fixy::sched::PriorAffinity,
     "the mask that one pin replaced: it holds the authority to undo that pin on its thread, and it is never a value "
     "in a kernel signature"},
    {^^::fixy::sched::CpuIndexTag, "the tag of the id of a CPU index: it holds no value and makes no claim"},
    {^^::fixy::session::witness::AtProtocol, kVocabulary},
    {^^::fixy::session::witness::UnderRow, kVocabulary},
};

inline constexpr Verdict kVerdict = run<kCensusNamespaces, kCarriers, kZeros>();

static_assert(kVerdict.roster > 0, "the reflected carrier roster is empty, so the census proves nothing");
static_assert(kVerdict.proven == kVerdict.roster,
              "a carrier in a censused namespace is unproven.  Either it folds to the zero slot that every bare "
              "payload shares, or it has no row in kCarriers or kZeros, or it has two.  Give the carrier a "
              "shape foundation/diag/RowHash.h reads and a witness in kCarriers, or, if it genuinely carries "
              "no row, a row in kZeros that says why.");
static_assert(kVerdict.stale == 0, "a row in kCarriers or kZeros names a type that is no longer in the census "
                                   "roster.  Remove the stale row.");

}  // namespace census
