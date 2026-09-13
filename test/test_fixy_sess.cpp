// A re-exported name must resolve to the same substrate entity, not merely
// to something that behaves the same way.  Comparing the types of the two
// function pointers is what makes that testable.

#include <crucible/fixy/Sess.h>
#include <crucible/permissions/FederationPermission.h>

#include <type_traits>
#include <utility>

namespace fsess = ::crucible::fixy::sess;
namespace proto = ::crucible::safety::proto;
namespace fed = ::crucible::safety::proto::federation;
namespace perm = ::crucible::permissions;
namespace saf = ::crucible::safety;
namespace eff = ::crucible::effects;

// The federation mints take an organisation parameter and an admittance
// witness by value.  This file probes function pointer types and never
// instantiates a mint, so a bare peer tag is enough to name them.
namespace test_fixy_sess {
struct PeerOrg {};
}  // namespace test_fixy_sess
using TestPeerAdmittance = saf::SharedPermission<perm::tag::FederatedPeer<test_fixy_sess::PeerOrg>>;

// A federation mint needs both IO and Block in the context row, and this
// context carries Bg, Alloc and IO only.  Widening is legal because the
// permitted row behind Bg already includes Block.
using FederationFitCtx =
    decltype(eff::BgCompileCtx{}
                 .in_row<eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>());
static_assert(fed::CtxFitsFederation<FederationFitCtx>, "the widened context must satisfy the federation gate");
static_assert(!fed::CtxFitsFederation<eff::BgCompileCtx>, "the unwidened context must not satisfy the gate; it carries "
                                                          "Row<Bg, Alloc, IO> and Block is missing");
static_assert(!fed::CtxFitsFederation<eff::HotFgCtx>,
              "a foreground context must not satisfy the gate; the Fg capability "
              "permits Row<> and can never carry IO and Block");

static_assert(std::is_same_v<fsess::Send<int, fsess::End>, proto::Send<int, proto::End>>,
              "fixy::sess::Send must alias proto::Send.");

static_assert(
    std::is_same_v<fsess::Recv<int, fsess::Loop<fsess::Continue>>, proto::Recv<int, proto::Loop<proto::Continue>>>,
    "fixy::sess::Recv/Loop/Continue compose identically.");

static_assert(std::is_same_v<fsess::Stop, proto::Stop>, "fixy::sess::Stop must alias proto::Stop.");

namespace test_fixy_sess {
struct KeyTag {};
}  // namespace test_fixy_sess

static_assert(
    std::is_same_v<decltype(&fsess::federation::mint_sender<test_fixy_sess::PeerOrg, test_fixy_sess::KeyTag,
                                                            FederationFitCtx, int>),
                   decltype(&fed::mint_sender<test_fixy_sess::PeerOrg, test_fixy_sess::KeyTag, FederationFitCtx, int>)>,
    "fixy::sess::federation::mint_sender must be the substrate function "
    "(name-lookup-only re-export).");

// Two distinct mint_channel factories exist.  The re-export gives them
// separate names rather than letting one shadow the other, so both stay
// callable without qualification.  The three assertions below pin the
// plain name to the session form, the explicit name to the federation
// form, and the namespace-qualified spelling of the latter.
//
// The protocol here is picked so that template substitution succeeds
// without a full set of resources at this site.
static_assert(
    std::is_same_v<decltype(&fsess::mint_channel<proto::Send<int, proto::End>, ::crucible::effects::BgCompileCtx,
                                                 ::crucible::effects::BgCompileCtx, int, int>),
                   decltype(&proto::mint_channel<proto::Send<int, proto::End>, ::crucible::effects::BgCompileCtx,
                                                 ::crucible::effects::BgCompileCtx, int, int>)>,
    "fixy::sess::mint_channel must be the session-protocol form "
    "(proto::mint_channel), not federation's.");

static_assert(
    std::is_same_v<
        decltype(fsess::mint_federation_channel<test_fixy_sess::PeerOrg, test_fixy_sess::KeyTag, FederationFitCtx, int,
                                                int>(std::declval<const FederationFitCtx&>(), std::declval<int>(),
                                                     std::declval<int>(), std::declval<TestPeerAdmittance>())),
        decltype(fed::mint_channel<test_fixy_sess::PeerOrg, test_fixy_sess::KeyTag, FederationFitCtx, int, int>(
            std::declval<const FederationFitCtx&>(), std::declval<int>(), std::declval<int>(),
            std::declval<TestPeerAdmittance>()))>,
    "fixy::sess::mint_federation_channel must forward to "
    "federation::mint_channel with identical return type.");

// Guards against accidental hiding when the using-declaration block is
// edited.
static_assert(
    std::is_same_v<
        decltype(fsess::federation::mint_channel<test_fixy_sess::PeerOrg, test_fixy_sess::KeyTag, FederationFitCtx, int,
                                                 int>(std::declval<const FederationFitCtx&>(), std::declval<int>(),
                                                      std::declval<int>(), std::declval<TestPeerAdmittance>())),
        decltype(fed::mint_channel<test_fixy_sess::PeerOrg, test_fixy_sess::KeyTag, FederationFitCtx, int, int>(
            std::declval<const FederationFitCtx&>(), std::declval<int>(), std::declval<int>(),
            std::declval<TestPeerAdmittance>()))>,
    "fixy::sess::federation::mint_channel must remain callable via the "
    "namespace alias.");

int main() {
    // Every claim here is a compile-time one.  Actually minting would need
    // a fully formed resource, which this file deliberately does not build.
    return 0;
}
