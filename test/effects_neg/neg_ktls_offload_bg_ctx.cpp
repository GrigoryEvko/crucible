// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A kTLS offload request is minted at initialization.  The background
// drain context owns no Init effect, so the mint refuses it.

#include <crucible/cntp/_wip/KtlsOffload.h>
#include <fixy/Ctx.h>

#include <array>
#include <cstddef>
#include <span>
#include <utility>

int main() {
    using namespace crucible::cntp::_wip;
    std::array<std::byte, 32> key{};
    std::array<std::byte, 12> iv{};
    auto material = admit_ktls_crypto_material(key, iv, {}, {});
    auto crypto = mint_ktls_crypto_info(std::move(*material));
    auto fd = admit_socket_fd(3).value();
    auto iface = NicInterfaceName::from("eth0").value();
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto request = mint_ktls_offload_for_socket(bg, fd, iface, std::move(*crypto), TlsOffloadDirection::Tx);
    return request.has_value() ? 0 : 1;
}
