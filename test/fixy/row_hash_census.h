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
#include <fixy/Borrowed.h>
#include <fixy/Budgeted.h>
#include <fixy/CanonicalOrder.h>
#include <fixy/Checked.h>
#include <fixy/Collision.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <fixy/concurrent/ChaseLevDeque.h>
#include <fixy/concurrent/Endpoint.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/PayloadRow.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/RingValue.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/concurrent/StageEndpointBridge.h>
#include <fixy/concurrent/StageShape.h>
#include <fixy/concurrent/SubstrateSessionBridge.h>
#include <fixy/concurrent/SwmrSession.h>
#include <fixy/concurrent/Topology.h>
#include <fixy/concurrent/WorkingSet.h>
#include <fixy/ConstantTime.h>
#include <fixy/Corpus.h>
#include <fixy/Ctx.h>
#include <fixy/Cyclic.h>
#include <fixy/CyclicBuffer.h>
#include <fixy/EpochVersioned.h>
#include <fixy/FixedArray.h>
#include <fixy/Fn.h>
#include <fixy/fp/Canonicalize.h>
#include <fixy/fp/Polynomial.h>
#include <fixy/GradedFacade.h>
#include <fixy/handle/LazyEstablishedChannel.h>
#include <fixy/handle/Once.h>
#include <fixy/handle/PublishOnce.h>
#include <fixy/Insights.h>
#include <fixy/Machine.h>
#include <fixy/Mutation.h>
#include <fixy/os/CipherDurable.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Io.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/NumaPlacement.h>
#include <fixy/os/Sched.h>
#include <fixy/os/SchedClass.h>
#include <fixy/os/Spawn.h>
#include <fixy/os/Socket.h>
#include <fixy/os/SpinLock.h>
#include <fixy/os/ThreadName.h>
#include <fixy/os/Time.h>
#include <fixy/OwnedFile.h>
#include <fixy/OwnedMmap.h>
#include <fixy/OwnedRegion.h>
#include <fixy/Path.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Reject.h>
#include <fixy/Role.h>
#include <fixy/Saturate.h>
#include <fixy/Saturated.h>
#include <fixy/ScopedView.h>
#include <fixy/Secret.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Handle.h>
#include <fixy/session/MachineBridge.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Stepping.h>
#include <fixy/session/VigilMode.h>
#include <fixy/SharedRegion.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/Throws.h>
#include <fixy/Witnessed.h>
#include <foundation/AlignedBuffer.h>
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
#include <foundation/diag/Catalog.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/diag/Insights.h>
#include <foundation/diag/JsonEmitter.h>
#include <foundation/diag/RowHash.h>
#include <foundation/diag/RowMismatch.h>
#include <foundation/diag/Runtime.h>
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
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/reflect/Enumerate.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>
#include <foundation/reflect/Instance.h>
#include <foundation/reflect/RawEscape.h>
#include <foundation/reflect/Signature.h>
#include <foundation/Saturate.h>
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
    ^^::foundation::simd,
    ^^::foundation::effects,
    ^^::foundation::effects::resource,
    ^^::fixy,
    ^^::fixy::session,
    ^^::fixy::session::vigil_mode,
    ^^::fixy::concurrent,
    ^^::fixy::concurrent::swmr_session,
    ^^::fixy::handle,
    ^^::fixy::io,
    ^^::fixy::fs,
    ^^::fixy::net,
    ^^::fixy::fp,
    ^^::fixy::sched,
    ^^::fixy::spin,
    ^^::fixy::time,
    ^^::fixy::witness,
    ^^::fixy::cipher::durable,
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
    {^^::fixy::fs::open_mode, kGradeVocabulary},
    {^^::fixy::fs::flag, kGradeVocabulary},
    {^^::fixy::fs::sync_op, kGradeVocabulary},
    {^^::fixy::fs::atomicity, kGradeVocabulary},
    {^^::fixy::net::socket_kind, kGradeVocabulary},
    {^^::fixy::mmap::prot, kGradeVocabulary},
    {^^::fixy::mmap::share, kGradeVocabulary},
    {^^::fixy::mmap::advice, kGradeVocabulary},
    {^^::fixy::concurrent::mpsc_tag, kGradeVocabulary},
    {^^::fixy::concurrent::spsc_tag, kGradeVocabulary},
    {^^::fixy::spawn, "the holder of the parallel-for fan-out: static members only, and no object of it exists"},
    {^^::fixy::sanitize::path_traversal, kGradeVocabulary},
    {^^::fixy::session::check, "abandonment policies, a property of the build and not of the claim"},
    {^^::fixy::session::detach_reason, kGradeVocabulary},
    {^^::fixy::session::position, "protocol positions that a view names; a view carries no claim to fold"},
    {^^::fixy::session::watch, "the runtime watch over live sessions: its records are process state, not a claim"},
    {^^::fixy::session::global, "global types, their labels and their reductions: the projection reads them at "
                                "compile time, and none of them is a value in a signature"},
    {^^::fixy::session::config, "configurations of the reduction semantics: a model of a running system that the "
                                "compiler reduces, and never a value in a signature"},
    {^^::fixy::session::projection_failure, "the reasons that a projection refuses, as types that a diagnostic names"},
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
    {^^::foundation::algebra::transition, "the protocol algebra: registrations, graph nodes and verdicts computed "
                                          "at compile time; nothing here is a value in a signature"},
    {^^::foundation::algebra::lattices, kGradeVocabulary},
    {^^::foundation::algebra::lattices::counter_tags, kGradeVocabulary},
    {^^::foundation::effects::cap, kGradeVocabulary},
    {^^::foundation::effects::ctx_cap, kGradeVocabulary},
    {^^::foundation::effects::host, "the owners a context is minted for, never passed as values"},
    {^^::foundation::effects::testing, "the test context's witness, never passed as a value"},
    {^^::foundation::effects::row_discipline, "discipline identities, declared and never defined"},
    {^^::foundation::permissions::tag, kGradeVocabulary},
    {^^::foundation::permissions::row_discipline, "discipline identities, declared and never defined"},
    {^^::foundation::brand, "brands, which are identities of instances and never fold"},
    {^^::foundation::decide, kMachinery},
    {^^::foundation::diag, "the row-hash fold and the diagnostic surface themselves"},
    {^^::foundation::diag::row_discipline, "discipline identities, declared and never defined"},
    {^^::foundation::fail_closed, kMachinery},
    {^^::foundation::contracts, "armed cells and the roster verdict, which are gate machinery"},
    {^^::foundation::lifetime, "the annotation that refuses a lifetime start over bytes, read at compile time"},
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
// The tags and the brand of the two ring channel witnesses.  A channel
// names its tag and the brand of its root.
struct SpscWitnessTag {};
struct MpscWitnessTag {};
struct ChannelWitnessBrand {};
using SpscWitness = ::fixy::concurrent::PermissionedSpscChannel<int, 8, SpscWitnessTag, ChannelWitnessBrand>;
using MpscWitness = ::fixy::concurrent::PermissionedMpscChannel<int, 8, MpscWitnessTag, ChannelWitnessBrand>;
struct PureRegionBrand {};
using SwmrWitness =
    ::fixy::concurrent::swmr_session::SwmrSession<int, SwmrWriterTag, SwmrReaderTag, SwmrReaderBrand, SwmrWriterBrand>;

using PureDet = fa::lattices::DetSafeLattice::At<fa::lattices::DetSafeTier::Pure>;
using BorrowedInt = ::fixy::Borrowed<int, PureRegionTag>;

// The pin proof and the placement proof.  Each is named and never built,
// because each one comes only from its mint.
using CpuPinnedWitness =
    ::fixy::CpuPinned<::fixy::AffinityMask::single(0), ::fixy::PinningPosture::PinnedExplicit, int>;
struct NumaProbeRegion final {};
using NumaPlacementWitness = ::fixy::NumaPlacement<NumaProbeRegion, ::fixy::mmap::prot::WriteCopy, PureRegionBrand>;

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
using MpmcStageWitness =
    ::fixy::concurrent::MpmcStage<&stage_fakes::stage_fan_in_two, ::fixy::HotFgCtx,
                                  std::tuple<stage_fakes::FakeConsumer<int>, stage_fakes::FakeConsumer<int>>,
                                  std::tuple<stage_fakes::FakeProducer<int>>>;
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

    {^^::fixy::BorrowedRef, ^^::fixy::BorrowedRef<int>},
    {^^::fixy::Borrowed, ^^BorrowedInt},
    {^^::fixy::OwnedRegion, ^^::fixy::OwnedRegion<int, PureRegionTag>},
    {^^::fixy::WeakRef, ^^::fixy::WeakRef<int>},
    {^^::fixy::fn, ^^::fixy::role::PureCopy<int>},
    {^^::fixy::graded_facade, ^^::fixy::graded_facade<fa::ModalityKind::Absolute, PureDet, int>},
    {^^::fixy::WriteOnce, ^^::fixy::WriteOnce<int>},
    {^^::fixy::WriteOnceNonNull, ^^::fixy::WriteOnceNonNull<int*>},
    {^^::fixy::AppendOnly, ^^::fixy::AppendOnly<int>},
    {^^::fixy::OrderedAppendOnly, ^^::fixy::OrderedAppendOnly<int>},
    {^^::fixy::Monotonic, ^^::fixy::Monotonic<std::uint64_t>},
    {^^::fixy::BoundedMonotonic, ^^::fixy::BoundedMonotonic<std::uint32_t, 1024U>},
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
    {^^::fixy::SchedClass, ^^::fixy::sched_class::Batch<int>},
    {^^::fixy::ThreadNamed, ^^::fixy::ThreadNamed<"census">},
    {^^::fixy::Machine, ^^::fixy::Machine<MachineState>},
    {^^::fixy::ClockSource, ^^::fixy::MonotonicClockBytes<int>},
    {^^::fixy::OwnedMmap, ^^::fixy::detail::SmokeOwnedMmap},
    {^^::fixy::Disjoint, ^^::fixy::Disjoint<PureRegionTag, ::foundation::brand::DefaultBrand, SplitSiteName, 2>},
    {^^::fixy::SharedRegion, ^^::fixy::SharedRegion<int, PureRegionTag>},
    {^^::fixy::ScopedView, ^^::fixy::ScopedView<::fixy::detail::sv_test_carrier, ::fixy::detail::sv_test_tag>},
    {^^::fixy::SharedRead, ^^::fixy::SharedRead<int, PureRegionTag>},
    {^^::fixy::Witnessed, ^^::fixy::Witnessed<BorrowedInt, ::fixy::witness::UnderRow<PureRegionTag>>},

    {^^::fixy::session::SessionHandle, ^^::fixy::session::SessionHandle<::fixy::session::End, int>},
    {^^::fixy::session::SessionFromMachine, ^^::fixy::session::SessionFromMachine<MachineState, ::fixy::session::End>},
    {^^::fixy::session::CrashWatched, ^^CrashWatchedHandle},
    {^^::fixy::session::Recorded, ^^RecordedHandle},
    {^^::fixy::session::CheckpointHandle, ^^CheckpointAtEnd},

    {^^::fixy::concurrent::ChaseLevDeque, ^^::fixy::concurrent::ChaseLevDeque<int, 8>},
    {^^::fixy::concurrent::MpscRing, ^^::fixy::concurrent::MpscRing<int, 8>},
    {^^::fixy::concurrent::SpscRing, ^^::fixy::concurrent::SpscRing<int, 8>},
    {^^::fixy::concurrent::PermissionedMpscChannel, ^^MpscWitness},
    {^^::fixy::concurrent::PermissionedSpscChannel, ^^SpscWitness},
    {^^::fixy::concurrent::AtomicSnapshot, ^^::fixy::concurrent::AtomicSnapshot<int>},
    {^^::fixy::concurrent::swmr_session::SwmrSession, ^^SwmrWitness},
    {^^::fixy::concurrent::Stage, ^^StageWitness},
    {^^::fixy::concurrent::MpmcStage, ^^MpmcStageWitness},
    {^^::fixy::concurrent::SwmrStage, ^^SwmrStageWitness},
    {^^::fixy::concurrent::Pipeline, ^^PipelineWitness},
    {^^::fixy::concurrent::PipelineDag, ^^PipelineDagWitness},

    {^^::fixy::handle::SetOnce, ^^::fixy::handle::SetOnce<int>},
    {^^::fixy::handle::Lazy, ^^::fixy::handle::Lazy<int>},
    {^^::fixy::handle::PublishOnce, ^^::fixy::handle::PublishOnce<int>},
    {^^::fixy::handle::PublishSlot, ^^::fixy::handle::PublishSlot<int>},
    {^^::fixy::handle::LazyEstablishedChannel,
     ^^::fixy::handle::LazyEstablishedChannel<::fixy::session::End, LazyChannelWire>},

    {^^::fixy::sched::SchedPriority, ^^::fixy::sched::SchedPriority<0>},
    {^^::fixy::spin::Gate, ^^::fixy::spin::SpinLock<PureRegionTag>},
    {^^::fixy::spin::GateGuard, ^^::fixy::spin::SpinGuard<PureRegionTag>},
    {^^::fixy::cipher::durable::CipherDurableHandle,
     ^^::fixy::cipher::durable::CipherDurableHandle<::fixy::cipher::durable::warm_writer_stance>},
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
    {^^::foundation::ChannelIdentity,
     "the identity of a channel instance, which a pipeline mint compares: never a kernel signature argument"},
    {^^::foundation::EndpointClaim,
     "the claim of one role of a channel: a member of a channel, never a kernel signature argument"},
    {^^::foundation::simd::vec, kPayload},
    {^^::foundation::simd::mask, kPayload},
    {^^::foundation::AlignedBuffer,
     "an owned allocation of elements: it holds storage and makes no claim about what the elements hold"},
    {^^::foundation::SwissTableBuffer,
     "the one allocation of an open-addressing table: it holds storage and makes no claim about the slots"},
    {^^::foundation::ThreadLocalRef,
     "a stateless handle onto a cell of the thread: it holds nothing, and the cell it names is process state, not a claim"},
    {^^::foundation::core::Option, kPayload},
    {^^::foundation::core::NoValue, "the marker of an empty Option: it holds nothing, and no Option holds it"},
    {^^::foundation::core::ExpectWhy,
     "the reason and the place of a fatal unwrap: the argument of expect(), never a value in a kernel signature"},
    {^^::foundation::core::niche, kMetafunction},
    {^^::foundation::core::OptionCursor,
     "the position of a loop over an Option: it lives inside the loop, and it is never a value in a signature"},
    {^^::foundation::core::Box,
     "an owned allocation of one object: it holds storage and makes no claim about what the object holds"},
    {^^::foundation::core::Atomic,
     "a cell that two threads share: its value is process state, and the cell is never a kernel signature argument"},
    {^^::foundation::core::CasOutcome, kPayload},
    {^^::foundation::core::CacheLine, "a layout wrapper: it aligns its cell and makes no claim about the cell"},
    {^^::foundation::core::Tally, kDescriptor},
    {^^::foundation::core::View,
     "a borrow of a run of elements: it owns nothing and makes no claim about what the elements hold"},
    {^^::foundation::core::ViewCursor,
     "the position of a loop over a View: it lives inside the loop, and it is never a value in a signature"},

    {^^fa::is_graded_specialization, kMetafunction},
    {^^fa::graded_modality, kMetafunction},
    {^^fa::grade_key, kPasskey},

    {^^fp::can_split_into, kMetafunction},
    {^^fp::can_split_into_pack, kMetafunction},
    {^^fp::has_split_authoring_witness, kMetafunction},
    {^^fp::has_split_pack_authoring_witness, kMetafunction},
    {^^fp::perm_mint_key, kPasskey},
    {^^fp::erase_brand_key, kPasskey},
    {^^fp::federation_admission_key, kPasskey},
    {^^fp::FederationAdmission, "the verifier of federation handshakes: it holds the local key and the replay window, "
                                "the peer tokens that it admits fold, and it is never a value in a signature"},
    {^^fp::PermissionForkRunner,
     "the holder of the fork body: it has static members only, no object of it exists, and it is never a value"},

    {^^fe::canonical_row, kMetafunction},
    {^^fe::EffectRowLattice, "the lattice over effect rows: a grade, where the row it grades is what folds"},
    {^^fe::is_cap_type, kMetafunction},
    {^^fe::cap_permitted_row, kMetafunction},
    {^^fe::cap_mint_key, kPasskey},
    {^^fe::ResourceTagDescriptor, kPayload},
    {^^fe::concurrent_row_value, kMetafunction},
    {^^fe::concurrent_row_descriptors, kMetafunction},
    {^^fe::EffectMask,
     "a set of effect atoms read at run time, from a sample or from bytes on the wire: its type names no row, and bits_from_row projects a row into it"},

    {^^::fixy::axis_traits, kMetafunction},
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
    {^^::fixy::ThreadNameLiteral, "a string literal usable as a template argument; ThreadNamed folds it"},
    {^^::fixy::Cyclic, kPayload},
    {^^::fixy::CyclicBuffer, kPayload},
    {^^::fixy::FixedArray, kPayload},
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
    {^^::fixy::OwnedFile, kDescriptor},
    {^^::fixy::OwnedFileDoor, "the door of the two file mints: it has static members only, no object of it exists, "
                              "and it is never a value"},
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
    {^^::fixy::session::is_content_addressed, kMetafunction},
    {^^::fixy::session::content_addressed_underlying, kMetafunction},
    {^^::fixy::session::unwrap_content_addressed, kMetafunction},
    {^^::fixy::session::PermHold,
     "the hold of the tokens that one session endpoint owns: it stays on the thread of that endpoint beside the "
     "handle, and it is never a template argument of a kernel signature"},
    {^^::fixy::session::HoldFactory,
     "the builder of every hold and of each of its transitions: it has static members only, no object of it "
     "exists, and it is never a value"},
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
     "the one writer of the message count of one crash cell: it holds a pointer to the cell, a crash mint consumes "
     "it, and it is never a template argument of a kernel signature"},
    {^^::fixy::session::PeerCrashCell, kDescriptor},
    {^^::fixy::session::CrashSession, kQuery},
    {^^::fixy::session::is_crash_session_admissible, kMetafunction},
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
    {^^::fixy::session::SessionEvent,
     "one record of a session event log: it holds the fields of one step, and it makes no claim about a value"},
    {^^::fixy::session::SessionEventLog, kDescriptor},
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

    {^^::fixy::concurrent::payload_row, kMetafunction},
    {^^::fixy::concurrent::payload_row_under, kMetafunction},
    {^^::fixy::concurrent::payload_family_rule,
     "a rule that a layer gives the payload row walk at compile time: it names a family and the arguments that "
     "the walk reads, and it is never a value in a signature"},
    {^^::fixy::concurrent::StageArity, kMetafunction},
    {^^::fixy::concurrent::Topology, "a measured description of the host, read by the scheduler and never a "
                                     "template argument of a kernel signature"},
    {^^::fixy::concurrent::WorkBudget, kPayload},
    {^^::fixy::concurrent::ParallelismDecision, kPayload},
    {^^::fixy::concurrent::ParallelismRule,
     "the parallelism rule: it has static members only, no object of it exists, and it is never a value"},
    {^^::fixy::concurrent::StagePack, kGraphShape},
    {^^::fixy::concurrent::EdgePack, kGraphShape},
    {^^::fixy::concurrent::StageEdge, kGraphShape},
    {^^::fixy::concurrent::StageGraph, kGraphShape},
    {^^::fixy::concurrent::is_stage_inline_safe, kMetafunction},
    {^^::fixy::concurrent::handle_for, kMetafunction},
    {^^::fixy::concurrent::default_proto_for, kMetafunction},
    {^^::fixy::concurrent::Endpoint,
     "a builder that binds a channel handle to a context once: a stage mint or a session mint consumes it, the "
     "stage or the session handle that comes out folds, and an endpoint is never a template argument of a kernel "
     "signature"},
    {^^::fixy::concurrent::EndpointPack,
     "a list of endpoint types that the stage gate reads at compile time: it has no member, and it is never a value"},
    {^^::fixy::concurrent::EndpointDoor,
     "the door of the endpoint mint: it has static members only, no object of it exists, and it is never a value"},
    {^^::fixy::concurrent::StageEndpointDoor,
     "the door of the stage mints that take endpoints: it has static members only, no object of it exists, and it "
     "is never a value"},

    {^^::fixy::handle::Once, kDescriptor},
    {^^::fixy::handle::OneShotFlag, kDescriptor},
    {^^::fixy::handle::PublishCommitCell,
     "a publication counter: its Tag names one pipeline and its WriteAuth one stage, it holds a count and not a "
     "value, and it is never a template argument of a kernel signature"},

    {^^::fixy::io::IoUringRing, kDescriptor},
    {^^::fixy::fs::OwnedFd, kDescriptor},
    {^^::fixy::fs::Dirfd, kDescriptor},
    {^^::fixy::fs::FileDoor, kDescriptorDoor},
    {^^::fixy::net::SocketDoor, kDescriptorDoor},
    {^^::fixy::time::PtpDeviceDoor, kDescriptorDoor},
    {^^::fixy::fp::CanonicalizeRecipeSpec, kPayload},
    {^^::fixy::fp::ReduceResult, kPayload},

    {^^::fixy::spin::UnwitnessedSpinLock, kDescriptor},
    {^^::fixy::spin::UnwitnessedBlockingLock, kDescriptor},
    {^^::fixy::time::ClockReader, kFactory},
    {^^::fixy::time::TscReader, kFactory},
    {^^::fixy::time::PtpClockReader, kFactory},
    {^^::fixy::time::BoundedSleeper, kFactory},
    {^^::fixy::sched::SchedProofDoor,
     "a door of the scheduling proof mints: it has static members only, no object of it exists, and it is never a "
     "value"},
    {^^::fixy::sched::PriorAffinity,
     "the mask that one pin replaced: it holds the authority to undo that pin on its thread, and it is never a value "
     "in a kernel signature"},
    {^^::fixy::witness::AtProtocol, kVocabulary},
    {^^::fixy::witness::UnderRow, kVocabulary},
    {^^::fixy::cipher::durable::warm_writer_stance, kVocabulary},
    {^^::fixy::cipher::durable::cold_writer_stance, kVocabulary},
    {^^::fixy::cipher::durable::head_advance_stance, kVocabulary},
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
