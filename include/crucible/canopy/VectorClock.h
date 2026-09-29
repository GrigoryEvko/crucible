#pragma once

#include <crucible/Platform.h>
#include <fixy/FixedArray.h>
#include <fixy/Refined.h>
#include <foundation/Pinned.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <atomic>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::canopy {

template <std::size_t MaxNodes>
concept VectorClockNodeBound = MaxNodes > 0
                            && MaxNodes <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max());

// The bound of a node id: at most MaxNodes - 1.
template <std::size_t MaxNodes>
    requires VectorClockNodeBound<MaxNodes>
inline constexpr auto vector_clock_node_bound = ::fixy::bounded_above<static_cast<std::uint16_t>(MaxNodes - 1)>;

// The bound of a delta entry count: at most MaxNodes.
template <std::size_t MaxNodes>
    requires VectorClockNodeBound<MaxNodes>
inline constexpr auto vector_clock_delta_bound = ::fixy::bounded_above<static_cast<std::uint16_t>(MaxNodes)>;

template <std::size_t MaxNodes>
    requires VectorClockNodeBound<MaxNodes>
using VectorClockNodeIndex = ::fixy::Refined<vector_clock_node_bound<MaxNodes>, std::uint16_t>;

template <std::size_t MaxNodes>
    requires VectorClockNodeBound<MaxNodes>
using VectorClockEntryCount = ::fixy::Refined<::fixy::positive, std::uint64_t>;

template <std::size_t MaxNodes>
    requires VectorClockNodeBound<MaxNodes>
using VectorClockDeltaCount = ::fixy::Refined<vector_clock_delta_bound<MaxNodes>, std::uint16_t>;

template <std::size_t MaxNodes, typename Tag>
    requires VectorClockNodeBound<MaxNodes>
struct VectorClockSnapshot;

template <std::size_t MaxNodes, typename Tag = void>
    requires VectorClockNodeBound<MaxNodes>
struct VectorClockDelta {
    using node_index_type = VectorClockNodeIndex<MaxNodes>;
    using count_type = VectorClockDeltaCount<MaxNodes>;

private:
    ::fixy::FixedArray<std::uint16_t, MaxNodes> node_ids{};
    ::fixy::FixedArray<std::uint64_t, MaxNodes> values{};
    std::uint16_t count_ = 0;

    friend struct VectorClockSnapshot<MaxNodes, Tag>;

public:
    [[nodiscard]] constexpr std::uint16_t raw_count() const noexcept { return count_; }

    // push() never lets the count pass MaxNodes.
    [[nodiscard]] constexpr count_type size() const noexcept {
        return ::fixy::mint_refined_trusted<vector_clock_delta_bound<MaxNodes>>(count_);
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    // Every stored id came in through push() as a node index.
    [[nodiscard]] constexpr node_index_type node_at(node_index_type slot) const noexcept {
        return ::fixy::mint_refined_trusted<vector_clock_node_bound<MaxNodes>>(node_ids[slot.value()]);
    }

    [[nodiscard]] constexpr std::uint64_t value_at(node_index_type slot) const noexcept { return values[slot.value()]; }

    constexpr bool push(node_index_type node, std::uint64_t value) noexcept {
        if (value == 0) {
            return true;
        }
        const std::uint16_t node_id = node.value();
        for (std::uint16_t i = 0; i < count_; ++i) {
            if (node_ids[i] == node_id) {
                if (values[i] < value) {
                    values[i] = value;
                }
                return true;
            }
        }
        if (count_ == MaxNodes) {
            return false;
        }
        node_ids[count_] = node_id;
        values[count_] = value;
        ++count_;
        return true;
    }
};

// A snapshot holds counts that crossed a wire or left a live clock, so it
// states its own order over them.  The lattice builds a clock only from a
// recorded history and has no door that takes counts, so no snapshot can
// become a lattice clock.  The order below is the lattice's pointwise
// order, and the check after the class pins the two against each other.
template <std::size_t MaxNodes, typename Tag = void>
    requires VectorClockNodeBound<MaxNodes>
struct VectorClockSnapshot {
    using lattice_type = ::foundation::algebra::lattices::HappensBeforeLattice<MaxNodes, Tag>;
    using lattice_clock_type = typename lattice_type::element_type;
    using node_index_type = VectorClockNodeIndex<MaxNodes>;
    using positive_entry_type = VectorClockEntryCount<MaxNodes>;
    using delta_type = VectorClockDelta<MaxNodes, Tag>;

    ::fixy::FixedArray<std::uint64_t, MaxNodes> entries{};

    constexpr VectorClockSnapshot() noexcept = default;

    template <typename... Counts>
        requires(sizeof...(Counts) == MaxNodes) && (std::convertible_to<Counts, std::uint64_t> && ...)
    constexpr explicit VectorClockSnapshot(std::in_place_t, Counts&&... counts) noexcept(
        (std::is_nothrow_constructible_v<std::uint64_t, Counts> && ...))
        : entries{std::in_place, std::forward<Counts>(counts)...} {}

    [[nodiscard]] constexpr std::uint64_t at(node_index_type node) const noexcept { return entries[node.value()]; }

    [[nodiscard]] constexpr std::optional<positive_entry_type> positive_at(node_index_type node) const noexcept {
        const std::uint64_t value = at(node);
        if (value == 0) {
            return std::nullopt;
        }
        return ::fixy::mint_refined_trusted<::fixy::positive>(value);
    }

    [[nodiscard]] static constexpr VectorClockSnapshot from_lattice_clock(lattice_clock_type clock) noexcept {
        VectorClockSnapshot out{};
        const std::array<std::uint64_t, MaxNodes> slots = clock.slots();
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            out.entries[i] = slots[i];
        }
        return out;
    }

    [[nodiscard]] constexpr std::partial_ordering operator<=>(VectorClockSnapshot const& other) const noexcept {
        const bool self_leq_other = leq_(*this, other);
        const bool other_leq_self = leq_(other, *this);
        if (self_leq_other && other_leq_self) return std::partial_ordering::equivalent;
        if (self_leq_other) return std::partial_ordering::less;
        if (other_leq_self) return std::partial_ordering::greater;
        return std::partial_ordering::unordered;
    }

    [[nodiscard]] constexpr bool operator==(VectorClockSnapshot const& other) const noexcept = default;

    [[nodiscard]] constexpr bool happens_before(VectorClockSnapshot const& other) const noexcept {
        return leq_(*this, other) && !(*this == other);
    }

    [[nodiscard]] constexpr bool concurrent_with(VectorClockSnapshot const& other) const noexcept {
        return !leq_(*this, other) && !leq_(other, *this);
    }

    [[nodiscard]] constexpr bool comparable_with(VectorClockSnapshot const& other) const noexcept {
        return leq_(*this, other) || leq_(other, *this);
    }

    // Every index is below MaxNodes.
    [[nodiscard]] constexpr delta_type sparse_delta() const noexcept {
        delta_type delta{};
        for (std::uint16_t i = 0; i < MaxNodes; ++i) {
            (void)delta.push(::fixy::mint_refined_trusted<vector_clock_node_bound<MaxNodes>>(i), entries[i]);
        }
        return delta;
    }

    [[nodiscard]] static constexpr VectorClockSnapshot from_sparse_delta(delta_type const& delta) noexcept {
        VectorClockSnapshot out{};
        const std::uint16_t n = delta.count_;
        for (std::uint16_t i = 0; i < n; ++i) {
            out.entries[delta.node_ids[i]] = delta.values[i];
        }
        return out;
    }

private:
    // The pointwise order of the lattice.  Linear in MaxNodes.
    [[nodiscard]] static constexpr bool leq_(VectorClockSnapshot const& lhs, VectorClockSnapshot const& rhs) noexcept {
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            if (lhs.entries[i] > rhs.entries[i]) return false;
        }
        return true;
    }
};

namespace detail::vector_clock_order_check {

// The snapshot order agrees with the lattice on every pair of four clocks
// that the lattice itself built: the empty history, one event at each of
// two processes, and their join.
[[nodiscard]] consteval bool snapshot_order_agrees_with_lattice() noexcept {
    using HB = ::foundation::algebra::lattices::HappensBeforeLattice<3>;
    using Snap = VectorClockSnapshot<3>;
    const auto first = HB::successor_at(HB::bottom(), 0);
    const auto second = HB::successor_at(HB::bottom(), 1);
    const std::array<HB::element_type, 4> clocks{HB::bottom(), first, second, HB::join(first, second)};
    for (const auto& lhs : clocks) {
        for (const auto& rhs : clocks) {
            const Snap left = Snap::from_lattice_clock(lhs);
            const Snap right = Snap::from_lattice_clock(rhs);
            if ((left <=> right) != (lhs <=> rhs)) return false;
            if (left.happens_before(right) != HB::happens_before(lhs, rhs)) return false;
            if (left.concurrent_with(right) != HB::is_concurrent(lhs, rhs)) return false;
            if (left.comparable_with(right) != HB::comparable(lhs, rhs)) return false;
        }
    }
    return true;
}

static_assert(snapshot_order_agrees_with_lattice(),
              "VectorClockSnapshot must order its counts as HappensBeforeLattice orders clocks.");

}  // namespace detail::vector_clock_order_check

template <std::size_t MaxNodes, typename Tag>
    requires VectorClockNodeBound<MaxNodes>
class VectorClock;

// The one door: a clock holds the causal state of one node of a process,
// so only a context that owns Init builds one.  The mint checks the id
// against MaxNodes.
template <std::size_t MaxNodes, typename Tag = void>
    requires VectorClockNodeBound<MaxNodes>
[[nodiscard]] constexpr VectorClock<MaxNodes, Tag> mint_vector_clock(::foundation::effects::Init,
                                                                     std::uint16_t self_id) noexcept;

template <std::size_t MaxNodes, typename Tag = void>
    requires VectorClockNodeBound<MaxNodes>
class alignas(64) VectorClock : public ::foundation::Pinned<VectorClock<MaxNodes, Tag>> {
public:
    using snapshot_type = VectorClockSnapshot<MaxNodes, Tag>;
    using delta_type = VectorClockDelta<MaxNodes, Tag>;
    using node_index_type = VectorClockNodeIndex<MaxNodes>;
    using positive_entry_type = VectorClockEntryCount<MaxNodes>;
    using lattice_type = typename snapshot_type::lattice_type;

    static constexpr std::size_t max_nodes = MaxNodes;

    [[nodiscard]] constexpr node_index_type self_id() const noexcept { return self_id_; }

    void on_local_event() noexcept { bump_slot_(entries_[self_id_.value()]); }

    [[nodiscard]] snapshot_type on_send() noexcept {
        on_local_event();
        return snapshot();
    }

    void on_send(VectorClock& outgoing) noexcept { outgoing.merge_snapshot_(on_send()); }

    void on_recv(snapshot_type incoming) noexcept {
        merge_snapshot_(incoming);
        on_local_event();
    }

    void on_recv(VectorClock const& incoming) noexcept { on_recv(incoming.snapshot()); }

    void on_recv(delta_type const& incoming) noexcept { on_recv(snapshot_type::from_sparse_delta(incoming)); }

    [[nodiscard]] snapshot_type snapshot() const noexcept {
        snapshot_type out{};
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            out.entries[i] = entries_[i].load(std::memory_order_acquire);
        }
        return out;
    }

    [[nodiscard]] std::uint64_t at(node_index_type node) const noexcept {
        return entries_[node.value()].load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<positive_entry_type> positive_at(node_index_type node) const noexcept {
        const std::uint64_t value = at(node);
        if (value == 0) {
            return std::nullopt;
        }
        return ::fixy::mint_refined_trusted<::fixy::positive>(value);
    }

    [[nodiscard]] std::partial_ordering operator<=>(VectorClock const& other) const noexcept {
        return snapshot() <=> other.snapshot();
    }

    [[nodiscard]] bool operator==(VectorClock const& other) const noexcept { return snapshot() == other.snapshot(); }

    [[nodiscard]] bool happens_before(VectorClock const& other) const noexcept {
        return snapshot().happens_before(other.snapshot());
    }

    [[nodiscard]] bool concurrent_with(VectorClock const& other) const noexcept {
        return snapshot().concurrent_with(other.snapshot());
    }

    [[nodiscard]] bool comparable_with(VectorClock const& other) const noexcept {
        return snapshot().comparable_with(other.snapshot());
    }

    [[nodiscard]] delta_type sparse_delta() const noexcept { return snapshot().sparse_delta(); }

    void apply_delta(delta_type const& delta) noexcept { merge_snapshot_(snapshot_type::from_sparse_delta(delta)); }

private:
    constexpr explicit VectorClock(node_index_type self_id) noexcept : self_id_{self_id} {}

    template <std::size_t N, typename T>
        requires VectorClockNodeBound<N>
    friend constexpr VectorClock<N, T> mint_vector_clock(::foundation::effects::Init, std::uint16_t) noexcept;

    void merge_snapshot_(snapshot_type incoming) noexcept {
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            atomic_max_(entries_[i], incoming.entries[i]);
        }
    }

    static void atomic_max_(std::atomic<std::uint64_t>& slot, std::uint64_t incoming) noexcept {
        auto current = slot.load(std::memory_order_acquire);
        while (
            current < incoming
            && !slot.compare_exchange_weak(current, incoming, std::memory_order_acq_rel, std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    static void bump_slot_(std::atomic<std::uint64_t>& slot) noexcept {
        auto current = slot.load(std::memory_order_acquire);
        for (;;) {
            if (current == std::numeric_limits<std::uint64_t>::max()) {
                return;
            }
            const std::uint64_t next = current + 1u;
            if (slot.compare_exchange_weak(current, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return;
            }
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    alignas(64)::fixy::FixedArray<std::atomic<std::uint64_t>, MaxNodes> entries_{};
    [[no_unique_address]] node_index_type self_id_;
};

static_assert(!std::is_constructible_v<VectorClock<1>, VectorClockNodeIndex<1>>);
static_assert(!std::is_copy_constructible_v<VectorClock<1>>);
static_assert(!std::is_move_constructible_v<VectorClock<1>>);
static_assert(alignof(VectorClock<1>) == 64);

// Every peer handler reads and writes these entries at the same time.  On an
// ISA that lacks the required instruction the standard library substitutes a
// mutex-backed atomic without saying so, and that mutex would serialize every
// peer against every other peer, which defeats the merge design.  The build
// refuses such a target instead of regressing quietly.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this target");
static_assert(std::is_trivially_copyable_v<VectorClockSnapshot<4>>);
static_assert(std::is_trivially_destructible_v<VectorClockSnapshot<4>>);
static_assert(sizeof(VectorClockSnapshot<4>) == 4 * sizeof(std::uint64_t));

template <std::size_t MaxNodes, typename Tag>
    requires VectorClockNodeBound<MaxNodes>
[[nodiscard]] constexpr VectorClock<MaxNodes, Tag> mint_vector_clock(::foundation::effects::Init,
                                                                     std::uint16_t self_id) noexcept {
    return VectorClock<MaxNodes, Tag>{::fixy::mint_refined<vector_clock_node_bound<MaxNodes>>(self_id)};
}

}  // namespace crucible::canopy
