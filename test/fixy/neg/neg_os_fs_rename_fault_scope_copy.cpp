// A RenameFaultScope is the one rename fault of its thread, and its end ends
// the fault.  A copy would end the fault two times, so the copy is deleted.

#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

int main() {
    fixy::TestRunnerCtx const test{eff::testing::test()};
    fs::RenameFaultScope const fault = fs::RenameFaultScope::arm(test, fs::RenameFailure::not_supported);
    fs::RenameFaultScope const copy{fault};
    return 0;
}
