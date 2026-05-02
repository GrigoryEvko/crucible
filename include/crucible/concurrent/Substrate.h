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

// ── Two distinct footprint metrics ──────────────────────────────────
//
// A queue/ring substrate has TWO independent byte budgets that often
// confuse "fit" checks.  Make them distinct, named, and documented.
//
// 1. channel_byte_footprint_v<S> — TOTAL static storage (ring buffer
//    or single Snapshot slot).  Diagnostic for:
//      * NUMA placement decisions (where to allocate the channel)
//      * Allocator-tier selection (huge-page hint?  arena fit?)
//      * The cliff recommender (storage > L2/core ⇒ benefits from
//        sharding/parallelization)
//    Does NOT include alignment padding, control-block overhead, or
//    per-thread cached counters — captures the dominant data
//    footprint only.  Snapshot has capacity = 0 in substrate_traits
//    but holds one T slot, so total is sizeof(T).
//
// 2. per_call_working_set_v<S> — bytes the producer/consumer's HOT
//    PATH actually touches per try_send / try_recv / publish / load
//    call.  Independent of total capacity: a 4 MB SpscRing's
//    producer touches the SAME cache lines per call as a 4 KB
//    SpscRing's producer (head counter + tail counter + 1
//    destination cell).  This is what hot-path Ctx residency
//    (HotFgCtx = L1Resident, BgDrainCtx = L2Resident, …) should be
//    checked against — NOT total storage.  See SubstrateCtxFit.h.
//
// The two metrics serve orthogonal questions:
//
//   "Does my hot path stay cache-resident?"
//     → per_call_working_set_v<S> vs ctx_residency_tier<HotCtx>()
//
//   "Will the whole channel fit in L3?  Should I shard?"
//     → channel_byte_footprint_v<S> vs ParallelismRule cache bounds
//
// Confusing them rejects valid configurations (large-N SpscRing on
// HotFgCtx — the hot path IS L1d-fitting because per_call_WS << 32
// KB even when total is 4 MB).

template <IsSubstrate S>
inline constexpr std::size_t channel_byte_footprint_v =
    substrate_capacity_v<S> > 0
        ? sizeof(substrate_value_type_t<S>) * substrate_capacity_v<S>
        : sizeof(substrate_value_type_t<S>);  // Snapshot single-slot

// ── per_call_working_set_v<S>: hot-path access footprint ───────────
//
// Bytes the producer/consumer touches per single op.  Counts:
//   * 1 cache line for each cross-thread atomic counter the hot path
//     reads/writes (head, tail, threshold, sequence, …).  All
//     primitives use alignas(64) on those counters per CLAUDE.md
//     §VIII (false-sharing isolation), so each costs one full line.
//   * The destination/source cell rounded up to a cache line
//     (sizeof(T) rounded up to the nearest 64-byte boundary).  T
//     values smaller than 64 B still occupy one whole line under
//     the alignas(64) cell layout used by every Permissioned*
//     primitive's underlying ring.
//
// Does NOT count:
//   * Total ring capacity (irrelevant — producer never reads the
//     other 99.9% of slots in steady state).
//   * Per-thread caches like SpscRing's `cached_tail_` (subsumed
//     under the head/tail line counts).
//   * Padding outside the touched lines.
//
// The numbers are CONSERVATIVE upper bounds, not measured truth.
// Every value here errs on the side of "touches more lines than it
// actually does," matching SubstrateFitsCtxResidency's safety
// posture (reject MORE than the runtime check would, never fewer).
//
// Per-topology breakdown (one cache line = 64 B; computed from each
// primitive's hot-path source):
//
//   OneToOne (SpscRing):
//     1 line (head_) + 1 line (tail_) + 1 line (cell)        = 192 B
//
//   ManyToOne (MpscRing):
//     1 line (head_) + 1 line (tail_) +
//     1 line (cell sequence atomic) + 1 line (cell payload)  = 256 B
//
//   ManyToMany (MpmcRing/SCQ):
//     1 line (Tail) + 1 line (Head) + 1 line (Threshold) +
//     1 line (cell state + payload)                          = 256 B
//
//   OneToMany_Latest (AtomicSnapshot):
//     1 line (seq counter) + ceil(sizeof(T)/64) lines for the
//     buffer (the writer memcpys the whole T; readers memcpy back).
//     Sized at min 1 line for tiny T, capped at 5 lines (320 B)
//     for the AtomicSnapshot 256-B max-T limit.
//
//   WorkStealing (ChaseLevDeque):
//     1 line (top_) + 1 line (bottom_) + 1 line (cell)       = 192 B
//
// All values fit comfortably within ANY supported host's L1d
// (conservative bound 32 KB, real-world 32–64 KB) — every hot-path
// primitive in the zoo is L1d-resident on its access pattern,
// independent of total channel capacity.  Substrate-fit-vs-ctx
// rejection should ONLY fire when sizeof(T) is so large that one
// cell exceeds the tier's bound (e.g., a 64-KB struct in a HotFgCtx
// would correctly fail the L1d gate).

namespace detail {

// Cache line size used for per-call WS estimation.  Matches the
// alignas(64) discipline pervasive in the Permissioned* primitives
// (CLAUDE.md §VIII — x86-64 + Graviton/Neoverse 64 B; Apple
// Silicon's 128 B handled separately when it lands).
inline constexpr std::size_t kHotPathCacheLineBytes = 64;

// Round sizeof(T) up to the nearest cache line.  A 4 B int still
// occupies one full cache line under cell-aligned layout.
[[nodiscard]] consteval std::size_t
cell_line_footprint(std::size_t value_bytes) noexcept {
    if (value_bytes == 0) return 0;
    return ((value_bytes + kHotPathCacheLineBytes - 1)
                / kHotPathCacheLineBytes) * kHotPathCacheLineBytes;
}

}  // namespace detail

// Primary per_call_working_set computation, dispatched on topology.
template <IsSubstrate S>
inline constexpr std::size_t per_call_working_set_v = [] consteval {
    constexpr std::size_t cell =
        detail::cell_line_footprint(sizeof(substrate_value_type_t<S>));
    constexpr ChannelTopology topo = substrate_topology_v<S>;
    if constexpr (topo == ChannelTopology::OneToOne) {
        // SpscRing: head + tail + cell.
        return 2 * detail::kHotPathCacheLineBytes + cell;
    } else if constexpr (topo == ChannelTopology::ManyToOne) {
        // MpscRing: head + tail + cell-sequence + cell-payload.
        return 3 * detail::kHotPathCacheLineBytes + cell;
    } else if constexpr (topo == ChannelTopology::ManyToMany) {
        // MpmcRing/SCQ: Tail + Head + Threshold + cell.
        return 3 * detail::kHotPathCacheLineBytes + cell;
    } else if constexpr (topo == ChannelTopology::OneToMany_Latest) {
        // AtomicSnapshot: seq + memcpy(T).  cell is at least one
        // line; for small T this is sizeof(seq line) + 1 cell line.
        return detail::kHotPathCacheLineBytes + (cell == 0 ? detail::kHotPathCacheLineBytes : cell);
    } else /* WorkStealing */ {
        // ChaseLevDeque owner-side: top + bottom + cell.
        return 2 * detail::kHotPathCacheLineBytes + cell;
    }
}();

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

// ── recommend_topology_for_workload — the cliff-aware variant ──────
//
// Same call signature plus a workload byte-budget.  Layers
// ParallelismRule's cache-tier rule on top of cardinality:
//
//   ws ≤ conservative_l2_per_core (256 KB)
//     → cardinality dictates topology (sequential is optimal;
//       sharding wouldn't pay back its overhead)
//
//   ws > conservative_l2_per_core
//     → recommend ManyToMany even when a single producer/consumer
//       was requested, BECAUSE crossing the cliff means the workload
//       benefits from sharding/parallelization.  Caller should then
//       wire ShardedSpscGrid<T, M, N> rather than a bare OneToOne.
//
// "OneToOne / ManyToOne / OneToMany_Latest above the cliff" is a
// VALID configuration (small per-call WS, large total storage —
// e.g., TraceRing); the recommender just emits "consider sharding
// if you have multiple bg drainers."  It's a SUGGESTION layer for
// the developer; SubstrateFitsCtxResidency is the HARD GATE on
// per-call hot-path WS (separate concern).
//
// Conservative bound used here matches SubstrateCtxFit.h's
// conservative_l2_per_core (256 KB) — the cliff per ParallelismRule
// §27_04.

inline constexpr std::size_t conservative_cliff_l2_per_core = 256ULL * 1024;

[[nodiscard]] consteval ChannelTopology recommend_topology_for_workload(
    std::size_t num_producers,
    std::size_t num_consumers,
    std::size_t workload_bytes,
    bool        latest_only = false) noexcept {
    // Below the cliff: cardinality alone decides.
    if (workload_bytes <= conservative_cliff_l2_per_core) {
        return recommend_topology(num_producers, num_consumers, latest_only);
    }
    // Above the cliff: WS benefits from parallelization.  Snapshot
    // (latest_only) is a special case — it has no FIFO ordering to
    // shard, so we keep OneToMany_Latest regardless of size.
    if (num_producers == 1 && num_consumers > 1 && latest_only) {
        return ChannelTopology::OneToMany_Latest;
    }
    // Single producer × single consumer ABOVE the cliff is the
    // TraceRing-style pattern: OneToOne is structurally correct
    // (one-thread-each-side) but per_call_working_set_v stays
    // L1-resident.  Recommend OneToOne; the storage-tier check
    // (StorageFitsCtxResidency in SubstrateCtxFit.h) catches the
    // total-storage decision separately.
    if (num_producers == 1 && num_consumers == 1) {
        return ChannelTopology::OneToOne;
    }
    // Genuine N×M above the cliff: sharding/MPMC wins.
    if (num_producers > 1 && num_consumers == 1) return ChannelTopology::ManyToOne;
    if (num_producers > 1 && num_consumers > 1) return ChannelTopology::ManyToMany;
    if (num_producers == 1 && num_consumers > 1) return ChannelTopology::ManyToMany;
    // 0 producer or 0 consumer: caller error; mirror recommend_topology.
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

// ── per_call_working_set_v pinning ──────────────────────────────────
//
// Numbers below are the conservative upper bounds documented in the
// per_call_working_set_v doc-block (head/tail/threshold lines + cell
// rounded up to a 64 B cache line).  Drift in the underlying ring's
// hot-path-touched-line count fires here.
//
// All values are well under the L1d conservative bound (32 KB), so
// every primitive in the zoo passes SubstrateFitsCtxResidency on
// HotFgCtx (L1Resident) regardless of total channel capacity.

// kHotPathCacheLineBytes lives in crucible::concurrent::detail (the
// parent of this self-test namespace).  Spell it fully-qualified to
// avoid the unqualified `detail::` lookup landing inside the
// self-test namespace itself.
inline constexpr std::size_t kLine = ::crucible::concurrent::detail::kHotPathCacheLineBytes;

// SPSC: 2 lines (head/tail) + 1 cell line (sizeof(int) padded to 64 B).
static_assert(per_call_working_set_v<SpscT> == 3 * kLine);

// MPSC: 3 lines (head/tail/cell-sequence) + 1 cell line.
static_assert(per_call_working_set_v<MpscT> == 4 * kLine);

// MPMC (SCQ): 3 lines (Tail/Head/Threshold) + 1 cell line.
static_assert(per_call_working_set_v<MpmcT> == 4 * kLine);

// Snapshot<double>: seq line + 1 cell line (8 B padded to 64 B).
static_assert(per_call_working_set_v<SnapT> == 2 * kLine);

// ChaseLevDeque: 2 lines (top/bottom) + 1 cell line.
static_assert(per_call_working_set_v<DequeT> == 3 * kLine);

// Cell-line scaling: a 100 B T occupies 2 cache lines (rounded up
// from 100 to 128).  An SPSC<100B-struct, N> hot path therefore
// touches 2 head/tail lines + 2 cell lines = 256 B regardless of N.
struct OneHundredByteValue {
    char pad[100];
    auto operator<=>(OneHundredByteValue const&) const = default;
};
using BigCellSpsc = Substrate_t<ChannelTopology::OneToOne,
                                 OneHundredByteValue, 16, VesselOpStream>;
static_assert(per_call_working_set_v<BigCellSpsc>
              == 2 * kLine + 128);  // 2 lines + 2 lines

// Per-call WS is INDEPENDENT of capacity.  An SPSC<int, 1M> has the
// SAME per-call WS as an SPSC<int, 64> — that's the load-bearing
// claim of the metric.  4 MB total storage but only 192 B touched
// per push.
using HugeSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024 * 1024, VesselOpStream>;
static_assert(channel_byte_footprint_v<HugeSpsc>  == 4 * 1024 * 1024);
static_assert(per_call_working_set_v<HugeSpsc>    == 3 * kLine);

// ── recommend_topology_for_workload pinning ────────────────────────
//
// Below the cliff (≤ 256 KB): cardinality dictates.  Above the
// cliff: 1×1 stays OneToOne (TraceRing-style), genuine N×M climbs
// to MPSC/MPMC.  Latest-only stays Snapshot regardless.

inline constexpr std::size_t kSmall = 4 * 1024;        //   4 KB
inline constexpr std::size_t kMid   = 64 * 1024;       //  64 KB (still below cliff)
inline constexpr std::size_t kBig   = 4 * 1024 * 1024; //   4 MB (above cliff)

// Below the cliff: identical to recommend_topology.
static_assert(recommend_topology_for_workload(1, 1, kSmall) == ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(4, 1, kSmall) == ChannelTopology::ManyToOne);
static_assert(recommend_topology_for_workload(8, 8, kMid)   == ChannelTopology::ManyToMany);
static_assert(recommend_topology_for_workload(1, 4, kMid, true) == ChannelTopology::OneToMany_Latest);

// Above the cliff:
// 1×1 stays OneToOne (TraceRing-style large SPSC is valid; per-call
// WS L1-resident, total storage L3-class).
static_assert(recommend_topology_for_workload(1, 1, kBig)  == ChannelTopology::OneToOne);
// N×1 climbs to MPSC.
static_assert(recommend_topology_for_workload(4, 1, kBig)  == ChannelTopology::ManyToOne);
// N×N climbs to MPMC.
static_assert(recommend_topology_for_workload(8, 8, kBig)  == ChannelTopology::ManyToMany);
// 1×N latest-only stays Snapshot regardless of size.
static_assert(recommend_topology_for_workload(1, 4, kBig, true) == ChannelTopology::OneToMany_Latest);
// 1×N stream above the cliff: ManyToMany (SPMC degeneration).
static_assert(recommend_topology_for_workload(1, 4, kBig, false) == ChannelTopology::ManyToMany);

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
