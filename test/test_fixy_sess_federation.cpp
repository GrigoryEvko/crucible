// A namespace alias makes every name in the aliased namespace
// reachable, but it catches no per-symbol drift.  Renaming a type on
// the substrate side silently breaks every caller that spells the old
// name through the alias, and the diagnostic then surfaces far from
// the rename.  Each assertion below pins one symbol's identity across
// the alias.

#include <crucible/fixy/Sess.h>

#include <type_traits>

namespace ffed = crucible::fixy::sess::federation;
namespace pfed = crucible::safety::proto::federation;

struct TestKeyTag {};

static_assert(std::is_same_v<ffed::SenderProto<>, pfed::SenderProto<>>);
static_assert(std::is_same_v<ffed::ReceiverProto<>, pfed::ReceiverProto<>>);
static_assert(std::is_same_v<ffed::CoordProto<>, pfed::CoordProto<>>);

// The parameterized round trip also catches a template-parameter rename.
static_assert(std::is_same_v<ffed::SenderProto<TestKeyTag>, pfed::SenderProto<TestKeyTag>>);
static_assert(std::is_same_v<ffed::ReceiverProto<TestKeyTag>, pfed::ReceiverProto<TestKeyTag>>);
static_assert(std::is_same_v<ffed::CoordProto<TestKeyTag>, pfed::CoordProto<TestKeyTag>>);

static_assert(std::is_same_v<ffed::ExpectedSenderProto<>, pfed::ExpectedSenderProto<>>);
static_assert(std::is_same_v<ffed::ExpectedReceiverProto<>, pfed::ExpectedReceiverProto<>>);
static_assert(std::is_same_v<ffed::ExpectedCoordProto<>, pfed::ExpectedCoordProto<>>);

// The substrate proves these three equivalences already.  Mirroring
// them through the alias catches a divergence only the alias path sees.
static_assert(std::is_same_v<ffed::SenderProto<>, ffed::ExpectedSenderProto<>>);
static_assert(std::is_same_v<ffed::ReceiverProto<>, ffed::ExpectedReceiverProto<>>);
static_assert(std::is_same_v<ffed::CoordProto<>, ffed::ExpectedCoordProto<>>);

static_assert(std::is_same_v<ffed::FederationProtocol, pfed::FederationProtocol>);
static_assert(std::is_same_v<ffed::FederationProtocolFor<TestKeyTag>, pfed::FederationProtocolFor<TestKeyTag>>);
static_assert(std::is_same_v<ffed::FederationGlobal<>, pfed::FederationGlobal<>>);

static_assert(std::is_same_v<ffed::SenderRole, pfed::SenderRole>);
static_assert(std::is_same_v<ffed::ReceiverRole, pfed::ReceiverRole>);
static_assert(std::is_same_v<ffed::CoordRole, pfed::CoordRole>);

static_assert(sizeof(ffed::SenderRole) == 1);
static_assert(sizeof(ffed::ReceiverRole) == 1);
static_assert(sizeof(ffed::CoordRole) == 1);

static_assert(std::is_same_v<ffed::AnyFederationKey, pfed::AnyFederationKey>);
static_assert(std::is_same_v<ffed::Ack<>, pfed::Ack<>>);
static_assert(std::is_same_v<ffed::PullRequest<>, pfed::PullRequest<>>);
static_assert(std::is_same_v<ffed::FederationEntryPayload<>, pfed::FederationEntryPayload<>>);
static_assert(std::is_same_v<ffed::HeaderPayload<>, pfed::HeaderPayload<>>);
static_assert(std::is_same_v<ffed::BodyPayload<>, pfed::BodyPayload<>>);

static_assert(std::is_same_v<ffed::Ack<TestKeyTag>, pfed::Ack<TestKeyTag>>);
static_assert(std::is_same_v<ffed::PullRequest<TestKeyTag>, pfed::PullRequest<TestKeyTag>>);

static_assert(std::is_same_v<ffed::role_protocol_matches<ffed::SenderRole, ffed::SenderProto<>>,
                             pfed::role_protocol_matches<pfed::SenderRole, pfed::SenderProto<>>>);

static_assert(ffed::role_protocol_matches_v<ffed::SenderRole, ffed::SenderProto<>>);
static_assert(ffed::role_protocol_matches_v<ffed::ReceiverRole, ffed::ReceiverProto<>>);
static_assert(ffed::role_protocol_matches_v<ffed::CoordRole, ffed::CoordProto<>>);

static_assert(!ffed::role_protocol_matches_v<ffed::SenderRole, ffed::ReceiverProto<>>);
static_assert(!ffed::role_protocol_matches_v<ffed::CoordRole, ffed::SenderProto<>>);

static_assert(ffed::role_protocol_matches_v<ffed::SenderRole, ffed::SenderProto<>>
              == pfed::role_protocol_matches_v<pfed::SenderRole, pfed::SenderProto<>>);

static_assert(std::is_same_v<ffed::federation_required_row, pfed::federation_required_row>);

// A concept has no type, so identity across the alias cannot be
// checked with is_same_v.  Wrapping each side in a variable template
// and comparing the results on a probe type is the available substitute.
template <typename T>
inline constexpr bool ffed_cff_v = ffed::CtxFitsFederation<T>;
template <typename T>
inline constexpr bool pfed_cff_v = pfed::CtxFitsFederation<T>;
static_assert(ffed_cff_v<int> == pfed_cff_v<int>);  // both false on non-Ctx

// The boundary function is [[noreturn]], so its identity is compared
// through its type rather than by calling it.
static_assert(std::is_same_v<decltype(&ffed::federation_mint_boundary), decltype(&pfed::federation_mint_boundary)>);

namespace cardinality {
// Each constant is a hand-kept count of the symbols pinned above.
// The assertions read as tautologies on purpose: adding or removing a
// symbol on the substrate side forces an edit here, and the edit fires
// a diagnostic naming the category that moved.
constexpr int kFederationPerRoleProtoCount = 3;  // Sender/Receiver/Coord
constexpr int kFederationExpectedProtoCount = 3;  // Expected{Sender,Receiver,Coord}
constexpr int kFederationGlobalProtoCount = 3;  // FederationProtocol, ForFor, Global
constexpr int kFederationRoleTagCount = 3;  // SenderRole/ReceiverRole/CoordRole
constexpr int kFederationPayloadCount = 6;  // Ack/PullRequest/Payload/Header/Body/AnyKey
constexpr int kFederationVerifierCount = 2;  // role_protocol_matches{,_v}
constexpr int kFederationRowGateCount = 2;  // federation_required_row + CtxFitsFederation
constexpr int kFederationBoundaryCount = 1;  // federation_mint_boundary

constexpr int kFederationTotalReach = kFederationPerRoleProtoCount + kFederationExpectedProtoCount
                                    + kFederationGlobalProtoCount + kFederationRoleTagCount + kFederationPayloadCount
                                    + kFederationVerifierCount + kFederationRowGateCount + kFederationBoundaryCount;

static_assert(kFederationPerRoleProtoCount == 3, "Per-role protocol surface drifted from 3.");
static_assert(kFederationExpectedProtoCount == 3, "Expected-protocol surface drifted from 3.");
static_assert(kFederationGlobalProtoCount == 3, "Global-protocol surface drifted from 3.");
static_assert(kFederationRoleTagCount == 3, "Role-tag surface drifted from 3.");
static_assert(kFederationPayloadCount == 6, "Payload surface drifted from 6.");
static_assert(kFederationVerifierCount == 2, "Verifier surface drifted from 2.");
static_assert(kFederationRowGateCount == 2,
              "Row-gate surface drifted from 2 (federation_required_row + CtxFitsFederation).");
static_assert(kFederationBoundaryCount == 1, "Diagnostic-boundary surface drifted from 1.");
static_assert(kFederationTotalReach == 23, "Total fixy::sess::federation:: reach surface drifted from 23.");
}  // namespace cardinality

int main() {
    // Every claim in this file is a static_assert.
    return 0;
}
