// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AutoRouter cardinality is a compile-time routing fact on the
// StaticAutoRoute / AutoRoute_t surface.  Producers=0 must be rejected
// at instantiation, not normalized in runtime planner code.

#include <crucible/concurrent/AutoRouter.h>

namespace {
struct RouteTag {};
}

using BadRoute = crucible::concurrent::AutoRoute_t<
    crucible::concurrent::RouteIntent::Stream,
    int,
    16,
    RouteTag,
    0,
    1,
    1024>;

[[maybe_unused]] BadRoute* force_instantiation = nullptr;

int main() { return 0; }
