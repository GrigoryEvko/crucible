#pragma once

// Runtime ownership for GAPS-136 connection-pool state.
//
// The pool is fixed-size, array-backed, and protected by a short spin gate.
// That keeps the substrate deterministic and avoids mutex/futex paths while
// still allowing a LeaseGuard destructor to return a connection safely.

#include <crucible/Platform.h>
#include <crucible/cntp/ConnectionPool.h>
#include <crucible/concurrent/SpinLock.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace crucible::cntp {

template <class Ctx>
concept CtxFitsConnectionPoolMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

template <class Ctx>
concept CtxFitsConnectionPoolRuntime =
       effects::IsExecCtx<Ctx>
    && (effects::row_contains_v<effects::row_type_of_t<Ctx>,
                                effects::Effect::Bg>
        || effects::row_contains_v<effects::row_type_of_t<Ctx>,
                                   effects::Effect::Test>);

template <cntp::TransportClass T,
          std::size_t MaxRemotes,
          std::size_t MaxPerRemote,
          std::size_t MaxEvents = MaxRemotes * MaxPerRemote * 2u>
    requires cntp::PoolTransportClass<T>
class ConnectionPool
    : public safety::Pinned<ConnectionPool<T, MaxRemotes, MaxPerRemote, MaxEvents>> {
    static_assert(MaxRemotes > 0, "ConnectionPool requires remote slots");
    static_assert(MaxPerRemote > 0, "ConnectionPool requires per-remote slots");
    static_assert(MaxEvents > 0, "ConnectionPool requires event slots");
    static_assert(MaxPerRemote <=
                      static_cast<std::size_t>(
                          std::numeric_limits<std::uint16_t>::max()),
                  "ConnectionPool per-remote count is uint16-backed");

    struct Slot {
        bool occupied = false;
        bool leased = false;
        bool quarantined = false;
        cntp::Connection<T> connection{};
        std::uint64_t last_used_ns = 0;
    };

    std::array<Slot, MaxRemotes * MaxPerRemote> slots_{};
    std::array<cntp::PoolEvent, MaxEvents> events_{};
    // fixy-A5-009 + FIXY-U-085: gate_ alone on its cache line so spinners on
    // test_and_set don't false-share with the counter writes below.  The
    // canonical concurrent::SpinLock is alignas(64) by construction (fixy-
    // A5-022), so cache-line isolation is inherited; no external alignas
    // needed at the field declaration.  Prior to the consolidation this file
    // shipped a private nested SpinGuard over a raw atomic_flag& — replaced
    // with the canonical primitive so every spin path (acquire/release order,
    // _mm_pause hint, lock-free assert) is uniform across substrate sites.
    mutable concurrent::SpinLock gate_{};
    // fixy-A5-009: counter group starts a fresh cache line, isolated
    // from gate_ so each producer store invalidates only one line.
    alignas(64) std::size_t next_event_ = 0;
    std::size_t event_count_ = 0;
    std::uint64_t sequence_ = 0;
    // fixy-A5-009: cached distinct-remote count collapses the prior
    // O(N²) loop (run under gate_ on every add_connection) to O(1).
    // Maintained by add_connection on first-slot-for-remote and by
    // each drain path on last-slot-for-remote.
    std::uint16_t distinct_remotes_ = 0;
    cntp::PoolConfig config_{};

    [[nodiscard]] static constexpr bool same_uuid(cog::Uuid lhs,
                                                  cog::Uuid rhs) noexcept {
        return lhs.hi == rhs.hi && lhs.lo == rhs.lo;
    }

    [[nodiscard]] static constexpr bool same_socket(cntp::SocketFd lhs,
                                                    cntp::SocketFd rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    [[nodiscard]] static constexpr std::uint16_t
    max_per_remote() noexcept {
        return static_cast<std::uint16_t>(MaxPerRemote);
    }

    [[nodiscard]] std::uint16_t remote_occupied_count(cog::Uuid remote) const noexcept {
        std::uint16_t count = 0;
        for (auto const& slot : slots_) {
            if (slot.occupied && same_uuid(slot.connection.remote_uuid, remote)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool has_remote(cog::Uuid remote) const noexcept {
        for (auto const& slot : slots_) {
            if (slot.occupied && same_uuid(slot.connection.remote_uuid, remote)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::uint16_t per_remote_limit() const noexcept {
        return config_.max_per_remote.value() < max_per_remote()
            ? config_.max_per_remote.value()
            : max_per_remote();
    }

    void append_event(cntp::PoolEventKind kind,
                      cntp::Connection<T> const& connection) noexcept {
        events_[next_event_] = cntp::PoolEvent{
            .kind = kind,
            .transport = T,
            .remote_uuid = connection.remote_uuid,
            .socket = connection.socket,
            .connection_id = connection.connection_id.value(),
            .sequence = ++sequence_,
        };
        next_event_ = (next_event_ + 1u) % events_.size();
        if (event_count_ < events_.size()) {
            ++event_count_;
        }
    }

    [[nodiscard]] Slot* slot_at(std::size_t index) noexcept {
        return index < slots_.size() ? &slots_[index] : nullptr;
    }

    [[nodiscard]] Slot const* slot_at(std::size_t index) const noexcept {
        return index < slots_.size() ? &slots_[index] : nullptr;
    }

    void return_index(std::size_t index) noexcept {
        concurrent::SpinGuard guard{gate_};
        Slot* slot = slot_at(index);
        if (slot == nullptr || !slot->occupied || !slot->leased) {
            return;
        }
        slot->leased = false;
        if (slot->quarantined || !slot->connection.healthy) {
            const cog::Uuid drained_uuid = slot->connection.remote_uuid;
            append_event(slot->quarantined
                             ? cntp::PoolEventKind::DrainedQuarantined
                             : cntp::PoolEventKind::EvictedUnhealthy,
                         slot->connection);
            *slot = Slot{};
            // fixy-A5-009: if this was the last slot for the remote,
            // distinct_remotes_ shrinks by one.  has_remote() is O(N)
            // per drain; acceptable because return_index drains at
            // most one slot per call.
            if (!has_remote(drained_uuid)) {
                --distinct_remotes_;
            }
            return;
        }
        append_event(cntp::PoolEventKind::Returned, slot->connection);
    }

public:
    class [[nodiscard]] LeaseGuard {
        ConnectionPool* pool_ = nullptr;
        std::size_t slot_index_ = 0;

        friend class ConnectionPool;

        constexpr LeaseGuard(ConnectionPool& pool, std::size_t slot_index) noexcept
            : pool_{&pool}, slot_index_{slot_index} {}

    public:
        constexpr LeaseGuard() noexcept = default;
        LeaseGuard(LeaseGuard const&) = delete;
        LeaseGuard& operator=(LeaseGuard const&) = delete;

        constexpr LeaseGuard(LeaseGuard&& other) noexcept
            : pool_{other.pool_}, slot_index_{other.slot_index_} {
            other.pool_ = nullptr;
        }

        LeaseGuard& operator=(LeaseGuard&& other) noexcept {
            if (this != &other) {
                reset();
                pool_ = other.pool_;
                slot_index_ = other.slot_index_;
                other.pool_ = nullptr;
            }
            return *this;
        }

        ~LeaseGuard() noexcept { reset(); }

        void reset() noexcept {
            if (pool_ != nullptr) {
                pool_->return_index(slot_index_);
                pool_ = nullptr;
            }
        }

        [[nodiscard]] cntp::Connection<T>& operator*() noexcept {
            return pool_->slots_[slot_index_].connection;
        }

        [[nodiscard]] cntp::Connection<T> const& operator*() const noexcept {
            return pool_->slots_[slot_index_].connection;
        }

        [[nodiscard]] cntp::Connection<T>* operator->() noexcept {
            return &pool_->slots_[slot_index_].connection;
        }

        [[nodiscard]] cntp::Connection<T> const* operator->() const noexcept {
            return &pool_->slots_[slot_index_].connection;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return pool_ != nullptr;
        }
    };

    constexpr explicit ConnectionPool(cntp::PoolConfig config = {}) noexcept
        : config_{config} {}

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] std::expected<void, cntp::PoolError>
    add_connection(Ctx const&,
                   cntp::LinearConnection<T>&& connection,
                   std::uint64_t now_ns = 0) noexcept {
        cntp::Connection<T> raw = std::move(connection).consume();
        if (raw.remote_uuid.is_zero()) {
            return std::unexpected(cntp::PoolError::InvalidRemoteCog);
        }

        concurrent::SpinGuard guard{gate_};
        // fixy-A5-009: capture has_remote() once (O(N)); the value is
        // load-bearing for BOTH the MaxRemotes gate AND the post-add
        // increment.  Pre-fix this section ran has_remote() then a
        // separate O(N²) distinct_remote_count(), both under gate_.
        const bool is_new_remote = !has_remote(raw.remote_uuid);
        if (is_new_remote &&
            distinct_remotes_ >= static_cast<std::uint16_t>(MaxRemotes)) {
            return std::unexpected(cntp::PoolError::PoolFull);
        }
        if (remote_occupied_count(raw.remote_uuid) >= per_remote_limit()) {
            return std::unexpected(cntp::PoolError::PoolFull);
        }
        for (auto& slot : slots_) {
            if (!slot.occupied) {
                slot.occupied = true;
                slot.leased = false;
                slot.quarantined = false;
                slot.connection = raw;
                slot.last_used_ns = now_ns;
                if (is_new_remote) {
                    ++distinct_remotes_;
                }
                append_event(cntp::PoolEventKind::Added, slot.connection);
                return {};
            }
        }
        return std::unexpected(cntp::PoolError::PoolFull);
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    CRUCIBLE_HOT std::expected<LeaseGuard, cntp::PoolError>
    lease(Ctx const&,
          cog::CogIdentity const& remote,
          std::uint64_t now_ns = 0) noexcept {
        if (!cntp::valid_remote(remote)) {
            return std::unexpected(cntp::PoolError::InvalidRemoteCog);
        }

        concurrent::SpinGuard guard{gate_};
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            auto& slot = slots_[i];
            if (!slot.occupied || !same_uuid(slot.connection.remote_uuid,
                                             remote.uuid)) {
                continue;
            }
            if (slot.quarantined) {
                return std::unexpected(cntp::PoolError::RemoteQuarantined);
            }
            if (!slot.connection.healthy) {
                continue;
            }
            if (!slot.leased) {
                slot.leased = true;
                slot.last_used_ns = now_ns;
                append_event(cntp::PoolEventKind::Leased, slot.connection);
                return LeaseGuard{*this, i};
            }
        }
        return std::unexpected(cntp::PoolError::PoolEmpty);
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    CRUCIBLE_HOT void return_lease(Ctx const&, LeaseGuard&& lease) noexcept {
        lease.reset();
    }

    [[nodiscard]] std::uint16_t
    available_count(cog::CogIdentity const& remote) const noexcept {
        if (!cntp::valid_remote(remote)) {
            return 0;
        }
        concurrent::SpinGuard guard{gate_};
        std::uint16_t count = 0;
        for (auto const& slot : slots_) {
            if (slot.occupied &&
                same_uuid(slot.connection.remote_uuid, remote.uuid) &&
                !slot.leased &&
                !slot.quarantined &&
                slot.connection.healthy) {
                ++count;
            }
        }
        return count;
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void evict_unhealthy(Ctx const&, cog::CogIdentity const& remote) noexcept {
        if (!cntp::valid_remote(remote)) {
            return;
        }
        concurrent::SpinGuard guard{gate_};
        // fixy-A5-009: was_present before / has_remote after is O(N)
        // each, total O(N) per call — strictly better than recomputing
        // distinct_remote_count() (O(N²)) per-slot drain.
        const bool was_present = has_remote(remote.uuid);
        for (auto& slot : slots_) {
            if (slot.occupied &&
                same_uuid(slot.connection.remote_uuid, remote.uuid) &&
                !slot.leased &&
                !slot.connection.healthy) {
                append_event(cntp::PoolEventKind::EvictedUnhealthy,
                             slot.connection);
                slot = Slot{};
            }
        }
        if (was_present && !has_remote(remote.uuid)) {
            --distinct_remotes_;
        }
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void mark_unhealthy(Ctx const&,
                        cog::CogIdentity const& remote,
                        cntp::SocketFd socket) noexcept {
        if (!cntp::valid_remote(remote)) {
            return;
        }
        concurrent::SpinGuard guard{gate_};
        for (auto& slot : slots_) {
            if (slot.occupied &&
                same_uuid(slot.connection.remote_uuid, remote.uuid) &&
                same_socket(slot.connection.socket, socket)) {
                slot.connection.healthy = false;
            }
        }
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void evict_idle(Ctx const&,
                    cog::CogIdentity const& remote,
                    std::uint64_t now_ns) noexcept {
        if (!cntp::valid_remote(remote)) {
            return;
        }
        concurrent::SpinGuard guard{gate_};
        // fixy-A5-009: O(N) bracket around the loop maintains the
        // cached distinct_remotes_ counter at amortized O(1).
        const bool was_present = has_remote(remote.uuid);
        for (auto& slot : slots_) {
            if (!slot.occupied || slot.leased ||
                !same_uuid(slot.connection.remote_uuid, remote.uuid)) {
                continue;
            }
            if (now_ns >= slot.last_used_ns &&
                now_ns - slot.last_used_ns >= config_.max_idle_ns.value()) {
                append_event(cntp::PoolEventKind::EvictedIdle,
                             slot.connection);
                slot = Slot{};
            }
        }
        if (was_present && !has_remote(remote.uuid)) {
            --distinct_remotes_;
        }
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void drain_quarantined(Ctx const&, cog::CogIdentity const& remote) noexcept {
        if (!cntp::valid_remote(remote)) {
            return;
        }
        concurrent::SpinGuard guard{gate_};
        // fixy-A5-009: was_present check is loop-bounded; the leased
        // slot retains the remote, so distinct_remotes_ only drops
        // when every slot for the remote was already unleased.
        const bool was_present = has_remote(remote.uuid);
        for (auto& slot : slots_) {
            if (!slot.occupied ||
                !same_uuid(slot.connection.remote_uuid, remote.uuid)) {
                continue;
            }
            slot.quarantined = true;
            if (!slot.leased) {
                append_event(cntp::PoolEventKind::DrainedQuarantined,
                             slot.connection);
                slot = Slot{};
            }
        }
        if (was_present && !has_remote(remote.uuid)) {
            --distinct_remotes_;
        }
    }

    [[nodiscard]] std::size_t event_count() const noexcept {
        concurrent::SpinGuard guard{gate_};
        return event_count_;
    }

    // fixy-A5-009: O(1) cached read.  Pre-fix walked every slot pair
    // (O(N²)) while holding gate_ on every add_connection call.
    [[nodiscard]] std::uint16_t distinct_remote_count() const noexcept {
        concurrent::SpinGuard guard{gate_};
        return distinct_remotes_;
    }

    [[nodiscard]] std::expected<cntp::DeclaredPoolEvent, cntp::PoolError>
    event_at(std::size_t index) const noexcept {
        concurrent::SpinGuard guard{gate_};
        if (index >= event_count_) {
            return std::unexpected(cntp::PoolError::InvalidConnectionId);
        }
        // fixy-A5-010: events_[index] indexes the PHYSICAL slot, not the
        // chronological position.  After wrap, the oldest event lives at
        // events_[next_event_] (the slot about to be overwritten) and the
        // physical-zero slot holds a far-newer event.  Unified formula
        // collapses both regimes: pre-wrap next_event_ ≡ event_count_ so
        // the subtraction is zero modulo size, returning slot 0.
        const std::size_t size = events_.size();
        const std::size_t base = (next_event_ + size - event_count_) % size;
        const std::size_t physical = (base + index) % size;
        return cntp::mint_pool_event(events_[physical]);
    }
};

template <cntp::TransportClass T,
          std::size_t MaxRemotes,
          std::size_t MaxPerRemote,
          class Ctx>
    requires cntp::PoolTransportClass<T> && CtxFitsConnectionPoolMint<Ctx>
[[nodiscard]] constexpr ConnectionPool<T, MaxRemotes, MaxPerRemote>
mint_connection_pool(Ctx const&, cntp::PoolConfig config = {}) noexcept {
    return ConnectionPool<T, MaxRemotes, MaxPerRemote>{config};
}

static_assert(CtxFitsConnectionPoolMint<effects::ColdInitCtx>);
static_assert(!CtxFitsConnectionPoolMint<effects::BgDrainCtx>);
static_assert(CtxFitsConnectionPoolRuntime<effects::BgDrainCtx>);
static_assert(CtxFitsConnectionPoolRuntime<effects::TestRunnerCtx>);
static_assert(!CtxFitsConnectionPoolRuntime<effects::HotFgCtx>);

}  // namespace crucible::cntp
