// A re-exported name must be the same entity as the substrate one, not a
// look-alike declared in the re-exporting namespace.  The admittance flow
// is then run for real to check the token it yields.

#include <crucible/fixy/Source.h>
#include <crucible/permissions/Permission.h>

// The admittance mint is deprecated while its verifier is a placeholder.
// This file exercises it deliberately, so the diagnostic is suppressed for
// the whole translation unit.  The suppression goes out with the
// deprecation.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <type_traits>

namespace ffed = crucible::fixy::source::federation;
namespace fsrc = crucible::fixy::tags::source;
namespace perm = crucible::permissions;
namespace ssrc = crucible::safety::source;

struct SelfOrg {};
struct OtherOrg {};

static_assert(std::is_same_v<ffed::FederatedPeer<SelfOrg>, perm::tag::FederatedPeer<SelfOrg>>,
              "fixy::source::federation::FederatedPeer<Org> must alias the "
              "substrate permissions tag.");

static_assert(std::is_same_v<ffed::LocalCipherTag, perm::tag::LocalCipherTag>, "LocalCipherTag must alias.");

// A second tag of the same name lives on the provenance axis.  The two are
// separate types and must each alias their own substrate tag.
static_assert(std::is_same_v<fsrc::FederatedPeer<SelfOrg>, ssrc::FederatedPeer<SelfOrg>>,
              "the provenance-axis FederatedPeer tag must alias its substrate tag");

static_assert(std::is_same_v<ffed::FederatedPeerPermission<SelfOrg>, perm::FederatedPeerPermission<SelfOrg>>,
              "FederatedPeerPermission<Org> alias identity preserved.");

static_assert(std::is_same_v<ffed::LocalCipherPermission, perm::LocalCipherPermission>,
              "LocalCipherPermission alias identity preserved.");

static_assert(
    std::is_same_v<ffed::FederatedPeerPermission<SelfOrg>, crucible::safety::Permission<ffed::FederatedPeer<SelfOrg>>>,
    "FederatedPeerPermission<Org> must resolve to a permission over the "
    "federated-peer tag");

static_assert(ffed::federation_org_id<SelfOrg> != ffed::federation_org_id<OtherOrg>,
              "federation_org_id<Org> must produce distinct IDs per Org.");

static_assert(ffed::policy::admit_orgs<SelfOrg>::template admits<SelfOrg>, "admit_orgs<SelfOrg> must admit SelfOrg.");
static_assert(!ffed::policy::admit_orgs<SelfOrg>::template admits<OtherOrg>,
              "admit_orgs<SelfOrg> must reject OtherOrg.");

static_assert(std::is_same_v<ffed::FederationHandshake, perm::FederationHandshake>,
              "FederationHandshake identity preserved.");

static_assert(sizeof(ffed::FederationHandshake) == 32, "the handshake is a 32-byte structure");

constexpr auto kSelfHandshake =
    ffed::make_self_signed_handshake<SelfOrg>(ffed::PeerKeyFingerprint{0xCAFEBABEu}, ffed::Nonce{0xDEADBEEFu});

static_assert(kSelfHandshake.org_id == ffed::federation_org_id<SelfOrg>,
              "Generated handshake's org_id must match federation_org_id<SelfOrg>.");

static_assert(kSelfHandshake.peer_key_fingerprint == ffed::PeerKeyFingerprint{0xCAFEBABEu},
              "Generated handshake's peer_key_fingerprint must round-trip.");

static_assert(kSelfHandshake.self_signature_fingerprint
                  == ffed::federation_signature_fingerprint(ffed::federation_org_id<SelfOrg>,
                                                            ffed::PeerKeyFingerprint{0xCAFEBABEu},
                                                            ffed::Nonce{0xDEADBEEFu}),
              "Handshake signature must equal signature_fingerprint over the "
              "(org_id, peer_key_fingerprint, nonce) triple.");

// The two spellings of the handshake factory are one function behind a
// forwarder, so they must agree byte for byte.

static_assert(ffed::FederationOrgTag<SelfOrg>, "an empty class type satisfies the organisation-tag concept, and the "
                                               "concept must be reachable through the re-export");

constexpr auto kSelfMintHandshake =
    ffed::mint_self_signed_handshake<SelfOrg>(ffed::PeerKeyFingerprint{0xCAFEBABEu}, ffed::Nonce{0xDEADBEEFu});

static_assert(kSelfMintHandshake.org_id == kSelfHandshake.org_id,
              "both spellings of the handshake factory must produce the same "
              "organisation id");

static_assert(kSelfMintHandshake.self_signature_fingerprint == kSelfHandshake.self_signature_fingerprint,
              "both spellings of the handshake factory must produce the same "
              "signature");

static_assert(ffed::admittance_error_name(ffed::AdmittanceError::OrgNotAllowed) == std::string_view{"OrgNotAllowed"},
              "admittance_error_name must round-trip the OrgNotAllowed enumerator.");

int main() {
    auto local = crucible::safety::mint_permission_root<ffed::LocalCipherTag>();

    auto admitted = ffed::mint_federation_admittance<SelfOrg>(local, kSelfHandshake);
    if (!admitted.has_value()) {
        return 1;
    }

    using Minted = decltype(admitted)::value_type;
    static_assert(std::is_same_v<Minted, ffed::FederatedPeerPermission<SelfOrg>>,
                  "admitting an organisation must yield a permission for that "
                  "organisation");

    auto bad_org = kSelfHandshake;
    bad_org.org_id = ffed::federation_org_id<OtherOrg>;
    auto rejected = ffed::mint_federation_admittance<SelfOrg>(local, bad_org);
    if (rejected.has_value()) {
        return 2;
    }
    if (rejected.error() != ffed::AdmittanceError::OrgMismatch) {
        return 3;
    }

    return 0;
}

#pragma GCC diagnostic pop
