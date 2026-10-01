#pragma once

// A Connection<T> arrives already established.  Nothing here creates an RDMA
// queue pair, performs a TLS handshake or probes a peer for health.

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cog/CogIdentity.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

enum class TransportClass : std::uint8_t {
    RdmaRcQp = 0,
    RdmaUdQp = 1,
    MtlsTcp = 2,
    AfXdp = 4,
    Tcp = 5,
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

// The name of a transport class or of an error is the identifier of its
// enumerator.  An event kind reads in lower-case words joined by '_', as a
// log line spells it.
[[nodiscard]] constexpr std::string_view transport_class_name(TransportClass cls) noexcept {
    return ::foundation::reflect::enum_name(cls);
}

[[nodiscard]] constexpr std::string_view pool_error_name(PoolError error) noexcept {
    return ::foundation::reflect::enum_name(error);
}

[[nodiscard]] constexpr std::string_view pool_event_kind_name(PoolEventKind kind) noexcept {
    return ::foundation::reflect::enum_words<PoolEventKind, '_'>(kind);
}

template <TransportClass T>
concept PoolTransportClass = T == TransportClass::RdmaRcQp || T == TransportClass::RdmaUdQp
                          || T == TransportClass::MtlsTcp || T == TransportClass::AfXdp || T == TransportClass::Tcp;

using PositivePoolSize = ::fixy::Positive<std::uint16_t>;
using PositiveIdleTimeoutNs = ::fixy::Positive<std::uint64_t>;
using PositiveConnectionId = ::fixy::Positive<std::uint64_t>;

struct PoolConfig {
    PositivePoolSize max_per_remote = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{65535});
    PositiveIdleTimeoutNs max_idle_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{30000000000ull});
};

template <TransportClass T>
    requires PoolTransportClass<T>
class Connection;

template <TransportClass T>
using LinearConnection = ::fixy::Linear<Connection<T>>;

[[nodiscard]] constexpr bool is_valid_remote(cog::CogIdentity const& remote) noexcept { return !remote.uuid.is_zero(); }

template <TransportClass T>
    requires PoolTransportClass<T>
[[nodiscard]] constexpr std::expected<LinearConnection<T>, PoolError>
mint_connection(SocketFd socket, cog::CogIdentity const& remote, PositiveConnectionId connection_id) noexcept;

// The identity of one established connection: its socket, the Cog at the far
// end and the id its owner gave it.  The constructor is private and
// mint_connection is its only caller, so every connection names a remote
// whose UUID is not zero.  A copy of a connection is the same connection.
// Health is pool state, so it lives in the pool slot and not here.
template <TransportClass T>
    requires PoolTransportClass<T>
class Connection {
public:
    [[nodiscard]] constexpr SocketFd socket() const noexcept { return socket_; }
    [[nodiscard]] constexpr cog::Uuid remote_uuid() const noexcept { return remote_uuid_; }
    [[nodiscard]] constexpr PositiveConnectionId connection_id() const noexcept { return connection_id_; }

private:
    constexpr Connection(SocketFd socket, cog::Uuid remote_uuid, PositiveConnectionId connection_id) noexcept
        : socket_{socket}, remote_uuid_{remote_uuid}, connection_id_{connection_id} {}

    SocketFd socket_;
    cog::Uuid remote_uuid_;
    PositiveConnectionId connection_id_;

    template <TransportClass U>
        requires PoolTransportClass<U>
    friend constexpr std::expected<LinearConnection<U>, PoolError>
    mint_connection(SocketFd socket, cog::CogIdentity const& remote, PositiveConnectionId connection_id) noexcept;
};

template <TransportClass T>
    requires PoolTransportClass<T>
[[nodiscard]] constexpr std::expected<LinearConnection<T>, PoolError>
mint_connection(SocketFd socket, cog::CogIdentity const& remote, PositiveConnectionId connection_id) noexcept {
    if (!is_valid_remote(remote)) {
        return std::unexpected(PoolError::InvalidRemoteCog);
    }
    return ::fixy::mint_linear<Connection<T>>(Connection<T>{socket, remote.uuid, connection_id});
}

struct PoolEvent {
    PoolEventKind kind = PoolEventKind::Added;
    TransportClass transport = TransportClass::Tcp;
    cog::Uuid remote_uuid{};
    SocketFd socket = ::fixy::mint_refined<::fixy::non_negative>(0);
    std::uint64_t connection_id = 0;
    std::uint64_t sequence = 0;
};

using DeclaredPoolEvent = ::fixy::Tagged<PoolEvent, ::fixy::tags::source::ConnectionPool>;

[[nodiscard]] constexpr std::expected<PositivePoolSize, PoolError> admit_pool_size(std::uint16_t size) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(size, PoolError::InvalidPoolSize);
}

[[nodiscard]] constexpr std::expected<PositiveIdleTimeoutNs, PoolError>
admit_idle_timeout_ns(std::uint64_t ns) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(ns, PoolError::InvalidIdleTimeout);
}

[[nodiscard]] constexpr std::expected<PositiveConnectionId, PoolError> admit_connection_id(std::uint64_t id) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(id, PoolError::InvalidConnectionId);
}

}  // namespace crucible::cntp
