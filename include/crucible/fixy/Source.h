#pragma once

#include <crucible/Types.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/_Tagged.h>

namespace crucible::fixy::tags {

namespace source = ::crucible::safety::source;
namespace trust = ::crucible::safety::trust;
namespace access = ::crucible::safety::access;
namespace version = ::crucible::safety::version;
namespace vessel_trust = ::crucible::safety::vessel_trust;
namespace secret_policy = ::crucible::safety::secret_policy;
namespace hash_family = ::crucible::hash_family;

// Reading the catalog works through this alias. Adding an admitted pair does
// not: a specialization cannot be declared through a using-declaration, so it
// has to be written against ::crucible::safety::retag_policy directly. The
// alias then sees it, as does every other alias of the same template.
using ::crucible::safety::retag_policy;
using ::crucible::safety::RetagAllowed;

// The sentinel pair is reserved and stays unspecialized forever. It is the
// fail-closed witness, so admitting a transition between the two would erase
// the only test of the primary template.
namespace retag_policy_test = ::crucible::safety::detail::retag_policy_test;

}  // namespace crucible::fixy::tags

// This namespace and fixy::tags::source both surface a FederatedPeer<Org>.
// They are distinct types on distinct axes: provenance, meaning who produced
// the value, against permission, meaning who may act on it. A translation unit
// that opens both namespaces sees an ambiguous FederatedPeer and has to
// qualify. That is the intended outcome, because the axis choice belongs at
// the use site.

namespace crucible::fixy::source::federation {

using ::crucible::permissions::tag::FederatedPeer;
using ::crucible::permissions::tag::LocalCipherTag;

using ::crucible::permissions::FederatedPeerPermission;
using ::crucible::permissions::LocalCipherPermission;

using ::crucible::permissions::federation_org_id;

using ::crucible::permissions::OrgId;
using ::crucible::permissions::PeerKeyFingerprint;
using ::crucible::permissions::Nonce;
using ::crucible::permissions::SignatureFingerprint;

namespace policy {
using ::crucible::permissions::policy::admit_orgs;
}  // namespace policy

using ::crucible::permissions::FederationHandshake;
using ::crucible::permissions::AdmittanceError;
using ::crucible::permissions::admittance_error_name;

using ::crucible::permissions::federation_signature_fingerprint;
using ::crucible::permissions::default_peer_key_fingerprint;
using ::crucible::permissions::make_self_signed_handshake;

using ::crucible::permissions::FederationOrgTag;
using ::crucible::permissions::mint_self_signed_handshake;
using ::crucible::permissions::mint_federation_admittance;

}  // namespace crucible::fixy::source::federation

namespace crucible::fixy::tags::self_test {

static_assert(std::is_same_v<source::FromUser, ::crucible::safety::source::FromUser>,
              "fixy::tags::source::FromUser must alias safety::source::FromUser");

static_assert(std::is_same_v<trust::Verified, ::crucible::safety::trust::Verified>,
              "fixy::tags::trust::Verified must alias safety::trust::Verified");

static_assert(std::is_same_v<access::RW, ::crucible::safety::access::RW>,
              "fixy::tags::access::RW must alias safety::access::RW");

static_assert(std::is_same_v<version::V<3>, ::crucible::safety::version::V<3>>,
              "fixy::tags::version::V<N> must alias safety::version::V<N>");

static_assert(std::is_same_v<vessel_trust::Validated, ::crucible::safety::vessel_trust::Validated>,
              "fixy::tags::vessel_trust::Validated must alias safety::vessel_trust::Validated");

static_assert(std::is_same_v<secret_policy::AuditedLogging, ::crucible::safety::secret_policy::AuditedLogging>,
              "fixy::tags::secret_policy::AuditedLogging must alias the substrate tag");

static_assert(std::is_same_v<secret_policy::AuthorizedReplay, ::crucible::safety::secret_policy::AuthorizedReplay>,
              "fixy::tags::secret_policy::AuthorizedReplay must alias the substrate tag");

static_assert(std::is_same_v<hash_family::FamilyA, ::crucible::hash_family::FamilyA>,
              "fixy::tags::hash_family::FamilyA must alias hash_family::FamilyA");

namespace a4_013_disambiguation {
struct ProbeOrg {};
}  // namespace a4_013_disambiguation

static_assert(!std::is_same_v<::crucible::fixy::tags::source::FederatedPeer<a4_013_disambiguation::ProbeOrg>,
                              ::crucible::fixy::source::federation::FederatedPeer<a4_013_disambiguation::ProbeOrg>>,
              "fixy::tags::source::FederatedPeer (provenance axis) and "
              "fixy::source::federation::FederatedPeer (permission axis) must remain "
              "distinct substrate types.  Unification would collapse two orthogonal "
              "axes (provenance vs frame-rule permission) into one "
              "and break the umbrella's axis-discrimination contract.");

static_assert(std::is_same_v<::crucible::fixy::tags::source::FederatedPeer<a4_013_disambiguation::ProbeOrg>,
                             ::crucible::safety::source::FederatedPeer<a4_013_disambiguation::ProbeOrg>>,
              "fixy::tags::source::FederatedPeer must alias "
              "safety::source::FederatedPeer (provenance axis).");

static_assert(std::is_same_v<::crucible::fixy::source::federation::FederatedPeer<a4_013_disambiguation::ProbeOrg>,
                             ::crucible::permissions::tag::FederatedPeer<a4_013_disambiguation::ProbeOrg>>,
              "fixy::source::federation::FederatedPeer must alias "
              "permissions::tag::FederatedPeer (permission axis).");

static_assert(retag_policy<source::FromUser, source::FromUser>::allowed,
              "fixy::tags::retag_policy identity specialization "
              "must admit (X → X).");

static_assert(!retag_policy<retag_policy_test::NeverFrom, retag_policy_test::NeverTo>::allowed,
              "fixy::tags::retag_policy primary template MUST be "
              "fail-closed for the sentinel pair when reached via the "
              "fixy alias.");

static_assert(retag_policy<source::External, source::Sanitized>::allowed,
              "the catalog entry (External → Sanitized) must be "
              "reachable through the fixy::tags alias.");

static_assert(retag_policy<vessel_trust::FromPytorch, vessel_trust::Validated>::allowed,
              "the catalog entry (vessel_trust::FromPytorch → Validated) "
              "must be reachable through the fixy::tags alias.");

static_assert(RetagAllowed<source::External, source::Sanitized>, "fixy::tags::RetagAllowed concept must admit catalog "
                                                                 "transitions through the alias.");

static_assert(!RetagAllowed<retag_policy_test::NeverFrom, retag_policy_test::NeverTo>,
              "fixy::tags::RetagAllowed concept must reject the "
              "sentinel pair through the alias.");

static_assert(RetagAllowed<source::FromUser, source::FromUser>, "fixy::tags::RetagAllowed concept must admit identity "
                                                                "(X → X) through the identity specialization.");

static_assert(!RetagAllowed<trust::Verified, trust::Unverified>,
              "trust ratchet (Verified → Unverified) MUST stay "
              "rejected; admitting it would defeat the verification-status "
              "monotonicity contract.");

}  // namespace crucible::fixy::tags::self_test
