// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1/2 for fix-15 (§XXI Universal Mint Pattern compliance).
//
// Premise: RecordingPermissionedSessionHandle<Proto, PS, Resource,
// LoopCtx> exists ONLY to be the product of
//   mint_recording_session(psh, log, self, peer)
// — the §XXI authorisation point that attaches the audit log + role
// identity to a freshly-minted permissioned session handle.  Before
// fix-15 every specialisation had two PUBLIC constructors, so a caller
// could write
//   RecordingPermissionedSessionHandle<Send<int,End>, ...>{
//       psh, log, self, peer}
// directly, bypassing the mint entirely.  §XXI demands the mint be the
// SOLE construction path.
//
// Fix shape (fix-15, mirroring detail::permissioned_session_construct_key
// in sessions/PermissionedSession.h:384): a private-default-ctor passkey
// `detail::recording_session_construct_key` is now the FIRST parameter
// of every specialisation's public ctor.  Only mint_recording_session,
// detail::wrap_next_permissioned_, and the class's own member methods
// can mint the key.  A production-side direct construction can no longer
// name the key → ill-formed.
//
// This fixture materialises a REAL legitimately-minted inner PSH at the
// Send<int, End> head (so the rejection is the OUTER recording ctor, not
// the inner PSH) and attempts the now-forbidden direct construction.
// Build MUST fail; diagnostic MUST mention "private" or
// "recording_session_construct_key".
//
// Sibling fixture (different specialisation / closure shape):
//   neg_recording_psh_offer_ctor_public_bypass.cpp (Offer<...> arm).

#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

#include <utility>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_recording_psh_ctor_public_bypass {

struct Channel { int unused = 0; };

// A linear Send head — the simplest non-terminal protocol shape.
using CarrierProto = proto::Send<int, proto::End>;

}  // namespace neg_recording_psh_ctor_public_bypass

int main() {
    using namespace neg_recording_psh_ctor_public_bypass;

    eff::HotFgCtx          ctx{};
    proto::SessionEventLog log{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // Build a REAL inner PSH at the Send<int, End> state via the
    // legitimate carrier mint.  This handle is well-formed; the only
    // thing wrong below is the OUTER recording-wrapper construction.
    auto inner = proto::mint_permissioned_session<CarrierProto>(
        ctx, Channel{});

    using InnerType = decltype(inner);
    using PS        = typename InnerType::perm_set;
    using Resource  = typename InnerType::resource_type;
    using LoopCtx   = typename InnerType::loop_ctx;

    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct brace-construction of the recording wrapper, supplying
    // (inner, log, self, peer) WITHOUT the passkey.  After fix-15 the
    // public ctor's first parameter is
    // detail::recording_session_construct_key, whose default ctor is
    // private — this caller cannot name it, so the construction is
    // ill-formed.  Before fix-15 this compiled silently and bypassed
    // the mint's audit-context attachment.
    proto::RecordingPermissionedSessionHandle<
        CarrierProto, PS, Resource, LoopCtx> bad{
            std::move(inner), log, self, peer};
    (void)bad;

    return 0;
}
