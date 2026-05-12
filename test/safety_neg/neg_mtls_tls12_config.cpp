#include <crucible/cntp/MtlsTransport.h>

// GAPS-126 fixture #1: mTLS transport policy is TLS 1.3 only. A TLS 1.2
// mint cannot produce a declared mTLS config.

int main() {
    using namespace crucible::cntp;
    auto config = mint_mtls_config<TlsVersion::V12>(
        MtlsCertificate{MtlsCertificateBytes{}},
        MtlsCertificate{MtlsCertificateBytes{}},
        MtlsPrivateKey{MtlsPrivateKeyBytes{}});
    (void)config;
    return 0;
}
