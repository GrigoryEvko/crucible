#pragma once

#include <crucible/Platform.h>
#include <crucible/canopy/Crdt.h>
#include <crucible/canopy/SlotTable.h>
#include <crucible/canopy/Swim.h>
#include <fixy/FixedArray.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstddef>
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

using ScuttlebuttDurationNs = ::fixy::Refined<::fixy::positive, std::uint64_t>;
using ScuttlebuttPositiveCount = ::fixy::Refined<::fixy::positive, std::uint16_t>;

struct ScuttlebuttConfig {
    ScuttlebuttDurationNs period_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{5'000'000'000});
    ScuttlebuttPositiveCount max_stale_rounds = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{64});
};

struct ScuttlebuttKey {
    std::uint64_t hash = 0;
    std::uint16_t length = 0;

    // A key names something only when admit_scuttlebutt_key built it: the
    // hash of an admitted key is never zero and its length is at least one.
    [[nodiscard]] constexpr bool is_empty() const noexcept { return hash == 0 || length == 0; }

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
    return admit_local_write(ScuttlebuttKey{
        .hash = detail::fnv1a64(key),
        .length = static_cast<std::uint16_t>(key.size()),
    });
}

// A replicated value that the anti-entropy layer can carry: a state-based
// CRDT with a state that the layer can copy into a delta.
template <typename C>
concept ScuttlebuttCrdt = Crdt<C> && std::copyable<typename C::state_type>;

struct ScuttlebuttVersionEntry {
    cog::Uuid origin{};
    ScuttlebuttKey key{};
    std::uint64_t version = 0;

    // An entry that names no origin or no key.  A version of zero is a
    // separate matter: it means "never written", which a digest skips.
    [[nodiscard]] constexpr bool names_nothing() const noexcept { return origin.is_zero() || key.is_empty(); }
};

template <typename State>
struct ScuttlebuttDelta {
    cog::Uuid origin{};
    ScuttlebuttKey key{};
    std::uint64_t version = 0;
    State state{};
};

template <typename State>
using LocalScuttlebuttDelta = LocalWrite<ScuttlebuttDelta<State>>;

template <typename State>
using GossipedScuttlebuttDelta = GossipedState<ScuttlebuttDelta<State>>;

namespace detail {

// The digest and the request set share one table shape: a dense run of
// version entries, one for each (origin, key), with a slot count.  Only
// entries[0, count) are live.  The tail keeps the FixedArray default.
template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
struct ScuttlebuttEntryTable {
    static constexpr std::size_t capacity = scuttlebutt_entry_capacity<MaxPeers, MaxKeys>;

    ::fixy::FixedArray<ScuttlebuttVersionEntry, capacity> entries{};
    SlotCount<capacity> count{};

    [[nodiscard]] constexpr BoundedSlotCount<capacity> size() const noexcept { return count.bounded(); }

protected:
    // Raises the version of an existing (origin, key) entry, or appends a
    // new one.  Refuses only when the table is full.
    [[nodiscard]] constexpr bool upsert_(ScuttlebuttVersionEntry entry) noexcept {
        for (std::uint16_t i = 0; i < count; ++i) {
            auto& existing = entries[static_cast<std::size_t>(i)];
            if (existing.origin == entry.origin && existing.key == entry.key) {
                if (existing.version < entry.version) {
                    existing.version = entry.version;
                }
                return true;
            }
        }
        // The count enforces the bound and yields the slot, so the fullness
        // test and the subscript cannot disagree.
        const auto slot = count.reserve_next();
        if (!slot) {
            return false;
        }
        entries.at(*slot) = entry;
        return true;
    }
};

}  // namespace detail

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
struct ScuttlebuttDigest : detail::ScuttlebuttEntryTable<MaxPeers, MaxKeys> {
    // A version of zero is "never written" and is skipped, not refused.
    [[nodiscard]] constexpr bool push(ScuttlebuttVersionEntry entry) noexcept {
        if (entry.version == 0) {
            return true;
        }
        return !entry.names_nothing() && this->upsert_(entry);
    }

    // The count cannot be more than the capacity, so the loop stays in range
    // for every value that the type admits.  The content of an entry is
    // different, because `entries` is public.  A caller can write a live
    // slot after push() did a check of it.  This function does a check of
    // that content.
    [[nodiscard]] constexpr bool well_formed() const noexcept {
        for (std::uint16_t i = 0; i < this->count; ++i) {
            auto const& a = this->entries[static_cast<std::size_t>(i)];
            if (a.version == 0 || a.names_nothing()) {
                return false;
            }
            for (std::uint16_t j = static_cast<std::uint16_t>(i + std::uint16_t{1}); j < this->count; ++j) {
                auto const& b = this->entries[static_cast<std::size_t>(j)];
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
using GossipedScuttlebuttDigest = GossipedState<ScuttlebuttDigest<MaxPeers, MaxKeys>>;

// An output-only local aggregate.  compare_digest is its only producer and
// pushes entries drawn from an already-validated digest and from the local
// tables, and its only consumer, delta_for_request, resolves origin and key
// before it indexes anything.  A validator with no caller would be weight,
// so it carries none.
template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
struct ScuttlebuttRequestSet : detail::ScuttlebuttEntryTable<MaxPeers, MaxKeys> {
    [[nodiscard]] constexpr bool push(ScuttlebuttVersionEntry entry) noexcept {
        return entry.version != 0 && !entry.names_nothing() && this->upsert_(entry);
    }
};

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
struct ScuttlebuttDiff {
    ScuttlebuttRequestSet<MaxPeers, MaxKeys> requests{};
    ScuttlebuttRequestSet<MaxPeers, MaxKeys> offers{};
};

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
class ScuttlebuttSync;

// The one door: a sync holds the anti-entropy state of a process, so only a
// context that owns Init builds one.
template <std::size_t MaxPeers = 128, std::size_t MaxKeys = 128>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
[[nodiscard]] constexpr ScuttlebuttSync<MaxPeers, MaxKeys>
mint_scuttlebutt(::foundation::effects::Init, SwimPeer local_peer, std::span<const SwimPeer> initial_peers = {},
                 ScuttlebuttConfig config = {}) noexcept;

template <std::size_t MaxPeers = 128, std::size_t MaxKeys = 128>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
class alignas(64) ScuttlebuttSync : public ::foundation::Pinned<ScuttlebuttSync<MaxPeers, MaxKeys>> {
public:
    using peer_type = SwimPeer;
    using digest_type = ScuttlebuttDigest<MaxPeers, MaxKeys>;
    using gossiped_digest_type = GossipedScuttlebuttDigest<MaxPeers, MaxKeys>;
    using request_set_type = ScuttlebuttRequestSet<MaxPeers, MaxKeys>;
    using diff_type = ScuttlebuttDiff<MaxPeers, MaxKeys>;

    [[nodiscard]] constexpr std::expected<void, ScuttlebuttError> add_peer(peer_type peer) noexcept {
        cog::CogIdentity const& id = peer.value();
        if (id.uuid.is_zero()) {
            return std::unexpected(ScuttlebuttError::ZeroUuid);
        }
        if (find_peer_(id.uuid).has_value()) {
            return std::unexpected(ScuttlebuttError::DuplicatePeer);
        }
        const auto slot = peer_count_.reserve_next();
        if (!slot) {
            return std::unexpected(ScuttlebuttError::CapacityExceeded);
        }
        peers_.at(*slot) = id.uuid;
        return {};
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<void, ScuttlebuttError> register_state(LocalScuttlebuttKey key, C& state) noexcept {
        (void)state;
        ScuttlebuttKey const& raw_key = key.value();
        // admit_local_write is generic, so a local key can still name
        // nothing.  Such a key would be registered and never gossiped.
        if (raw_key.is_empty()) {
            return std::unexpected(ScuttlebuttError::EmptyKey);
        }
        void const* cookie = detail::crdt_type_cookie<C>();
        if (auto existing = find_key_(raw_key)) {
            if (keys_[*existing].type_cookie != cookie) {
                return std::unexpected(ScuttlebuttError::TypeMismatch);
            }
            return {};
        }
        const auto slot = key_count_.reserve_next();
        if (!slot) {
            return std::unexpected(ScuttlebuttError::CapacityExceeded);
        }
        keys_.at(*slot) = KeySlot{.key = raw_key, .type_cookie = cookie};
        return {};
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<LocalScuttlebuttDelta<typename C::state_type>, ScuttlebuttError>
    publish_local_change(LocalScuttlebuttKey key, C const& state) noexcept {
        auto key_idx = require_key_<C>(key.value());
        if (!key_idx) {
            return std::unexpected(key_idx.error());
        }
        std::uint64_t& version = versions_[kLocalIndex][*key_idx];
        if (version == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ScuttlebuttError::VersionOverflow);
        }
        ++version;
        ++publish_count_;
        return admit_local_write(ScuttlebuttDelta<typename C::state_type>{
            .origin = peers_[kLocalIndex],
            .key = key.value(),
            .version = version,
            .state = state.state(),
        });
    }

    [[nodiscard]] digest_type digest() const noexcept {
        digest_type out{};
        for (std::uint16_t p = 0; p < peer_count_; ++p) {
            for (std::uint16_t k = 0; k < key_count_; ++k) {
                (void)out.push(ScuttlebuttVersionEntry{
                    .origin = peers_[p],
                    .key = keys_[k].key,
                    .version = versions_[p][k],
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
            auto const& entry = incoming.entries[i];
            auto peer_idx = find_peer_(entry.origin);
            if (!peer_idx) {
                return std::unexpected(ScuttlebuttError::UnknownPeer);
            }
            auto key_idx = find_key_(entry.key);
            if (!key_idx) {
                return std::unexpected(ScuttlebuttError::UnknownKey);
            }
            if (entry.version > versions_[*peer_idx][*key_idx] && !out.requests.push(entry)) {
                return std::unexpected(ScuttlebuttError::CapacityExceeded);
            }
        }

        for (std::uint16_t p = 0; p < peer_count_; ++p) {
            for (std::uint16_t k = 0; k < key_count_; ++k) {
                const std::uint64_t local_version = versions_[p][k];
                if (local_version == 0) {
                    continue;
                }
                const ScuttlebuttVersionEntry entry{
                    .origin = peers_[p],
                    .key = keys_[k].key,
                    .version = local_version,
                };
                if (version_in_digest_(incoming, entry.origin, entry.key) < local_version
                    && !out.offers.push(entry)) {
                    return std::unexpected(ScuttlebuttError::CapacityExceeded);
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
        const std::uint64_t local_version = versions_[*peer_idx][*key_idx];
        if (local_version < request.version || local_version == 0) {
            return std::unexpected(ScuttlebuttError::NotAvailable);
        }
        return admit_local_write(ScuttlebuttDelta<typename C::state_type>{
            .origin = request.origin,
            .key = request.key,
            .version = local_version,
            .state = state.state(),
        });
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<bool, ScuttlebuttError>
    apply_delta(GossipedScuttlebuttDelta<typename C::state_type> delta, C& state) noexcept {
        auto const& incoming = delta.value();
        if (incoming.origin.is_zero() || incoming.key.is_empty() || incoming.version == 0) {
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

        std::uint64_t& local_version = versions_[*peer_idx][*key_idx];
        if (incoming.version <= local_version) {
            return false;
        }
        if (!state.merge(admit_gossiped(incoming.state))) {
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
            std::uint64_t& version = versions_[*peer_idx][k];
            if (version != 0 && version < version_floor) {
                version = 0;
                ++dropped;
            }
        }
        return dropped;
    }

    [[nodiscard]] constexpr ScuttlebuttConfig config() const noexcept { return config_; }

    [[nodiscard]] constexpr std::uint16_t peer_count() const noexcept { return peer_count_; }

    [[nodiscard]] constexpr std::uint16_t key_count() const noexcept { return key_count_; }

    [[nodiscard]] constexpr std::uint64_t publish_count() const noexcept { return publish_count_; }

    [[nodiscard]] constexpr std::uint64_t merge_count() const noexcept { return merge_count_; }

private:
    // The local peer is added first, so it always owns slot zero.
    static constexpr std::size_t kLocalIndex = 0;

    struct KeySlot {
        ScuttlebuttKey key{};
        void const* type_cookie = nullptr;
    };

    constexpr ScuttlebuttSync(peer_type local_peer, std::span<const peer_type> initial_peers,
                              ScuttlebuttConfig config) noexcept
        : config_{config} {
        CRUCIBLE_FATAL_INVARIANT(add_peer(local_peer).has_value());
        for (peer_type const& peer : initial_peers) {
            CRUCIBLE_FATAL_INVARIANT(add_peer(peer).has_value());
        }
    }

    template <std::size_t P, std::size_t K>
        requires ScuttlebuttShape<P, K>
    friend constexpr ScuttlebuttSync<P, K> mint_scuttlebutt(::foundation::effects::Init, SwimPeer,
                                                            std::span<const SwimPeer>, ScuttlebuttConfig) noexcept;

    // Slots are dense and never removed, so the live ones are [0, count).
    [[nodiscard]] constexpr std::optional<std::size_t> find_peer_(cog::Uuid peer) const noexcept {
        for (std::uint16_t i = 0; i < peer_count_; ++i) {
            if (peers_[i] == peer) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<std::size_t> find_key_(ScuttlebuttKey key) const noexcept {
        for (std::uint16_t i = 0; i < key_count_; ++i) {
            if (keys_[i].key == key) {
                return i;
            }
        }
        return std::nullopt;
    }

    template <ScuttlebuttCrdt C>
    [[nodiscard]] std::expected<std::size_t, ScuttlebuttError> require_key_(ScuttlebuttKey key) const noexcept {
        auto idx = find_key_(key);
        if (!idx) {
            return std::unexpected(ScuttlebuttError::UnknownKey);
        }
        if (keys_[*idx].type_cookie != detail::crdt_type_cookie<C>()) {
            return std::unexpected(ScuttlebuttError::TypeMismatch);
        }
        return *idx;
    }

    [[nodiscard]] static constexpr std::uint64_t version_in_digest_(digest_type const& digest, cog::Uuid origin,
                                                                    ScuttlebuttKey key) noexcept {
        for (std::uint16_t i = 0; i < digest.count; ++i) {
            auto const& entry = digest.entries[i];
            if (entry.origin == origin && entry.key == key) {
                return entry.version;
            }
        }
        return 0;
    }

    ScuttlebuttConfig config_{};
    ::fixy::FixedArray<cog::Uuid, MaxPeers> peers_{};
    ::fixy::FixedArray<KeySlot, MaxKeys> keys_{};
    ::fixy::FixedArray<::fixy::FixedArray<std::uint64_t, MaxKeys>, MaxPeers> versions_{};
    SlotCount<MaxPeers> peer_count_{};
    SlotCount<MaxKeys> key_count_{};
    std::uint64_t publish_count_ = 0;
    std::uint64_t merge_count_ = 0;
};

static_assert(!std::is_copy_constructible_v<ScuttlebuttSync<4, 4>>);
static_assert(!std::is_move_constructible_v<ScuttlebuttSync<4, 4>>);

template <std::size_t MaxPeers, std::size_t MaxKeys>
    requires ScuttlebuttShape<MaxPeers, MaxKeys>
[[nodiscard]] constexpr ScuttlebuttSync<MaxPeers, MaxKeys>
mint_scuttlebutt(::foundation::effects::Init, SwimPeer local_peer, std::span<const SwimPeer> initial_peers,
                 ScuttlebuttConfig config) noexcept {
    return ScuttlebuttSync<MaxPeers, MaxKeys>{local_peer, initial_peers, config};
}

}  // namespace crucible::canopy
