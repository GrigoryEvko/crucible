// A count of a perf event is the difference of two reads, and a call of the
// right ioctl_perf of a second handle can stop or reset the event between the
// two.  So a perf event gives no duplicate.

#include <fixy/os/Handle.h>
#include <fixy/os/PerfEvent.h>

namespace eff = foundation::effects;
namespace os = fixy::os;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    fixy::perf::PerfEventHandle const event{};
    static_cast<void>(os::mint_duplicate<os::RightSet::read>(ctx, event.ref()));
    return 0;
}
