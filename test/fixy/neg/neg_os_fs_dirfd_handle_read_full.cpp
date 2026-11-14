// A Dirfd gives no reference to the descriptor that it holds.  So a door of
// a descriptor, such as read_full, takes no directory: the call names a
// member that Dirfd does not have.

#include <fixy/os/Fs.h>
#include <foundation/core/Array.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of each call of the file surface.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::Dirfd const dir{};
    foundation::core::FixedArray<std::byte, 4> bytes{};
    [[maybe_unused]] auto read = fs::read_full(ctx, dir.handle(), bytes.view());
    return 0;
}
