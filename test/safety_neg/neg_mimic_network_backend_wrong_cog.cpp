// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/mimic/_wip/network/Backend.h>

namespace mb = crucible::mimic::_wip::network;

static_assert(mb::BackendAcceptsCog<
              mb::NetworkBackendVendor::Mellanox,
              crucible::cog::CogKind::Gpu>);
