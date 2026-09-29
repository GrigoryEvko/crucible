// commit_atomic<atomicity::None> renames nothing, so a success from it
// would say that a commit was made when none was.  A caller that needs
// no commit does not call commit_atomic, and the gate refuses None.

#include <fixy/Path.h>
#include <fixy/os/Fs.h>

#include <filesystem>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(const char* raw) {
    return *fixy::sanitize::path_traversal::sanitize_path_no_dotdot(
        fixy::mint_tagged<src::External>(std::filesystem::path{raw}));
}
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fs::commit_atomic<fs::atomicity::None>(ctx, sanitized("/tmp/fixy-neg-none.tmp"),
                                                                           sanitized("/tmp/fixy-neg-none"));
    return 0;
}
