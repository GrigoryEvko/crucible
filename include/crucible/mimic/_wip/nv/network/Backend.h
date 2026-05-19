#pragma once

#include <crucible/mimic/_wip/network/Backend.h>

// fixy-A5-037: per-vendor Mimic network backend is a M2-M9 stub.
// `NetworkBackendTraits<vendor>::has_emit_path = false` (fixy-A5-043)
// gates `plan_network_kernel` to return `BackendUnavailable` and is
// the SSoT for emit-capability.  The sentinel below locks the current
// stub state — a future PR that flips the shared trait to `true`
// without shipping per-vendor emit logic in this file fires a
// targeted compile error pointing here.  Real-emitter ship-day:
// replace the alias surface with a vendor-specific Backend
// specialization and flip `has_emit_path` in the shared header.

namespace crucible::mimic::_wip::nv::network {
inline constexpr auto vendor =
    ::crucible::mimic::_wip::network::NetworkBackendVendor::Nv;
template <::crucible::cog::CogKind Kind>
using Backend = ::crucible::mimic::_wip::network::NetworkBackend<vendor, Kind>;
using Kernel = ::crucible::mimic::_wip::network::DeclaredNetworkKernel<vendor>;

static_assert(
    !::crucible::mimic::_wip::network::network_backend_has_emit_path_v<vendor>,
    "fixy-A5-037: NetworkBackendTraits<Nv>::has_emit_path was "
    "flipped to true but this per-vendor `_wip` header has not "
    "shipped real emit logic.  Either ship a vendor-specific "
    "Backend specialization here or revert the shared trait flip.  "
    "See MIMIC.md M2-M9.");

}  // namespace crucible::mimic::_wip::nv::network
