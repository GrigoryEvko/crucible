// The Permission is the witness that the caller owns what the gate
// protects, on the blocking gate as on the spin gate.  A guard built
// without one has nothing to prove, even under a context that may block.

#include <fixy/os/SpinLock.h>

namespace eff = foundation::effects;

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct GateBrand {};
using BgLoadCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    BgLoadCtx ctx{eff::testing::bg()};
    fixy::spin::BlockingLock<GateTag> gate{};
    fixy::spin::BlockingGuard<GateTag, GateBrand> guard{ctx, gate};
    return 0;
}
