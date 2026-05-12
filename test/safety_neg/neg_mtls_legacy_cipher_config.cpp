#include <crucible/cntp/MtlsTransport.h>

// GAPS-126 fixture #2: legacy RSA/3DES cipher suites cannot be admitted
// into a declared mTLS config.

int main() {
    using namespace crucible::cntp;
    auto config = mint_mtls_config<
        TlsVersion::V13,
        MtlsCipherSuite::LegacyRsa3desSha,
        MtlsCipherSuite::TlsAes256GcmSha384>(
        MtlsCertificate{MtlsCertificateBytes{}},
        MtlsCertificate{MtlsCertificateBytes{}},
        MtlsPrivateKey{MtlsPrivateKeyBytes{}});
    (void)config;
    return 0;
}
