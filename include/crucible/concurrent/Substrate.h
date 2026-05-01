#pragma once

// ── crucible::concurrent::Substrate — topology → Permissioned* ──────
//
// Type-level metafunction mapping a topology pattern (one-to-one /
// many-to-one / one-to-many-latest / many-to-many / work-stealing)
// to the matching Permissioned*Channel / Snapshot / Deque type.
// Lets call sites express channel topology declaratively without
// spelling each Permissioned* template.
//
//   Axiom coverage: TypeSafe — ChannelTopology enum is closed; non-axis
//                   patterns fail at the unspecialized primary
//                   template (incomplete-type diagnostic).
//                   InitSafe — pure metafunction; no construction.
//                   DetSafe — consteval-only.
//   Runtime cost:   zero.  sizeof(Substrate_t<...>) ==
//                   sizeof(underlying Permissioned* type), no
//                   wrapper indirection.
//
// ── Pattern coverage ────────────────────────────────────────────────
//
//   ChannelTopology::OneToOne          → PermissionedSpscChannel<T, Cap, UserTag>
//   ChannelTopology::ManyToOne         → PermissionedMpscChannel<T, Cap, UserTag>
//   ChannelTopology::OneToMany_Latest  → PermissionedSnapshot<T, UserTag>
//   ChannelTopology::ManyToMany        → PermissionedMpmcChannel<T, Cap, UserTag>
//   ChannelTopology::WorkStealing      → PermissionedChaseLevDeque<T, Cap, UserTag>
//
// Sharded grid (M producers × N consumers indexed) and priority-
// bucketed Calendar grid take additional template parameters
// (M / N / Routing / KeyExtractor / Quantum) that don't fit a
// uniform 4-arg metafunction.  Use the existing
// PermissionedShardedGrid<...> / PermissionedShardedCalendarGrid<...>
// templates directly for those — Substrate explicitly does NOT
// alias them.
//
// ── Why a separate header (vs concurrent/Queue.h) ──────────────────
//
// Queue.h ships a USER-FACING facade `Queue<T, Kind>` over the same
// Permissioned* family — production code that wants to instantiate
// a queue picks `Queue<int, kind::spsc<1024>>`.  Substrate is the
// META-FUNCTION yielding the underlying Permissioned* TYPE — useful
// for downstream consumers (Tier 2 Endpoint<Substrate, Direction,
// Ctx>) that need the type without the Queue wrapper layer.
// Complementary, not redundant.
//
// ── Recognition + extractors ────────────────────────────────────────
//
//   IsSubstrate<S>               concept — S is one of the recognized
//                                Permissioned* types.
//   substrate_topology_v<S>      → ChannelTopology enum value.
//   substrate_value_type_t<S>    → element type T.
//   substrate_user_tag_t<S>      → UserTag identity tag.
//   substrate_capacity_v<S>      → compile-time capacity (0 for
//                                Snapshot which has no ring).

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::concurrent {

// ── ChannelTopology enum ───────────────────────────────────────────────────

enum class ChannelTopology : std::uint8_t {
    OneToOne          = 0,   // SPSC ring                — single producer, single consumer
    ManyToOne         = 1,   // MPSC ring                — many producers, one consumer
    OneToMany_Latest  = 2,   // SWMR snapshot            — one writer, many readers (latest value)
    ManyToMany        = 3,   // MPMC ring                — many producers, many consumers
    WorkStealing      = 4,   // Chase-Lev deque          — owner pushes/pops, thieves steal
};

// ── Substrate metafunction ──────────────────────────────────────────
//
// Primary template undefined; partial specs map each ChannelTopology value
// to the matching Permissioned* class.  Calls with an unspecialized
// (ChannelTopology, T, Cap, UserTag) tuple hit the primary and fail with
// "incomplete type" — correct, since there's no Permissioned* for
// non-axis patterns.

template <ChannelTopology Pat, class T, std::size_t Cap, class UserTag>
struct Substrate;

template <class T, std::size_t Cap, class UserTag>
struct Substrate<ChannelTopology::OneToOne, T, Cap, UserTag> {
    using type = PermissionedSpscChannel<T, Cap, UserTag>;
};

template <class T, std::size_t Cap, class UserTag>
struct Substrate<ChannelTopology::ManyToOne, T, Cap, UserTag> {
    using type = PermissionedMpscChannel<T, Cap, UserTag>;
};

template <class T, std::size_t Cap, class UserTag>
struct Substrate<ChannelTopology::OneToMany_Latest, T, Cap, UserTag> {
    // Snapshot has no ring capacity; Cap is ignored at this layer
    // (caller still supplies the canonical 4-arg form for uniform
    // dispatch via Substrate).
    using type = PermissionedSnapshot<T, UserTag>;
};

template <class T, std::size_t Cap, class UserTag>
struct Substrate<ChannelTopology::ManyToMany, T, Cap, UserTag> {
    using type = PermissionedMpmcChannel<T, Cap, UserTag>;
};

template <class T, std::size_t Cap, class UserTag>
struct Substrate<ChannelTopology::WorkStealing, T, Cap, UserTag> {
    using type = PermissionedChaseLevDeque<T, Cap, UserTag>;
};

template <ChannelTopology Pat, class T, std::size_t Cap, class UserTag>
using Substrate_t = typename Substrate<Pat, T, Cap, UserTag>::type;

// ── Recognition + extractors ────────────────────────────────────────
//
// Pattern-match per Permissioned* template to project the topology /
// value type / user tag / capacity from a substrate type.  Primary
// templates undefined on non-Permissioned* T, so misuse fires at
// instantiation rather than substituting silently.

template <class S> struct substrate_traits;

template <class T, std::size_t Cap, class UserTag>
struct substrate_traits<PermissionedSpscChannel<T, Cap, UserTag>> {
    static constexpr ChannelTopology     topology = ChannelTopology::OneToOne;
    static constexpr std::size_t  capacity = Cap;
    using value_type = T;
    using user_tag   = UserTag;
};

template <class T, std::size_t Cap, class UserTag>
struct substrate_traits<PermissionedMpscChannel<T, Cap, UserTag>> {
    static constexpr ChannelTopology     topology = ChannelTopology::ManyToOne;
    static constexpr std::size_t  capacity = Cap;
    using value_type = T;
    using user_tag   = UserTag;
};

template <class T, class UserTag>
struct substrate_traits<PermissionedSnapshot<T, UserTag>> {
    static constexpr ChannelTopology     topology = ChannelTopology::OneToMany_Latest;
    static constexpr std::size_t  capacity = 0;  // single slot
    using value_type = T;
    using user_tag   = UserTag;
};

template <class T, std::size_t Cap, class UserTag>
struct substrate_traits<PermissionedMpmcChannel<T, Cap, UserTag>> {
    static constexpr ChannelTopology     topology = ChannelTopology::ManyToMany;
    static constexpr std::size_t  capacity = Cap;
    using value_type = T;
    using user_tag   = UserTag;
};

template <class T, std::size_t Cap, class UserTag>
struct substrate_traits<PermissionedChaseLevDeque<T, Cap, UserTag>> {
    static constexpr ChannelTopology     topology = ChannelTopology::WorkStealing;
    static constexpr std::size_t  capacity = Cap;
    using value_type = T;
    using user_tag   = UserTag;
};

// is_substrate_v: true iff S has a substrate_traits specialization.
template <class S, class = void>
struct is_substrate : std::false_type {};

template <class S>
struct is_substrate<S, std::void_t<typename substrate_traits<S>::value_type>>
    : std::true_type {};

template <class S> inline constexpr bool is_substrate_v = is_substrate<S>::value;
template <class S> concept IsSubstrate = is_substrate_v<S>;

template <IsSubstrate S>
inline constexpr ChannelTopology substrate_topology_v = substrate_traits<S>::topology;

template <IsSubstrate S>
using substrate_value_type_t = typename substrate_traits<S>::value_type;

template <IsSubstrate S>
using substrate_user_tag_t = typename substrate_traits<S>::user_tag;

template <IsSubstrate S>
inline constexpr std::size_t substrate_capacity_v = substrate_traits<S>::capacity;

// ── Working-set footprint ───────────────────────────────────────────
//
// channel_byte_footprint_v<S>: bytes of channel storage (ring buffer
// or single Snapshot slot).  Used by SubstrateFitsCtxResidency in
// concurrent/SubstrateCtxFit.h to verify a Ctx's claimed residency
// tier can hold the channel's working set.  Does NOT include
// alignment padding, control-block overhead, or per-thread cached
// counters — captures the dominant data footprint only.
//
// Snapshot has capacity = 0 in substrate_traits but holds one T
// slot, so the footprint is sizeof(T).  Ring substrates have
// capacity = N and the footprint is sizeof(T) * N.

template <IsSubstrate S>
inline constexpr std::size_t channel_byte_footprint_v =
    substrate_capacity_v<S> > 0
        ? sizeof(substrate_value_type_t<S>) * substrate_capacity_v<S>
        : sizeof(substrate_value_type_t<S>);  // Snapshot single-slot

// ── Topology recommendation ─────────────────────────────────────────
//
// recommend_topology(num_producers, num_consumers, latest_only) —
// pick the canonical ChannelTopology for a given producer/consumer
// cardinality.  consteval; usable at template-instantiation sites
// that want to derive the topology from compile-time configuration.
//
//   1 producer, 1 consumer            → OneToOne (SPSC)
//   N producers, 1 consumer           → ManyToOne (MPSC)
//   N producers, N consumers          → ManyToMany (MPMC)
//   1 producer, N consumers, latest    → OneToMany_Latest (Snapshot)
//   1 producer, N consumers, !latest  → ManyToMany (caller wants
//                                       stream fan-out; closest
//                                       primitive is MPMC at N=1
//                                       producer side)
//
// WorkStealing is NOT recommended automatically — it requires a
// task-shape decision (variable-cost work items distributed across
// thieves) that's orthogonal to producer/consumer cardinality.
// Callers wanting Chase-Lev semantics select WorkStealing
// explicitly.

[[nodiscard]] consteval ChannelTopology recommend_topology(
    std::size_t num_producers,
    std::size_t num_consumers,
    bool        latest_only = false) noexcept {
    if (num_producers == 1 && num_consumers == 1) return ChannelTopology::OneToOne;
    if (num_producers >  1 && num_consumers == 1) return ChannelTopology::ManyToOne;
    if (num_producers == 1 && num_consumers >  1 && latest_only)
        return ChannelTopology::OneToMany_Latest;
    if (num_producers >  1 && num_consumers >  1) return ChannelTopology::ManyToMany;
    // 1 producer, N consumers without latest_only:
    // closest match is ManyToMany at producer-side N=1 (the MPMC
    // ring degenerates to SPMC when only one producer is active).
    if (num_producers == 1 && num_consumers >  1) return ChannelTopology::ManyToMany;
    // num_producers == 0 || num_consumers == 0: caller error;
    // default to OneToOne which static_asserts on capacity > 0
    // downstream.
    return ChannelTopology::OneToOne;
}

// ── ChannelTopology discrimination concepts ────────────────────────────────
//
// Selective dispatch on a substrate's topology — useful for code that
// needs different setup per pattern (e.g., MPMC needs cookie
// fingerprints; Snapshot needs different reader-handle semantics).

template <class S>
concept IsOneToOneSubstrate = IsSubstrate<S>
                           && substrate_topology_v<S> == ChannelTopology::OneToOne;
template <class S>
concept IsManyToOneSubstrate = IsSubstrate<S>
                            && substrate_topology_v<S> == ChannelTopology::ManyToOne;
template <class S>
concept IsOneToManyLatestSubstrate = IsSubstrate<S>
                                  && substrate_topology_v<S> == ChannelTopology::OneToMany_Latest;
template <class S>
concept IsManyToManySubstrate = IsSubstrate<S>
                             && substrate_topology_v<S> == ChannelTopology::ManyToMany;
template <class S>
concept IsWorkStealingSubstrate = IsSubstrate<S>
                               && substrate_topology_v<S> == ChannelTopology::WorkStealing;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::substrate_self_test {

struct VesselOpStream {};
struct ConductorCompile {};

// ── Substrate maps each ChannelTopology to the right Permissioned* ─────────

static_assert(std::is_same_v<Substrate_t<ChannelTopology::OneToOne, int, 1024, VesselOpStream>,
                              PermissionedSpscChannel<int, 1024, VesselOpStream>>);

static_assert(std::is_same_v<Substrate_t<ChannelTopology::ManyToOne, int, 256, VesselOpStream>,
                              PermissionedMpscChannel<int, 256, VesselOpStream>>);

static_assert(std::is_same_v<Substrate_t<ChannelTopology::OneToMany_Latest, double, 0, ConductorCompile>,
                              PermissionedSnapshot<double, ConductorCompile>>);

static_assert(std::is_same_v<Substrate_t<ChannelTopology::ManyToMany, int, 64, VesselOpStream>,
                              PermissionedMpmcChannel<int, 64, VesselOpStream>>);

static_assert(std::is_same_v<Substrate_t<ChannelTopology::WorkStealing, int, 128, ConductorCompile>,
                              PermissionedChaseLevDeque<int, 128, ConductorCompile>>);

// ── Extractors round-trip ───────────────────────────────────────────
using SpscT = Substrate_t<ChannelTopology::OneToOne, int, 1024, VesselOpStream>;
static_assert(substrate_topology_v<SpscT>      == ChannelTopology::OneToOne);
static_assert(substrate_capacity_v<SpscT>      == 1024);
static_assert(std::is_same_v<substrate_value_type_t<SpscT>, int>);
static_assert(std::is_same_v<substrate_user_tag_t<SpscT>,  VesselOpStream>);

using SnapT = Substrate_t<ChannelTopology::OneToMany_Latest, double, 0, ConductorCompile>;
static_assert(substrate_topology_v<SnapT> == ChannelTopology::OneToMany_Latest);
static_assert(substrate_capacity_v<SnapT> == 0);   // Snapshot has no ring
static_assert(std::is_same_v<substrate_value_type_t<SnapT>, double>);

// ── Recognition ─────────────────────────────────────────────────────
static_assert( IsSubstrate<SpscT>);
static_assert( IsSubstrate<SnapT>);
static_assert(!IsSubstrate<int>);
static_assert(!IsSubstrate<void>);

// ── ChannelTopology discrimination ─────────────────────────────────────────
static_assert( IsOneToOneSubstrate<SpscT>);
static_assert(!IsOneToOneSubstrate<SnapT>);
static_assert( IsOneToManyLatestSubstrate<SnapT>);
static_assert(!IsOneToManyLatestSubstrate<SpscT>);

using MpscT = Substrate_t<ChannelTopology::ManyToOne, int, 64, VesselOpStream>;
static_assert( IsManyToOneSubstrate<MpscT>);
static_assert(!IsManyToOneSubstrate<SpscT>);

using MpmcT = Substrate_t<ChannelTopology::ManyToMany, int, 32, VesselOpStream>;
static_assert( IsManyToManySubstrate<MpmcT>);

using DequeT = Substrate_t<ChannelTopology::WorkStealing, int, 256, ConductorCompile>;
static_assert( IsWorkStealingSubstrate<DequeT>);

// ── Sizeof preserved (no wrapper indirection) ───────────────────────
static_assert(sizeof(Substrate_t<ChannelTopology::OneToOne, int, 1024, VesselOpStream>) ==
              sizeof(PermissionedSpscChannel<int, 1024, VesselOpStream>));

// ── channel_byte_footprint_v pinning ───────────────────────────────
//
// SPSC<int, 1024> = 4 KB.  MPMC<int, 64> = 256 B.  Snapshot<double>
// = 8 B (single slot).  Catches drift in either sizeof(T) or capacity.

static_assert(channel_byte_footprint_v<SpscT>  == sizeof(int) * 1024);
static_assert(channel_byte_footprint_v<MpscT>  == sizeof(int) * 64);
static_assert(channel_byte_footprint_v<SnapT>  == sizeof(double));      // single slot
static_assert(channel_byte_footprint_v<MpmcT>  == sizeof(int) * 32);
static_assert(channel_byte_footprint_v<DequeT> == sizeof(int) * 256);

// ── recommend_topology pinning ──────────────────────────────────────
static_assert(recommend_topology(1, 1)         == ChannelTopology::OneToOne);
static_assert(recommend_topology(4, 1)         == ChannelTopology::ManyToOne);
static_assert(recommend_topology(8, 8)         == ChannelTopology::ManyToMany);
static_assert(recommend_topology(1, 4, true)   == ChannelTopology::OneToMany_Latest);
static_assert(recommend_topology(1, 4, false)  == ChannelTopology::ManyToMany);
static_assert(recommend_topology(1, 1, true)   == ChannelTopology::OneToOne);  // 1-1 trumps latest

}  // namespace detail::substrate_self_test

// ── Runtime smoke test ──────────────────────────────────────────────

[[gnu::cold]] inline void runtime_smoke_test_substrate() noexcept {
    // Type-level lookups don't need actual instances; the smoke test
    // confirms the metafunction resolves to a constructible type for
    // each topology and the extractors agree.
    struct UserTag {};

    using SpscT = Substrate_t<ChannelTopology::OneToOne, int, 64, UserTag>;
    using MpscT = Substrate_t<ChannelTopology::ManyToOne, int, 64, UserTag>;
    using SnapT = Substrate_t<ChannelTopology::OneToMany_Latest, int, 0, UserTag>;
    using MpmcT = Substrate_t<ChannelTopology::ManyToMany, int, 64, UserTag>;
    using DequeT = Substrate_t<ChannelTopology::WorkStealing, int, 64, UserTag>;

    // Type-level coverage at runtime context: ensure every
    // metafunction resolution is callable.
    static_assert(substrate_topology_v<SpscT>  == ChannelTopology::OneToOne);
    static_assert(substrate_topology_v<MpscT>  == ChannelTopology::ManyToOne);
    static_assert(substrate_topology_v<SnapT>  == ChannelTopology::OneToMany_Latest);
    static_assert(substrate_topology_v<MpmcT>  == ChannelTopology::ManyToMany);
    static_assert(substrate_topology_v<DequeT> == ChannelTopology::WorkStealing);

    // Concept-based capability checks.
    static_assert( IsSubstrate<SpscT>);
    static_assert( IsOneToOneSubstrate<SpscT>);
    static_assert(!IsManyToManySubstrate<SpscT>);
}

}  // namespace crucible::concurrent
