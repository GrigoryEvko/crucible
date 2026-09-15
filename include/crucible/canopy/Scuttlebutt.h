#pragma once

#include <crucible/Platform.h>
#include <crucible/canopy/Crdt.h>
#include <crucible/canopy/Swim.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::canopy {

template <std::size_t MaxPeers, std::size_t MaxKeys>
concept ScuttlebuttShape = MaxPeers > 0 && MaxKeys > 0
                        && MaxPeers <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())
                        && MaxKeys <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())
                        && MaxKeys <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) / MaxPeers;

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
inline constexpr std::size_t scuttlebutt_entry_capacity = MaxPeers * MaxKeys;

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
using ScuttlebuttEntryCount =
    safety::Refined<safety::bounded_above<static_cast<std::uint16_t>(scuttlebutt_entry_capacity<MaxPeers, MaxKeys>)>,
                    std::uint16_t>;

// The live-entry count of a bounded entry table.
//
// It used to be a bare public std::uint16_t sitting beside the entries
// array on an aggregate, so any caller could store a value past
// `capacity` and no path anywhere looked.  push() then read
// entries[capacity, count) during its dedup scan and wrote
// entries[count], both out of bounds.  The `count == capacity` fullness
// test could not see it, because a count already past the bound
// compares unequal against it: 41 == 4 is false.
//
// The value is private here and reserve_next() is the only thing that
// moves it, so `value_ <= Capacity` is an invariant of the type rather
// than a habit of its callers, and a count past the bound is
// unrepresentable rather than merely unchecked.
//
// Every Debug-only backstop that masked this is absent from Release:
// _GLIBCXX_ASSERTIONS is not defined there, CRUCIBLE_PRE degrades to
// [[assume]], and safety::FixedArray::operator[] carries no
// precondition by design.  A P2900 contract_assert would have survived
// -- Release compiles contracts as `observe` onto a handler that aborts
// -- but the type is the better answer.  It costs nothing, no build
// flag can switch it off, and it refuses the bad value at the
// assignment instead of one call later at the subscript.
template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
class ScuttlebuttSlotCount {
public:
    // Exactly safety::FixedArray<T, Capacity>::index_type, so the proof
    // token reserve_next() returns is accepted by FixedArray::at().
    using index_type = safety::Refined<safety::bounded_above<Capacity - 1>, std::size_t>;

    constexpr ScuttlebuttSlotCount() noexcept = default;

    // Implicit and lossless on purpose.  Readers spell `i < digest.count`
    // and `static_cast<std::size_t>(set.count)`, and widening a bounded
    // count to its own underlying type cannot lose information.  The
    // conversion is one-way -- nothing converts back in -- so the bound
    // cannot be re-entered from outside.
    [[nodiscard]] constexpr operator std::uint16_t() const noexcept { return value_; }

    [[nodiscard]] constexpr std::uint16_t value() const noexcept { return value_; }

    [[nodiscard]] constexpr bool full() const noexcept { return value_ >= Capacity; }

    // Reserves the slot one past the last live entry and hands back a
    // proof-token index for it.  This is the sole mutator, and it
    // refuses at the bound, so every index it returns is in range by
    // construction and no caller needs a comparison of its own.
    [[nodiscard]] constexpr std::optional<index_type> reserve_next() noexcept {
        if (full()) {
            return std::nullopt;
        }
        const auto slot = static_cast<std::size_t>(value_);
        ++value_;
        return index_type{slot};
    }

private:
    std::uint16_t value_ = 0;
};

static_assert(sizeof(ScuttlebuttSlotCount<4>) == sizeof(std::uint16_t));
static_assert(!std::is_assignable_v<ScuttlebuttSlotCount<4>&, std::uint16_t>,
              "a count past the bound must stay unrepresentable");

using ScuttlebuttDurationNs = safety::Refined<safety::positive, std::uint64_t>;
using ScuttlebuttPositiveCount = safety::Refined<safety::positive, std::uint16_t>;

struct ScuttlebuttConfig {
    ScuttlebuttDurationNs period_ns{5000000000ULL};
    ScuttlebuttPositiveCount max_stale_rounds{64};
};

struct ScuttlebuttKey {
    std::uint64_t hash = 0;
    std::uint16_t length = 0;

    [[nodiscard]] friend constexpr bool operator==(ScuttlebuttKey const&, ScuttlebuttKey const&) = default;
};

using LocalScuttlebuttKey = LocalWrite<ScuttlebuttKey>;

enum class ScuttlebuttError : std::uint8_t {
    CapacityExceeded,
    DuplicatePeer,
    EmptyKey,
    KeyTooLong,
    MalformedDelta,
    MalformedDigest,
    MergeRejected,
    NotAvailable,
    TypeMismatch,
    UnknownKey,
    UnknownPeer,
    VersionOverflow,
    ZeroUuid,
};

namespace detail {

[[nodiscard]] constexpr std::uint64_t fnv1a64(std::string_view text) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (char raw : text) {
        auto const ch = static_cast<unsigned char>(raw);
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash == 0 ? std::uint64_t{1} : hash;
}

template <typename C>
inline constexpr std::uint8_t scuttlebutt_crdt_type_anchor = 0;

template <typename C>
[[nodiscard]] constexpr void const* crdt_type_cookie() noexcept {
    return &scuttlebutt_crdt_type_anchor<C>;
}

}  // namespace detail

[[nodiscard]] inline std::expected<LocalScuttlebuttKey, ScuttlebuttError>
admit_scuttlebutt_key(std::string_view key) noexcept {
    if (key.empty()) {
        return std::unexpected(ScuttlebuttError::EmptyKey);
    }
    if (key.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return std::unexpected(ScuttlebuttError::KeyTooLong);
    }
    return LocalScuttlebuttKey{ScuttlebuttKey{
        .hash = detail::fnv1a64(key),
        .length = static_cast<std::uint16_t>(key.size()),
    }};
}

template <typename C>
concept ScuttlebuttCrdt = requires(C& crdt, C const& const_crdt, typename C::state_type state) {
    typename C::state_type;
    { const_crdt.state() } -> std::same_as<typename C::state_type>;
    { crdt.merge(GossipedState<typename C::state_type>{state}) } -> std::same_as<bool>;
} && std::copyable<typename C::state_type>;

struct ScuttlebuttVersionEntry {
    cog::Uuid origin{};
    ScuttlebuttKey key{};
    std::uint64_t version = 0;
};

template <typename State>
struct ScuttlebuttDelta {
    cog::Uuid origin{};
    ScuttlebuttKey key{};
    std::uint64_t version = 0;
    State state{};
};

template <typename State>
using LocalScuttlebuttDelta = safety::Tagged<ScuttlebuttDelta<State>, safety::source::Local>;

template <typename State>
using GossipedScuttlebuttDelta = safety::Tagged<ScuttlebuttDelta<State>, safety::source::Gossiped>;

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
struct ScuttlebuttDigest {
    static constexpr std::size_t capacity = scuttlebutt_entry_capacity<MaxPeers, MaxKeys>;

    // Only entries[0, count) are live.  The tail keeps whatever the
    // FixedArray default gave it and no reader looks at it.
    safety::FixedArray<ScuttlebuttVersionEntry, capacity> entries{};
    ScuttlebuttSlotCount<capacity> count{};

    [[nodiscard]] constexpr ScuttlebuttEntryCount<MaxPeers, MaxKeys> size() const noexcept {
        // Trusted is sound here because ScuttlebuttSlotCount cannot hold
        // a value above capacity.  It was not sound while count was a
        // bare public member: that path minted a Refined whose predicate
        // its own value did not satisfy.
        return ScuttlebuttEntryCount<MaxPeers, MaxKeys>{count.value(),
                                                        typename ScuttlebuttEntryCount<MaxPeers, MaxKeys>::Trusted{}};
    }

    [[nodiscard]] bool push(ScuttlebuttVersionEntry entry) noexcept {
        if (entry.version == 0) {
            return true;
        }
        if (entry.origin.is_zero() || entry.key.hash == 0 || entry.key.length == 0) {
            return false;
        }
        for (std::uint16_t i = 0; i < count; ++i) {
            auto& existing = entries[static_cast<std::size_t>(i)];
            if (existing.origin == entry.origin && existing.key == entry.key) {
                if (existing.version < entry.version) {
                    existing.version = entry.version;
                }
                return true;
            }
        }
        // The count enforces the bound and yields the slot, so the
        // fullness test and the subscript cannot disagree.
        const auto slot = count.reserve_next();
        if (!slot) {
            return false;
        }
        entries.at(*slot) = entry;
        return true;
    }

    // No count-versus-capacity arm: ScuttlebuttSlotCount cannot carry a
    // count above capacity, so the loop below indexes in range for every
    // value the type admits.  What stays representable is entry content,
    // because `entries` is public -- a caller can overwrite a live slot
    // after push() validated it -- and that is what this checks.
    [[nodiscard]] bool well_formed() const noexcept {
        for (std::uint16_t i = 0; i < count; ++i) {
            auto const& a = entries[static_cast<std::size_t>(i)];
            if (a.version == 0 || a.origin.is_zero() || a.key.hash == 0 || a.key.length == 0) {
                return false;
            }
            const std::uint16_t j0 = static_cast<std::uint16_t>(i + std::uint16_t{1});
            for (std::uint16_t j = j0; j < count; ++j) {
                auto const& b = entries[static_cast<std::size_t>(j)];
                if (a.origin == b.origin && a.key == b.key) {
                    return false;
                }
            }
        }
        return true;
    }
};

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
using GossipedScuttlebuttDigest = safety::Tagged<ScuttlebuttDigest<MaxPeers, MaxKeys>, safety::source::Gossiped>;

// This one carries no well_formed(), and unlike the digest it does not
// need one.  It is an output-only local aggregate: compare_digest is its
// only producer and it pushes entries drawn from an already-validated
// digest and from the local peer/key tables.  Its bound is now a
// property of ScuttlebuttSlotCount rather than of a comparison, and its
// only consumer -- delta_for_request -- resolves origin and key through
// find_peer_ and require_key_ before it indexes anything.  A validator
// with no caller would be weight, not safety.
template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
struct ScuttlebuttRequestSet {
    static constexpr std::size_t capacity = scuttlebutt_entry_capacity<MaxPeers, MaxKeys>;

    // Only entries[0, count) are live.
    safety::FixedArray<ScuttlebuttVersionEntry, capacity> entries{};
    ScuttlebuttSlotCount<capacity> count{};

    [[nodiscard]] constexpr ScuttlebuttEntryCount<MaxPeers, MaxKeys> size() const noexcept {
        return ScuttlebuttEntryCount<MaxPeers, MaxKeys>{count.value(),
                                                        typename ScuttlebuttEntryCount<MaxPeers, MaxKeys>::Trusted{}};
    }

    [[nodiscard]] bool push(ScuttlebuttVersionEntry entry) noexcept {
        if (entry.version == 0 || entry.origin.is_zero() || entry.key.hash == 0 || entry.key.length == 0) {
            return false;
        }
        for (std::uint16_t i = 0; i < count; ++i) {
            auto& existing = entries[static_cast<std::size_t>(i)];
            if (existing.origin == entry.origin && existing.key == entry.key) {
                if (existing.version < entry.version) {
                    existing.version = entry.version;
                }
                return true;
            }
        }
        const auto slot = count.reserve_next();
        if (!slot) {
            return false;
        }
        entries.at(*slot) = entry;
        return true;
    }
};

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
struct ScuttlebuttDiff {
    ScuttlebuttRequestSet<MaxPeers, MaxKeys> requests{};
    ScuttlebuttRequestSet<MaxPeers, MaxKeys> offers{};
};

template <std::size_t MaxPeers = 128, std::size_t MaxKeys = 128>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
class alignas(64) ScuttlebuttSync : public safety::Pinned<ScuttlebuttSync<MaxPeers, MaxKeys>> {
public:
    using peer_type = SwimPeer;
    using digest_type = ScuttlebuttDigest<MaxPeers, MaxKeys>;
    using gossiped_digest_type = GossipedScuttlebuttDigest<MaxPeers, MaxKeys>;
    using request_set_type = ScuttlebuttRequestSet<MaxPeers, MaxKeys>;
    using diff_type = ScuttlebuttDiff<MaxPeers, MaxKeys>;

    explicit ScuttlebuttSync(peer_type local_peer, std::span<const peer_type> initial_peers = {},
                             ScuttlebuttConfig config = {}) noexcept
        : config_{config} {
        CRUCIBLE_FATAL_INVARIANT(add_peer(local_peer).has_value());
        local_index_ = std::uint16_t{0};
        for (peer_type const& peer : initial_peers) {
            CRUCIBLE_FATAL_INVARIANT(add_peer(peer).has_value());
        }
    }

    [[nodiscard]] std::expected<void, ScuttlebuttError> add_peer(peer_type peer) noexcept {
        cog::CogIdentity const& id = peer.value();
        if (id.uuid.is_zero()) {
            return std::unexpected(ScuttlebuttError::ZeroUuid);
        }
        if (find_peer_(id.uuid).has_value()) {
            return std::unexpected(ScuttlebuttError::DuplicatePeer);
        }
        if (peer_count_ == MaxPeers) {
            return std::unexpected(ScuttlebuttError::CapacityExceeded);
        }
        peers_[static_cast<std::size_t>(peer_count_)] = PeerSlot{
            .occupied = true,
            .id = id.uuid,
        };
        ++peer_count_;
        return {};
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<void, ScuttlebuttError> register_state(LocalScuttlebuttKey key, C& state) noexcept {
        (void)state;
        ScuttlebuttKey const& raw_key = key.value();
        auto existing = find_key_(raw_key);
        void const* cookie = detail::crdt_type_cookie<C>();
        if (existing.has_value()) {
            KeySlot& slot = keys_[*existing];
            if (slot.type_cookie != cookie) {
                return std::unexpected(ScuttlebuttError::TypeMismatch);
            }
            return {};
        }
        if (key_count_ == MaxKeys) {
            return std::unexpected(ScuttlebuttError::CapacityExceeded);
        }
        keys_[static_cast<std::size_t>(key_count_)] = KeySlot{
            .occupied = true,
            .key = raw_key,
            .type_cookie = cookie,
        };
        ++key_count_;
        return {};
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<LocalScuttlebuttDelta<typename C::state_type>, ScuttlebuttError>
    publish_local_change(LocalScuttlebuttKey key, C const& state) noexcept {
        auto key_idx = require_key_<C>(key.value());
        if (!key_idx) {
            return std::unexpected(key_idx.error());
        }
        std::uint64_t& version = versions_[local_index_][static_cast<std::size_t>(*key_idx)];
        if (version == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ScuttlebuttError::VersionOverflow);
        }
        ++version;
        ++publish_count_;
        return LocalScuttlebuttDelta<typename C::state_type>{ScuttlebuttDelta<typename C::state_type>{
            .origin = peers_[local_index_].id,
            .key = key.value(),
            .version = version,
            .state = state.state(),
        }};
    }

    [[nodiscard]] digest_type digest() const noexcept {
        digest_type out{};
        for (std::uint16_t p = 0; p < peer_count_; ++p) {
            for (std::uint16_t k = 0; k < key_count_; ++k) {
                const std::uint64_t version = versions_[static_cast<std::size_t>(p)][static_cast<std::size_t>(k)];
                if (version == 0) {
                    continue;
                }
                (void)out.push(ScuttlebuttVersionEntry{
                    .origin = peers_[static_cast<std::size_t>(p)].id,
                    .key = keys_[static_cast<std::size_t>(k)].key,
                    .version = version,
                });
            }
        }
        return out;
    }

    [[nodiscard]] std::expected<diff_type, ScuttlebuttError>
    compare_digest(gossiped_digest_type remote) const noexcept {
        digest_type const& incoming = remote.value();
        if (!incoming.well_formed()) {
            return std::unexpected(ScuttlebuttError::MalformedDigest);
        }

        diff_type out{};
        for (std::uint16_t i = 0; i < incoming.count; ++i) {
            auto const& entry = incoming.entries[static_cast<std::size_t>(i)];
            auto peer_idx = find_peer_(entry.origin);
            if (!peer_idx) {
                return std::unexpected(ScuttlebuttError::UnknownPeer);
            }
            auto key_idx = find_key_(entry.key);
            if (!key_idx) {
                return std::unexpected(ScuttlebuttError::UnknownKey);
            }
            const std::uint64_t local_version =
                versions_[static_cast<std::size_t>(*peer_idx)][static_cast<std::size_t>(*key_idx)];
            if (entry.version > local_version && !out.requests.push(entry)) {
                return std::unexpected(ScuttlebuttError::CapacityExceeded);
            }
        }

        for (std::uint16_t p = 0; p < peer_count_; ++p) {
            for (std::uint16_t k = 0; k < key_count_; ++k) {
                const std::uint64_t local_version = versions_[static_cast<std::size_t>(p)][static_cast<std::size_t>(k)];
                if (local_version == 0) {
                    continue;
                }
                ScuttlebuttVersionEntry entry{
                    .origin = peers_[static_cast<std::size_t>(p)].id,
                    .key = keys_[static_cast<std::size_t>(k)].key,
                    .version = local_version,
                };
                if (version_in_digest_(incoming, entry.origin, entry.key) < local_version) {
                    if (!out.offers.push(entry)) {
                        return std::unexpected(ScuttlebuttError::CapacityExceeded);
                    }
                }
            }
        }
        return out;
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<LocalScuttlebuttDelta<typename C::state_type>, ScuttlebuttError>
    delta_for_request(ScuttlebuttVersionEntry request, C const& state) const noexcept {
        auto peer_idx = find_peer_(request.origin);
        if (!peer_idx) {
            return std::unexpected(ScuttlebuttError::UnknownPeer);
        }
        auto key_idx = require_key_<C>(request.key);
        if (!key_idx) {
            return std::unexpected(key_idx.error());
        }
        const std::uint64_t local_version =
            versions_[static_cast<std::size_t>(*peer_idx)][static_cast<std::size_t>(*key_idx)];
        if (local_version < request.version || local_version == 0) {
            return std::unexpected(ScuttlebuttError::NotAvailable);
        }
        return LocalScuttlebuttDelta<typename C::state_type>{ScuttlebuttDelta<typename C::state_type>{
            .origin = request.origin,
            .key = request.key,
            .version = local_version,
            .state = state.state(),
        }};
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<bool, ScuttlebuttError>
    apply_delta(GossipedScuttlebuttDelta<typename C::state_type> delta, C& state) noexcept {
        auto const& incoming = delta.value();
        if (incoming.origin.is_zero() || incoming.key.hash == 0 || incoming.key.length == 0 || incoming.version == 0) {
            return std::unexpected(ScuttlebuttError::MalformedDelta);
        }
        auto peer_idx = find_peer_(incoming.origin);
        if (!peer_idx) {
            return std::unexpected(ScuttlebuttError::UnknownPeer);
        }
        auto key_idx = require_key_<C>(incoming.key);
        if (!key_idx) {
            return std::unexpected(key_idx.error());
        }

        std::uint64_t& local_version =
            versions_[static_cast<std::size_t>(*peer_idx)][static_cast<std::size_t>(*key_idx)];
        if (incoming.version <= local_version) {
            return false;
        }
        if (!state.merge(GossipedState<typename C::state_type>{incoming.state})) {
            return std::unexpected(ScuttlebuttError::MergeRejected);
        }
        local_version = incoming.version;
        ++merge_count_;
        return true;
    }

    [[nodiscard]] std::expected<std::uint16_t, ScuttlebuttError>
    compact_peer_versions(peer_type peer, std::uint64_t version_floor) noexcept {
        auto peer_idx = find_peer_(peer.value().uuid);
        if (!peer_idx) {
            return std::unexpected(ScuttlebuttError::UnknownPeer);
        }
        std::uint16_t dropped = 0;
        for (std::uint16_t k = 0; k < key_count_; ++k) {
            std::uint64_t& version = versions_[static_cast<std::size_t>(*peer_idx)][static_cast<std::size_t>(k)];
            if (version != 0 && version < version_floor) {
                version = 0;
                ++dropped;
            }
        }
        return dropped;
    }

    [[nodiscard]] ScuttlebuttConfig config() const noexcept { return config_; }

    [[nodiscard]] std::uint16_t peer_count() const noexcept { return peer_count_; }

    [[nodiscard]] std::uint16_t key_count() const noexcept { return key_count_; }

    [[nodiscard]] std::uint64_t publish_count() const noexcept { return publish_count_; }

    [[nodiscard]] std::uint64_t merge_count() const noexcept { return merge_count_; }

private:
    struct PeerSlot {
        bool occupied = false;
        cog::Uuid id{};
    };

    struct KeySlot {
        bool occupied = false;
        ScuttlebuttKey key{};
        void const* type_cookie = nullptr;
    };

    [[nodiscard]] std::optional<std::uint16_t> find_peer_(cog::Uuid peer) const noexcept {
        for (std::uint16_t i = 0; i < peer_count_; ++i) {
            auto const& slot = peers_[static_cast<std::size_t>(i)];
            if (slot.occupied && slot.id == peer) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint16_t> find_key_(ScuttlebuttKey key) const noexcept {
        for (std::uint16_t i = 0; i < key_count_; ++i) {
            auto const& slot = keys_[static_cast<std::size_t>(i)];
            if (slot.occupied && slot.key == key) {
                return i;
            }
        }
        return std::nullopt;
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<std::uint16_t, ScuttlebuttError> require_key_(ScuttlebuttKey key) const noexcept {
        auto idx = find_key_(key);
        if (!idx) {
            return std::unexpected(ScuttlebuttError::UnknownKey);
        }
        if (keys_[static_cast<std::size_t>(*idx)].type_cookie != detail::crdt_type_cookie<C>()) {
            return std::unexpected(ScuttlebuttError::TypeMismatch);
        }
        return *idx;
    }

    [[nodiscard]] static std::uint64_t version_in_digest_(digest_type const& digest, cog::Uuid origin,
                                                          ScuttlebuttKey key) noexcept {
        for (std::uint16_t i = 0; i < digest.count; ++i) {
            auto const& entry = digest.entries[static_cast<std::size_t>(i)];
            if (entry.origin == origin && entry.key == key) {
                return entry.version;
            }
        }
        return 0;
    }

    ScuttlebuttConfig config_{};
    safety::FixedArray<PeerSlot, MaxPeers> peers_{};
    safety::FixedArray<KeySlot, MaxKeys> keys_{};
    safety::FixedArray<safety::FixedArray<std::uint64_t, MaxKeys>, MaxPeers> versions_{};
    std::uint16_t peer_count_ = 0;
    std::uint16_t key_count_ = 0;
    std::uint16_t local_index_ = 0;
    std::uint64_t publish_count_ = 0;
    std::uint64_t merge_count_ = 0;
};

static_assert(!std::is_copy_constructible_v<ScuttlebuttSync<4, 4>>);
static_assert(!std::is_move_constructible_v<ScuttlebuttSync<4, 4>>);

template <std::size_t MaxPeers = 128, std::size_t MaxKeys = 128>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
[[nodiscard]] ScuttlebuttSync<MaxPeers, MaxKeys> mint_scuttlebutt(effects::Init, SwimPeer local_peer,
                                                                  std::span<const SwimPeer> initial_peers = {},
                                                                  ScuttlebuttConfig config = {}) noexcept {
    return ScuttlebuttSync<MaxPeers, MaxKeys>{local_peer, initial_peers, config};
}

}  // namespace crucible::canopy
