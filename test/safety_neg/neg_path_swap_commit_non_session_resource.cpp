#include <crucible/cntp/PathSwap.h>

// GAPS-122 fixture #3: the new path resource must satisfy the same
// SessionResource pin discipline as ordinary SessionHandle minting.
// Non-pinned lvalue references are rejected because a later move of the
// channel would dangle the swapped handle.

struct Wire {
    int id = 0;
};

int main() {
    namespace cntp = crucible::cntp;
    namespace proto = crucible::safety::proto;

    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};
    auto swapper = cntp::mint_path_swapper(init);
    auto handle = proto::mint_session_handle<proto::Send<int, proto::End>>(
        Wire{.id = 1});
    Wire next{.id = 2};
    auto result = swapper.commit_sender(bg, std::move(handle), next, 0);
    (void)result;
    return 0;
}
