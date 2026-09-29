// A TX enqueue takes a view borrowed from this socket's UMEM.  A view that
// another owner lent does not convert, because the owner is part of the type.

#include <crucible/cntp/AfXdp.h>
#include <fixy/Ctx.h>

struct OtherOwner {};

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto cfg = ::fixy::mint_tagged<::fixy::tags::source::AfXdp>(crucible::cntp::AfXdpConfig{});
    auto socket = crucible::cntp::mint_af_xdp_socket<131072, 2048, 64, 64, 64, 64>(init, cfg);
    std::byte raw[64]{};
    ::fixy::Borrowed<std::byte, OtherOwner> wrong{raw};
    auto result = socket.enqueue_tx(wrong);
    return result.has_value() ? 0 : 1;
}
