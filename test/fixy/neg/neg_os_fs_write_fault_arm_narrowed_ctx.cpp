// WriteFaultScope::arm takes a context whose row holds Test.  A test context
// that a caller narrowed to the row of a file call keeps the test source, and
// write_full reads the fault for it, but the narrowed row claims no Test, so
// the arm refuses it at its signature.

#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>

#include <foundation/core/Quantity.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
using FileRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
}  // namespace

int main() {
    fixy::TestRunnerCtx const test{eff::testing::test()};
    auto const narrowed = test.in_row<FileRow>();
    fs::WriteFaultScope const fault =
        fs::WriteFaultScope::arm(narrowed, foundation::core::ByteCount{0}, fs::WriteFailure::no_space);
    return 0;
}
