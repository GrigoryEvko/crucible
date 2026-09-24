// The head advancer opens a file, so a context without Block is
// refused.

#include <fixy/Path.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/CipherDurable.h>
#include <fixy/os/Fs.h>

#include <filesystem>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace durable = fixy::cipher::durable;
namespace atom_fs = fixy::atom::fs;
namespace src = fixy::tags::source;

namespace {
// Admits IO and Block: the row every call here needs.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
// Admits IO but NOT Block: what an init phase holds.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;

[[maybe_unused]] fixy::Path<src::Sanitized> sanitized(const char* raw) {
    return *fixy::sanitize::path_traversal::sanitize_path_no_dotdot(
        fixy::mint_tagged<src::External>(std::filesystem::path{raw}));
}
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::init()};
    [[maybe_unused]] auto r = durable::mint_head_advancer<>(ctx, sanitized("/tmp/fixy-neg-head"));
    return 0;
}
