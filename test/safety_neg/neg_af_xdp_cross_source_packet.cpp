#include <crucible/cntp/AfXdp.h>

struct OtherOwner {};

int main() {
    crucible::effects::ColdInitCtx init{};
    auto cfg = crucible::cntp::DeclaredAfXdpConfig{
        crucible::cntp::AfXdpConfig{}};
    auto socket =
        crucible::cntp::mint_af_xdp_socket<131'072, 2'048, 64, 64, 64, 64>(
            init, cfg);
    std::byte raw[64]{};
    crucible::safety::Borrowed<std::byte, OtherOwner> wrong{raw};
    auto result = socket.enqueue_tx(wrong);
    return result.has_value() ? 0 : 1;
}
