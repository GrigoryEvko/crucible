#pragma once

// Every function-local static singleton of the runtime is registered here,
// and the build proves that their lazy-initialization graph has no cycle.
// A cycle is the lazy form of the static-initialization-order problem: the
// first thread to touch either of two singletons that need each other
// enters the initializer again and reads a peer that is half constructed.
//
// Register the tag of each singleton here when you add one, together with
// each edge that its initializer makes.  An edge init_edge<From, To> reads
// "the initializer of From touches the singleton To".
//
// The registry is central, not an annotation at each singleton.  An
// annotation would put fixy into the include graph of the headers that
// hold the singletons, and one of them is hot and widely included.  Here
// the graph stays in one cold header, and all of it is visible at once.
//
// An edge names two singleton atoms.  An edge that names a singleton
// outside the registry fails the gate.

#include <fixy/Atom.h>
#include <fixy/Core.h>
#include <fixy/atoms/Global.h>

#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <cstdint>

namespace crucible::singleton_init_graph {

namespace fg = ::fixy::atom::global;

struct CKernelTableTag final {};  // global_ckernel_table()
struct HotRegionRegistryTag final {};  // HotRegionRegistry::instance()
struct BpfLogCbTag final {};  // install_libbpf_log_cb_once()

using CKernelTableSingleton = fg::singleton<CKernelTableTag>;
using HotRegionRegistrySingleton = fg::singleton<HotRegionRegistryTag>;
using BpfLogCbSingleton = fg::singleton<BpfLogCbTag>;

// The initializer of From touches the singleton To.
template <class From, class To>
struct init_edge final {
    using from_type = From;
    using to_type = To;
};

namespace detail {

// The largest registry: the walk below holds one bit for each node.
inline constexpr std::size_t max_registry_size = 64;

// A registry entry is a singleton atom of fixy/atoms/Global.h.
template <class Entry>
inline constexpr bool is_singleton_atom_ =
    ::fixy::atom::IsAtom<Entry> && ::foundation::reflect::IsInstanceOf<Entry, ^^fg::singleton>;

// A Pack of at most max_registry_size entries, each a singleton atom, and
// no entry two times.  Each other type is no registry.
template <class Registry>
inline constexpr bool is_registry_ = false;
template <class... Registered>
inline constexpr bool is_registry_<::fixy::Pack<Registered...>> =
    sizeof...(Registered) <= max_registry_size && (is_singleton_atom_<Registered> && ...)
    && ::fixy::pack_unique_v<::fixy::Pack<Registered...>>;

// A Pack of init_edge whose two ends are each in the registry.  Only a Pack
// of init_edge matches the partial specialization, so an edge of a
// different type, or edges that are no Pack, give false.
template <class Registry, class Edges>
inline constexpr bool edges_name_registered_ = false;
template <class Registry, class... From, class... To>
inline constexpr bool edges_name_registered_<Registry, ::fixy::Pack<init_edge<From, To>...>> =
    ((::fixy::pack_contains_v<From, Registry> && ::fixy::pack_contains_v<To, Registry>) && ...);

// Kahn's walk over the set of the retired nodes, one bit for each node:
// retire each node whose edges in all come from a retired node, until no
// node retires.  The graph has no cycle when each node retires.  The caller
// proves that each end of each edge is in the registry one time.
// Complexity: nodes times (nodes plus edges).
template <class Registry, class... From, class... To>
[[nodiscard]] consteval bool retires_each_node_(::fixy::Pack<init_edge<From, To>...> /*edges*/) noexcept {
    constexpr std::size_t nodes = ::fixy::pack_size_v<Registry>;
    std::uint64_t retired = 0;
    bool has_progress = true;
    while (has_progress) {
        has_progress = false;
        for (std::size_t node = 0; node < nodes; ++node) {
            const std::uint64_t node_bit = std::uint64_t{1} << node;
            // A fold over the comma, because two equal edges would give a
            // fold over || with two equal operands.
            bool is_blocked = false;
            ((is_blocked = is_blocked
                        || (::fixy::pack_index_of_v<To, Registry> == node
                            && (retired & (std::uint64_t{1} << ::fixy::pack_index_of_v<From, Registry>)) == 0)),
             ...);
            if ((retired & node_bit) != 0 || is_blocked) continue;
            retired |= node_bit;
            has_progress = true;
        }
    }
    return retired == ::fixy::low_mask<std::uint64_t>(static_cast<int>(nodes));
}

}  // namespace detail

// A Pack of distinct singleton atoms, at most max_registry_size of them.
template <class Registry>
concept SingletonRegistry = detail::is_registry_<Registry>;

// A Pack of init_edge, each of whose ends the registry holds.
template <class Registry, class Edges>
concept EdgesNameRegisteredSingletons = SingletonRegistry<Registry> && detail::edges_name_registered_<Registry, Edges>;

// The registry and the edges form a graph with no cycle.
template <class Registry, class Edges>
concept AcyclicInitGraph =
    EdgesNameRegisteredSingletons<Registry, Edges> && detail::retires_each_node_<Registry>(Edges{});

// The gate.  Naming init_graph<Registry, Edges> for a graph with a cycle,
// or with an edge to an unregistered singleton, fails the build.
template <class Registry, class Edges>
    requires AcyclicInitGraph<Registry, Edges>
struct init_graph final {
    using registry_type = Registry;
    using edges_type = Edges;
};

using registry = ::fixy::Pack<CKernelTableSingleton, HotRegionRegistrySingleton, BpfLogCbSingleton>;

// Empty, because no registered initializer reaches another singleton.
using init_edges = ::fixy::Pack<>;

using runtime_init_graph = init_graph<registry, init_edges>;

}  // namespace crucible::singleton_init_graph
