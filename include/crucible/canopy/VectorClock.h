#pragma once

// Operational vector clock for Canopy protocols.
//
// The algebraic partial-order substrate already lives in
// algebra/lattices/HappensBefore.h.  This header is the mutable
// per-node clock: atomic slot storage, local/send/receive event
// updates, snapshot comparison, and compact sparse-delta transport.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/HappensBefore.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>

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
concept VectorClockNodeBound =
    MaxNodes > 0 &&
    MaxNodes <= static_cast<std::size_t>(
        std::numeric_limits<std::uint16_t>::max());

template <std::size_t MaxNodes>
    requires VectorClockNodeBound<MaxNodes>
using VectorClockNodeIndex =
    safety::Refined<safety::bounded_above<
                        static_cast<std::uint16_t>(MaxNodes - 1)>,
                    std::uint16_t>;

template <std::size_t MaxNodes>
    requires VectorClockNodeBound<MaxNodes>
using VectorClockEntryCount =
    safety::Refined<safety::positive, std::uint64_t>;

template <std::size_t MaxNodes>
    requires VectorClockNodeBound<MaxNodes>
using VectorClockDeltaCount =
    safety::Refined<safety::bounded_above<
                        static_cast<std::uint16_t>(MaxNodes)>,
                    std::uint16_t>;

template <std::size_t MaxNodes, typename Tag>
    requires VectorClockNodeBound<MaxNodes>
struct VectorClockSnapshot;

template <std::size_t MaxNodes, typename Tag = void>
    requires VectorClockNodeBound<MaxNodes>
struct VectorClockDelta {
    using node_index_type = VectorClockNodeIndex<MaxNodes>;
    using count_type = VectorClockDeltaCount<MaxNodes>;

private:
    safety::FixedArray<std::uint16_t, MaxNodes> node_ids{};
    safety::FixedArray<std::uint64_t, MaxNodes> values{};
    std::uint16_t count_ = 0;

    friend struct VectorClockSnapshot<MaxNodes, Tag>;

public:
    [[nodiscard]] constexpr std::uint16_t raw_count() const noexcept {
        return count_;
    }

    [[nodiscard]] constexpr count_type size() const noexcept {
        return count_type{count_, typename count_type::Trusted{}};
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return count_ == 0;
    }

    [[nodiscard]] constexpr node_index_type
    node_at(node_index_type slot) const noexcept {
        return node_index_type{
            node_ids[slot.value()],
            typename node_index_type::Trusted{}};
    }

    [[nodiscard]] constexpr std::uint64_t
    value_at(node_index_type slot) const noexcept {
        return values[slot.value()];
    }

    constexpr bool push(node_index_type node, std::uint64_t value) noexcept {
        if (value == 0) {
            return true;
        }
        if (count_ == MaxNodes) {
            return false;
        }
        node_ids[count_] = node.value();
        values[count_] = value;
        ++count_;
        return true;
    }
};

template <std::size_t MaxNodes, typename Tag = void>
    requires VectorClockNodeBound<MaxNodes>
struct VectorClockSnapshot {
    using lattice_type =
        algebra::lattices::HappensBeforeLattice<MaxNodes, Tag>;
    using lattice_clock_type = typename lattice_type::element_type;
    using node_index_type = VectorClockNodeIndex<MaxNodes>;
    using positive_entry_type = VectorClockEntryCount<MaxNodes>;
    using delta_type = VectorClockDelta<MaxNodes, Tag>;

    safety::FixedArray<std::uint64_t, MaxNodes> entries{};

    constexpr VectorClockSnapshot() noexcept = default;

    template <typename... Counts>
        requires (sizeof...(Counts) == MaxNodes) &&
                 (std::convertible_to<Counts, std::uint64_t> && ...)
    constexpr explicit VectorClockSnapshot(std::in_place_t, Counts&&... counts)
        noexcept((std::is_nothrow_constructible_v<std::uint64_t, Counts> && ...))
        : entries{std::in_place, std::forward<Counts>(counts)...} {}

    [[nodiscard]] constexpr std::uint64_t
    at(node_index_type node) const noexcept {
        return entries[node.value()];
    }

    [[nodiscard]] constexpr std::optional<positive_entry_type>
    positive_at(node_index_type node) const noexcept {
        const std::uint64_t value = at(node);
        if (value == 0) {
            return std::nullopt;
        }
        return positive_entry_type{
            value,
            typename positive_entry_type::Trusted{}};
    }

    [[nodiscard]] constexpr lattice_clock_type
    as_lattice_clock() const noexcept {
        lattice_clock_type out{};
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            out.clock[i] = entries[i];
        }
        return out;
    }

    [[nodiscard]] static constexpr VectorClockSnapshot
    from_lattice_clock(lattice_clock_type clock) noexcept {
        VectorClockSnapshot out{};
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            out.entries[i] = clock.clock[i];
        }
        return out;
    }

    [[nodiscard]] constexpr std::partial_ordering
    operator<=>(VectorClockSnapshot const& other) const noexcept {
        return as_lattice_clock() <=> other.as_lattice_clock();
    }

    [[nodiscard]] constexpr bool
    operator==(VectorClockSnapshot const& other) const noexcept = default;

    [[nodiscard]] constexpr bool
    happens_before(VectorClockSnapshot const& other) const noexcept {
        return lattice_type::happens_before(
            as_lattice_clock(),
            other.as_lattice_clock());
    }

    [[nodiscard]] constexpr bool
    concurrent_with(VectorClockSnapshot const& other) const noexcept {
        return lattice_type::is_concurrent(
            as_lattice_clock(),
            other.as_lattice_clock());
    }

    [[nodiscard]] constexpr bool
    comparable_with(VectorClockSnapshot const& other) const noexcept {
        return lattice_type::comparable(
            as_lattice_clock(),
            other.as_lattice_clock());
    }

    [[nodiscard]] constexpr delta_type sparse_delta() const noexcept {
        delta_type delta{};
        for (std::uint16_t i = 0; i < MaxNodes; ++i) {
            const auto node = node_index_type{
                i,
                typename node_index_type::Trusted{}};
            (void)delta.push(node, entries[i]);
        }
        return delta;
    }

    [[nodiscard]] static constexpr VectorClockSnapshot
    from_sparse_delta(delta_type const& delta) noexcept {
        VectorClockSnapshot out{};
        const std::uint16_t n = delta.count_;
        for (std::uint16_t i = 0; i < n; ++i) {
            out.entries[delta.node_ids[i]] = delta.values[i];
        }
        return out;
    }
};

template <std::size_t MaxNodes, typename Tag = void>
    requires VectorClockNodeBound<MaxNodes>
class alignas(64) VectorClock
    : public safety::Pinned<VectorClock<MaxNodes, Tag>> {
public:
    using snapshot_type = VectorClockSnapshot<MaxNodes, Tag>;
    using delta_type = VectorClockDelta<MaxNodes, Tag>;
    using node_index_type = VectorClockNodeIndex<MaxNodes>;
    using positive_entry_type = VectorClockEntryCount<MaxNodes>;
    using lattice_type = typename snapshot_type::lattice_type;

    static constexpr std::size_t max_nodes = MaxNodes;

    explicit VectorClock(node_index_type self_id) noexcept
        : self_id_{self_id} {}

    [[nodiscard]] node_index_type self_id() const noexcept {
        return self_id_;
    }

    void on_local_event() noexcept {
        bump_slot_(entries_[self_id_.value()]);
    }

    [[nodiscard]] snapshot_type on_send() noexcept {
        on_local_event();
        return snapshot();
    }

    void on_send(VectorClock& outgoing) noexcept {
        outgoing.merge_snapshot_(on_send());
    }

    void on_recv(snapshot_type incoming) noexcept {
        merge_snapshot_(incoming);
        on_local_event();
    }

    void on_recv(VectorClock const& incoming) noexcept {
        on_recv(incoming.snapshot());
    }

    void on_recv(delta_type const& incoming) noexcept {
        on_recv(snapshot_type::from_sparse_delta(incoming));
    }

    [[nodiscard]] snapshot_type snapshot() const noexcept {
        snapshot_type out{};
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            out.entries[i] = entries_[i].load(std::memory_order_acquire);
        }
        return out;
    }

    [[nodiscard]] std::uint64_t
    at(node_index_type node) const noexcept {
        return entries_[node.value()].load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<positive_entry_type>
    positive_at(node_index_type node) const noexcept {
        const std::uint64_t value = at(node);
        if (value == 0) {
            return std::nullopt;
        }
        return positive_entry_type{
            value,
            typename positive_entry_type::Trusted{}};
    }

    [[nodiscard]] std::partial_ordering
    operator<=>(VectorClock const& other) const noexcept {
        return snapshot() <=> other.snapshot();
    }

    [[nodiscard]] bool
    operator==(VectorClock const& other) const noexcept {
        return snapshot() == other.snapshot();
    }

    [[nodiscard]] bool
    happens_before(VectorClock const& other) const noexcept {
        return snapshot().happens_before(other.snapshot());
    }

    [[nodiscard]] bool
    concurrent_with(VectorClock const& other) const noexcept {
        return snapshot().concurrent_with(other.snapshot());
    }

    [[nodiscard]] bool
    comparable_with(VectorClock const& other) const noexcept {
        return snapshot().comparable_with(other.snapshot());
    }

    [[nodiscard]] delta_type sparse_delta() const noexcept {
        return snapshot().sparse_delta();
    }

    void apply_delta(delta_type const& delta) noexcept {
        merge_snapshot_(snapshot_type::from_sparse_delta(delta));
    }

private:
    void merge_snapshot_(snapshot_type incoming) noexcept {
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            atomic_max_(entries_[i], incoming.entries[i]);
        }
    }

    static void atomic_max_(
        std::atomic<std::uint64_t>& slot,
        std::uint64_t incoming) noexcept {
        auto current = slot.load(std::memory_order_acquire);
        while (current < incoming &&
               !slot.compare_exchange_weak(
                   current,
                   incoming,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
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
            if (slot.compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return;
            }
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    alignas(64) safety::FixedArray<std::atomic<std::uint64_t>, MaxNodes>
        entries_{};
    [[no_unique_address]] node_index_type self_id_;
};

static_assert(!std::is_copy_constructible_v<VectorClock<1>>);
static_assert(!std::is_move_constructible_v<VectorClock<1>>);
static_assert(alignof(VectorClock<1>) == 64);
static_assert(std::is_trivially_copyable_v<VectorClockSnapshot<4>>);
static_assert(std::is_trivially_destructible_v<VectorClockSnapshot<4>>);
static_assert(sizeof(VectorClockSnapshot<4>) == 4 * sizeof(std::uint64_t));

template <std::size_t MaxNodes, typename Tag = void>
    requires VectorClockNodeBound<MaxNodes>
[[nodiscard]] inline VectorClock<MaxNodes, Tag>
mint_vector_clock(effects::Init, std::uint16_t self_id) noexcept {
    return VectorClock<MaxNodes, Tag>{
        VectorClockNodeIndex<MaxNodes>{self_id}};
}

}  // namespace crucible::canopy
