#include <crucible/cntp/KtlsOffload.h>

// GAPS-146 fixture #2: Linux/NIC kTLS offload in this substrate admits
// AES-GCM record suites, not ChaCha20 software-TLS suites.

int main() {
    using namespace crucible::cntp;
    KtlsCryptoMaterial material{};
    auto crypto = mint_ktls_crypto_info<
        TlsVersion::V13,
        MtlsCipherSuite::TlsChacha20Poly1305Sha256>(std::move(material));
    (void)crypto;
    return 0;
}
