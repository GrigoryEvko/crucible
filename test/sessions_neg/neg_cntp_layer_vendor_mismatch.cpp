// GAPS-068 fixture #2: a CNTP carrier pinned to NV cannot delegate an
// AMD-pinned upper-layer protocol.

#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

struct Wire {};

using AmdUpperLayer = proto::VendorPinned<proto::VendorBackend::AMD, proto::End>;
using NvCntpCarrier = proto::VendorPinned<
    proto::VendorBackend::NV,
    proto::Delegate_seq<AmdUpperLayer, proto::End>>;

using BadMint = decltype(proto::mint_session<NvCntpCarrier>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<Wire>()));

int main() { return sizeof(BadMint); }
