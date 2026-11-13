// read_count needs a context that admits Block as well as IO.  The gate reads
// the row off the read syscall atom, and the catalog puts read in the file
// family, which lifts to IO and Block.  A context that holds IO alone is
// refused at the requires-clause.  The handle is empty, and nothing here is
// ever run.

#include <fixy/os/PerfEvent.h>

namespace eff = foundation::effects;
namespace perf = fixy::perf;

namespace {
// Admits IO but NOT Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::init()};
    fixy::fs::OwnedFd const counter{};
    [[maybe_unused]] auto count = perf::read_count(ctx, counter);
    return 0;
}
