#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// A commit is a background transition.  The foreground context owns no Bg,
// so it cannot move a live session handle to a new transport resource.
// commit_sender is deprecated as a stub, and the pragma keeps that warning
// out, so the context is the one reason this file does not compile.

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

struct Wire {
    int id = 0;
};

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;
    namespace sess = ::fixy::session;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::HotFgCtx fg{fe::testing::foreground()};
    auto swapper = cntp::mint_path_swapper(init);
    auto handle = sess::mint_session_handle<sess::Send<int, sess::End>>(Wire{.id = 1});
    auto result = swapper.commit_sender(fg, std::move(handle), Wire{.id = 2}, 0);
    (void)result;
    return 0;
}
