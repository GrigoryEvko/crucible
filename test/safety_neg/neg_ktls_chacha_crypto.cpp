#include <crucible/cntp/_wip/KtlsOffload.h>

// GAPS-146 fixture #2: Linux/NIC kTLS offload in this substrate admits
// AES-GCM record suites, not ChaCha20 software-TLS suites.
//
// The names live in `crucible::cntp::_wip`, not `crucible::cntp`.  With
// the shorter using-directive every name in the body was undeclared, so
// `KtlsAesGcmCipherSuite` never ran and this fixture witnessed nothing.

int main() {
    using namespace crucible::cntp::_wip;
    KtlsCryptoMaterial material{};
    auto crypto =
        mint_ktls_crypto_info<TlsVersion::V13, MtlsCipherSuite::TlsChacha20Poly1305Sha256>(std::move(material));
    (void)crypto;
    return 0;
}
