// A directory holds no read right (the rights of fixy/os/Handle.h).  So the
// full read, which asks for the read right, takes no borrow of a directory.

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
    fs::DirectoryHandle const dir{};
    auto [bytes, bytes_key] = foundation::core::mint_keyed_owner(foundation::core::fixed_array_owner<std::byte, 4>);
    bytes.with_mut(bytes_key, [&ctx, &dir](auto run) noexcept {
        [[maybe_unused]] auto read = fs::read_full(ctx, dir.ref(), std::move(run));
    });
    return 0;
}
