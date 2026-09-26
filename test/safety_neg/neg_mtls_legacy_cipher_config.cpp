#include <crucible/cntp/MtlsTransport.h>

// Legacy RSA/3DES cipher suites cannot enter a declared mTLS config.  The
// certificates and the key come from their doors, so the cipher gate is
// the only refusal.

int main() {
    using namespace crucible::cntp;
    auto config =
        mint_mtls_config<TlsVersion::V13, MtlsCipherSuite::LegacyRsa3desSha, MtlsCipherSuite::TlsAes256GcmSha384>(
            ::fixy::mint_linear<MtlsCertificateBytes>(), ::fixy::mint_linear<MtlsCertificateBytes>(),
            ::fixy::mint_secret<MtlsPrivateKeyBytes>());
    (void)config;
    return 0;
}
