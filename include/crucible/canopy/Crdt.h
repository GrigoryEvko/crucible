#pragma once

#include <crucible/Platform.h>
#include <crucible/canopy/Hlc.h>
#include <crucible/canopy/VectorClock.h>
#include <fixy/FixedArray.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/Saturate.h>
#include <foundation/reflect/Hash.h>

#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::canopy {

// A write this replica authored, and state that arrived by gossip.  The
// two lanes are distinct types, so received state cannot pass for a
// local write and neither can enter a replica as a bare value.
template <typename T>
using LocalWrite = ::fixy::Tagged<T, ::fixy::tags::source::Local>;

template <typename State>
using GossipedState = ::fixy::Tagged<State, ::fixy::tags::source::Gossiped>;

// The two doors into the lanes.  Anything that arrives by gossip enters
// through the second one: a CRDT state here, and a digest or a delta in
// the anti-entropy layer above.
template <typename T>
[[nodiscard]] constexpr LocalWrite<T> admit_local_write(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return ::fixy::mint_tagged<::fixy::tags::source::Local>(std::move(value));
}

template <typename T>
[[nodiscard]] constexpr GossipedState<T> admit_gossiped(T received) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return ::fixy::mint_tagged<::fixy::tags::source::Gossiped>(std::move(received));
}

template <std::size_t Capacity>
concept CrdtCapacity = Capacity > 0 && Capacity <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max());

template <std::size_t Capacity>
    requires CrdtCapacity<Capacity>
inline constexpr auto crdt_index_bound = ::fixy::bounded_above<static_cast<std::uint16_t>(Capacity - 1)>;

template <std::size_t Capacity>
    requires CrdtCapacity<Capacity>
inline constexpr auto crdt_count_bound = ::fixy::bounded_above<static_cast<std::uint16_t>(Capacity)>;

template <std::size_t Capacity>
    requires CrdtCapacity<Capacity>
using CrdtIndex = ::fixy::Refined<crdt_index_bound<Capacity>, std::uint16_t>;

template <std::size_t Capacity>
    requires CrdtCapacity<Capacity>
using CrdtCount = ::fixy::Refined<crdt_count_bound<Capacity>, std::uint16_t>;

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
using ReplicaIndex = CrdtIndex<MaxReplicas>;

using CounterAmount = ::fixy::Refined<::fixy::positive, std::uint64_t>;

namespace detail {

// The state of a replica is well formed by construction, so its count
// always satisfies the bound.  The checked mint also does a check of the
// predicate.  A broken invariant then stops here, and it does not continue
// as a false refinement.
template <std::size_t Capacity>
    requires CrdtCapacity<Capacity>
[[nodiscard]] constexpr CrdtCount<Capacity> crdt_count(std::uint16_t live) noexcept {
    return ::fixy::mint_refined<crdt_count_bound<Capacity>>(live);
}

// The slot positions of the open-addressing table are part of the persisted
// CRDT state.  The slot hash must give the same bits on every platform and in
// every build.  A standard library hash is implementation-defined and can
// change between versions.  A state that one process wrote would then probe
// different slots in a different process and give an incorrect answer.  The
// mix is the one fmix64 of the tree.  A change to it makes every persisted
// state incorrect.
//
// The admitted set is closed: enums and integers, whose bits alone are the
// value.  A pointer hashes an address, which differs between processes, so
// two peers would place one value in different slots.  A floating-point value
// breaks the probe: -0.0 and +0.0 compare equal but hash apart, and a NaN
// never compares equal to itself, so the set would store it again on every
// insert.
template <typename T>
concept HashableValue = std::integral<T> || std::is_enum_v<T>;

template <HashableValue T>
[[nodiscard]] constexpr std::uint64_t stable_hash(T value) noexcept {
    if constexpr (std::is_enum_v<T>) {
        return ::foundation::reflect::fmix64(static_cast<std::uint64_t>(std::to_underlying(value)));
    } else {
        return ::foundation::reflect::fmix64(static_cast<std::uint64_t>(value));
    }
}

// A staged merge: the incoming state folds into a copy of the target, and
// the target changes only when every step succeeded.  A refused merge
// leaves the target as it was.
template <typename State, typename MergeInto>
[[nodiscard]] constexpr bool commit_merge(State& target, State const& incoming, MergeInto merge_into) {
    State staged = target;
    if (!merge_into(staged, incoming)) {
        return false;
    }
    target = staged;
    return true;
}

template <HashableValue T, std::size_t Capacity>
    requires CrdtCapacity<Capacity>
struct BoundedHashSetState {
    struct Slot {
        bool occupied = false;
        T value{};
    };

    ::fixy::FixedArray<Slot, Capacity> slots{};
    std::uint16_t count = 0;

    // The occupied slots always number at most Capacity, so a count that
    // agrees with them is also in bound.
    [[nodiscard]] bool well_formed() const noexcept {
        std::size_t occupied = 0;
        for (std::size_t i = 0; i < Capacity; ++i) {
            occupied += slots[i].occupied ? std::size_t{1} : std::size_t{0};
        }
        return occupied == count;
    }

    [[nodiscard]] bool contains(T const& value) const {
        const std::size_t start = static_cast<std::size_t>(stable_hash<T>(value)) % Capacity;
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
        const std::size_t start = static_cast<std::size_t>(stable_hash<T>(value)) % Capacity;
        for (std::size_t n = 0; n < Capacity; ++n) {
            const std::size_t idx = (start + n) % Capacity;
            if (slots[idx].occupied && slots[idx].value == value) {
                return true;
            }
            if (!slots[idx].occupied) {
                // `>=`, not `==`: count is public, so a count already
                // past Capacity would read as "not full" here and the
                // ++ below would push it further out of range.
                if (count >= Capacity) {
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
        if (!other.well_formed()) {
            return false;
        }
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (other.slots[i].occupied && !insert(other.slots[i].value)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] friend bool operator==(BoundedHashSetState const& a, BoundedHashSetState const& b) {
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
    ::fixy::FixedArray<T, Capacity> entries{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr bool well_formed() const noexcept { return count <= Capacity; }
};

// The sum of the counts of the replicas.  The sum saturates at the maximum
// and does not wrap.
template <std::size_t MaxReplicas, typename Counts>
[[nodiscard]] constexpr std::uint64_t saturating_total(Counts const& counts) noexcept {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < MaxReplicas; ++i) {
        sum = ::foundation::sat::add_sat(sum, counts[i]);
    }
    return sum;
}

}  // namespace detail

template <typename T, std::size_t Capacity = 64>
    requires detail::HashableValue<T> && CrdtCapacity<Capacity>
class GSet : public ::foundation::Pinned<GSet<T, Capacity>> {
public:
    using value_type = T;
    using state_type = detail::BoundedHashSetState<T, Capacity>;
    using local_value_type = LocalWrite<T>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool add(local_value_type value) { return state_.insert(value.value()); }

    [[nodiscard]] bool merge(gossiped_state_type const& other) {
        return detail::commit_merge(state_, other.value(), merge_state_into_);
    }

    [[nodiscard]] bool merge(GSet const& other) { return detail::commit_merge(state_, other.state_, merge_state_into_); }

    // `a` is the merge TARGET and arrives from the caller, so it is as
    // untrusted as `b`.  BoundedHashSetState::merge validates only `b`,
    // so both sides are checked here.  On refusal `a` comes back
    // unchanged.
    [[nodiscard]] static state_type merge(state_type a, state_type const& b) {
        if (a.well_formed()) {
            (void)detail::commit_merge(a, b, merge_state_into_);
        }
        return a;
    }

    [[nodiscard]] state_type state() const { return state_; }

    [[nodiscard]] bool contains(T const& value) const { return state_.contains(value); }

    [[nodiscard]] CrdtCount<Capacity> size() const noexcept { return detail::crdt_count<Capacity>(state_.count); }

private:
    [[nodiscard]] static bool merge_state_into_(state_type& target, state_type const& other) {
        return target.merge(other);
    }

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

template <typename T, typename TagId = std::uint64_t, std::size_t Capacity = 128>
    requires CrdtCapacity<Capacity> && std::default_initializable<T> && std::copyable<T> && std::equality_comparable<T>
          && std::default_initializable<TagId> && std::copyable<TagId> && std::equality_comparable<TagId>
class OrSet : public ::foundation::Pinned<OrSet<T, TagId, Capacity>> {
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
        return upsert_into_(state_, entry_type{.value = op.value, .tag = op.tag});
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

    [[nodiscard]] bool merge(gossiped_state_type const& other) {
        return detail::commit_merge(state_, other.value(), merge_state_into_);
    }

    [[nodiscard]] bool merge(OrSet const& other) { return detail::commit_merge(state_, other.state_, merge_state_into_); }

    // `a` is the merge TARGET and arrives from the caller, so it is as
    // untrusted as `b`.  merge_state_into_ validates only `b`, so a
    // caller-supplied `a` with count past Capacity would be walked by
    // find_ (out-of-bounds read) and then written at entries[count]
    // (out-of-bounds write).  On refusal `a` comes back unchanged.
    [[nodiscard]] static state_type merge(state_type a, state_type const& b) {
        if (a.well_formed()) {
            (void)detail::commit_merge(a, b, merge_state_into_);
        }
        return a;
    }

    [[nodiscard]] state_type state() const { return state_; }

private:
    [[nodiscard]] static std::optional<std::uint16_t> find_(state_type const& state, T const& value, TagId const& tag) {
        for (std::uint16_t i = 0; i < state.count; ++i) {
            auto const& e = state.entries[i];
            if (e.value == value && e.tag == tag) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool upsert_into_(state_type& state, entry_type incoming) {
        if (auto idx = find_(state, incoming.value, incoming.tag)) {
            state.entries[*idx].removed = state.entries[*idx].removed || incoming.removed;
            return true;
        }
        // `>=`, not `==`: BoundedTaggedState::count is public, so a
        // count already past Capacity would read as "not full" here and
        // the write below would land outside entries.
        if (state.count >= Capacity) {
            return false;
        }
        state.entries[state.count] = incoming;
        ++state.count;
        return true;
    }

    [[nodiscard]] static bool merge_state_into_(state_type& target, state_type const& other) {
        if (!other.well_formed()) {
            return false;
        }
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

    [[nodiscard]] friend constexpr bool operator==(LwwRegisterState const&, LwwRegisterState const&) = default;
};

template <typename V, typename Clock>
    requires std::default_initializable<V> && std::copyable<V> && std::totally_ordered<V>
          && std::default_initializable<Clock> && std::copyable<Clock>
          && std::three_way_comparable<Clock, std::strong_ordering>
class LwwRegister : public ::foundation::Pinned<LwwRegister<V, Clock>> {
public:
    using value_type = V;
    using clock_type = Clock;
    using write_type = LwwRegisterWrite<V, Clock>;
    using state_type = LwwRegisterState<V, Clock>;
    using local_write_type = LocalWrite<write_type>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool assign(local_write_type write) noexcept {
        state_ =
            merge(state_, state_type{.value = write.value().value, .clock = write.value().clock, .has_value = true});
        return true;
    }

    [[nodiscard]] bool merge(gossiped_state_type const& other) noexcept {
        state_ = merge(state_, other.value());
        return true;
    }

    [[nodiscard]] bool merge(LwwRegister const& other) noexcept {
        state_ = merge(state_, other.state_);
        return true;
    }

    [[nodiscard]] static constexpr state_type merge(state_type a, state_type b) noexcept {
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

    [[nodiscard]] constexpr state_type state() const noexcept { return state_; }

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

// The checked door for a counter update.  A replica outside the counter or
// an amount of zero is refused here, before either becomes a refinement.
template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
[[nodiscard]] constexpr std::optional<LocalWrite<CounterUpdate<MaxReplicas>>>
admit_counter_update(std::uint16_t replica, std::uint64_t amount) noexcept {
    if (replica >= MaxReplicas || amount == 0) {
        return std::nullopt;
    }
    return admit_local_write(CounterUpdate<MaxReplicas>{
        .replica = ::fixy::mint_refined<crdt_index_bound<MaxReplicas>>(replica),
        .amount = ::fixy::mint_refined<::fixy::positive>(amount),
    });
}

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
struct GCounterState {
    ::fixy::FixedArray<std::uint64_t, MaxReplicas> counts{};

    [[nodiscard]] friend constexpr bool operator==(GCounterState const&, GCounterState const&) = default;

    constexpr void add(CounterUpdate<MaxReplicas> const& update) noexcept {
        auto& slot = counts[update.replica.value()];
        slot = ::foundation::sat::add_sat(slot, update.amount.value());
    }

    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return detail::saturating_total<MaxReplicas>(counts);
    }
};

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
class GCounter : public ::foundation::Pinned<GCounter<MaxReplicas>> {
public:
    using state_type = GCounterState<MaxReplicas>;
    using update_type = CounterUpdate<MaxReplicas>;
    using local_update_type = LocalWrite<update_type>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool increment(local_update_type update) noexcept {
        state_.add(update.value());
        return true;
    }

    [[nodiscard]] bool merge(gossiped_state_type const& other) noexcept {
        state_ = merge(state_, other.value());
        return true;
    }

    [[nodiscard]] bool merge(GCounter const& other) noexcept {
        state_ = merge(state_, other.state_);
        return true;
    }

    [[nodiscard]] static constexpr state_type merge(state_type a, state_type b) noexcept {
        for (std::size_t i = 0; i < MaxReplicas; ++i) {
            if (a.counts[i] < b.counts[i]) {
                a.counts[i] = b.counts[i];
            }
        }
        return a;
    }

    [[nodiscard]] constexpr state_type state() const noexcept { return state_; }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return state_.total(); }

private:
    state_type state_{};
};

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
struct PNCounterState {
    GCounterState<MaxReplicas> positive{};
    GCounterState<MaxReplicas> negative{};

    [[nodiscard]] friend constexpr bool operator==(PNCounterState const&, PNCounterState const&) = default;
};

template <std::size_t MaxReplicas>
    requires CrdtCapacity<MaxReplicas>
class PNCounter : public ::foundation::Pinned<PNCounter<MaxReplicas>> {
public:
    using state_type = PNCounterState<MaxReplicas>;
    using update_type = CounterUpdate<MaxReplicas>;
    using local_update_type = LocalWrite<update_type>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool increment(local_update_type update) noexcept {
        state_.positive.add(update.value());
        return true;
    }

    [[nodiscard]] bool decrement(local_update_type update) noexcept {
        state_.negative.add(update.value());
        return true;
    }

    [[nodiscard]] bool merge(gossiped_state_type const& other) noexcept {
        state_ = merge(state_, other.value());
        return true;
    }

    [[nodiscard]] bool merge(PNCounter const& other) noexcept {
        state_ = merge(state_, other.state_);
        return true;
    }

    [[nodiscard]] static constexpr state_type merge(state_type a, state_type b) noexcept {
        a.positive = GCounter<MaxReplicas>::merge(a.positive, b.positive);
        a.negative = GCounter<MaxReplicas>::merge(a.negative, b.negative);
        return a;
    }

    [[nodiscard]] constexpr state_type state() const noexcept { return state_; }

    [[nodiscard]] constexpr std::uint64_t positive() const noexcept { return state_.positive.total(); }

    [[nodiscard]] constexpr std::uint64_t negative() const noexcept { return state_.negative.total(); }

    [[nodiscard]] constexpr std::int64_t value() const noexcept {
        const std::uint64_t pos = positive();
        const std::uint64_t neg = negative();
        if (pos >= neg) {
            const std::uint64_t diff = pos - neg;
            const auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            return diff > max ? std::numeric_limits<std::int64_t>::max() : static_cast<std::int64_t>(diff);
        }
        const std::uint64_t diff = neg - pos;
        const auto min_abs = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u;
        return diff >= min_abs ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(diff);
    }

private:
    state_type state_{};
};

template <typename V, std::size_t MaxNodes, typename ClockTag = void>
    requires CrdtCapacity<MaxNodes> && std::default_initializable<V> && std::copyable<V> && std::totally_ordered<V>
struct MVRegisterVersion {
    V value{};
    VectorClockSnapshot<MaxNodes, ClockTag> clock{};
};

template <typename V, std::size_t MaxVersions, std::size_t MaxNodes, typename ClockTag = void>
    requires CrdtCapacity<MaxVersions> && CrdtCapacity<MaxNodes>
struct MVRegisterState {
    using version_type = MVRegisterVersion<V, MaxNodes, ClockTag>;

    ::fixy::FixedArray<version_type, MaxVersions> versions{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr bool well_formed() const noexcept { return count <= MaxVersions; }
};

template <typename V, std::size_t MaxVersions = 16, std::size_t MaxNodes = 8, typename ClockTag = void>
    requires CrdtCapacity<MaxVersions> && CrdtCapacity<MaxNodes> && std::default_initializable<V> && std::copyable<V>
          && std::totally_ordered<V>
class MVRegister : public ::foundation::Pinned<MVRegister<V, MaxVersions, MaxNodes, ClockTag>> {
public:
    using version_type = MVRegisterVersion<V, MaxNodes, ClockTag>;
    using state_type = MVRegisterState<V, MaxVersions, MaxNodes, ClockTag>;
    using local_write_type = LocalWrite<version_type>;
    using gossiped_state_type = GossipedState<state_type>;

    [[nodiscard]] bool assign(local_write_type version) { return insert_version_into_(state_, version.value()); }

    [[nodiscard]] bool merge(gossiped_state_type const& other) {
        return detail::commit_merge(state_, other.value(), merge_state_into_);
    }

    [[nodiscard]] bool merge(MVRegister const& other) {
        return detail::commit_merge(state_, other.state_, merge_state_into_);
    }

    // `a` is the merge TARGET and arrives from the caller.
    // insert_version_into_ does gate `state.count > MaxVersions`, but
    // only once it is reached: an empty `b` skips the loop entirely and
    // would return a malformed state.  Gate `a` up front so no malformed
    // state leaves this function.
    [[nodiscard]] static state_type merge(state_type a, state_type const& b) {
        if (a.well_formed()) {
            (void)detail::commit_merge(a, b, merge_state_into_);
        }
        return a;
    }

    [[nodiscard]] state_type state() const { return state_; }

    [[nodiscard]] CrdtCount<MaxVersions> size() const noexcept { return detail::crdt_count<MaxVersions>(state_.count); }

private:
    [[nodiscard]] static bool same_version_(version_type const& a, version_type const& b) {
        return a.value == b.value && a.clock == b.clock;
    }

    [[nodiscard]] static bool clock_less_(version_type const& a, version_type const& b) noexcept {
        for (std::size_t i = 0; i < MaxNodes; ++i) {
            if (a.clock.entries[i] != b.clock.entries[i]) {
                return a.clock.entries[i] < b.clock.entries[i];
            }
        }
        return false;
    }

    [[nodiscard]] static bool version_less_(version_type const& a, version_type const& b) {
        if (clock_less_(a, b)) {
            return true;
        }
        if (clock_less_(b, a)) {
            return false;
        }
        return a.value < b.value;
    }

    static void canonicalize_(state_type& state) {
        const std::size_t n = state.count;
        for (std::size_t i = 1; i < n; ++i) {
            version_type key = state.versions[i];
            std::size_t j = i;
            while (j > 0 && version_less_(key, state.versions[j - 1])) {
                state.versions[j] = state.versions[j - 1];
                --j;
            }
            state.versions[j] = key;
        }
    }

    [[nodiscard]] static bool insert_version_into_(state_type& state, version_type incoming) {
        if (!state.well_formed()) {
            return false;
        }
        std::uint16_t out = 0;
        ::fixy::FixedArray<version_type, MaxVersions> kept{};
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
        // `>=`, not `==`: `out` is bounded by state.count today, but the
        // equality form only holds while that stays true, and the write
        // below is the one that would land outside `kept`.
        if (out >= MaxVersions) {
            return false;
        }
        kept[out] = incoming;
        ++out;
        state.versions = kept;
        state.count = out;
        canonicalize_(state);
        return true;
    }

    [[nodiscard]] static bool merge_state_into_(state_type& target, state_type const& other) {
        if (!other.well_formed()) {
            return false;
        }
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
    requires CrdtCapacity<Capacity> && std::default_initializable<T> && std::copyable<T>
struct RgaMaterialized {
    ::fixy::FixedArray<T, Capacity> values{};
    std::uint16_t count = 0;
};

template <typename T, typename Id = std::uint64_t, std::size_t Capacity = 128>
    requires CrdtCapacity<Capacity> && std::default_initializable<T> && std::copyable<T> && std::totally_ordered<T>
          && std::default_initializable<Id> && std::copyable<Id> && std::totally_ordered<Id>
class RgaList : public ::foundation::Pinned<RgaList<T, Id, Capacity>> {
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
        return upsert_into_(state_, node_type{.id = op.id, .after = op.after, .value = op.value});
    }

    [[nodiscard]] bool erase(local_erase_type id) noexcept {
        if (auto idx = find_in_(state_, id.value())) {
            state_.entries[*idx].tombstone = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool merge(gossiped_state_type const& other) {
        return detail::commit_merge(state_, other.value(), merge_state_into_);
    }

    [[nodiscard]] bool merge(RgaList const& other) {
        return detail::commit_merge(state_, other.state_, merge_state_into_);
    }

    // `a` is the merge TARGET and arrives from the caller, so it is as
    // untrusted as `b`.  merge_state_into_ validates only `b`, so a
    // caller-supplied `a` with count past Capacity would be walked by
    // find_in_ (out-of-bounds read) and then written at entries[count]
    // (out-of-bounds write).  On refusal `a` comes back unchanged.
    [[nodiscard]] static state_type merge(state_type a, state_type const& b) {
        if (a.well_formed()) {
            (void)detail::commit_merge(a, b, merge_state_into_);
        }
        return a;
    }

    [[nodiscard]] state_type state() const { return state_; }

    [[nodiscard]] materialized_type materialize() const {
        materialized_type out{};
        ::fixy::FixedArray<bool, Capacity> visited{};
        emit_after_(Id{}, visited, out);
        return out;
    }

private:
    [[nodiscard]] static std::optional<std::uint16_t> find_in_(state_type const& state, Id const& id) {
        for (std::uint16_t i = 0; i < state.count; ++i) {
            if (state.entries[i].id == id) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool upsert_into_(state_type& state, node_type incoming) {
        if (auto idx = find_in_(state, incoming.id)) {
            auto& existing = state.entries[*idx];
            const bool incoming_preferred = incoming.after < existing.after
                                         || (incoming.after == existing.after && incoming.value < existing.value);
            if (incoming_preferred) {
                const bool removed = existing.tombstone || incoming.tombstone;
                existing = incoming;
                existing.tombstone = removed;
            } else {
                existing.tombstone = existing.tombstone || incoming.tombstone;
            }
            return true;
        }
        // `>=`, not `==`: BoundedTaggedState::count is public, so a
        // count already past Capacity would read as "not full" here and
        // the write below would land outside entries.
        if (state.count >= Capacity) {
            return false;
        }
        state.entries[state.count] = incoming;
        ++state.count;
        return true;
    }

    [[nodiscard]] static bool merge_state_into_(state_type& target, state_type const& other) {
        if (!other.well_formed()) {
            return false;
        }
        for (std::uint16_t i = 0; i < other.count; ++i) {
            if (!upsert_into_(target, other.entries[i])) {
                return false;
            }
        }
        return true;
    }

    void emit_after_(Id const& parent, ::fixy::FixedArray<bool, Capacity>& visited, materialized_type& out) const {
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
    { c.merge(admit_gossiped(state)) } -> std::same_as<bool>;
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
