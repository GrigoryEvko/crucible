// mint_perf_event needs a context that admits IO as well as Block.  This
// context owns Block and not IO, so the mint refuses it at the row clause of
// its gate.  The id of the process is a constant, because the identity read
// also needs IO, and nothing here is ever run.

#include <fixy/os/PerfEvent.h>
#include <fixy/os/Process.h>

namespace eff = foundation::effects;
namespace perf = fixy::perf;

namespace {
using BlockOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Block>>;
}  // namespace

int main() {
    BlockOnlyCtx ctx{eff::testing::test()};
    perf::Event const migrations = perf::software_event(perf::SoftwareEvent::cpu_migrations);
    [[maybe_unused]] auto event = perf::mint_perf_event(ctx, migrations, 1, fixy::process::ProcessId{1});
    return 0;
}
