#include <crucible/cntp/_wip/KtlsOffload.h>

// GAPS-146 fixture #3: offload requests require DeclaredTlsCryptoInfo
// tagged with _wip::wip_source::KtlsOffloaded. Raw TLS crypto structs cannot
// program kTLS/NIC state.

int main() {
    using namespace crucible::cntp::_wip;
    auto init = crucible::effects::testing::init();
    auto fd = admit_socket_fd(3).value();
    auto iface = NicInterfaceName::from("eth0").value();
    TlsCryptoInfo raw{
        .cipher = MtlsCipherSuite::TlsAes256GcmSha384,
        .shape = KtlsCryptoShape{},
        .material = KtlsSecretMaterial{KtlsCryptoMaterial{}},
    };
    auto request = mint_ktls_offload_for_socket(
        init, fd, iface, std::move(raw), TlsOffloadDirection::Tx);
    (void)request;
    return 0;
}
