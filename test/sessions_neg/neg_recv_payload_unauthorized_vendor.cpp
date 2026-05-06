// GAPS-068 fixture #8: receiving an AMD-pinned payload in an NV-pinned
// session is rejected at the session mint boundary.

#include <crucible/safety/Vendor.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;
namespace safe  = ::crucible::safety;

struct Wire {};
struct Tile {};

using AmdTile = safe::Vendor<proto::VendorBackend::AMD, Tile>;
using NvSession = proto::VendorPinned<
    proto::VendorBackend::NV,
    proto::Recv<AmdTile, proto::End>>;

using BadMint = decltype(proto::mint_session<NvSession>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<Wire>()));

int main() { return sizeof(BadMint); }
