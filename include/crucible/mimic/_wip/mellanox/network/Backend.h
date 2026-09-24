#pragma once

#include <crucible/mimic/_wip/network/Backend.h>

namespace crucible::mimic::_wip::mellanox::network {
inline constexpr auto vendor = ::crucible::mimic::_wip::network::NetworkBackendVendor::Mellanox;
template <::crucible::cog::CogKind Kind>
using Backend = ::crucible::mimic::_wip::network::NetworkBackend<vendor, Kind>;
using Kernel = ::crucible::mimic::_wip::network::DeclaredNetworkKernel<vendor>;

static_assert(!::crucible::mimic::_wip::network::network_backend_has_emit_path_v<vendor>,
              "has_emit_path is true for Mellanox but this header ships no "
              "emit logic. Ship a vendor-specific Backend specialization here "
              "or set has_emit_path back to false.");

}  // namespace crucible::mimic::_wip::mellanox::network
