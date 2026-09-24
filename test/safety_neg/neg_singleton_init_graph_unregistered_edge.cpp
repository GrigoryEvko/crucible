// An edge to a singleton that the registry does not hold.  The old
// detector dropped such an edge and reported the graph acyclic.  The gate
// refuses it at its constraint.

#include <crucible/SingletonInitGraph.h>

#include <tuple>

namespace neg_singleton_init_graph_unregistered_edge_types {
struct registered_tag final {};
struct outside_tag final {};
using Registered = ::fixy::atom::global::singleton<registered_tag>;
using Outside = ::fixy::atom::global::singleton<outside_tag>;
using Edges = std::tuple<::crucible::singleton_init_graph::init_edge<Registered, Outside>>;
}  // namespace neg_singleton_init_graph_unregistered_edge_types

namespace types = neg_singleton_init_graph_unregistered_edge_types;

int main() {
    return sizeof(::crucible::singleton_init_graph::init_graph<std::tuple<types::Registered>, types::Edges>) == 1 ? 0
                                                                                                                 : 1;
}
