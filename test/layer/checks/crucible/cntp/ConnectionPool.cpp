// The compile-time checks of crucible/cntp/ConnectionPool.h.

#include <crucible/cntp/ConnectionPool.h>

namespace crucible::cntp {

static_assert(PoolTransportClass<TransportClass::RdmaRcQp>);
static_assert(PoolTransportClass<TransportClass::Tcp>);
static_assert(!PoolTransportClass<static_cast<TransportClass>(255)>);
static_assert(sizeof(PositivePoolSize) == sizeof(std::uint16_t));
static_assert(sizeof(PositiveIdleTimeoutNs) == sizeof(std::uint64_t));
static_assert(sizeof(PositiveConnectionId) == sizeof(std::uint64_t));
static_assert(sizeof(DeclaredPoolEvent) == sizeof(PoolEvent));
// A refined member makes a record not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what a copy of the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<PoolConfig> && std::is_trivially_destructible_v<PoolConfig>);
static_assert(std::is_trivially_copy_constructible_v<PoolEvent> && std::is_trivially_destructible_v<PoolEvent>);
static_assert(std::is_trivially_copy_constructible_v<Connection<TransportClass::Tcp>>
              && std::is_trivially_destructible_v<Connection<TransportClass::Tcp>>);
static_assert(!std::is_default_constructible_v<Connection<TransportClass::Tcp>>
                  && !std::is_aggregate_v<Connection<TransportClass::Tcp>>,
              "mint_connection must be the only door to a connection");

}  // namespace crucible::cntp
