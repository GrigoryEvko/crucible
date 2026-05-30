// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2/2 for fix-15 (§XXI Universal Mint Pattern compliance).
//
// Premise: identical §XXI closure as fixture #1
// (neg_recording_psh_ctor_public_bypass.cpp) — the only public
// construction path for any RecordingPermissionedSessionHandle
// specialisation is the mint_recording_session factory; the
// detail::recording_session_construct_key passkey (fix-15, mirroring
// sessions/PermissionedSession.h:384) gates every public ctor.
//
// What is DIFFERENT here: this fixture exercises the
//   RecordingPermissionedSessionHandle<Offer<Branches...>, PS, R, L>
// specialisation — an EXTERNAL-CHOICE branching head, structurally
// distinct from fixture #1's linear Send<T, K> head.  The Offer spec is
// the one whose member method branch() internally re-wraps each chosen
// branch handle into a fresh sibling-spec RecordingPSH (an authorised
// re-construction site that mints the key through class-friendship).
// Closing the Offer spec's PUBLIC ctor while keeping that internal
// re-wrap working is the second, distinct closure shape this fixture
// witnesses.
//
// The fixture materialises a REAL legitimately-minted inner PSH at the
// Offer<Recv<int, End>, End> head (so the rejection is the OUTER
// recording ctor, not the inner PSH) and attempts the forbidden direct
// construction WITHOUT the passkey.  Build MUST fail; diagnostic MUST
// mention "private" or "recording_session_construct_key".
//
// Sibling fixture (the Send arm / first closure shape):
//   neg_recording_psh_ctor_public_bypass.cpp.

#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

#include <utility>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_recording_psh_offer_ctor_public_bypass {

struct Channel { int unused = 0; };

// An external-choice (Offer) head with two branches — structurally
// distinct from fixture #1's linear Send head.
using CarrierProto =
    proto::Offer<proto::Recv<int, proto::End>, proto::End>;

}  // namespace neg_recording_psh_offer_ctor_public_bypass

int main() {
    using namespace neg_recording_psh_offer_ctor_public_bypass;

    eff::HotFgCtx          ctx{};
    proto::SessionEventLog log{};
    proto::RoleTagId       self{3};
    proto::RoleTagId       peer{4};

    // Build a REAL inner PSH at the Offer<...> state via the legitimate
    // carrier mint.  Well-formed; only the OUTER construction is wrong.
    auto inner = proto::mint_permissioned_session<CarrierProto>(
        ctx, Channel{});

    using InnerType = decltype(inner);
    using PS        = typename InnerType::perm_set;
    using Resource  = typename InnerType::resource_type;
    using LoopCtx   = typename InnerType::loop_ctx;

    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct brace-construction of the Offer-spec recording wrapper
    // WITHOUT the passkey.  After fix-15 the public ctor's first
    // parameter is detail::recording_session_construct_key, whose
    // default ctor is private — unnameable here → ill-formed.
    proto::RecordingPermissionedSessionHandle<
        CarrierProto, PS, Resource, LoopCtx> bad{
            std::move(inner), log, self, peer};
    (void)bad;

    return 0;
}
