// The compile-time checks of fixy/concurrent/Topology.h.

#include <fixy/concurrent/Topology.h>

namespace fixy::concurrent {

static_assert(std::is_class_v<Topology>);
// The address of the singleton is the identity of the probed data, so it
// neither copies nor moves.
static_assert(!std::is_copy_constructible_v<Topology>);
static_assert(!std::is_move_constructible_v<Topology>);

}  // namespace fixy::concurrent
