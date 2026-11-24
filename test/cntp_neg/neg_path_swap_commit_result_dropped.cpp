#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>

// A commit of the sender gives the new handle, or a refusal that holds the
// handle of the old path.  A caller that drops the Result would end one of
// the two sessions in silence, so the Result is a linear sum and the drop is
// refused.  commit_sender is deprecated as a stub, and the pragma keeps that
// warning out, so the drop is the one reason this file does not compile.

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
    auto swapper = cntp::mint_path_swapper(init);
    auto handle = sess::mint_session<sess::Send<int, sess::End>>(bg, Wire{.id = 1});
    static_cast<void>(swapper.commit_sender(bg, std::move(handle), Wire{.id = 2}, cntp::SwapNanoseconds{0}));
    return 0;
}
