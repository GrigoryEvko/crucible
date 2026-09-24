// The Permission is the witness that the caller owns what the gate
// protects.  A guard built without one has nothing to prove.

#include <fixy/os/SpinLock.h>

namespace eff = foundation::effects;

namespace {
struct GateTag {
    using permission_row = ::foundation::effects::Row<>;
};
using ForegroundCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
}  // namespace

int main() {
    ForegroundCtx ctx{eff::testing::test()};
    fixy::spin::SpinLock<GateTag> gate{};
    fixy::spin::SpinGuard<GateTag> guard{ctx, gate};
    return 0;
}
