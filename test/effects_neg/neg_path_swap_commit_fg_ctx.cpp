#include <crucible/cntp/PathSwap.h>

// GAPS-122 fixture #2: committing a path swap is a runtime background
// transition. Foreground hot-path code may not rewrite the transport
// resource under a live session handle.

struct Wire {
    int id = 0;
};

int main() {
    namespace cntp = crucible::cntp;
    namespace proto = crucible::safety::proto;

    crucible::effects::ColdInitCtx init{};
    crucible::effects::HotFgCtx fg{};
    auto swapper = cntp::mint_path_swapper(init);
    auto handle = proto::mint_session_handle<proto::Send<int, proto::End>>(
        Wire{.id = 1});
    auto result = swapper.commit_sender(fg, std::move(handle), Wire{.id = 2}, 0);
    (void)result;
    return 0;
}
