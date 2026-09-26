// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// An offload request takes declared crypto info, and mint_ktls_crypto_info
// is the one function that makes it.  Raw crypto info built by hand does
// not convert.

#include <crucible/cntp/_wip/KtlsOffload.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    using namespace crucible::cntp::_wip;
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto fd = admit_socket_fd(3).value();
    auto iface = NicInterfaceName::from("eth0").value();
    TlsCryptoInfo raw{
        .cipher = MtlsCipherSuite::TlsAes256GcmSha384,
        .shape = KtlsCryptoShape{},
        .material = ::fixy::mint_secret<KtlsCryptoMaterial>(),
    };
    auto request = mint_ktls_offload_for_socket(init, fd, iface, std::move(raw), TlsOffloadDirection::Tx);
    return request.has_value() ? 0 : 1;
}
