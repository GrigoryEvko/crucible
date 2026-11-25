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

    // The tag of the lock of the state.  It is private and nested in the
    // class template, so only this instantiation names the lock.
    struct GateTag {};

    // The state of the pool, which only a body inside the lock reaches.  The
    // const readers take the lock too, to serialize against writers.
    struct State {
        ::fixy::FixedArray<Slot, kSlots> slots{};
        ::fixy::FixedArray<PoolEvent, MaxEvents> events{};
        EventIndex next_event{::fixy::index<0>()};
        std::size_t event_count = 0;
        std::uint64_t sequence = 0;
        // Cached rather than recomputed.  add_connection increments it on
        // the first slot for a remote and every drain path decrements it on
        // the last.
        std::uint16_t distinct_remotes = 0;
    };

    // The slot that a lease took, and a copy of its connection.  The lease
    // reads the copy, so it reads no slot with no lock.  The connection of a
    // leased slot does not change until the lease gives the slot back.
    struct LeasedSlot {
        SlotIndex index;
        Connection<T> connection;
    };

    // The lock word fills its own cache line, so a store to the state does
    // not invalidate the line that the waiters poll.
    ::fixy::spin::BlockingLocked<State, GateTag> state_{};
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

    [[nodiscard]] static constexpr bool same_socket(SocketFd lhs, SocketFd rhs) noexcept { return lhs == rhs; }

    [[nodiscard]] static constexpr bool holds_remote(Slot const& slot, cog::Uuid remote) noexcept {
        for (Connection<T> const& connection : slot.connection) {
            return same_uuid(connection.remote_uuid(), remote);
        }
        return false;
    }

    // The connection of an occupied slot.  Each caller tested the slot first.
    [[nodiscard]] static constexpr Connection<T> const& connection_of(Slot const& slot
                                                                      CRUCIBLE_LIFETIMEBOUND) noexcept {
        for (Connection<T> const& connection : slot.connection) {
            return connection;
        }
        ::fixy::fatal("a connection pool read the connection of an empty slot");
    }
    static void connection_of(Slot const&&) =
        delete("the reference names the connection of the slot, and a temporary slot ends with the full expression");

    [[nodiscard]] static constexpr std::uint16_t max_per_remote() noexcept {
        return static_cast<std::uint16_t>(MaxPerRemote);
    }

    // The two scans below are O(MaxRemotes * MaxPerRemote).  Each caller
    // holds the lock and gives its state.
    [[nodiscard]] static std::uint16_t remote_occupied_count(State const& state, cog::Uuid remote) noexcept {
        std::uint16_t count = 0;
        for (Slot const& slot : state.slots) {
            if (holds_remote(slot, remote)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] static bool has_remote(State const& state, cog::Uuid remote) noexcept {
        for (Slot const& slot : state.slots) {
            if (holds_remote(slot, remote)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::uint16_t per_remote_limit() const noexcept {
        return ::fixy::min(config_.max_per_remote.value(), max_per_remote());
    }

    // The caller holds the lock and gives its state.
    static void append_event(State& state, PoolEventKind kind, Connection<T> const& connection) noexcept {
        state.events.set(state.next_event, PoolEvent{
                                               .kind = kind,
                                               .transport = T,
                                               .remote_uuid = connection.remote_uuid(),
                                               .socket = connection.socket(),
                                               .connection_id = connection.connection_id().value(),
                                               .sequence = ++state.sequence,
                                           });
        state.next_event = state.next_event.next_cyclic();
        if (state.event_count < MaxEvents) {
            ++state.event_count;
        }
    }

    // Records the event and empties the slot at index.  The caller holds the
    // lock and gives its state.
    static void drain_slot(State& state, SlotIndex index, PoolEventKind kind) noexcept {
        append_event(state, kind, connection_of(state.slots.at(index)));
        state.slots.set(index, Slot{});
    }

    template <class Ctx>
    void return_index(Ctx const& ctx, SlotIndex index) noexcept {
        state_.with(ctx, [index](State& state) noexcept {
            Slot const& slot = state.slots.at(index);
            if (slot.connection.is_none() || !slot.leased) {
                return;
            }
            state.slots.update(index, [](Slot& held) noexcept { held.leased = false; });
            if (slot.quarantined || !slot.healthy) {
                const cog::Uuid drained_uuid = connection_of(slot).remote_uuid();
                drain_slot(state, index,
                           slot.quarantined ? PoolEventKind::DrainedQuarantined : PoolEventKind::EvictedUnhealthy);
                if (!has_remote(state, drained_uuid)) {
                    --state.distinct_remotes;
                }
                return;
            }
            append_event(state, PoolEventKind::Returned, connection_of(slot));
        });
    }

public:
    // A lease holds one slot until reset() or the destructor gives the slot
    // back.  It carries the context of the lease call, because the return
    // takes the lock again and a destructor has no context of its own.  The
    // access is read-only: the identity of a leased connection is pool
    // bookkeeping, and a holder that could rewrite it would break the remote
    // count.  The lease keeps a copy of the connection of its slot, which the
    // lease call took under the lock, so a read takes no lock.
    template <class Ctx>
    class [[nodiscard]] LeaseGuard {
        ::fixy::Option<::fixy::Ref<ConnectionPool>> pool_{};
        SlotIndex slot_index_{::fixy::index<0>()};
        Connection<T> connection_;
        [[no_unique_address]] Ctx ctx_;

        friend class ConnectionPool;

        constexpr LeaseGuard(ConnectionPool& pool, LeasedSlot const& leased, Ctx const& ctx) noexcept
            : pool_{::fixy::some(::fixy::erase_brand(::fixy::to(pool)))},
              slot_index_{leased.index},
              connection_{leased.connection},
              ctx_{ctx} {}

        // The connection of a live lease.  Each caller tested the lease first.
        [[nodiscard]] Connection<T> const& live_connection() const noexcept {
            if (pool_.is_none()) [[unlikely]] {
                ::fixy::fatal("a read through a lease that gave its slot back");
            }
            return connection_;
        }

    public:
        LeaseGuard(LeaseGuard const&) = delete("a lease names one holder of a slot; a copy would return it twice");
        LeaseGuard&
        operator=(LeaseGuard const&) = delete("a lease names one holder of a slot; a copy would return it twice");

        constexpr LeaseGuard(LeaseGuard&& other) noexcept
            : pool_{::fixy::exchange(other.pool_, ::fixy::none)},
              slot_index_{other.slot_index_},
              connection_{other.connection_},
              ctx_{other.ctx_} {}

        LeaseGuard& operator=(LeaseGuard&& other) noexcept {
            if (!::fixy::same_object(::fixy::to(*this), ::fixy::to(other))) {
                reset();
                pool_ = ::fixy::exchange(other.pool_, ::fixy::none);
                slot_index_ = other.slot_index_;
                connection_ = other.connection_;
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

        [[nodiscard]] Connection<T> const& operator*() const noexcept { return live_connection(); }

        [[nodiscard]] ::fixy::Ref<Connection<T> const> operator->() const noexcept {
            return ::fixy::erase_brand(::fixy::to(live_connection()));
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept { return pool_.is_some(); }
    };

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, PoolError>
    add_connection(Ctx const& ctx, LinearConnection<T>&& connection, std::uint64_t now_ns = 0) noexcept {
        Connection<T> const raw = std::move(connection).consume();
        const std::uint16_t limit = per_remote_limit();
        return state_.with(ctx,
                           [&raw, limit, now_ns](State& state) noexcept -> ::fixy::Result<::fixy::Unit, PoolError> {
                               const bool is_new_remote = !has_remote(state, raw.remote_uuid());
                               if (is_new_remote && state.distinct_remotes >= static_cast<std::uint16_t>(MaxRemotes)) {
                                   return ::fixy::err(PoolError::PoolFull);
                               }
                               if (remote_occupied_count(state, raw.remote_uuid()) >= limit) {
                                   return ::fixy::err(PoolError::PoolFull);
                               }
                               for (SlotIndex index : state.slots.indices()) {
                                   if (state.slots.at(index).connection.is_none()) {
                                       state.slots.set(index, Slot{.connection = ::fixy::some(raw),
                                                                   .leased = false,
                                                                   .quarantined = false,
                                                                   .healthy = true,
                                                                   .last_used_ns = now_ns});
                                       if (is_new_remote) {
                                           ++state.distinct_remotes;
                                       }
                                       append_event(state, PoolEventKind::Added, raw);
                                       return ::fixy::Unit{};
                                   }
                               }
                               return ::fixy::err(PoolError::PoolFull);
                           });
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] CRUCIBLE_HOT ::fixy::Result<LeaseGuard<Ctx>, PoolError>
    lease(Ctx const& ctx, cog::CogIdentity const& remote, std::uint64_t now_ns = 0) noexcept {
        if (!is_valid_remote(remote)) {
            return ::fixy::err(PoolError::InvalidRemoteCog);
        }

        CRUCIBLE_TRY(
            leased, state_.with(ctx, [&remote, now_ns](State& state) noexcept -> ::fixy::Result<LeasedSlot, PoolError> {
                for (SlotIndex index : state.slots.indices()) {
                    Slot const& slot = state.slots.at(index);
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
                        state.slots.update(index, [now_ns](Slot& held) noexcept {
                            held.leased = true;
                            held.last_used_ns = now_ns;
                        });
                        append_event(state, PoolEventKind::Leased, connection_of(slot));
                        return LeasedSlot{.index = index, .connection = connection_of(slot)};
                    }
                }
                return ::fixy::err(PoolError::PoolEmpty);
            }));
        return LeaseGuard<Ctx>{*this, leased, ctx};
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
        return state_.with(ctx, [&remote](State const& state) noexcept {
            std::uint16_t count = 0;
            for (Slot const& slot : state.slots) {
                if (holds_remote(slot, remote.uuid) && !slot.leased && !slot.quarantined && slot.healthy) {
                    ++count;
                }
            }
            return count;
        });
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void evict_unhealthy(Ctx const& ctx, cog::CogIdentity const& remote) noexcept {
        if (!is_valid_remote(remote)) {
            return;
        }
        state_.with(ctx, [&remote](State& state) noexcept {
            const bool was_present = has_remote(state, remote.uuid);
            for (SlotIndex index : state.slots.indices()) {
                Slot const& slot = state.slots.at(index);
                if (holds_remote(slot, remote.uuid) && !slot.leased && !slot.healthy) {
                    drain_slot(state, index, PoolEventKind::EvictedUnhealthy);
                }
            }
            if (was_present && !has_remote(state, remote.uuid)) {
                --state.distinct_remotes;
            }
        });
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void mark_unhealthy(Ctx const& ctx, cog::CogIdentity const& remote, SocketFd socket) noexcept {
        if (!is_valid_remote(remote)) {
            return;
        }
        state_.with(ctx, [&remote, socket](State& state) noexcept {
            for (SlotIndex index : state.slots.indices()) {
                Slot const& slot = state.slots.at(index);
                if (holds_remote(slot, remote.uuid) && same_socket(connection_of(slot).socket(), socket)) {
                    state.slots.update(index, [](Slot& held) noexcept { held.healthy = false; });
                }
            }
        });
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void evict_idle(Ctx const& ctx, cog::CogIdentity const& remote, std::uint64_t now_ns) noexcept {
        if (!is_valid_remote(remote)) {
            return;
        }
        const std::uint64_t max_idle_ns = config_.max_idle_ns.value();
        state_.with(ctx, [&remote, now_ns, max_idle_ns](State& state) noexcept {
            const bool was_present = has_remote(state, remote.uuid);
            for (SlotIndex index : state.slots.indices()) {
                Slot const& slot = state.slots.at(index);
                if (slot.leased || !holds_remote(slot, remote.uuid)) {
                    continue;
                }
                if (now_ns >= slot.last_used_ns && now_ns - slot.last_used_ns >= max_idle_ns) {
                    drain_slot(state, index, PoolEventKind::EvictedIdle);
                }
            }
            if (was_present && !has_remote(state, remote.uuid)) {
                --state.distinct_remotes;
            }
        });
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    void drain_quarantined(Ctx const& ctx, cog::CogIdentity const& remote) noexcept {
        if (!is_valid_remote(remote)) {
            return;
        }
        state_.with(ctx, [&remote](State& state) noexcept {
            // A quarantined slot that is still leased keeps its slot, so the
            // remote can survive this call and the count of the remotes must
            // not drop.
            const bool was_present = has_remote(state, remote.uuid);
            for (SlotIndex index : state.slots.indices()) {
                Slot const& slot = state.slots.at(index);
                if (!holds_remote(slot, remote.uuid)) {
                    continue;
                }
                state.slots.update(index, [](Slot& held) noexcept { held.quarantined = true; });
                if (!slot.leased) {
                    drain_slot(state, index, PoolEventKind::DrainedQuarantined);
                }
            }
            if (was_present && !has_remote(state, remote.uuid)) {
                --state.distinct_remotes;
            }
        });
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] std::size_t event_count(Ctx const& ctx) const noexcept {
        return state_.with(ctx, [](State const& state) noexcept { return state.event_count; });
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] std::uint16_t distinct_remote_count(Ctx const& ctx) const noexcept {
        return state_.with(ctx, [](State const& state) noexcept { return state.distinct_remotes; });
    }

    template <class Ctx>
        requires CtxFitsConnectionPoolRuntime<Ctx>
    [[nodiscard]] ::fixy::Result<DeclaredPoolEvent, PoolError> event_at(Ctx const& ctx,
                                                                        std::size_t index) const noexcept {
        return state_.with(ctx, [index](State const& state) noexcept -> ::fixy::Result<DeclaredPoolEvent, PoolError> {
            if (index >= state.event_count) {
                return ::fixy::err(PoolError::InvalidConnectionId);
            }
            // index is chronological, the ring is physical.  After a wrap the
            // oldest event lives at the slot of next_event, the slot about to
            // be overwritten, and physical slot 0 holds a much newer one.
            // Before a wrap next_event equals event_count, so the subtraction
            // is zero modulo size and base lands on slot 0.  One formula covers
            // both.
            const std::size_t base = (state.next_event.raw() + MaxEvents - state.event_count) % MaxEvents;
            const EventIndex physical =
                EventIndex::check((base + index) % MaxEvents).expect("a position modulo the ring size is in it");
            return ::fixy::mint_tagged<::fixy::tags::source::ConnectionPool>(state.events.at(physical));
        });
    }
};

template <TransportClass T, std::size_t MaxRemotes, std::size_t MaxPerRemote, class Ctx>
    requires PoolTransportClass<T> && CtxFitsConnectionPoolMint<Ctx>
[[nodiscard]] constexpr ConnectionPool<T, MaxRemotes, MaxPerRemote> mint_connection_pool(Ctx const&,
                                                                                         PoolConfig config) noexcept {
    return ConnectionPool<T, MaxRemotes, MaxPerRemote>{config};
}

}  // namespace crucible::cntp
