// A class that owns two runs is no input of a parallel loop (reads() of
// fixy/concurrent/Spawn.h).  The runner compares the bytes of one run with the
// region of the shards, and it cannot see each run of such a class.  The caller
// gives the View of each run.

#include "../parallel_loop_rig.h"

namespace {

namespace rig = ::parallel_loop_rig;

struct TwoRuns {
    ::fixy::HeapArray<rig::Word> first{};
    ::fixy::HeapArray<rig::Word> second{};
};

}  // namespace

int main() {
    TwoRuns const runs{rig::make_items(), rig::make_items()};
    [[maybe_unused]] auto inputs = ::fixy::spawn::reads(runs);
    return 0;
}
