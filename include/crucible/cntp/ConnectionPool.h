#pragma once

// GAPS-136. CNT-P connection-pool substrate.
//
// CNT-P owns typed connection facts and lease-event provenance. The runtime
// pool in rt/ConnectionPool.h owns mutable bounded reuse state. This substrate
// deliberately does not create RDMA queue pairs, perform TLS handshakes,
// schedule health probes, or mutate quarantine policy; those producers feed
// already-owned Connection<T> values into the pool.

#include <crucible/Platform.h>
#include <crucible/cntp/CongestionControl.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

enum class TransportClass : std::uint8_t {
    RdmaRcQp = 0,
    RdmaUdQp = 1,
    MtlsTcp  = 2,
    Quic     = 3,
    AfXdp    = 4,
    Tcp      = 5,
};

enum class PoolError : std::uint8_t {
    InvalidRemoteCog,
    InvalidPoolSize,
    InvalidIdleTimeout,
    InvalidConnectionId,
    InvalidTransportClass,
    PoolFull,
    PoolEmpty,
    ConnectionAlreadyLeased,
    ConnectionNotLeased,
    ConnectionUnhealthy,
    RemoteQuarantined,
};

enum class PoolEventKind : std::uint8_t {
    Added,
    Leased,
    Returned,
    EvictedUnhealthy,
    EvictedIdle,
    DrainedQuarantined,
};

[[nodiscard]] std::string_view transport_class_name(TransportClass cls) noexcept;
[[nodiscard]] std::string_view pool_error_name(PoolError error) noexcept;
[[nodiscard]] std::string_view pool_event_kind_name(PoolEventKind kind) noexcept;

template <TransportClass T>
concept PoolTransportClass =
       T == TransportClass::RdmaRcQp
    || T == TransportClass::RdmaUdQp
    || T == TransportClass::MtlsTcp
    || T == TransportClass::Quic
    || T == TransportClass::AfXdp
    || T == TransportClass::Tcp;

using PositivePoolSize = safety::Positive<std::uint16_t>;
using PositiveIdleTimeoutNs = safety::Positive<std::uint64_t>;
using PositiveConnectionId = safety::Positive<std::uint64_t>;

struct PoolConfig {
    PositivePoolSize max_per_remote{std::uint16_t{65'535}};
    PositiveIdleTimeoutNs max_idle_ns{std::uint64_t{30'000'000'000ull}};
    bool probe_health = true;
};

template <TransportClass T>
    requires PoolTransportClass<T>
struct Connection {
    SocketFd socket{0, typename SocketFd::Trusted{}};
    cog::Uuid remote_uuid{};
    PositiveConnectionId connection_id{std::uint64_t{1}};
    bool healthy = true;
};

template <TransportClass T>
    requires PoolTransportClass<T>
using LinearConnection = safety::Linear<Connection<T>>;

struct PoolEvent {
    PoolEventKind kind = PoolEventKind::Added;
    TransportClass transport = TransportClass::Tcp;
    cog::Uuid remote_uuid{};
    SocketFd socket{0, typename SocketFd::Trusted{}};
    std::uint64_t connection_id = 0;
    std::uint64_t sequence = 0;
};

using DeclaredPoolEvent =
    safety::Tagged<PoolEvent, safety::source::ConnectionPool>;

[[nodiscard]] constexpr std::expected<PositivePoolSize, PoolError>
admit_pool_size(std::uint16_t size) noexcept {
    if (size == 0) {
        return std::unexpected(PoolError::InvalidPoolSize);
    }
    return PositivePoolSize{size, typename PositivePoolSize::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveIdleTimeoutNs, PoolError>
admit_idle_timeout_ns(std::uint64_t ns) noexcept {
    if (ns == 0) {
        return std::unexpected(PoolError::InvalidIdleTimeout);
    }
    return PositiveIdleTimeoutNs{
        ns, typename PositiveIdleTimeoutNs::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveConnectionId, PoolError>
admit_connection_id(std::uint64_t id) noexcept {
    if (id == 0) {
        return std::unexpected(PoolError::InvalidConnectionId);
    }
    return PositiveConnectionId{id, typename PositiveConnectionId::Trusted{}};
}

[[nodiscard]] constexpr bool valid_remote(cog::CogIdentity const& remote) noexcept {
    return !remote.uuid.is_zero();
}

template <TransportClass T>
    requires PoolTransportClass<T>
[[nodiscard]] constexpr std::expected<LinearConnection<T>, PoolError>
mint_connection(SocketFd socket,
                cog::CogIdentity const& remote,
                PositiveConnectionId connection_id) noexcept {
    if (!valid_remote(remote)) {
        return std::unexpected(PoolError::InvalidRemoteCog);
    }
    return LinearConnection<T>{Connection<T>{
        .socket = socket,
        .remote_uuid = remote.uuid,
        .connection_id = connection_id,
        .healthy = true,
    }};
}

[[nodiscard]] constexpr DeclaredPoolEvent
mint_pool_event(PoolEvent event) noexcept {
    return DeclaredPoolEvent{event};
}

static_assert(PoolTransportClass<TransportClass::RdmaRcQp>);
static_assert(PoolTransportClass<TransportClass::Tcp>);
static_assert(!PoolTransportClass<static_cast<TransportClass>(255)>);
static_assert(sizeof(PositivePoolSize) == sizeof(std::uint16_t));
static_assert(sizeof(PositiveIdleTimeoutNs) == sizeof(std::uint64_t));
static_assert(sizeof(PositiveConnectionId) == sizeof(std::uint64_t));
static_assert(sizeof(DeclaredPoolEvent) == sizeof(PoolEvent));
static_assert(std::is_trivially_copyable_v<PoolConfig>);
static_assert(std::is_trivially_copyable_v<PoolEvent>);
static_assert(std::is_trivially_copyable_v<Connection<TransportClass::Tcp>>);

}  // namespace crucible::cntp
