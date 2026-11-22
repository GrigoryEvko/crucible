// Negative-compile test. This file must fail to compile.

#include <crucible/mimic/_wip/network/Backend.h>

namespace mb = crucible::mimic::_wip::network;

static_assert(mb::BackendAcceptsCog<mb::NetworkBackendVendor::Mellanox, crucible::cog::CogKind::Gpu>);
