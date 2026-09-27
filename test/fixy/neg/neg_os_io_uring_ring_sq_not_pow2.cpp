// The kernel rounds a submission count up to a power of two, so a ring
// sized by a count that is not one holds a different number of entries
// than the caller asked for.  The ring mint refuses such a count.

#include <fixy/os/Io.h>

namespace eff = foundation::effects;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using Engine = fixy::atom::io::engine<fixy::io::engine::IoUring>;
using Sq7 = fixy::atom::io::sq_entries<7>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fixy::io::mint_io_uring_ring<Engine, Sq7>(ctx);
    return 0;
}
