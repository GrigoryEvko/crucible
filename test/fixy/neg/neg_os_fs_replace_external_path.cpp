// replace_file takes Path<Sanitized>, so a path that no sanitizer checked is
// refused at its signature.

#include <fixy/os/Path.h>
#include <fixy/os/Fs.h>

#include <foundation/os/PathText.h>

#include <cstddef>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {
// Admits IO and Block: the row every call here needs.
using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto r =
        fs::replace_file(ctx, fixy::mint_tagged<src::External>(foundation::os::PathText<fixy::kPathBytes>{}),
                         foundation::core::View<std::byte const>{});
    return 0;
}
