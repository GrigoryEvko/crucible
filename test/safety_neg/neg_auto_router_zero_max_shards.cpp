// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AutoRouter's static shard cap is a compile-time routing fact.
// MaxShards=0 must be rejected at instantiation, not repaired by a
// runtime branch.

#include <crucible/concurrent/AutoRouter.h>

namespace {
struct RouteTag {};
}

using BadRoute = crucible::concurrent::StaticAutoRoute_t<
    crucible::concurrent::RouteIntent::Shardable,
    int,
    16,
    RouteTag,
    1,
    1,
    64ULL * 1024ULL * 1024ULL,
    0>;

[[maybe_unused]] BadRoute* force_instantiation = nullptr;

int main() { return 0; }
