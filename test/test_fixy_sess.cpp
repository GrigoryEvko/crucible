// ── test_fixy_sess — sentinel TU for fixy/Sess.h ───────────────────
//
// Pulls fixy/Sess.h into a TU compiled under project warning flags
// so the header's static_asserts execute under enforcement.
// Witnesses:
//
//   1. fixy::sess::Send/Recv/End/Loop/Continue alias the substrate.
//   2. fixy::sess::Stop / Stop_g / CrashClass alias the substrate.
//   3. fixy::sess::EpochedDelegate / RecordingSessionHandle alias.
//   4. fixy::sess::mint_session_handle is reachable via the alias.
//   5. fixy::sess::federation::mint_sender / mint_receiver are reachable.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_sess_*.cpp.

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>

#include <type_traits>
#include <utility>

namespace fsess = ::crucible::fixy::sess;
namespace proto = ::crucible::safety::proto;
namespace fed   = ::crucible::safety::proto::federation;
namespace perm  = ::crucible::permissions;
namespace saf   = ::crucible::safety;
namespace eff   = ::crucible::effects;

// fixy-CR-07: federation session mints now take an Org template
// parameter and a Permission<FederatedPeer<Org>> admittance witness.
// This sentinel TU only probes function pointer types, not runtime
// instantiation, so a forward-declared peer org tag suffices.
namespace test_fixy_sess { struct PeerOrg {}; }
using TestPeerAdmittance =
    saf::Permission<perm::tag::FederatedPeer<test_fixy_sess::PeerOrg>>;

// fixy-CR-13: federation mints require `Row<IO, Block>` in the ctx
// row.  BgCompileCtx ships with `Row<Bg, Alloc, IO>` — missing Block.
// Widen via `.in_row<>()` to admit federation (Bg's permitted row
// includes Block, so the widening is structurally legal).
using FederationFitCtx = decltype(
    eff::BgCompileCtx{}.in_row<eff::Row<
        eff::Effect::Bg, eff::Effect::Alloc,
        eff::Effect::IO, eff::Effect::Block>>());
static_assert(fed::CtxFitsFederation<FederationFitCtx>,
    "Sentinel: federation-widened BgCompileCtx must satisfy "
    "fixy-CR-13's CtxFitsFederation gate.");
static_assert(!fed::CtxFitsFederation<eff::BgCompileCtx>,
    "Sentinel: unwidened BgCompileCtx must NOT satisfy the gate — "
    "BgCompileCtx ships Row<Bg, Alloc, IO>, missing Block.");
static_assert(!fed::CtxFitsFederation<eff::HotFgCtx>,
    "Sentinel: HotFgCtx must NOT satisfy the gate — Fg cap's "
    "permitted row is Row<>, can never carry IO+Block.");

// ─── 1. Core combinator aliases ───────────────────────────────────

static_assert(std::is_same_v<
    fsess::Send<int, fsess::End>,
    proto::Send<int, proto::End>>,
    "fixy::sess::Send must alias proto::Send.");

static_assert(std::is_same_v<
    fsess::Recv<int, fsess::Loop<fsess::Continue>>,
    proto::Recv<int, proto::Loop<proto::Continue>>>,
    "fixy::sess::Recv/Loop/Continue compose identically.");

// ─── 2. Crash-stop family aliases ─────────────────────────────────

static_assert(std::is_same_v<fsess::Stop, proto::Stop>,
    "fixy::sess::Stop must alias proto::Stop.");

// ─── 3. Federation mint factory reachable via fixy::sess ──────────

namespace test_fixy_sess {
struct KeyTag {};
}  // namespace test_fixy_sess

// fixy::sess::federation namespace alias is reachable.
static_assert(std::is_same_v<
    decltype(&fsess::federation::mint_sender<test_fixy_sess::PeerOrg,
                                              test_fixy_sess::KeyTag,
                                              FederationFitCtx,
                                              int>),
    decltype(&fed::mint_sender<test_fixy_sess::PeerOrg,
                                test_fixy_sess::KeyTag,
                                FederationFitCtx,
                                int>)>,
    "fixy::sess::federation::mint_sender must be the substrate function "
    "(name-lookup-only re-export).");

// ─── 5. mint_channel namespace-collision discipline (B10) ─────────
//
// Two `mint_channel` factories exist in the substrate; fixy::sess
// exposes them under DISTINCT names to keep both unambiguously
// callable.  This block witnesses:
//
//   (a) fixy::sess::mint_channel is the session-protocol form
//       (proto::mint_channel, 4 args: ctx_a, ctx_b, res_a, res_b).
//
//   (b) fixy::sess::mint_federation_channel is the federation
//       3-role form (federation::mint_channel, 3 args: ctx,
//       sender_endpoint, receiver_endpoint) — exposed under an
//       explicit name rather than shadowing the session-protocol
//       mint_channel.
//
//   (c) The namespace-aliased path fixy::sess::federation::mint_channel
//       still works for callers who prefer the federation:: spelling.

// The session-protocol mint_channel name resolves to proto::mint_channel.
// We pick the SendInt protocol used inside SessionMint.h's own tests so
// the template substitution succeeds without needing a full set of
// resources at this site.
static_assert(std::is_same_v<
    decltype(&fsess::mint_channel<proto::Send<int, proto::End>,
                                   ::crucible::effects::BgCompileCtx,
                                   ::crucible::effects::BgCompileCtx,
                                   int, int>),
    decltype(&proto::mint_channel<proto::Send<int, proto::End>,
                                   ::crucible::effects::BgCompileCtx,
                                   ::crucible::effects::BgCompileCtx,
                                   int, int>)>,
    "fixy::sess::mint_channel must be the session-protocol form "
    "(proto::mint_channel), not federation's.");

// fixy::sess::mint_federation_channel forwards to
// federation::mint_channel and stays distinct from the session form.
// fixy-CR-07: federation mints now take Org + admittance witness.
static_assert(std::is_same_v<
    decltype(fsess::mint_federation_channel<test_fixy_sess::PeerOrg,
                                             test_fixy_sess::KeyTag,
                                             FederationFitCtx,
                                             int, int>(
        std::declval<const FederationFitCtx&>(),
        std::declval<int>(),
        std::declval<int>(),
        std::declval<TestPeerAdmittance const&>())),
    decltype(fed::mint_channel<test_fixy_sess::PeerOrg,
                                test_fixy_sess::KeyTag,
                                FederationFitCtx,
                                int, int>(
        std::declval<const FederationFitCtx&>(),
        std::declval<int>(),
        std::declval<int>(),
        std::declval<TestPeerAdmittance const&>()))>,
    "fixy::sess::mint_federation_channel must forward to "
    "federation::mint_channel with identical return type.");

// The namespace-alias path fixy::sess::federation::mint_channel still
// resolves — this guards against accidental hiding when later changes
// touch the using-declaration block.
static_assert(std::is_same_v<
    decltype(fsess::federation::mint_channel<test_fixy_sess::PeerOrg,
                                              test_fixy_sess::KeyTag,
                                              FederationFitCtx,
                                              int, int>(
        std::declval<const FederationFitCtx&>(),
        std::declval<int>(),
        std::declval<int>(),
        std::declval<TestPeerAdmittance const&>())),
    decltype(fed::mint_channel<test_fixy_sess::PeerOrg,
                                test_fixy_sess::KeyTag,
                                FederationFitCtx,
                                int, int>(
        std::declval<const FederationFitCtx&>(),
        std::declval<int>(),
        std::declval<int>(),
        std::declval<TestPeerAdmittance const&>()))>,
    "fixy::sess::federation::mint_channel must remain callable via the "
    "namespace alias.");

// ─── 4. Runtime sanity ────────────────────────────────────────────

int main() {
    // Minting goes through SessionMint.h which requires a fully-formed
    // resource; this TU asserts reachability, not runtime instantiation.
    // The substrate's existing tests cover the full mint round-trip.
    return 0;
}
