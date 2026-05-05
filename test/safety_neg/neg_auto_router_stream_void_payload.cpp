// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AutoRouter must reject payloads at its own boundary instead of
// relying on whichever Permissioned* substrate happens to instantiate
// later.  A void stream payload is not SpscValue<T>.

#include <crucible/concurrent/AutoRouter.h>

namespace {
struct RouteTag {};
}

using BadRoute = crucible::concurrent::AutoRoute_t<
    crucible::concurrent::RouteIntent::Stream,
    void,
    16,
    RouteTag,
    1,
    1,
    1024>;

[[maybe_unused]] BadRoute* force_instantiation = nullptr;

int main() { return 0; }
