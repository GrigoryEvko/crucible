// GAPS-068 fixture #1: an NV-pinned substrate session cannot mint a
// producer endpoint whose wire payload is AMD-pinned.

#include <crucible/concurrent/SubstrateSessionBridge.h>

#include <utility>

namespace cc    = ::crucible::concurrent;
namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;
namespace safe  = ::crucible::safety;

struct Tag {};

using AmdPayload = safe::Vendor<proto::VendorBackend::AMD, int>;
using AmdSpsc = cc::PermissionedSpscChannel<AmdPayload, 64, Tag>;
using AmdProducer = cc::handle_for_t<AmdSpsc, cc::Direction::Producer>;

using BadMint = decltype(cc::mint_substrate_session<
    AmdSpsc,
    cc::Direction::Producer,
    proto::VendorCtx<proto::VendorBackend::NV>>(
        std::declval<eff::HotFgCtx const&>(),
        std::declval<AmdProducer&>()));

int main() { return sizeof(BadMint); }
