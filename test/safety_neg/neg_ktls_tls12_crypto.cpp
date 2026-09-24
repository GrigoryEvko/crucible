#include <crucible/cntp/_wip/KtlsOffload.h>

// GAPS-146 fixture #1: kTLS offload consumes TLS 1.3 record secrets only.
// A TLS 1.2 crypto-info mint cannot produce a declared kTLS request.
//
// The names live in `crucible::cntp::_wip`, not `crucible::cntp`.  With
// the shorter using-directive every name in the body was undeclared, so
// `SupportedKtlsVersion` never ran and this fixture witnessed nothing.

int main() {
    using namespace crucible::cntp::_wip;
    KtlsCryptoMaterial material{};
    auto crypto = mint_ktls_crypto_info<TlsVersion::V12>(std::move(material));
    (void)crypto;
    return 0;
}
