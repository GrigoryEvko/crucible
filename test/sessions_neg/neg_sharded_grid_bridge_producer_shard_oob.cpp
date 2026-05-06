// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-082 fixture #3 — ShardId<I> is part of the bridge type.  A
// producer shard outside [0, M) must fail before any runtime handle or
// endpoint exists.

#include <crucible/concurrent/SubstrateSessionBridge.h>

namespace cc = crucible::concurrent;

namespace {
struct Tag {};
using Grid = cc::PermissionedShardedGrid<int, 2, 2, 32, Tag>;
using BadHandle = cc::handle_for_t<Grid, cc::Direction::Producer,
                                   cc::ShardId<5>>;
}

int main() {
    [[maybe_unused]] BadHandle* impossible = nullptr;
    return 0;
}
