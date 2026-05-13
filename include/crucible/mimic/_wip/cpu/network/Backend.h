#pragma once

#include <crucible/mimic/_wip/network/Backend.h>

namespace crucible::mimic::_wip::cpu::network {
inline constexpr auto vendor =
    ::crucible::mimic::_wip::network::NetworkBackendVendor::Cpu;
template <::crucible::cog::CogKind Kind>
using Backend = ::crucible::mimic::_wip::network::NetworkBackend<vendor, Kind>;
using Kernel = ::crucible::mimic::_wip::network::DeclaredNetworkKernel<vendor>;
}  // namespace crucible::mimic::_wip::cpu::network
