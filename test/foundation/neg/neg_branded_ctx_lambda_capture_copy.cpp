// A branded foreground context does not go into a lambda by value.
//
// VIOLATION: a lambda captures the context by value.  The lambda can run
// on a thread that holds no claim of the brand, and the copy there is
// evidence of a claim that the thread does not hold.
//
// Expected diagnostic: the copy constructor of the context is deleted,
// because the copy constructor of the branded source is deleted.

#include <foundation/effects/Ctx.h>

namespace {
struct Brand {};
}  // namespace

int main() {
    namespace fe = ::foundation::effects;
    const auto fg = fe::testing::foreground<Brand>();
    auto task = [fg] { static_cast<void>(fg); };
    task();
    return 0;
}
