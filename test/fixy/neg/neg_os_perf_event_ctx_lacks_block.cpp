// mint_perf_event needs a context that admits Block as well as IO.  The gate
// reads the row off the perf_event_open syscall atom, and the catalog puts
// perf_event_open in the privilege family, which lifts to IO and Block.  A
// context that holds IO alone is refused at the requires-clause.

#include <fixy/os/PerfEvent.h>
#include <fixy/os/Process.h>

namespace eff = foundation::effects;
namespace perf = fixy::perf;

namespace {
// Admits IO but NOT Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::init()};
    perf::Event const migrations = perf::software_event(perf::SoftwareEvent::cpu_migrations);
    [[maybe_unused]] auto event = perf::mint_perf_event(ctx, migrations, 1, fixy::process::current_process(ctx));
    return 0;
}
