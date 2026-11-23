#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>

// The new resource must be a SessionResource, as for any session mint.  An
// lvalue reference to a type that is not Pinned is refused: a later move of
// the channel would invalidate the new handle.  commit_sender is
// deprecated as a stub, and the pragma keeps that warning out, so the
// resource is the one reason this file does not compile.

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

struct Wire {
    int id = 0;
};

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;
    namespace sess = ::fixy::session;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    const ::fixy::TestRunnerCtx ctx{fe::testing::test()};
    auto swapper = cntp::mint_path_swapper(init);
    auto handle = sess::mint_session<sess::Send<int, sess::End>>(ctx, Wire{.id = 1});
    Wire next{.id = 2};
    auto result = swapper.commit_sender(bg, std::move(handle), next, cntp::SwapNanoseconds{0});
    (void)result;
    return 0;
}
