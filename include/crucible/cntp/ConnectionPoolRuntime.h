#pragma once

#include <crucible/cntp/ConnectionPool.h>
#include <fixy/Ctx.h>
#include <fixy/os/SpinLock.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <type_traits>

namespace crucible::cntp {

// A pool is built at startup.
template <class Ctx>
concept CtxFitsConnectionPoolMint = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// Every other operation takes the blocking gate of the pool, and a wait on
// that gate is a block.  So the operation is background or test work in a
// context that owns Block.
template <class Ctx>
concept CtxFitsConnectionPoolRuntime =
    ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Test>
    && ::fixy::spin::CtxMayBlock<Ctx>;

template <TransportClass T, std::size_t MaxRemotes, std::size_t MaxPerRemote,
          std::size_t MaxEvents = MaxRemotes * MaxPerRemote * 2u>
    requires PoolTransportClass<T>
class ConnectionPool;

template <TransportClass T, std::size_t MaxRemotes, std::size_t MaxPerRemote, class Ctx>
    requires PoolTransportClass<T> && CtxFitsConnectionPoolMint<Ctx>
[[nodiscard]] constexpr ConnectionPool<T, MaxRemotes, MaxPerRemote>
mint_connection_pool(Ctx const& ctx, PoolConfig config = {}) noexcept;

template <TransportClass T, std::size_t MaxRemotes, std::size_t MaxPerRemote, std::size_t MaxEvents>
    requires PoolTransportClass<T>
class ConnectionPool : public ::foundation::Pinned<ConnectionPool<T, MaxRemotes, MaxPerRemote, MaxEvents>> {
    static_assert(MaxRemotes > 0, "ConnectionPool requires remote slots");
    static_assert(MaxPerRemote > 0, "ConnectionPool requires per-remote slots");
    static_assert(MaxEvents > 0, "ConnectionPool requires event slots");
    static_assert(MaxPerRemote <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()),
                  "ConnectionPool per-remote count is uint16-backed");

    // A slot is occupied exactly when it holds a connection.
    struct Slot {
        std::optional<Connection<T>> connection{};
        bool leased = false;
        bool quarantined = false;
        bool healthy = true;
        std::uint64_t last_used_ns = 0;
    };

    // Each acquisition mints a token of this tag.  The tag is private and
    // nested in the class template, so only this instantiation can mint one:
    // the token witnesses that the acquisition comes from inside the pool.
    struct GateTag {
        using permission_row = ::foundation::effects::Row<>;
    };

    std::array<Slot, MaxRemotes * MaxPerRemote> slots_{};
    std::array<PoolEvent, MaxEvents> events_{};
    // The const readers take the gate too, to serialize against writers.
    mutable ::fixy::spin::BlockingLock<GateTag> gate_{};
    // A fresh cache line, so a counter store does not invalidate the line
    // that the gate waiters poll.
    alignas(64) std::size_t next_event_ = 0;
    std::size_t event_count_ = 0;
    std::uint64_t sequence_ = 0;
    // Cached rather than recomputed.  add_connection increments it on the
    // first slot for a remote and every drain path decrements it on the last.
    std::uint16_t distinct_remotes_ = 0;
    PoolConfig config_{};

    // The mint is the only door, so a pool exists only where a context that
    // owns Init built it.
    constexpr explicit ConnectionPool(PoolConfig config) noexcept : config_{config} {}

    template <TransportClass U, std::size_t R, std::size_t P, class Ctx>
        requires PoolTransportClass<U> && CtxFitsConnectionPoolMint<Ctx>
    friend constexpr ConnectionPool<U, R, P> mint_connection_pool(Ctx const& ctx, PoolConfig config) noexcept;

    [[nodiscard]] static constexpr bool same_uuid(cog::Uuid lhs, cog::Uuid rhs) noexcept {
        return lhs.hi == rhs.hi && lhs.lo == rhs.lo;
    }

    [[nodiscard]] static constexpr bool same_socket(SocketFd lhs, SocketFd rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    [[nodiscard]] static constexpr bool holds_remote(Slot const& slot, cog::Uuid remote) noexcept {
        return slot.connection.has_value() && same_uuid(slot.connection->remote_uuid(), remote);
    }

    [[nodiscard]] static constexpr std::uint16_t max_per_remote() noexcept {
        return static_cast<std::uint16_t>(MaxPerRemote);
    }

    // The two scans below are O(MaxRemotes * MaxPerRemote).  Each caller
    // holds the gate.
    [[nodiscard]] std::uint16_t remote_occupied_count(cog::Uuid remote) const noexcept {
        std::uint16_t count = 0;
        for (auto const& slot : slots_) {
            if (holds_remote(slot, remote)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool has_remote(cog::Uuid remote) const noexcept {
        for (auto const& slot : slots_) {
            if (holds_remote(slot, remote)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::uint16_t per_remote_limit() const noexcept {
        return config_.max_per_remote.value() < max_per_remote() ? config_.max_per_remote.value() : max_per_remote();
    }

    void append_event(PoolEventKind kind, Connection<T> const& connection) noexcept {
        events_[next_event_] = PoolEvent{
            .kind = kind,
            .transport = T,
            .remote_uuid = connection.remote_uuid(),
            .socket = connection.socket(),
            .connection_id = connection.connection_id().value(),
            .sequence = ++sequence_,
        };
        next_event_ = (next_event_ + 1u) % events_.size();
        if (event_count_ < events_.size()) {
            ++event_count_;
        }
    }

    // Records the event and empties the slot.  The caller holds the gate.
    void drain_slot(Slot& slot, PoolEventKind kind) noexcept {
        append_event(kind, *slot.connection);
        slot = Slot{};
    }

    template <class Ctx>
    void return_index(Ctx const& ctx, std::size_t index) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        if (index >= slots_.size()) {
            return;
        }
        Slot& slot = slots_[index];
        if (!slot.connection.has_value() || !slot.leased) {
            return;
        }
        slot.leased = false;
        if (slot.quarantined || !slot.healthy) {
            const cog::Uuid drained_uuid = slot.connection->remote_uuid();
            drain_slot(slot, slot.quarantined ? PoolEventKind::DrainedQuarantined : PoolEventKind::EvictedUnhealthy);
            if (!has_remote(drained_uuid)) {
                --distinct_remotes_;
            }
            return;
        }
        append_event(PoolEventKind::Returned, *slot.connection);
    }

public:
    // A lease holds one slot until reset() or the destructor gives the slot
    // back.  It carries the context of the lease call, because the return
    // takes the gate again and a destructor has no context of its own.  The
    // access is read-only: the identity of a leased connection is pool
    // bookkeeping, and a holder that could rewrite it would break the remote
    // count.
    template <class Ctx>
    class [[nodiscard]] LeaseGuard {
        ConnectionPool* pool_ = nullptr;
        std::size_t slot_index_ = 0;
        [[no_unique_address]] Ctx ctx_;

        friend class ConnectionPool;

        constexpr LeaseGuard(ConnectionPool& pool, std::size_t slot_index, Ctx const& ctx) noexcept
            : pool_{&pool}, slot_index_{slot_index}, ctx_{ctx} {}

    public:
        LeaseGuard(LeaseGuard const&) = delete("a lease names one holder of a slot; a copy would return it twice");
        LeaseGuard&
        operator=(LeaseGuard const&) = delete("a lease names one holder of a slot; a copy would return it twice");

        constexpr LeaseGuard(LeaseGuard&& other) noexcept
            : pool_{other.pool_}, slot_index_{other.slot_index_}, ctx_{other.ctx_} {
            other.pool_ = nullptr;
        }

        LeaseGuard& operator=(LeaseGuard&& other) noexcept {
            if (this != &other) {
                reset();
                pool_ = other.pool_;
                slot_index_ = other.slot_index_;
                ctx_ = other.ctx_;
                other.pool_ = nullptr;
            }
            return *this;
        }

        ~LeaseGuard() noexcept { reset(); }

        void reset() noexcept {
            if (pool_ != nullptr) {
                pool_->return_index(ctx_, slot_index_);
                pool_ = nullptr;
            }
        }

        [[nodiscard]] Connection<T> const& operator*() const noexcept { return *pool_->slots_[slot_index_].connection; }

        [[nodiscard]] Connection<T> const* operator->() const noexcept {
            return &*pool_->slots_[slot_index_].connection;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept { return pool_ != nullptr; }
    };

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] std::expected<void, PoolError> add_connection(Ctx const& ctx, LinearConnection<T>&& connection,
                                                                std::uint64_t now_ns = 0) noexcept {
        Connection<T> raw = std::move(connection).consume();

        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        const bool is_new_remote = !has_remote(raw.remote_uuid());
        if (is_new_remote && distinct_remotes_ >= static_cast<std::uint16_t>(MaxRemotes)) {
            return std::unexpected(PoolError::PoolFull);
        }
        if (remote_occupied_count(raw.remote_uuid()) >= per_remote_limit()) {
            return std::unexpected(PoolError::PoolFull);
        }
        for (auto& slot : slots_) {
            if (!slot.connection.has_value()) {
                slot.connection = raw;
                slot.leased = false;
                slot.quarantined = false;
                slot.healthy = true;
                slot.last_used_ns = now_ns;
                if (is_new_remote) {
                    ++distinct_remotes_;
                }
                append_event(PoolEventKind::Added, *slot.connection);
                return {};
            }
        }
        return std::unexpected(PoolError::PoolFull);
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] CRUCIBLE_HOT std::expected<LeaseGuard<Ctx>, PoolError>
    lease(Ctx const& ctx, cog::CogIdentity const& remote, std::uint64_t now_ns = 0) noexcept {
        if (!is_valid_remote(remote)) {
            return std::unexpected(PoolError::InvalidRemoteCog);
        }

        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            auto& slot = slots_[i];
            if (!holds_remote(slot, remote.uuid)) {
                continue;
            }
            if (slot.quarantined) {
                return std::unexpected(PoolError::RemoteQuarantined);
            }
            if (!slot.healthy) {
                continue;
            }
            if (!slot.leased) {
                slot.leased = true;
                slot.last_used_ns = now_ns;
                append_event(PoolEventKind::Leased, *slot.connection);
                return LeaseGuard<Ctx>{*this, i, ctx};
            }
        }
        return std::unexpected(PoolError::PoolEmpty);
    }

    template <class Ctx, class LeaseCtx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    CRUCIBLE_HOT void return_lease(Ctx const&, LeaseGuard<LeaseCtx>&& lease) noexcept {
        lease.reset();
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] std::uint16_t available_count(Ctx const& ctx, cog::CogIdentity const& remote) const noexcept {
        if (!is_valid_remote(remote)) {
            return 0;
        }
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        std::uint16_t count = 0;
        for (auto const& slot : slots_) {
            if (holds_remote(slot, remote.uuid) && !slot.leased && !slot.quarantined && slot.healthy) {
                ++count;
            }
        }
        return count;
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void evict_unhealthy(Ctx const& ctx, cog::CogIdentity const& remote) noexcept {
        if (!is_valid_remote(remote)) {
            return;
        }
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        const bool was_present = has_remote(remote.uuid);
        for (auto& slot : slots_) {
            if (holds_remote(slot, remote.uuid) && !slot.leased && !slot.healthy) {
                drain_slot(slot, PoolEventKind::EvictedUnhealthy);
            }
        }
        if (was_present && !has_remote(remote.uuid)) {
            --distinct_remotes_;
        }
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void mark_unhealthy(Ctx const& ctx, cog::CogIdentity const& remote, SocketFd socket) noexcept {
        if (!is_valid_remote(remote)) {
            return;
        }
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        for (auto& slot : slots_) {
            if (holds_remote(slot, remote.uuid) && same_socket(slot.connection->socket(), socket)) {
                slot.healthy = false;
            }
        }
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void evict_idle(Ctx const& ctx, cog::CogIdentity const& remote, std::uint64_t now_ns) noexcept {
        if (!is_valid_remote(remote)) {
            return;
        }
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        const bool was_present = has_remote(remote.uuid);
        for (auto& slot : slots_) {
            if (slot.leased || !holds_remote(slot, remote.uuid)) {
                continue;
            }
            if (now_ns >= slot.last_used_ns && now_ns - slot.last_used_ns >= config_.max_idle_ns.value()) {
                drain_slot(slot, PoolEventKind::EvictedIdle);
            }
        }
        if (was_present && !has_remote(remote.uuid)) {
            --distinct_remotes_;
        }
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void drain_quarantined(Ctx const& ctx, cog::CogIdentity const& remote) noexcept {
        if (!is_valid_remote(remote)) {
            return;
        }
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        // A quarantined slot that is still leased keeps its slot, so the
        // remote can survive this call and distinct_remotes_ must not drop.
        const bool was_present = has_remote(remote.uuid);
        for (auto& slot : slots_) {
            if (!holds_remote(slot, remote.uuid)) {
                continue;
            }
            slot.quarantined = true;
            if (!slot.leased) {
                drain_slot(slot, PoolEventKind::DrainedQuarantined);
            }
        }
        if (was_present && !has_remote(remote.uuid)) {
            --distinct_remotes_;
        }
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] std::size_t event_count(Ctx const& ctx) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return event_count_;
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] std::uint16_t distinct_remote_count(Ctx const& ctx) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        return distinct_remotes_;
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] std::expected<DeclaredPoolEvent, PoolError> event_at(Ctx const& ctx,
                                                                       std::size_t index) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        if (index >= event_count_) {
            return std::unexpected(PoolError::InvalidConnectionId);
        }
        // index is chronological, events_ is physical.  After a wrap the
        // oldest event lives at events_[next_event_], the slot about to be
        // overwritten, and physical slot 0 holds a much newer one.  Before a
        // wrap next_event_ equals event_count_, so the subtraction is zero
        // modulo size and base lands on slot 0.  One formula covers both.
        const std::size_t size = events_.size();
        const std::size_t base = (next_event_ + size - event_count_) % size;
        const std::size_t physical = (base + index) % size;
        return ::fixy::mint_tagged<::fixy::tags::source::ConnectionPool>(events_[physical]);
    }
};

template <TransportClass T, std::size_t MaxRemotes, std::size_t MaxPerRemote, class Ctx>
    requires PoolTransportClass<T> && CtxFitsConnectionPoolMint<Ctx>
[[nodiscard]] constexpr ConnectionPool<T, MaxRemotes, MaxPerRemote> mint_connection_pool(Ctx const&,
                                                                                         PoolConfig config) noexcept {
    return ConnectionPool<T, MaxRemotes, MaxPerRemote>{config};
}

}  // namespace crucible::cntp
