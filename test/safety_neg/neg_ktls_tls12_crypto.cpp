// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A kTLS offload takes TLS 1.3 record secrets only.  The crypto mint refuses
// a TLS 1.2 version at compile time.

#include <crucible/cntp/_wip/KtlsOffload.h>

#include <utility>

int main() {
    using namespace crucible::cntp::_wip;
    KtlsCryptoMaterial material{};
    auto crypto = mint_ktls_crypto_info<TlsVersion::V12>(std::move(material));
    (void)crypto;
    return 0;
}
