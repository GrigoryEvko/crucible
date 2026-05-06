// GAPS-068 fixture #3: a collective-like protocol with Vendor<T>
// payloads must declare an explicit VendorPinned/VendorCtx boundary.

#include <crucible/safety/Vendor.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;
namespace safe  = ::crucible::safety;

struct Wire {};
struct Tile {};

using NvTile = safe::Vendor<proto::VendorBackend::NV, Tile>;
using CollectiveProto = proto::Loop<
    proto::Send<NvTile, proto::Continue>>;

using BadMint = decltype(proto::mint_session<CollectiveProto>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<Wire>()));

int main() { return sizeof(BadMint); }
