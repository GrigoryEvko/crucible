#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <foundation/Quarantine.h>

// A value resource that can be copied must be self-contained.  A resource
// that holds a raw pointer reaches outside itself, so a copy of it is a
// second channel to the same peer, and the swap refuses it.  commit_sender is
// deprecated as a stub, and the pragma keeps that warning out, so the
// resource is the one reason this file does not compile.

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

struct Wire {
    int id = 0;
};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives the swap a resource that holds a raw pointer")
struct PointerWire {
    int* peer = nullptr;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;
    namespace sess = ::fixy::session;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    const ::fixy::TestRunnerCtx ctx{fe::testing::test()};
    auto swapper = cntp::mint_path_swapper(init);
    auto handle = sess::mint_session<sess::Send<int, sess::End>>(ctx, Wire{.id = 1});
    int peer_state = 0;
    auto result =
        swapper.commit_sender(bg, std::move(handle), PointerWire{.peer = &peer_state}, cntp::SwapNanoseconds{0});
    (void)result;
    return 0;
}
