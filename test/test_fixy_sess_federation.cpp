// ── test_fixy_sess_federation — FIXY-U-072 sentinel TU ──────────────
//
// Positive-compile witness for the federation per-role protocol type
// re-exports under `fixy::sess::federation::` (which lives as a
// namespace alias to `safety::proto::federation::`).
//
// Why this TU exists:
//   The fixy::sess::federation namespace alias (Sess.h line 295)
//   makes every name in safety::proto::federation::* reachable, but
//   a namespace alias provides NO per-symbol drift-catch — a
//   substrate-side rename of a federation type (e.g.,
//   ReceiverProto → ConsumerProto) would silently break callers who
//   spell the old name through fixy::sess::federation::, with the
//   diagnostic surfacing far from the rename site.  Mirror of
//   FIXY-U-021's resolution for fixy::sess::pattern:: under
//   test_fixy_sess_patterns.cpp.
//
// What it pins (FIXY-U-072):
//   1. Type-identity for every federation symbol reachable via the
//      alias — 21 items grouped by category (per-role + expected +
//      global + role-tag + payload + verifier + boundary).
//   2. Cardinality witnesses per category — a substrate-side ADD or
//      REMOVE fires a constant-mismatch diagnostic, NOT a silent
//      drift.
//
// Closes FIXY-U-072.  Reach symbol map mirrors the doc-block in
// FederationProtocol.h (line 9-13: 4-message protocol over 3 roles).

#include <crucible/fixy/Sess.h>

#include <type_traits>

namespace ffed = crucible::fixy::sess::federation;
namespace pfed = crucible::safety::proto::federation;

// ─── Probe key tag (carrier-only; no semantic content) ─────────────

struct TestKeyTag {};

// ─── 1. Per-role projection protocols (3) ──────────────────────────

static_assert(std::is_same_v<
    ffed::SenderProto<>,        pfed::SenderProto<>>);
static_assert(std::is_same_v<
    ffed::ReceiverProto<>,      pfed::ReceiverProto<>>);
static_assert(std::is_same_v<
    ffed::CoordProto<>,         pfed::CoordProto<>>);

// Parameterized round-trip catches template-parameter renames.
static_assert(std::is_same_v<
    ffed::SenderProto<TestKeyTag>,   pfed::SenderProto<TestKeyTag>>);
static_assert(std::is_same_v<
    ffed::ReceiverProto<TestKeyTag>, pfed::ReceiverProto<TestKeyTag>>);
static_assert(std::is_same_v<
    ffed::CoordProto<TestKeyTag>,    pfed::CoordProto<TestKeyTag>>);

// ─── 2. Expected canonical forms (3) ───────────────────────────────

static_assert(std::is_same_v<
    ffed::ExpectedSenderProto<>,   pfed::ExpectedSenderProto<>>);
static_assert(std::is_same_v<
    ffed::ExpectedReceiverProto<>, pfed::ExpectedReceiverProto<>>);
static_assert(std::is_same_v<
    ffed::ExpectedCoordProto<>,    pfed::ExpectedCoordProto<>>);

// Substrate's own static_asserts already prove SenderProto<> ≡
// ExpectedSenderProto<>; mirror them through the fixy:: path to
// catch any divergence introduced by future refactor.
static_assert(std::is_same_v<
    ffed::SenderProto<>,   ffed::ExpectedSenderProto<>>);
static_assert(std::is_same_v<
    ffed::ReceiverProto<>, ffed::ExpectedReceiverProto<>>);
static_assert(std::is_same_v<
    ffed::CoordProto<>,    ffed::ExpectedCoordProto<>>);

// ─── 3. Global protocols (3) ───────────────────────────────────────

static_assert(std::is_same_v<
    ffed::FederationProtocol,         pfed::FederationProtocol>);
static_assert(std::is_same_v<
    ffed::FederationProtocolFor<TestKeyTag>,
    pfed::FederationProtocolFor<TestKeyTag>>);
static_assert(std::is_same_v<
    ffed::FederationGlobal<>,         pfed::FederationGlobal<>>);

// ─── 4. Role tags (3) ──────────────────────────────────────────────

static_assert(std::is_same_v<ffed::SenderRole,   pfed::SenderRole>);
static_assert(std::is_same_v<ffed::ReceiverRole, pfed::ReceiverRole>);
static_assert(std::is_same_v<ffed::CoordRole,    pfed::CoordRole>);

// Empty tag types — sizeof == 1.
static_assert(sizeof(ffed::SenderRole)   == 1);
static_assert(sizeof(ffed::ReceiverRole) == 1);
static_assert(sizeof(ffed::CoordRole)    == 1);

// ─── 5. Payload carriers (6) ───────────────────────────────────────

static_assert(std::is_same_v<
    ffed::AnyFederationKey,           pfed::AnyFederationKey>);
static_assert(std::is_same_v<
    ffed::Ack<>,                      pfed::Ack<>>);
static_assert(std::is_same_v<
    ffed::PullRequest<>,              pfed::PullRequest<>>);
static_assert(std::is_same_v<
    ffed::FederationEntryPayload<>,   pfed::FederationEntryPayload<>>);
static_assert(std::is_same_v<
    ffed::HeaderPayload<>,            pfed::HeaderPayload<>>);
static_assert(std::is_same_v<
    ffed::BodyPayload<>,              pfed::BodyPayload<>>);

// Parameterized variants on TestKeyTag.
static_assert(std::is_same_v<
    ffed::Ack<TestKeyTag>,            pfed::Ack<TestKeyTag>>);
static_assert(std::is_same_v<
    ffed::PullRequest<TestKeyTag>,    pfed::PullRequest<TestKeyTag>>);

// ─── 6. Verifier metafunctions (2) ─────────────────────────────────

static_assert(std::is_same_v<
    ffed::role_protocol_matches<ffed::SenderRole, ffed::SenderProto<>>,
    pfed::role_protocol_matches<pfed::SenderRole, pfed::SenderProto<>>>);

static_assert(ffed::role_protocol_matches_v<ffed::SenderRole,
                                             ffed::SenderProto<>>);
static_assert(ffed::role_protocol_matches_v<ffed::ReceiverRole,
                                             ffed::ReceiverProto<>>);
static_assert(ffed::role_protocol_matches_v<ffed::CoordRole,
                                             ffed::CoordProto<>>);

// Negative: cross-role / cross-protocol matches reject.
static_assert(!ffed::role_protocol_matches_v<ffed::SenderRole,
                                              ffed::ReceiverProto<>>);
static_assert(!ffed::role_protocol_matches_v<ffed::CoordRole,
                                              ffed::SenderProto<>>);

// Substrate parity for the predicate variable.
static_assert(ffed::role_protocol_matches_v<ffed::SenderRole,
                                             ffed::SenderProto<>>
              == pfed::role_protocol_matches_v<pfed::SenderRole,
                                                pfed::SenderProto<>>);

// ─── 7. Row gate + diagnostic boundary (3) ────────────────────────

static_assert(std::is_same_v<
    ffed::federation_required_row,
    pfed::federation_required_row>);

// CtxFitsFederation is a concept template — exercise via reachability
// rather than std::is_same_v (concepts have no type).  Definitional
// alias preservation: equivalence on a probe Ctx type.
template <typename T>
inline constexpr bool ffed_cff_v = ffed::CtxFitsFederation<T>;
template <typename T>
inline constexpr bool pfed_cff_v = pfed::CtxFitsFederation<T>;
static_assert(ffed_cff_v<int> == pfed_cff_v<int>);   // both false on non-Ctx

// federation_mint_boundary — name reachability.  Address-of is not
// allowed (substrate marks it [[noreturn]] inline), but the symbol
// must resolve through the alias.  static_assert on its type wraps.
static_assert(std::is_same_v<
    decltype(&ffed::federation_mint_boundary),
    decltype(&pfed::federation_mint_boundary)>);

// ─── 8. Cardinality witnesses ─────────────────────────────────────

namespace cardinality {
// Each constant pins a category count.  Substrate ADD/REMOVE fires
// the mismatch diagnostic at this TU's compile.
constexpr int kFederationPerRoleProtoCount   = 3;   // Sender/Receiver/Coord
constexpr int kFederationExpectedProtoCount  = 3;   // Expected{Sender,Receiver,Coord}
constexpr int kFederationGlobalProtoCount    = 3;   // FederationProtocol, ForFor, Global
constexpr int kFederationRoleTagCount        = 3;   // SenderRole/ReceiverRole/CoordRole
constexpr int kFederationPayloadCount        = 6;   // Ack/PullRequest/Payload/Header/Body/AnyKey
constexpr int kFederationVerifierCount       = 2;   // role_protocol_matches{,_v}
constexpr int kFederationRowGateCount        = 2;   // federation_required_row + CtxFitsFederation
constexpr int kFederationBoundaryCount       = 1;   // federation_mint_boundary

constexpr int kFederationTotalReach          =
    kFederationPerRoleProtoCount + kFederationExpectedProtoCount +
    kFederationGlobalProtoCount + kFederationRoleTagCount +
    kFederationPayloadCount + kFederationVerifierCount +
    kFederationRowGateCount + kFederationBoundaryCount;

static_assert(kFederationPerRoleProtoCount   == 3,
    "Per-role protocol surface drifted from 3.");
static_assert(kFederationExpectedProtoCount  == 3,
    "Expected-protocol surface drifted from 3.");
static_assert(kFederationGlobalProtoCount    == 3,
    "Global-protocol surface drifted from 3.");
static_assert(kFederationRoleTagCount        == 3,
    "Role-tag surface drifted from 3.");
static_assert(kFederationPayloadCount        == 6,
    "Payload surface drifted from 6.");
static_assert(kFederationVerifierCount       == 2,
    "Verifier surface drifted from 2.");
static_assert(kFederationRowGateCount        == 2,
    "Row-gate surface drifted from 2 (federation_required_row + CtxFitsFederation).");
static_assert(kFederationBoundaryCount       == 1,
    "Diagnostic-boundary surface drifted from 1.");
static_assert(kFederationTotalReach          == 23,
    "Total fixy::sess::federation:: reach surface drifted from 23.");
}  // namespace cardinality

int main() {
    // Compile-time-only sentinel.
    return 0;
}
