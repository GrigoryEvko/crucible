#pragma once

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/WorkingSet.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::concurrent {

enum class ChannelTopology : std::uint8_t {
    OneToOne = 0,
    ManyToOne = 1,
    OneToMany_Latest = 2,
    ManyToMany = 3,
    WorkStealing = 4,
};

// Sharded and priority-bucketed grids take extra parameters — shard counts, a
// routing policy, a key extractor, a time quantum — that do not fit this
// uniform four-argument form, so they are deliberately absent and must be
// named directly.

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

template <class S>
struct substrate_traits;

template <class T, std::size_t Cap, class UserTag>
struct substrate_traits<PermissionedSpscChannel<T, Cap, UserTag>> {
    static constexpr ChannelTopology topology = ChannelTopology::OneToOne;
    static constexpr std::size_t capacity = Cap;
    using value_type = T;
    using user_tag = UserTag;
};

template <class T, std::size_t Cap, class UserTag>
struct substrate_traits<PermissionedMpscChannel<T, Cap, UserTag>> {
    static constexpr ChannelTopology topology = ChannelTopology::ManyToOne;
    static constexpr std::size_t capacity = Cap;
    using value_type = T;
    using user_tag = UserTag;
};

template <class T, class UserTag>
struct substrate_traits<PermissionedSnapshot<T, UserTag>> {
    static constexpr ChannelTopology topology = ChannelTopology::OneToMany_Latest;
    static constexpr std::size_t capacity = 0;  // no ring: one live slot
    using value_type = T;
    using user_tag = UserTag;
};

template <class T, std::size_t Cap, class UserTag>
struct substrate_traits<PermissionedMpmcChannel<T, Cap, UserTag>> {
    static constexpr ChannelTopology topology = ChannelTopology::ManyToMany;
    static constexpr std::size_t capacity = Cap;
    using value_type = T;
    using user_tag = UserTag;
};

template <class T, std::size_t Cap, class UserTag>
struct substrate_traits<PermissionedChaseLevDeque<T, Cap, UserTag>> {
    static constexpr ChannelTopology topology = ChannelTopology::WorkStealing;
    static constexpr std::size_t capacity = Cap;
    using value_type = T;
    using user_tag = UserTag;
};

template <class S, class = void>
struct is_substrate : std::false_type {};

template <class S>
struct is_substrate<S, std::void_t<typename substrate_traits<S>::value_type>> : std::true_type {};

template <class S>
inline constexpr bool is_substrate_v = is_substrate<S>::value;
template <class S>
concept IsSubstrate = is_substrate_v<S>;

template <IsSubstrate S>
inline constexpr ChannelTopology substrate_topology_v = substrate_traits<S>::topology;

template <IsSubstrate S>
using substrate_value_type_t = typename substrate_traits<S>::value_type;

template <IsSubstrate S>
using substrate_user_tag_t = typename substrate_traits<S>::user_tag;

template <IsSubstrate S>
inline constexpr std::size_t substrate_capacity_v = substrate_traits<S>::capacity;

// Two independent byte budgets, easily confused with each other.
//
// channel_byte_footprint_v is total static storage: the ring buffer, or the
// one live slot of a snapshot.  It answers where to place the channel and
// whether the whole thing fits in a shared cache.  It excludes alignment
// padding, control blocks and per-thread cached counters.
//
// per_call_working_set_v is what one send, receive, publish or load touches.
// It does not scale with capacity: the producer on a 4 MB ring touches the
// same lines as the producer on a 4 KB ring.  Hot-path residency belongs
// against this metric.  Checking residency against total storage instead
// rejects valid configurations, such as a large ring whose per-call footprint
// is L1-resident.

template <IsSubstrate S>
inline constexpr std::size_t channel_byte_footprint_v =
    substrate_capacity_v<S> > 0 ? sizeof(substrate_value_type_t<S>) * substrate_capacity_v<S>
                                : sizeof(substrate_value_type_t<S>);

// The per-call estimate charges one whole cache line for every cross-thread
// atomic counter the hot path reads or writes, because each of those counters
// is line-aligned to keep it off its neighbours' lines.  The cell is likewise
// charged rounded up to a line.  Total capacity does not enter: in steady
// state neither side reads the slots it is not working on.
//
// Every value is a deliberate upper bound rather than a measurement, so a
// residency gate built on it rejects more configurations than a runtime check
// would and never fewer.  The bounds are small enough that a gate only fires
// once one cell on its own exceeds the tier.

namespace detail {

inline constexpr std::size_t kHotPathCacheLineBytes = ::crucible::concurrent::hot_path_cache_line_bytes;

[[nodiscard]] consteval std::size_t cell_line_footprint(std::size_t value_bytes) noexcept {
    return ::crucible::concurrent::cell_line_footprint(value_bytes);
}

}  // namespace detail

template <IsSubstrate S>
inline constexpr std::size_t per_call_working_set_v = [] consteval {
    constexpr std::size_t cell = detail::cell_line_footprint(sizeof(substrate_value_type_t<S>));
    constexpr ChannelTopology topo = substrate_topology_v<S>;
    if constexpr (topo == ChannelTopology::OneToOne) {
        // Head and tail counters, plus the cell.
        return 2 * detail::kHotPathCacheLineBytes + cell;
    } else if constexpr (topo == ChannelTopology::ManyToOne) {
        // Head, tail and the cell's sequence number, plus the cell itself.
        return 3 * detail::kHotPathCacheLineBytes + cell;
    } else if constexpr (topo == ChannelTopology::ManyToMany) {
        // Head, tail and threshold counters, plus the cell.
        return 3 * detail::kHotPathCacheLineBytes + cell;
    } else if constexpr (topo == ChannelTopology::OneToMany_Latest) {
        // Sequence counter, plus the buffer both sides copy through.
        return detail::kHotPathCacheLineBytes + (cell == 0 ? detail::kHotPathCacheLineBytes : cell);
    } else /* WorkStealing */ {
        // Owner side: top and bottom counters, plus the cell.
        return 2 * detail::kHotPathCacheLineBytes + cell;
    }
}();

// WorkStealing is never recommended.  It answers a question about the shape of
// the work — items of widely varying cost, redistributed on demand — which is
// orthogonal to producer and consumer counts, so callers select it explicitly.

[[nodiscard]] consteval ChannelTopology recommend_topology(std::size_t num_producers, std::size_t num_consumers,
                                                           bool latest_only = false) noexcept {
    if (num_producers == 1 && num_consumers == 1) return ChannelTopology::OneToOne;
    if (num_producers > 1 && num_consumers == 1) return ChannelTopology::ManyToOne;
    if (num_producers == 1 && num_consumers > 1 && latest_only) return ChannelTopology::OneToMany_Latest;
    if (num_producers > 1 && num_consumers > 1) return ChannelTopology::ManyToMany;
    // One producer fanning out a stream: the many-to-many ring degenerates to
    // single-producer when only one producer is ever active, so it is the
    // closest fit.
    if (num_producers == 1 && num_consumers > 1) return ChannelTopology::ManyToMany;
    // Zero on either side is a caller error.  The fallback fails a capacity
    // assertion once instantiated.
    return ChannelTopology::OneToOne;
}

// Below the cliff a workload stays inside one core's private cache, and
// spreading it over more threads costs more in coherence traffic and cold
// caches than the extra cores return.  Above the cliff the workload is bound
// by memory latency, and each added thread brings its own private caches, so
// splitting the traffic across shards pays.
//
// The bound sits high on purpose.  Set too high it leaves a workload
// sequential that could have been split, forgoing a speedup.  Set too low it
// splits one that then regresses.
//
// This is advice on where to place data, not a safety gate.  A single
// producer and consumer moving far more than the cliff through one ring stays
// a correct configuration, because the per-call footprint is what the hot path
// must keep resident and that stays small at any capacity.

inline constexpr std::size_t conservative_cliff_l2_per_core = 256ULL * 1024;

[[nodiscard]] consteval ChannelTopology recommend_topology_for_workload(std::size_t num_producers,
                                                                        std::size_t num_consumers,
                                                                        std::size_t workload_bytes,
                                                                        bool latest_only = false) noexcept {
    if (workload_bytes <= conservative_cliff_l2_per_core) {
        return recommend_topology(num_producers, num_consumers, latest_only);
    }
    // A latest-value snapshot keeps no order to shard, so size does not move
    // it.
    if (num_producers == 1 && num_consumers > 1 && latest_only) {
        return ChannelTopology::OneToMany_Latest;
    }
    // One thread on each side has nothing to spread the traffic over, and its
    // per-call footprint stays resident whatever the capacity.
    if (num_producers == 1 && num_consumers == 1) {
        return ChannelTopology::OneToOne;
    }
    if (num_producers > 1 && num_consumers == 1) return ChannelTopology::ManyToOne;
    if (num_producers > 1 && num_consumers > 1) return ChannelTopology::ManyToMany;
    if (num_producers == 1 && num_consumers > 1) return ChannelTopology::ManyToMany;
    return ChannelTopology::OneToOne;
}

template <class S>
concept IsOneToOneSubstrate = IsSubstrate<S> && substrate_topology_v<S> == ChannelTopology::OneToOne;
template <class S>
concept IsManyToOneSubstrate = IsSubstrate<S> && substrate_topology_v<S> == ChannelTopology::ManyToOne;
template <class S>
concept IsOneToManyLatestSubstrate = IsSubstrate<S> && substrate_topology_v<S> == ChannelTopology::OneToMany_Latest;
template <class S>
concept IsManyToManySubstrate = IsSubstrate<S> && substrate_topology_v<S> == ChannelTopology::ManyToMany;
template <class S>
concept IsWorkStealingSubstrate = IsSubstrate<S> && substrate_topology_v<S> == ChannelTopology::WorkStealing;

namespace detail::substrate_self_test {

struct VesselOpStream {};
struct ConductorCompile {};

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

using SpscT = Substrate_t<ChannelTopology::OneToOne, int, 1024, VesselOpStream>;
static_assert(substrate_topology_v<SpscT> == ChannelTopology::OneToOne);
static_assert(substrate_capacity_v<SpscT> == 1024);
static_assert(std::is_same_v<substrate_value_type_t<SpscT>, int>);
static_assert(std::is_same_v<substrate_user_tag_t<SpscT>, VesselOpStream>);

using SnapT = Substrate_t<ChannelTopology::OneToMany_Latest, double, 0, ConductorCompile>;
static_assert(substrate_topology_v<SnapT> == ChannelTopology::OneToMany_Latest);
static_assert(substrate_capacity_v<SnapT> == 0);
static_assert(std::is_same_v<substrate_value_type_t<SnapT>, double>);

static_assert(IsSubstrate<SpscT>);
static_assert(IsSubstrate<SnapT>);
static_assert(!IsSubstrate<int>);
static_assert(!IsSubstrate<void>);

static_assert(IsOneToOneSubstrate<SpscT>);
static_assert(!IsOneToOneSubstrate<SnapT>);
static_assert(IsOneToManyLatestSubstrate<SnapT>);
static_assert(!IsOneToManyLatestSubstrate<SpscT>);

using MpscT = Substrate_t<ChannelTopology::ManyToOne, int, 64, VesselOpStream>;
static_assert(IsManyToOneSubstrate<MpscT>);
static_assert(!IsManyToOneSubstrate<SpscT>);

using MpmcT = Substrate_t<ChannelTopology::ManyToMany, int, 32, VesselOpStream>;
static_assert(IsManyToManySubstrate<MpmcT>);

using DequeT = Substrate_t<ChannelTopology::WorkStealing, int, 256, ConductorCompile>;
static_assert(IsWorkStealingSubstrate<DequeT>);

static_assert(sizeof(Substrate_t<ChannelTopology::OneToOne, int, 1024, VesselOpStream>)
              == sizeof(PermissionedSpscChannel<int, 1024, VesselOpStream>));

static_assert(channel_byte_footprint_v<SpscT> == sizeof(int) * 1024);
static_assert(channel_byte_footprint_v<MpscT> == sizeof(int) * 64);
static_assert(channel_byte_footprint_v<SnapT> == sizeof(double));
static_assert(channel_byte_footprint_v<MpmcT> == sizeof(int) * 32);
static_assert(channel_byte_footprint_v<DequeT> == sizeof(int) * 256);

static_assert(recommend_topology(1, 1) == ChannelTopology::OneToOne);
static_assert(recommend_topology(4, 1) == ChannelTopology::ManyToOne);
static_assert(recommend_topology(8, 8) == ChannelTopology::ManyToMany);
static_assert(recommend_topology(1, 4, true) == ChannelTopology::OneToMany_Latest);
static_assert(recommend_topology(1, 4, false) == ChannelTopology::ManyToMany);
static_assert(recommend_topology(1, 1, true) == ChannelTopology::OneToOne);

// Fully qualified: an unqualified detail:: from inside this namespace resolves
// against this namespace, not its parent.
inline constexpr std::size_t kLine = ::crucible::concurrent::detail::kHotPathCacheLineBytes;

static_assert(per_call_working_set_v<SpscT> == 3 * kLine);
static_assert(per_call_working_set_v<MpscT> == 4 * kLine);
static_assert(per_call_working_set_v<MpmcT> == 4 * kLine);
static_assert(per_call_working_set_v<SnapT> == 2 * kLine);
static_assert(per_call_working_set_v<DequeT> == 3 * kLine);

struct OneHundredByteValue {
    char pad[100];
    auto operator<=>(OneHundredByteValue const&) const = default;
};
using BigCellSpsc = Substrate_t<ChannelTopology::OneToOne, OneHundredByteValue, 16, VesselOpStream>;
static_assert(per_call_working_set_v<BigCellSpsc> == 2 * kLine + 128);

using HugeSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024 * 1024, VesselOpStream>;
static_assert(channel_byte_footprint_v<HugeSpsc> == 4 * 1024 * 1024);
static_assert(per_call_working_set_v<HugeSpsc> == 3 * kLine);

inline constexpr std::size_t kSmall = 4 * 1024;
inline constexpr std::size_t kMid = 64 * 1024;
inline constexpr std::size_t kBig = 4 * 1024 * 1024;

static_assert(recommend_topology_for_workload(1, 1, kSmall) == ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(4, 1, kSmall) == ChannelTopology::ManyToOne);
static_assert(recommend_topology_for_workload(8, 8, kMid) == ChannelTopology::ManyToMany);
static_assert(recommend_topology_for_workload(1, 4, kMid, true) == ChannelTopology::OneToMany_Latest);

static_assert(recommend_topology_for_workload(1, 1, kBig) == ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(4, 1, kBig) == ChannelTopology::ManyToOne);
static_assert(recommend_topology_for_workload(8, 8, kBig) == ChannelTopology::ManyToMany);
static_assert(recommend_topology_for_workload(1, 4, kBig, true) == ChannelTopology::OneToMany_Latest);
static_assert(recommend_topology_for_workload(1, 4, kBig, false) == ChannelTopology::ManyToMany);

inline constexpr std::size_t k16KiB = 16ULL * 1024ULL;
inline constexpr std::size_t k256KiB = 256ULL * 1024ULL;
inline constexpr std::size_t k16MiB = 16ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t k1GiB = 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t k16GiB = 16ULL * 1024ULL * 1024ULL * 1024ULL;

static_assert(recommend_topology_for_workload(1, 1, k16KiB) == ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(1, 1, k256KiB) == ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(1, 1, k16MiB) == ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(1, 1, k1GiB) == ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(1, 1, k16GiB) == ChannelTopology::OneToOne);

static_assert(recommend_topology_for_workload(4, 4, k16KiB) == ChannelTopology::ManyToMany);
static_assert(recommend_topology_for_workload(4, 4, k256KiB) == ChannelTopology::ManyToMany);
static_assert(recommend_topology_for_workload(4, 4, k16MiB) == ChannelTopology::ManyToMany);
static_assert(recommend_topology_for_workload(4, 4, k1GiB) == ChannelTopology::ManyToMany);
static_assert(recommend_topology_for_workload(4, 4, k16GiB) == ChannelTopology::ManyToMany);

}  // namespace detail::substrate_self_test

}  // namespace crucible::concurrent
