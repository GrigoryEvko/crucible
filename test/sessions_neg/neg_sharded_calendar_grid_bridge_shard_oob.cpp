// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-084 fixture #3 - ShardId<S> is part of the bridge type.  A shard
// outside [0, NumShards) must fail before any runtime handle exists.

#include <crucible/concurrent/SubstrateSessionBridge.h>

#include <cstdint>

namespace cc = crucible::concurrent;

namespace {
struct Tag {};
struct Key {
    static std::uint64_t key(int value) noexcept {
        return static_cast<std::uint64_t>(value);
    }
};
using Grid = cc::PermissionedShardedCalendarGrid<int, 2, 8, 16, Key, 1ULL, Tag>;
using BadHandle = cc::handle_for_t<Grid, cc::Direction::Producer,
                                   cc::ShardId<5>>;
}

int main() {
    [[maybe_unused]] BadHandle* impossible = nullptr;
    return 0;
}
