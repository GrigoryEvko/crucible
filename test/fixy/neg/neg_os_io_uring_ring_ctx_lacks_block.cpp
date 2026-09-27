// io_uring_setup and the three ring mappings can park the caller, so the
// io atoms lift to IO and Block, and the ring mint asks for a context
// that owns both.  A context with IO and no Block is refused.

#include <fixy/os/Io.h>

namespace eff = foundation::effects;

namespace {
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
using Engine = fixy::atom::io::engine<fixy::io::engine::IoUring>;
using Sq8 = fixy::atom::io::sq_entries<8>;
}  // namespace

int main() {
    IoOnlyCtx const ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fixy::io::mint_io_uring_ring<Engine, Sq8>(ctx);
    return 0;
}
