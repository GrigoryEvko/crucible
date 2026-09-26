// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A send over the DPU channel is background work.  The initialization
// context owns no Bg effect, so the send refuses it.  A channel exists only
// for an offload that a DPU runs, so the fixture takes one by reference.

#include <crucible/cntp/_wip/Doca.h>
#include <fixy/Ctx.h>

#include <array>
#include <cstddef>

namespace doca = crucible::cntp::_wip::doca;

int send_from_init(doca::DpuCommChannel& channel);

int send_from_init(doca::DpuCommChannel& channel) {
    std::array<std::byte, 1> payload{};
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto sent = channel.send_to_dpu(init, payload);
    return sent.has_value() ? 0 : 1;
}

int main() { return 0; }
