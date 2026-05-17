// ── test_fixy_source_federation — FIXY-AUDIT-C9 sentinel ──────────
//
// Positive-compile witness for the federation re-export under
// `fixy::source::federation::`:
//
//   tag::FederatedPeer<Org>            — permission tag
//   FederatedPeerPermission<Org>       — Permission<tag::FederatedPeer<Org>>
//   federation_org_id<Org>             — stable type id
//   policy::admit_orgs<Orgs...>        — admission policy
//   FederationHandshake                — handshake POD
//   AdmittanceError + admittance_error_name
//   federation_signature_fingerprint(...)
//   default_peer_key_fingerprint<Org>()
//   make_self_signed_handshake<Org>(...)
//   mint_federation_admittance<Org, Policy>(local, hs)
//
// Mints a Permission<FederatedPeer<SomeOrg>> via the admittance flow
// and verifies type identity with the substrate-spelled equivalent.
// Task #1431.

#include <crucible/fixy/Source.h>
#include <crucible/permissions/Permission.h>

#include <type_traits>

namespace ffed   = crucible::fixy::source::federation;
namespace fsrc   = crucible::fixy::tags::source;
namespace perm   = crucible::permissions;
namespace ssrc   = crucible::safety::source;

// ─── Pretend tenant tags ──────────────────────────────────────────

struct SelfOrg {};
struct OtherOrg {};

// ─── 1. Tag identity ──────────────────────────────────────────────

static_assert(std::is_same_v<
    ffed::FederatedPeer<SelfOrg>,
    perm::tag::FederatedPeer<SelfOrg>>,
    "fixy::source::federation::FederatedPeer<Org> must alias the "
    "substrate permissions tag.");

static_assert(std::is_same_v<
    ffed::LocalCipherTag, perm::tag::LocalCipherTag>,
    "LocalCipherTag must alias.");

// The source-axis provenance variant rides under fixy::tags::source.
static_assert(std::is_same_v<
    fsrc::FederatedPeer<SelfOrg>, ssrc::FederatedPeer<SelfOrg>>,
    "fixy::tags::source::FederatedPeer must alias safety::source::"
    "FederatedPeer (provenance-axis tag).");

// ─── 2. Permission carrier identity ───────────────────────────────

static_assert(std::is_same_v<
    ffed::FederatedPeerPermission<SelfOrg>,
    perm::FederatedPeerPermission<SelfOrg>>,
    "FederatedPeerPermission<Org> alias identity preserved.");

static_assert(std::is_same_v<
    ffed::LocalCipherPermission, perm::LocalCipherPermission>,
    "LocalCipherPermission alias identity preserved.");

// The substrate spells the carrier as Permission<tag::FederatedPeer<Org>>;
// verify the fixy re-export reaches the same.
static_assert(std::is_same_v<
    ffed::FederatedPeerPermission<SelfOrg>,
    crucible::safety::Permission<ffed::FederatedPeer<SelfOrg>>>,
    "FederatedPeerPermission<Org> resolves to Permission<tag::"
    "FederatedPeer<Org>>.");

// ─── 3. Policy and identity ───────────────────────────────────────

static_assert(ffed::federation_org_id<SelfOrg>
              != ffed::federation_org_id<OtherOrg>,
    "federation_org_id<Org> must produce distinct IDs per Org.");

static_assert(ffed::policy::admit_orgs<SelfOrg>::template admits<SelfOrg>,
    "admit_orgs<SelfOrg> must admit SelfOrg.");
static_assert(!ffed::policy::admit_orgs<SelfOrg>::template admits<OtherOrg>,
    "admit_orgs<SelfOrg> must reject OtherOrg.");

// ─── 4. Handshake structure ───────────────────────────────────────

static_assert(std::is_same_v<
    ffed::FederationHandshake, perm::FederationHandshake>,
    "FederationHandshake POD identity preserved.");

static_assert(sizeof(ffed::FederationHandshake) == 32,
    "FederationHandshake substrate guarantee: 32 bytes.");

// ─── 5. Compile-time handshake construction ───────────────────────

constexpr auto kSelfHandshake =
    ffed::make_self_signed_handshake<SelfOrg>(0xCAFEBABEu, 0xDEADBEEFu);

static_assert(kSelfHandshake.org_id == ffed::federation_org_id<SelfOrg>,
    "Generated handshake's org_id must match federation_org_id<SelfOrg>.");

static_assert(kSelfHandshake.peer_key_fingerprint == 0xCAFEBABEu,
    "Generated handshake's peer_key_fingerprint must round-trip.");

static_assert(kSelfHandshake.self_signature_fingerprint
              == ffed::federation_signature_fingerprint(
                     ffed::federation_org_id<SelfOrg>,
                     0xCAFEBABEu, 0xDEADBEEFu),
    "Handshake signature must equal signature_fingerprint over the "
    "(org_id, peer_key_fingerprint, nonce) triple.");

// ─── 6. Admittance error surface ──────────────────────────────────

static_assert(ffed::admittance_error_name(
                  ffed::AdmittanceError::OrgNotAllowed)
              == std::string_view{"OrgNotAllowed"},
    "admittance_error_name must round-trip the OrgNotAllowed enumerator.");

// ─── 7. Runtime mint flow — derive FederatedPeerPermission via the
//      admittance gate ─────────────────────────────────────────────

int main() {
    // Local-cipher authority (root mint per the substrate).
    auto local =
        crucible::safety::mint_permission_root<ffed::LocalCipherTag>();

    // Verified handshake admits SelfOrg.
    auto admitted =
        ffed::mint_federation_admittance<SelfOrg>(local, kSelfHandshake);
    if (!admitted.has_value()) {
        return 1;
    }

    // The minted token has the expected static type.
    using Minted = decltype(admitted)::value_type;
    static_assert(std::is_same_v<Minted, ffed::FederatedPeerPermission<SelfOrg>>,
        "mint_federation_admittance<SelfOrg>(...) must return "
        "FederatedPeerPermission<SelfOrg>.");

    // Mismatched org_id rejects.
    auto bad_org = kSelfHandshake;
    bad_org.org_id = ffed::federation_org_id<OtherOrg>;
    auto rejected =
        ffed::mint_federation_admittance<SelfOrg>(local, bad_org);
    if (rejected.has_value()) {
        return 2;
    }
    if (rejected.error() != ffed::AdmittanceError::OrgMismatch) {
        return 3;
    }

    return 0;
}
