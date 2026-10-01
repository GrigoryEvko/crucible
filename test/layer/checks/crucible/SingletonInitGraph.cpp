// The compile-time checks of crucible/SingletonInitGraph.h.

#include <crucible/SingletonInitGraph.h>

namespace crucible::singleton_init_graph::detail::self_test {

static_assert(sizeof(runtime_init_graph) == 1);
static_assert(std::tuple_size_v<registry> == 3);

// The gate refuses a cycle, so the proof above cannot hold vacuously.
struct probe_first_tag final {};
struct probe_second_tag final {};
using ProbeFirst = fg::singleton<probe_first_tag>;
using ProbeSecond = fg::singleton<probe_second_tag>;
using ProbeRegistry = std::tuple<ProbeFirst, ProbeSecond>;

static_assert(AcyclicInitGraph<ProbeRegistry, std::tuple<init_edge<ProbeFirst, ProbeSecond>>>);
static_assert(!AcyclicInitGraph<ProbeRegistry, std::tuple<init_edge<ProbeFirst, ProbeFirst>>>);
static_assert(!AcyclicInitGraph<ProbeRegistry,
                                std::tuple<init_edge<ProbeFirst, ProbeSecond>, init_edge<ProbeSecond, ProbeFirst>>>);

}  // namespace crucible::singleton_init_graph::detail::self_test
