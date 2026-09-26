// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A kTLS offload takes an AES-GCM record suite only.  The crypto mint refuses
// a ChaCha20 suite at compile time.

#include <crucible/cntp/_wip/KtlsOffload.h>

#include <utility>

int main() {
    using namespace crucible::cntp::_wip;
    KtlsCryptoMaterial material{};
    auto crypto =
        mint_ktls_crypto_info<TlsVersion::V13, MtlsCipherSuite::TlsChacha20Poly1305Sha256>(std::move(material));
    (void)crypto;
    return 0;
}
