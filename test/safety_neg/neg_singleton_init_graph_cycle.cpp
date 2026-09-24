// Two singletons whose initializers touch each other: the lazy form of
// the static-initialization-order problem.  The gate refuses the graph at
// its constraint.

#include <crucible/SingletonInitGraph.h>

#include <tuple>

namespace neg_singleton_init_graph_cycle_types {
struct first_tag final {};
struct second_tag final {};
using First = ::fixy::atom::global::singleton<first_tag>;
using Second = ::fixy::atom::global::singleton<second_tag>;
using Edges = std::tuple<::crucible::singleton_init_graph::init_edge<First, Second>,
                         ::crucible::singleton_init_graph::init_edge<Second, First>>;
}  // namespace neg_singleton_init_graph_cycle_types

namespace types = neg_singleton_init_graph_cycle_types;

int main() {
    return sizeof(::crucible::singleton_init_graph::init_graph<std::tuple<types::First, types::Second>, types::Edges>)
                == 1
             ? 0
             : 1;
}
