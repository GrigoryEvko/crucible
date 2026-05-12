#include <crucible/cntp/ConnectionPool.h>

namespace crucible::cntp {

std::string_view transport_class_name(TransportClass cls) noexcept {
    switch (cls) {
        case TransportClass::RdmaRcQp: return "RdmaRcQp";
        case TransportClass::RdmaUdQp: return "RdmaUdQp";
        case TransportClass::MtlsTcp:  return "MtlsTcp";
        case TransportClass::Quic:     return "Quic";
        case TransportClass::AfXdp:    return "AfXdp";
        case TransportClass::Tcp:      return "Tcp";
        default:                       return "<unknown TransportClass>";
    }
}

std::string_view pool_error_name(PoolError error) noexcept {
    switch (error) {
        case PoolError::InvalidRemoteCog:        return "InvalidRemoteCog";
        case PoolError::InvalidPoolSize:         return "InvalidPoolSize";
        case PoolError::InvalidIdleTimeout:      return "InvalidIdleTimeout";
        case PoolError::InvalidConnectionId:     return "InvalidConnectionId";
        case PoolError::InvalidTransportClass:   return "InvalidTransportClass";
        case PoolError::PoolFull:                return "PoolFull";
        case PoolError::PoolEmpty:               return "PoolEmpty";
        case PoolError::ConnectionAlreadyLeased: return "ConnectionAlreadyLeased";
        case PoolError::ConnectionNotLeased:     return "ConnectionNotLeased";
        case PoolError::ConnectionUnhealthy:     return "ConnectionUnhealthy";
        case PoolError::RemoteQuarantined:       return "RemoteQuarantined";
        default:                                 return "<unknown PoolError>";
    }
}

std::string_view pool_event_kind_name(PoolEventKind kind) noexcept {
    switch (kind) {
        case PoolEventKind::Added:              return "added";
        case PoolEventKind::Leased:             return "leased";
        case PoolEventKind::Returned:           return "returned";
        case PoolEventKind::EvictedUnhealthy:   return "evicted_unhealthy";
        case PoolEventKind::EvictedIdle:        return "evicted_idle";
        case PoolEventKind::DrainedQuarantined: return "drained_quarantined";
        default:                                return "unknown";
    }
}

}  // namespace crucible::cntp
