// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// dispatch_auto_split fans out by copying the body into scheduler jobs.
// Move-only bodies must therefore fail at the AutoSplitShardBody
// boundary instead of entering the queue machinery.

#include <crucible/concurrent/AutoSplit.h>

namespace cc = crucible::concurrent;

namespace {

struct MoveOnlyBody {
    MoveOnlyBody() = default;
    MoveOnlyBody(const MoveOnlyBody&) = delete;
    MoveOnlyBody& operator=(const MoveOnlyBody&) = delete;
    MoveOnlyBody(MoveOnlyBody&&) = default;
    MoveOnlyBody& operator=(MoveOnlyBody&&) = default;

    void operator()(cc::AutoSplitShard) noexcept {}
};

void misuse(cc::Pool<cc::scheduler::Fifo>& pool) {
    (void)cc::dispatch_auto_split(
        pool,
        cc::AutoSplitRequest{.item_count = 16, .bytes_per_item = 64},
        MoveOnlyBody{});
}

}  // namespace

int main() { return 0; }
