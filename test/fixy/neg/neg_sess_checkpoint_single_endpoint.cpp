// A checkpoint protocol runs only as one side of a checked pair.  The
// plain factory, which sees one endpoint, refuses it, so no endpoint can
// take a rollback decision that its peer never checked.

#include <fixy/Ctx.h>
#include <fixy/session/Checkpoint.h>
#include <fixy/session/Entry.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Decide>(ctx, Wire{});
    (void)handle;
    return 0;
}
