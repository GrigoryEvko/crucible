// GAPS-068 fixture #7: a raw/pure session cannot carry Vendor<T>
// payloads without an explicit VendorPinned/VendorCtx boundary.

#include <crucible/safety/Vendor.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;
namespace safe  = ::crucible::safety;

struct Wire {};
struct Tile {};

using NvTile = safe::Vendor<proto::VendorBackend::NV, Tile>;
using PureProto = proto::Send<NvTile, proto::End>;

using BadMint = decltype(proto::mint_session<PureProto>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<Wire>()));

int main() { return sizeof(BadMint); }
