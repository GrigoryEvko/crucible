// The commit of a durable replace is Rename or RenameAt2NoReplace
// (ReplaceCommit).  atomicity::None renames nothing, so a replace with it
// would leave its bytes in the temporary file, and the door refuses it at its
// signature.

#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>

#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

// The directory of the replace.  The fixture does not link, so no body is
// necessary, and no mint call makes the directory.
fs::DirectoryHandle const& fixture_directory() noexcept;

int main() {
    fixy::TestRunnerCtx const test{eff::testing::test()};
    [[maybe_unused]] auto const replaced = fs::replace_file_at<fs::atomicity::None>(
        test, fixture_directory().ref(), "file", foundation::core::TextView{"bytes"});
    return 0;
}
