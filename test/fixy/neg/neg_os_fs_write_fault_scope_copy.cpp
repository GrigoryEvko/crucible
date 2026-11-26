// A WriteFaultScope is the one write fault of its thread, and its end ends
// the fault.  A copy would end the fault two times, so the copy is deleted.

#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>

#include <foundation/core/Quantity.h>
#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

int main() {
    fixy::TestRunnerCtx const test{eff::testing::test()};
    fs::WriteFaultScope const fault =
        fs::WriteFaultScope::arm(test, foundation::core::ByteCount{0}, fs::WriteFailure::no_space);
    fs::WriteFaultScope const copy{fault};
    return 0;
}
