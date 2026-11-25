// A duplicate shares the position of its file, which read_full reads and moves.
// So a duplicate of a file with the read right does not compile: two handles
// would move one position.

#include <fixy/os/Fs.h>
#include <fixy/os/Handle.h>

namespace eff = foundation::effects;
namespace os = fixy::os;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    fixy::fs::TemporaryFile const file{};
    static_cast<void>(os::mint_duplicate<os::RightSet::read>(ctx, file.ref()));
    return 0;
}
