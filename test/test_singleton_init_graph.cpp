// The registry of runtime singletons and the gate on their lazy-init graph.
// The gate must accept the real registry, and it must refuse a cycle, an
// edge to an unregistered singleton and a registry that is not a set of
// singleton atoms.  Each refusal below is what keeps the acceptance of the
// real registry from being vacuous.

#include <crucible/SingletonInitGraph.h>

#include <fixy/atoms/Global.h>
#include <fixy/Axis.h>

#include <tuple>
#include <type_traits>

// The tags of the probe singletons.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it.
namespace singleton_graph_tags {
struct first_tag final {};
struct second_tag final {};
struct third_tag final {};
struct outside_tag final {};
}  // namespace singleton_graph_tags

namespace {

namespace sig = ::crucible::singleton_init_graph;
namespace fg = ::fixy::atom::global;

// The real registry: three distinct singleton atoms on the global-state
// axis, with no edge between them.
static_assert(std::tuple_size_v<sig::registry> == 3);
static_assert(std::tuple_size_v<sig::init_edges> == 0);
static_assert(sig::AcyclicInitGraph<sig::registry, sig::init_edges>);
static_assert(std::is_same_v<sig::runtime_init_graph::registry_type, sig::registry>);
static_assert(sig::CKernelTableSingleton::axis == ::fixy::Axis::GlobalState);
static_assert(!std::is_same_v<sig::CKernelTableSingleton, sig::HotRegionRegistrySingleton>);
static_assert(!std::is_same_v<sig::HotRegionRegistrySingleton, sig::BpfLogCbSingleton>);
static_assert(!std::is_same_v<sig::CKernelTableSingleton, sig::BpfLogCbSingleton>);

using singleton_graph_tags::first_tag;
using First = fg::singleton<first_tag>;
using Second = fg::singleton<singleton_graph_tags::second_tag>;
using Third = fg::singleton<singleton_graph_tags::third_tag>;
using Outside = fg::singleton<singleton_graph_tags::outside_tag>;
using Three = std::tuple<First, Second, Third>;

// A chain has no cycle.  A self-loop and a three-cycle have one.
static_assert(sig::AcyclicInitGraph<Three, std::tuple<sig::init_edge<First, Second>, sig::init_edge<Second, Third>>>);
static_assert(!sig::AcyclicInitGraph<Three, std::tuple<sig::init_edge<Second, Second>>>);
static_assert(!sig::AcyclicInitGraph<Three, std::tuple<sig::init_edge<First, Second>, sig::init_edge<Second, Third>,
                                                       sig::init_edge<Third, First>>>);

// Two paths into one node are not a cycle, and a repeated edge is not one.
static_assert(sig::AcyclicInitGraph<Three, std::tuple<sig::init_edge<First, Third>, sig::init_edge<Second, Third>>>);
static_assert(sig::AcyclicInitGraph<Three, std::tuple<sig::init_edge<First, Second>, sig::init_edge<First, Second>>>);

// An edge to a singleton outside the registry is refused, at either end.
// The old detector dropped such an edge and reported the graph acyclic.
static_assert(!sig::EdgesNameRegisteredSingletons<Three, std::tuple<sig::init_edge<First, Outside>>>);
static_assert(!sig::EdgesNameRegisteredSingletons<Three, std::tuple<sig::init_edge<Outside, First>>>);
static_assert(!sig::AcyclicInitGraph<Three, std::tuple<sig::init_edge<First, Outside>>>);

// An edge must be an init_edge, and the edges must be a tuple.
static_assert(!sig::EdgesNameRegisteredSingletons<Three, std::tuple<std::pair<First, Second>>>);
static_assert(!sig::EdgesNameRegisteredSingletons<Three, sig::init_edge<First, Second>>);

// A registry holds each singleton one time, holds only singleton atoms,
// and is a tuple.
static_assert(sig::SingletonRegistry<Three>);
static_assert(sig::SingletonRegistry<std::tuple<>>);
static_assert(!sig::SingletonRegistry<std::tuple<First, First>>);
static_assert(!sig::SingletonRegistry<std::tuple<First, fg::thread_local_<first_tag>>>);
static_assert(!sig::SingletonRegistry<std::tuple<First, int>>);
static_assert(!sig::SingletonRegistry<First>);

}  // namespace

int main() {
    // Every claim here is a compile-time one.  The runtime part is the
    // one byte of the gate type, which names the registry and holds no
    // state.
    return sizeof(::crucible::singleton_init_graph::runtime_init_graph) == 1 ? 0 : 1;
}
