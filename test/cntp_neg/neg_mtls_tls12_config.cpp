#include <crucible/cntp/MtlsTransport.h>

// The mTLS transport policy is TLS 1.3 only.  A TLS 1.2 mint cannot
// produce a declared mTLS config.  The certificates and the key come from
// their doors, so the version gate is the only refusal.

int main() {
    using namespace crucible::cntp;
    auto config = mint_mtls_config<TlsVersion::V12>(::fixy::mint_linear<MtlsCertificateBytes>(),
                                                    ::fixy::mint_linear<MtlsCertificateBytes>(),
                                                    ::fixy::mint_secret<MtlsPrivateKeyBytes>());
    (void)config;
    return 0;
}
