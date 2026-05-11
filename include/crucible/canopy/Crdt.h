#pragma once

// State-based CRDT substrate for Canopy.
//
// The protocol layer (Scuttlebutt, federation gossip, topology
// distribution) is intentionally outside this header.  This file
// provides bounded, deterministic merge kernels with explicit
// Local/Gossiped provenance at every mutation boundary.

#include <crucible/Platform.h>
#include <crucible/canopy/Hlc.h>
#include <crucible/canopy/VectorClock.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <compare>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace crucible::canopy {

template <typename T>
using LocalWrite = safety::Tagged<T, safety::source::Local>;

template <typename State>
using GossipedState = safety::Tagged<State, safety::source::Gossiped>;

template <std::size_t Capacity>
concept CrdtCapacity =
    Capacity > 0 &&
    Capacity <= static_cast<std::size_t>(
        std::numeric_limits<std::uint16_t>::max());

template <std::size_t Capacity>
    requires CrdtCapacity<Capacity>
using CrdtIndex =
    safety::Refined<safety::bounded_above<
                        static_cast<std::uint16_t>(Capacity - 1)>,
                    std::uint16_t>;

template <std::size_t Capacity>
    requires CrdtCapacity<Capacity>
using CrdtCount =
    safety::Refined<safety::bounded_above<
                        static_cast<std::uint16_t>(Capacity)>,
                    std::uint16_t>;

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
using ReplicaIndex = CrdtIndex<MaxReplicas>;

using CounterAmount = safety::Refined<safety::positive, std::uint64_t>;

namespace detail {

[[nodiscard]] constexpr std::uint64_t
sat_add(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    return a > max - b ? max : a + b;
}

template <typename T>
concept HashableValue =
    std::default_initializable<T> &&
    std::copyable<T> &&
    std::equality_comparable<T> &&
    requires(T const& v) {
        { std::hash<T>{}(v) } -> std::convertible_to<std::size_t>;
    };

template <HashableValue T, std::size_t Capacity>
    requires CrdtCapacity<Capacity>
struct BoundedHashSetState {
    struct Slot {
        bool occupied = false;
        T value{};
    };

    safety::FixedArray<Slot, Capacity> slots{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr CrdtCount<Capacity> size() const noexcept {
        return CrdtCount<Capacity>{
            count,
            typename CrdtCount<Capacity>::Trusted{}};
    }

    [[nodiscard]] bool contains(T const& value) const {
        const std::size_t start = std::hash<T>{}(value) % Capacity;
        for (std::size_t n = 0; n < Capacity; ++n) {
            const std::size_t idx = (start + n) % Capacity;
            if (!slots[idx].occupied) {
                return false;
            }
            if (slots[idx].value == value) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool insert(T const& value) {
        const std::size_t start = std::hash<T>{}(value) % Capacity;
        for (std::size_t n = 0; n < Capacity; ++n) {
            const std::size_t idx = (start + n) % Capacity;
            if (slots[idx].occupied && slots[idx].value == value) {
                return true;
            }
            if (!slots[idx].occupied) {
                if (count == Capacity) {
                    return false;
                }
                slots[idx].occupied = true;
                slots[idx].value = value;
                ++count;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool merge(BoundedHashSetState const& other) {
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (other.slots[i].occupied && !insert(other.slots[i].value)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] friend bool operator==(
        BoundedHashSetState const& a,
        BoundedHashSetState const& b) {
        if (a.count != b.count) {
            return false;
        }
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (a.slots[i].occupied && !b.contains(a.slots[i].value)) {
                return false;
            }
        }
        return true;
    }
};

template <typename T, typename Id, std::size_t Capacity>
    requires CrdtCapacity<Capacity>
struct BoundedTaggedState {
    safety::FixedArray<T, Capacity> entries{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr CrdtCount<Capacity> size() const noexcept {
        return CrdtCount<Capacity>{
            count,
            typename CrdtCount<Capacity>::Trusted{}};
    }

};

}  // namespace detail

template <typename T, std::size_t Capacity = 64>
    requires detail::HashableValue<T> && CrdtCapacity<Capacity>
class GSet : public safety::Pinned<GSet<T, Capacity>> {
public:
    using value_type = T;
    using state_type = detail::BoundedHashSetState<T, Capacity>;
    using local_value_type = LocalWrite<T>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool add(local_value_type value) {
        return state_.insert(value.value());
    }

    [[nodiscard]] bool merge(gossiped_state_type other) {
        state_type staged = state_;
        if (!staged.merge(other.value())) {
            return false;
        }
        state_ = staged;
        return true;
    }

    [[nodiscard]] bool merge(GSet const& other) {
        state_type staged = state_;
        if (!staged.merge(other.state_)) {
            return false;
        }
        state_ = staged;
        return true;
    }

    [[nodiscard]] static state_type merge(state_type a, state_type const& b) {
        state_type staged = a;
        return staged.merge(b) ? staged : a;
    }

    [[nodiscard]] state_type state() const {
        return state_;
    }

    [[nodiscard]] bool contains(T const& value) const {
        return state_.contains(value);
    }

    [[nodiscard]] CrdtCount<Capacity> size() const noexcept {
        return state_.size();
    }

private:
    state_type state_{};
};

template <typename T, typename TagId>
struct OrSetAdd {
    T value{};
    TagId tag{};
};

template <typename T, typename TagId>
struct OrSetEntry {
    T value{};
    TagId tag{};
    bool removed = false;
};

template <
    typename T,
    typename TagId = std::uint64_t,
    std::size_t Capacity = 128>
    requires CrdtCapacity<Capacity> &&
             std::default_initializable<T> &&
             std::copyable<T> &&
             std::equality_comparable<T> &&
             std::default_initializable<TagId> &&
             std::copyable<TagId> &&
             std::equality_comparable<TagId>
class OrSet : public safety::Pinned<OrSet<T, TagId, Capacity>> {
public:
    using value_type = T;
    using tag_type = TagId;
    using add_type = OrSetAdd<T, TagId>;
    using entry_type = OrSetEntry<T, TagId>;
    using state_type = detail::BoundedTaggedState<entry_type, TagId, Capacity>;
    using local_add_type = LocalWrite<add_type>;
    using local_remove_type = LocalWrite<T>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool add(local_add_type add_op) {
        auto const& op = add_op.value();
        return upsert_(entry_type{.value = op.value, .tag = op.tag});
    }

    [[nodiscard]] bool remove(local_remove_type value) noexcept {
        for (std::uint16_t i = 0; i < state_.count; ++i) {
            if (state_.entries[i].value == value.value()) {
                state_.entries[i].removed = true;
            }
        }
        return true;
    }

    [[nodiscard]] bool contains(T const& value) const noexcept {
        for (std::uint16_t i = 0; i < state_.count; ++i) {
            auto const& e = state_.entries[i];
            if (e.value == value && !e.removed) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool merge(gossiped_state_type other) {
        state_type staged = state_;
        if (!merge_state_into_(staged, other.value())) {
            return false;
        }
        state_ = staged;
        return true;
    }

    [[nodiscard]] bool merge(OrSet const& other) {
        state_type staged = state_;
        if (!merge_state_into_(staged, other.state_)) {
            return false;
        }
        state_ = staged;
        return true;
    }

    [[nodiscard]] static state_type merge(state_type a, state_type const& b) {
        OrSet tmp{};
        state_type staged = a;
        return tmp.merge_state_into_(staged, b) ? staged : a;
    }

    [[nodiscard]] state_type state() const {
        return state_;
    }

private:
    [[nodiscard]] std::optional<std::uint16_t>
    find_(state_type const& state, T const& value, TagId const& tag) const {
        for (std::uint16_t i = 0; i < state.count; ++i) {
            auto const& e = state.entries[i];
            if (e.value == value && e.tag == tag) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool upsert_into_(state_type& state, entry_type incoming) const {
        if (auto idx = find_(state, incoming.value, incoming.tag)) {
            state.entries[*idx].removed =
                state.entries[*idx].removed || incoming.removed;
            return true;
        }
        if (state.count == Capacity) {
            return false;
        }
        state.entries[state.count] = incoming;
        ++state.count;
        return true;
    }

    [[nodiscard]] bool upsert_(entry_type incoming) {
        return upsert_into_(state_, incoming);
    }

    [[nodiscard]] bool merge_state_into_(
        state_type& target,
        state_type const& other) const {
        for (std::uint16_t i = 0; i < other.count; ++i) {
            if (!upsert_into_(target, other.entries[i])) {
                return false;
            }
        }
        return true;
    }

    state_type state_{};
};

template <typename V, typename Clock>
struct LwwRegisterWrite {
    V value{};
    Clock clock{};
};

template <typename V, typename Clock>
struct LwwRegisterState {
    V value{};
    Clock clock{};
    bool has_value = false;

    [[nodiscard]] friend constexpr bool operator==(
        LwwRegisterState const&,
        LwwRegisterState const&) = default;
};

template <typename V, typename Clock>
    requires std::default_initializable<V> &&
             std::copyable<V> &&
             std::totally_ordered<V> &&
             std::default_initializable<Clock> &&
             std::copyable<Clock> &&
             std::three_way_comparable<Clock, std::strong_ordering>
class LwwRegister : public safety::Pinned<LwwRegister<V, Clock>> {
public:
    using value_type = V;
    using clock_type = Clock;
    using write_type = LwwRegisterWrite<V, Clock>;
    using state_type = LwwRegisterState<V, Clock>;
    using local_write_type = LocalWrite<write_type>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool assign(local_write_type write) noexcept {
        state_ = merge(state_, state_type{
            .value = write.value().value,
            .clock = write.value().clock,
            .has_value = true});
        return true;
    }

    [[nodiscard]] bool merge(gossiped_state_type other) noexcept {
        state_ = merge(state_, other.value());
        return true;
    }

    [[nodiscard]] bool merge(LwwRegister const& other) noexcept {
        state_ = merge(state_, other.state_);
        return true;
    }

    [[nodiscard]] static constexpr state_type
    merge(state_type a, state_type b) noexcept {
        if (!a.has_value) {
            return b;
        }
        if (!b.has_value) {
            return a;
        }
        const auto order = a.clock <=> b.clock;
        if (std::is_lt(order)) {
            return b;
        }
        if (std::is_gt(order)) {
            return a;
        }
        return b.value < a.value ? a : b;
    }

    [[nodiscard]] constexpr state_type state() const noexcept {
        return state_;
    }

    [[nodiscard]] constexpr std::optional<V> value() const {
        if (!state_.has_value) {
            return std::nullopt;
        }
        return state_.value;
    }

private:
    state_type state_{};
};

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
struct CounterUpdate {
    ReplicaIndex<MaxReplicas> replica;
    CounterAmount amount;
};

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
struct GCounterState {
    safety::FixedArray<std::uint64_t, MaxReplicas> counts{};

    [[nodiscard]] friend constexpr bool operator==(
        GCounterState const&,
        GCounterState const&) = default;
};

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
class GCounter : public safety::Pinned<GCounter<MaxReplicas>> {
public:
    using state_type = GCounterState<MaxReplicas>;
    using update_type = CounterUpdate<MaxReplicas>;
    using local_update_type = LocalWrite<update_type>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool increment(local_update_type update) noexcept {
        auto const& op = update.value();
        auto& slot = state_.counts[op.replica.value()];
        slot = detail::sat_add(slot, op.amount.value());
        return true;
    }

    [[nodiscard]] bool merge(gossiped_state_type other) noexcept {
        state_ = merge(state_, other.value());
        return true;
    }

    [[nodiscard]] bool merge(GCounter const& other) noexcept {
        state_ = merge(state_, other.state_);
        return true;
    }

    [[nodiscard]] static constexpr state_type
    merge(state_type a, state_type b) noexcept {
        for (std::size_t i = 0; i < MaxReplicas; ++i) {
            if (a.counts[i] < b.counts[i]) {
                a.counts[i] = b.counts[i];
            }
        }
        return a;
    }

    [[nodiscard]] constexpr state_type state() const noexcept {
        return state_;
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < MaxReplicas; ++i) {
            sum = detail::sat_add(sum, state_.counts[i]);
        }
        return sum;
    }

private:
    state_type state_{};
};

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
struct PNCounterState {
    GCounterState<MaxReplicas> positive{};
    GCounterState<MaxReplicas> negative{};

    [[nodiscard]] friend constexpr bool operator==(
        PNCounterState const&,
        PNCounterState const&) = default;
};

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
class PNCounter : public safety::Pinned<PNCounter<MaxReplicas>> {
public:
    using state_type = PNCounterState<MaxReplicas>;
    using update_type = CounterUpdate<MaxReplicas>;
    using local_update_type = LocalWrite<update_type>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool increment(local_update_type update) noexcept {
        auto const& op = update.value();
        auto& slot = state_.positive.counts[op.replica.value()];
        slot = detail::sat_add(slot, op.amount.value());
        return true;
    }

    [[nodiscard]] bool decrement(local_update_type update) noexcept {
        auto const& op = update.value();
        auto& slot = state_.negative.counts[op.replica.value()];
        slot = detail::sat_add(slot, op.amount.value());
        return true;
    }

    [[nodiscard]] bool merge(gossiped_state_type other) noexcept {
        state_ = merge(state_, other.value());
        return true;
    }

    [[nodiscard]] bool merge(PNCounter const& other) noexcept {
        state_ = merge(state_, other.state_);
        return true;
    }

    [[nodiscard]] static constexpr state_type
    merge(state_type a, state_type b) noexcept {
        a.positive = GCounter<MaxReplicas>::merge(a.positive, b.positive);
        a.negative = GCounter<MaxReplicas>::merge(a.negative, b.negative);
        return a;
    }

    [[nodiscard]] constexpr state_type state() const noexcept {
        return state_;
    }

    [[nodiscard]] constexpr std::uint64_t positive() const noexcept {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < MaxReplicas; ++i) {
            sum = detail::sat_add(sum, state_.positive.counts[i]);
        }
        return sum;
    }

    [[nodiscard]] constexpr std::uint64_t negative() const noexcept {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < MaxReplicas; ++i) {
            sum = detail::sat_add(sum, state_.negative.counts[i]);
        }
        return sum;
    }

    [[nodiscard]] constexpr std::int64_t value() const noexcept {
        const std::uint64_t pos = positive();
        const std::uint64_t neg = negative();
        if (pos >= neg) {
            const std::uint64_t diff = pos - neg;
            const auto max = static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max());
            return diff > max
                ? std::numeric_limits<std::int64_t>::max()
                : static_cast<std::int64_t>(diff);
        }
        const std::uint64_t diff = neg - pos;
        const auto min_abs = static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) + 1u;
        return diff >= min_abs
            ? std::numeric_limits<std::int64_t>::min()
            : -static_cast<std::int64_t>(diff);
    }

private:
    state_type state_{};
};

template <typename V, std::size_t MaxNodes, typename ClockTag = void>
    requires CrdtCapacity<MaxNodes> &&
             std::default_initializable<V> &&
             std::copyable<V> &&
             std::equality_comparable<V>
struct MVRegisterVersion {
    V value{};
    VectorClockSnapshot<MaxNodes, ClockTag> clock{};
};

template <typename V, std::size_t MaxVersions, std::size_t MaxNodes, typename ClockTag = void>
    requires CrdtCapacity<MaxVersions> && CrdtCapacity<MaxNodes>
struct MVRegisterState {
    using version_type = MVRegisterVersion<V, MaxNodes, ClockTag>;

    safety::FixedArray<version_type, MaxVersions> versions{};
    std::uint16_t count = 0;
};

template <
    typename V,
    std::size_t MaxVersions = 16,
    std::size_t MaxNodes = 8,
    typename ClockTag = void>
    requires CrdtCapacity<MaxVersions> &&
             CrdtCapacity<MaxNodes> &&
             std::default_initializable<V> &&
             std::copyable<V> &&
             std::equality_comparable<V>
class MVRegister
    : public safety::Pinned<MVRegister<V, MaxVersions, MaxNodes, ClockTag>> {
public:
    using version_type = MVRegisterVersion<V, MaxNodes, ClockTag>;
    using state_type = MVRegisterState<V, MaxVersions, MaxNodes, ClockTag>;
    using local_write_type = LocalWrite<version_type>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool assign(local_write_type version) {
        return insert_version_(version.value());
    }

    [[nodiscard]] bool merge(gossiped_state_type other) {
        state_type staged = state_;
        if (!merge_state_into_(staged, other.value())) {
            return false;
        }
        state_ = staged;
        return true;
    }

    [[nodiscard]] bool merge(MVRegister const& other) {
        state_type staged = state_;
        if (!merge_state_into_(staged, other.state_)) {
            return false;
        }
        state_ = staged;
        return true;
    }

    [[nodiscard]] static state_type merge(state_type a, state_type const& b) {
        MVRegister tmp{};
        state_type staged = a;
        return tmp.merge_state_into_(staged, b) ? staged : a;
    }

    [[nodiscard]] state_type state() const {
        return state_;
    }

    [[nodiscard]] CrdtCount<MaxVersions> size() const noexcept {
        return CrdtCount<MaxVersions>{
            state_.count,
            typename CrdtCount<MaxVersions>::Trusted{}};
    }

private:
    [[nodiscard]] static bool same_version_(
        version_type const& a,
        version_type const& b) {
        return a.value == b.value && a.clock == b.clock;
    }

    [[nodiscard]] static bool insert_version_into_(
        state_type& state,
        version_type incoming) {
        std::uint16_t out = 0;
        safety::FixedArray<version_type, MaxVersions> kept{};
        for (std::uint16_t i = 0; i < state.count; ++i) {
            auto const& existing = state.versions[i];
            if (same_version_(existing, incoming)) {
                return true;
            }
            if (incoming.clock.happens_before(existing.clock)) {
                return true;
            }
            if (!existing.clock.happens_before(incoming.clock)) {
                kept[out] = existing;
                ++out;
            }
        }
        if (out == MaxVersions) {
            return false;
        }
        kept[out] = incoming;
        ++out;
        state.versions = kept;
        state.count = out;
        return true;
    }

    [[nodiscard]] bool insert_version_(version_type incoming) {
        return insert_version_into_(state_, incoming);
    }

    [[nodiscard]] static bool merge_state_into_(
        state_type& target,
        state_type const& other) {
        for (std::uint16_t i = 0; i < other.count; ++i) {
            if (!insert_version_into_(target, other.versions[i])) {
                return false;
            }
        }
        return true;
    }

    state_type state_{};
};

template <typename Id, typename T>
struct RgaInsert {
    Id id{};
    Id after{};
    T value{};
};

template <typename Id, typename T>
struct RgaNode {
    Id id{};
    Id after{};
    T value{};
    bool tombstone = false;
};

template <typename T, std::size_t Capacity>
    requires CrdtCapacity<Capacity> &&
             std::default_initializable<T> &&
             std::copyable<T>
struct RgaMaterialized {
    safety::FixedArray<T, Capacity> values{};
    std::uint16_t count = 0;
};

template <
    typename T,
    typename Id = std::uint64_t,
    std::size_t Capacity = 128>
    requires CrdtCapacity<Capacity> &&
             std::default_initializable<T> &&
             std::copyable<T> &&
             std::totally_ordered<T> &&
             std::default_initializable<Id> &&
             std::copyable<Id> &&
             std::totally_ordered<Id>
class RgaList : public safety::Pinned<RgaList<T, Id, Capacity>> {
public:
    using value_type = T;
    using id_type = Id;
    using insert_type = RgaInsert<Id, T>;
    using node_type = RgaNode<Id, T>;
    using state_type = detail::BoundedTaggedState<node_type, Id, Capacity>;
    using materialized_type = RgaMaterialized<T, Capacity>;
    using local_insert_type = LocalWrite<insert_type>;
    using local_erase_type = LocalWrite<Id>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool insert_after(local_insert_type insert) {
        auto const& op = insert.value();
        return upsert_(node_type{
            .id = op.id,
            .after = op.after,
            .value = op.value});
    }

    [[nodiscard]] bool erase(local_erase_type id) noexcept {
        if (auto idx = find_(id.value())) {
            state_.entries[*idx].tombstone = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool merge(gossiped_state_type other) {
        state_type staged = state_;
        if (!merge_state_into_(staged, other.value())) {
            return false;
        }
        state_ = staged;
        return true;
    }

    [[nodiscard]] bool merge(RgaList const& other) {
        state_type staged = state_;
        if (!merge_state_into_(staged, other.state_)) {
            return false;
        }
        state_ = staged;
        return true;
    }

    [[nodiscard]] static state_type merge(state_type a, state_type const& b) {
        RgaList tmp{};
        state_type staged = a;
        return tmp.merge_state_into_(staged, b) ? staged : a;
    }

    [[nodiscard]] state_type state() const {
        return state_;
    }

    [[nodiscard]] materialized_type materialize() const {
        materialized_type out{};
        safety::FixedArray<bool, Capacity> visited{};
        emit_after_(Id{}, visited, out);
        return out;
    }

private:
    [[nodiscard]] static std::optional<std::uint16_t>
    find_in_(state_type const& state, Id const& id) {
        for (std::uint16_t i = 0; i < state.count; ++i) {
            if (state.entries[i].id == id) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint16_t> find_(Id const& id) const {
        return find_in_(state_, id);
    }

    [[nodiscard]] static bool upsert_into_(
        state_type& state,
        node_type incoming) {
        if (auto idx = find_in_(state, incoming.id)) {
            auto& existing = state.entries[*idx];
            const bool incoming_preferred =
                incoming.after < existing.after ||
                (incoming.after == existing.after &&
                 incoming.value < existing.value);
            if (incoming_preferred) {
                const bool removed = existing.tombstone || incoming.tombstone;
                existing = incoming;
                existing.tombstone = removed;
            } else {
                existing.tombstone = existing.tombstone || incoming.tombstone;
            }
            return true;
        }
        if (state.count == Capacity) {
            return false;
        }
        state.entries[state.count] = incoming;
        ++state.count;
        return true;
    }

    [[nodiscard]] bool upsert_(node_type incoming) {
        return upsert_into_(state_, incoming);
    }

    [[nodiscard]] static bool merge_state_into_(
        state_type& target,
        state_type const& other) {
        for (std::uint16_t i = 0; i < other.count; ++i) {
            if (!upsert_into_(target, other.entries[i])) {
                return false;
            }
        }
        return true;
    }

    void emit_after_(
        Id const& parent,
        safety::FixedArray<bool, Capacity>& visited,
        materialized_type& out) const {
        for (;;) {
            std::optional<std::uint16_t> next{};
            for (std::uint16_t i = 0; i < state_.count; ++i) {
                auto const& node = state_.entries[i];
                if (visited[i] || !(node.after == parent)) {
                    continue;
                }
                if (!next || node.id < state_.entries[*next].id) {
                    next = i;
                }
            }
            if (!next) {
                return;
            }
            visited[*next] = true;
            auto const& node = state_.entries[*next];
            if (!node.tombstone && out.count < Capacity) {
                out.values[out.count] = node.value;
                ++out.count;
            }
            emit_after_(node.id, visited, out);
        }
    }

    state_type state_{};
};

template <typename C>
concept Crdt = requires(C& c, C const& other, typename C::state_type state) {
    typename C::state_type;
    typename C::gossiped_state_type;
    { c.state() } -> std::same_as<typename C::state_type>;
    { c.merge(other) } -> std::same_as<bool>;
    { c.merge(typename C::gossiped_state_type{state}) } -> std::same_as<bool>;
    { C::merge(c.state(), other.state()) } -> std::same_as<typename C::state_type>;
};

static_assert(Crdt<GSet<int, 8>>);
static_assert(Crdt<OrSet<int, std::uint64_t, 8>>);
static_assert(Crdt<LwwRegister<int, HlcTimestamp>>);
static_assert(Crdt<GCounter<4>>);
static_assert(Crdt<PNCounter<4>>);
static_assert(Crdt<MVRegister<int, 4, 4>>);
static_assert(Crdt<RgaList<int, std::uint64_t, 8>>);

}  // namespace crucible::canopy
