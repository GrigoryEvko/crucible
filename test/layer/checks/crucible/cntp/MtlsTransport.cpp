// The compile-time checks of crucible/cntp/MtlsTransport.h.

#include <crucible/cntp/MtlsTransport.h>

namespace crucible::cntp {

static_assert(MtlsDnsName::max_bytes <= std::numeric_limits<std::uint8_t>::max(),
              "MtlsDnsName::size_ must be able to name every byte it admits");

static_assert(sizeof(MtlsPeerCount) == sizeof(std::uint8_t));
static_assert(MtlsPolicy::max_peer_names <= std::numeric_limits<std::uint8_t>::max(),
              "MtlsPeerCount must be able to name every allowlist slot");

static_assert(sizeof(MtlsCertificate) == sizeof(MtlsCertificateBytes));
static_assert(sizeof(MtlsPrivateKey) == sizeof(MtlsPrivateKeyBytes));
static_assert(sizeof(MtlsCertificateFingerprint) == sizeof(MtlsSha256Fingerprint));
static_assert(sizeof(AuthenticatedMtlsPeer) == sizeof(MtlsPeerIdentity));
static_assert(!std::copy_constructible<MtlsPrivateKeyBytes>);
static_assert(!std::copy_constructible<MtlsConfig>);
static_assert(std::move_constructible<MtlsConfig>);
static_assert(SupportedMtlsVersion<TlsVersion::V13>);
static_assert(!SupportedMtlsVersion<TlsVersion::V12>);
static_assert(ApprovedMtlsCipherSuite<MtlsCipherSuite::TlsAes256GcmSha384>);
static_assert(!ApprovedMtlsCipherSuite<MtlsCipherSuite::LegacyRsa3desSha>);
static_assert(ApprovedMtlsKeyAlgorithm<MtlsKeyAlgorithm::Ed25519>);
static_assert(!ApprovedMtlsKeyAlgorithm<MtlsKeyAlgorithm::RsaPkcs1>);

}  // namespace crucible::cntp
