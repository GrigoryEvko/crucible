// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A receive over the DPU channel is background work.  The foreground
// context claims no effect, so the receive refuses it.  A channel exists
// only for an offload that a DPU runs, so the fixture takes one by
// reference.

#include <crucible/cntp/_wip/Doca.h>
#include <fixy/Ctx.h>

#include <array>
#include <cstddef>

namespace doca = crucible::cntp::_wip::doca;

int recv_on_hot_path(doca::DpuCommChannel& channel);

int recv_on_hot_path(doca::DpuCommChannel& channel) {
    std::array<std::byte, 8> output{};
    const ::fixy::HotFgCtx hot = ::foundation::effects::testing::foreground();
    auto received = channel.recv_from_dpu(hot, output);
    return received.has_value() ? 0 : 1;
}

int main() { return 0; }
