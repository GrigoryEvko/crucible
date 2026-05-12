#pragma once

// Runtime ownership for GAPS-136 connection-pool state.
//
// The pool is fixed-size, array-backed, and protected by a short spin gate.
// That keeps the substrate deterministic and avoids mutex/futex paths while
// still allowing a LeaseGuard destructor to return a connection safely.

#include <crucible/Platform.h>
#include <crucible/cntp/ConnectionPool.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <atomic>
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

    class SpinGuard {
        std::atomic_flag& flag_;

    public:
        explicit SpinGuard(std::atomic_flag& flag) noexcept : flag_{flag} {
            while (flag_.test_and_set(std::memory_order_acquire)) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }

        SpinGuard(SpinGuard const&) = delete;
        SpinGuard& operator=(SpinGuard const&) = delete;

        ~SpinGuard() noexcept {
            flag_.clear(std::memory_order_release);
        }
    };

    std::array<Slot, MaxRemotes * MaxPerRemote> slots_{};
    std::array<cntp::PoolEvent, MaxEvents> events_{};
    mutable std::atomic_flag gate_ = ATOMIC_FLAG_INIT;
    std::size_t next_event_ = 0;
    std::size_t event_count_ = 0;
    std::uint64_t sequence_ = 0;
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

    [[nodiscard]] std::uint16_t distinct_remote_count() const noexcept {
        std::uint16_t count = 0;
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].occupied) {
                continue;
            }
            bool seen_before = false;
            for (std::size_t j = 0; j < i; ++j) {
                if (slots_[j].occupied &&
                    same_uuid(slots_[j].connection.remote_uuid,
                              slots_[i].connection.remote_uuid)) {
                    seen_before = true;
                    break;
                }
            }
            if (!seen_before) {
                ++count;
            }
        }
        return count;
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
        SpinGuard guard{gate_};
        Slot* slot = slot_at(index);
        if (slot == nullptr || !slot->occupied || !slot->leased) {
            return;
        }
        slot->leased = false;
        if (slot->quarantined || !slot->connection.healthy) {
            append_event(slot->quarantined
                             ? cntp::PoolEventKind::DrainedQuarantined
                             : cntp::PoolEventKind::EvictedUnhealthy,
                         slot->connection);
            *slot = Slot{};
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

        SpinGuard guard{gate_};
        if (!has_remote(raw.remote_uuid) &&
            distinct_remote_count() >= static_cast<std::uint16_t>(MaxRemotes)) {
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

        SpinGuard guard{gate_};
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
        SpinGuard guard{gate_};
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
        SpinGuard guard{gate_};
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
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void mark_unhealthy(Ctx const&,
                        cog::CogIdentity const& remote,
                        cntp::SocketFd socket) noexcept {
        if (!cntp::valid_remote(remote)) {
            return;
        }
        SpinGuard guard{gate_};
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
        SpinGuard guard{gate_};
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
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void drain_quarantined(Ctx const&, cog::CogIdentity const& remote) noexcept {
        if (!cntp::valid_remote(remote)) {
            return;
        }
        SpinGuard guard{gate_};
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
    }

    [[nodiscard]] std::size_t event_count() const noexcept {
        SpinGuard guard{gate_};
        return event_count_;
    }

    [[nodiscard]] std::expected<cntp::DeclaredPoolEvent, cntp::PoolError>
    event_at(std::size_t index) const noexcept {
        SpinGuard guard{gate_};
        if (index >= event_count_) {
            return std::unexpected(cntp::PoolError::InvalidConnectionId);
        }
        return cntp::mint_pool_event(events_[index]);
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
