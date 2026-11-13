#pragma once

#include <crucible/cntp/ConnectionPool.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/SpinLock.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
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
    static_assert(MaxPerRemote <= std::size_t{::fixy::max_of<std::uint16_t>},
                  "ConnectionPool per-remote count is uint16-backed");

    static constexpr std::size_t kSlots = MaxRemotes * MaxPerRemote;

    using SlotIndex = ::fixy::Index<kSlots>;
    using EventIndex = ::fixy::Index<MaxEvents>;

    // A slot is occupied exactly when it holds a connection.
    struct Slot {
        ::fixy::Option<Connection<T>> connection{};
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

    ::foundation::core::FixedArray<Slot, kSlots> slots_{};
    ::foundation::core::FixedArray<PoolEvent, MaxEvents> events_{};
    // The const readers take the gate too, to serialize against writers.
    mutable ::fixy::spin::BlockingLock<GateTag> gate_{};
    // A fresh cache line, so a counter store does not invalidate the line
    // that the gate waiters poll.
    alignas(64) EventIndex next_event_{::fixy::index<0>()};
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
        for (Connection<T> const& connection : slot.connection) {
            return same_uuid(connection.remote_uuid(), remote);
        }
        return false;
    }

    // The connection of an occupied slot.  Each caller tested the slot first.
    [[nodiscard]] static constexpr Connection<T> const& connection_of(Slot const& slot) noexcept {
        for (Connection<T> const& connection : slot.connection) {
            return connection;
        }
        ::fixy::fatal("a connection pool read the connection of an empty slot");
    }

    [[nodiscard]] static constexpr std::uint16_t max_per_remote() noexcept {
        return static_cast<std::uint16_t>(MaxPerRemote);
    }

    // The two scans below are O(MaxRemotes * MaxPerRemote).  Each caller
    // holds the gate.
    [[nodiscard]] std::uint16_t remote_occupied_count(cog::Uuid remote) const noexcept {
        std::uint16_t count = 0;
        for (Slot const& slot : slots_) {
            if (holds_remote(slot, remote)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool has_remote(cog::Uuid remote) const noexcept {
        for (Slot const& slot : slots_) {
            if (holds_remote(slot, remote)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::uint16_t per_remote_limit() const noexcept {
        return ::fixy::min(config_.max_per_remote.value(), max_per_remote());
    }

    // The slot of the ring that follows index.
    [[nodiscard]] static EventIndex next_event_index(EventIndex index) noexcept {
        return EventIndex::check((index.raw() + 1u) % MaxEvents).expect("a position modulo the ring size is in it");
    }

    void append_event(PoolEventKind kind, Connection<T> const& connection) noexcept {
        events_.at(next_event_) = PoolEvent{
            .kind = kind,
            .transport = T,
            .remote_uuid = connection.remote_uuid(),
            .socket = connection.socket(),
            .connection_id = connection.connection_id().value(),
            .sequence = ++sequence_,
        };
        next_event_ = next_event_index(next_event_);
        if (event_count_ < MaxEvents) {
            ++event_count_;
        }
    }

    // Records the event and empties the slot.  The caller holds the gate.
    void drain_slot(Slot& slot, PoolEventKind kind) noexcept {
        append_event(kind, connection_of(slot));
        slot = Slot{};
    }

    template <class Ctx>
    void return_index(Ctx const& ctx, SlotIndex index) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        Slot& slot = slots_.at(index);
        if (slot.connection.is_none() || !slot.leased) {
            return;
        }
        slot.leased = false;
        if (slot.quarantined || !slot.healthy) {
            const cog::Uuid drained_uuid = connection_of(slot).remote_uuid();
            drain_slot(slot, slot.quarantined ? PoolEventKind::DrainedQuarantined : PoolEventKind::EvictedUnhealthy);
            if (!has_remote(drained_uuid)) {
                --distinct_remotes_;
            }
            return;
        }
        append_event(PoolEventKind::Returned, connection_of(slot));
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
        ::fixy::Option<::fixy::Ref<ConnectionPool>> pool_{};
        SlotIndex slot_index_{::fixy::index<0>()};
        [[no_unique_address]] Ctx ctx_;

        friend class ConnectionPool;

        constexpr LeaseGuard(ConnectionPool& pool, SlotIndex slot_index, Ctx const& ctx) noexcept
            : pool_{::fixy::some(::fixy::Ref<ConnectionPool>{::fixy::to(pool)})}, slot_index_{slot_index}, ctx_{ctx} {}

        // The pool of a live lease.  Each caller tested the lease first.
        [[nodiscard]] ConnectionPool const& pool() const noexcept {
            for (::fixy::Ref<ConnectionPool> const& held : pool_) {
                return *held;
            }
            ::fixy::fatal("a read through a lease that gave its slot back");
        }

    public:
        LeaseGuard(LeaseGuard const&) = delete("a lease names one holder of a slot; a copy would return it twice");
        LeaseGuard&
        operator=(LeaseGuard const&) = delete("a lease names one holder of a slot; a copy would return it twice");

        constexpr LeaseGuard(LeaseGuard&& other) noexcept
            : pool_{::fixy::exchange(other.pool_, ::fixy::none)}, slot_index_{other.slot_index_}, ctx_{other.ctx_} {}

        LeaseGuard& operator=(LeaseGuard&& other) noexcept {
            if (!::fixy::same_object(::fixy::to(*this), ::fixy::to(other))) {
                reset();
                pool_ = ::fixy::exchange(other.pool_, ::fixy::none);
                slot_index_ = other.slot_index_;
                ctx_ = other.ctx_;
            }
            return *this;
        }

        ~LeaseGuard() noexcept { reset(); }

        void reset() noexcept {
            for (::fixy::Ref<ConnectionPool> held : ::fixy::exchange(pool_, ::fixy::none)) {
                held->return_index(ctx_, slot_index_);
            }
        }

        [[nodiscard]] Connection<T> const& operator*() const noexcept {
            return connection_of(pool().slots_.at(slot_index_));
        }

        [[nodiscard]] ::fixy::Ref<Connection<T> const> operator->() const noexcept {
            return ::fixy::to(connection_of(pool().slots_.at(slot_index_)));
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept { return pool_.is_some(); }
    };

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, PoolError>
    add_connection(Ctx const& ctx, LinearConnection<T>&& connection, std::uint64_t now_ns = 0) noexcept {
        Connection<T> raw = std::move(connection).consume();

        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        const bool is_new_remote = !has_remote(raw.remote_uuid());
        if (is_new_remote && distinct_remotes_ >= static_cast<std::uint16_t>(MaxRemotes)) {
            return ::fixy::err(PoolError::PoolFull);
        }
        if (remote_occupied_count(raw.remote_uuid()) >= per_remote_limit()) {
            return ::fixy::err(PoolError::PoolFull);
        }
        for (Slot& slot : slots_) {
            if (slot.connection.is_none()) {
                slot.connection = ::fixy::some(raw);
                slot.leased = false;
                slot.quarantined = false;
                slot.healthy = true;
                slot.last_used_ns = now_ns;
                if (is_new_remote) {
                    ++distinct_remotes_;
                }
                append_event(PoolEventKind::Added, raw);
                return ::fixy::Unit{};
            }
        }
        return ::fixy::err(PoolError::PoolFull);
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] CRUCIBLE_HOT ::fixy::Result<LeaseGuard<Ctx>, PoolError>
    lease(Ctx const& ctx, cog::CogIdentity const& remote, std::uint64_t now_ns = 0) noexcept {
        if (!is_valid_remote(remote)) {
            return ::fixy::err(PoolError::InvalidRemoteCog);
        }

        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        for (SlotIndex index : slots_.indices()) {
            Slot& slot = slots_.at(index);
            if (!holds_remote(slot, remote.uuid)) {
                continue;
            }
            if (slot.quarantined) {
                return ::fixy::err(PoolError::RemoteQuarantined);
            }
            if (!slot.healthy) {
                continue;
            }
            if (!slot.leased) {
                slot.leased = true;
                slot.last_used_ns = now_ns;
                append_event(PoolEventKind::Leased, connection_of(slot));
                return LeaseGuard<Ctx>{*this, index, ctx};
            }
        }
        return ::fixy::err(PoolError::PoolEmpty);
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
        for (Slot const& slot : slots_) {
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
        for (Slot& slot : slots_) {
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
        for (Slot& slot : slots_) {
            if (holds_remote(slot, remote.uuid) && same_socket(connection_of(slot).socket(), socket)) {
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
        for (Slot& slot : slots_) {
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
        for (Slot& slot : slots_) {
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
    [[nodiscard]] ::fixy::Result<DeclaredPoolEvent, PoolError> event_at(Ctx const& ctx,
                                                                        std::size_t index) const noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<GateTag>();
        ::fixy::spin::GateGuard guard{ctx, gate_, proof};
        if (index >= event_count_) {
            return ::fixy::err(PoolError::InvalidConnectionId);
        }
        // index is chronological, events_ is physical.  After a wrap the
        // oldest event lives at events_[next_event_], the slot about to be
        // overwritten, and physical slot 0 holds a much newer one.  Before a
        // wrap next_event_ equals event_count_, so the subtraction is zero
        // modulo size and base lands on slot 0.  One formula covers both.
        const std::size_t base = (next_event_.raw() + MaxEvents - event_count_) % MaxEvents;
        const EventIndex physical =
            EventIndex::check((base + index) % MaxEvents).expect("a position modulo the ring size is in it");
        return ::fixy::mint_tagged<::fixy::tags::source::ConnectionPool>(events_.at(physical));
    }
};

template <TransportClass T, std::size_t MaxRemotes, std::size_t MaxPerRemote, class Ctx>
    requires PoolTransportClass<T> && CtxFitsConnectionPoolMint<Ctx>
[[nodiscard]] constexpr ConnectionPool<T, MaxRemotes, MaxPerRemote> mint_connection_pool(Ctx const&,
                                                                                         PoolConfig config) noexcept {
    return ConnectionPool<T, MaxRemotes, MaxPerRemote>{config};
}

}  // namespace crucible::cntp
