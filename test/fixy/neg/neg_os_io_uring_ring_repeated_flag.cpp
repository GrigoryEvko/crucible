// A ring flag names one IORING_SETUP_* bit.  A pack that names one flag
// two times names nothing new, and it is the mark of a pack assembled
// wrong, so the ring mint refuses it.  Two different flags fold together
// and are admitted.

#include <fixy/os/Io.h>

namespace eff = foundation::effects;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using Engine = fixy::atom::io::engine<fixy::io::engine::IoUring>;
using Sq8 = fixy::atom::io::sq_entries<8>;
using SqPoll = fixy::atom::io::ring_flag<fixy::io::ring_flag::SqPoll>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fixy::io::mint_io_uring_ring<Engine, Sq8, SqPoll, SqPoll>(ctx);
    return 0;
}
