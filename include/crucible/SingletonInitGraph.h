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
#include <fixy/atoms/Global.h>

#include <foundation/reflect/Instance.h>

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>

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

template <class T>
inline constexpr bool is_tuple_ = false;
template <class... Members>
inline constexpr bool is_tuple_<std::tuple<Members...>> = true;

// A registry entry is a singleton atom of fixy/atoms/Global.h.
template <class Entry>
inline constexpr bool is_singleton_atom_ =
    ::fixy::atom::IsAtom<Entry> && ::foundation::reflect::IsInstanceOf<Entry, ^^fg::singleton>;

// The position of Entry in the registry, or the size of the registry when
// the registry does not hold it.  Complexity: linear in the registry.
template <class Entry, class... Registered>
[[nodiscard]] consteval std::size_t position_of_(std::tuple<Registered...>* /*registry*/) noexcept {
    std::size_t position = 0;
    static_cast<void>(((std::is_same_v<Entry, Registered> ? false : (++position, true)) && ...));
    return position;
}

// Every entry is a singleton atom, and no entry occurs twice.  The empty
// registry holds no entry to read the parameter for.
// Complexity: quadratic in the registry.
template <class... Registered>
[[nodiscard]] consteval bool is_registry_([[maybe_unused]] std::tuple<Registered...>* registry) noexcept {
    std::size_t index = 0;
    bool valid = true;
    ((valid = valid && is_singleton_atom_<Registered> && position_of_<Registered>(registry) == index, ++index), ...);
    return valid;
}

// The two ends of every edge are in the registry.  The parameter deduces
// only a tuple of init_edge, so an edge of another type makes the call
// ill-formed, and the concept that makes the call is not satisfied.  The
// locals are unused when the pack of edges is empty.
template <class Registry, class... From, class... To>
[[nodiscard]] consteval bool edges_name_registered_(std::tuple<init_edge<From, To>...>* /*edges*/) noexcept {
    [[maybe_unused]] constexpr std::size_t nodes = std::tuple_size_v<Registry>;
    [[maybe_unused]] Registry* const registry = nullptr;
    return ((position_of_<From>(registry) < nodes && position_of_<To>(registry) < nodes) && ...);
}

// Kahn's walk: retire each node that no unretired node reaches, until
// no node retires.  The graph has no cycle when every node retires.
// Complexity: nodes times (nodes plus edges).
template <class Registry, class... From, class... To>
[[nodiscard]] consteval bool is_acyclic_(std::tuple<init_edge<From, To>...>* /*edges*/) noexcept {
    constexpr std::size_t nodes = std::tuple_size_v<Registry>;
    constexpr std::size_t edge_count = sizeof...(From);
    [[maybe_unused]] Registry* const registry = nullptr;
    const std::array<std::size_t, edge_count> sources{position_of_<From>(registry)...};
    const std::array<std::size_t, edge_count> targets{position_of_<To>(registry)...};

    std::array<std::size_t, nodes> in_degree{};
    for (std::size_t edge = 0; edge < edge_count; ++edge)
        ++in_degree[targets[edge]];

    std::array<bool, nodes> is_retired{};
    std::size_t retired_count = 0;
    bool has_progress = true;
    while (has_progress) {
        has_progress = false;
        for (std::size_t node = 0; node < nodes; ++node) {
            if (is_retired[node] || in_degree[node] != 0) continue;
            is_retired[node] = true;
            ++retired_count;
            has_progress = true;
            for (std::size_t edge = 0; edge < edge_count; ++edge) {
                if (sources[edge] == node) --in_degree[targets[edge]];
            }
        }
    }
    return retired_count == nodes;
}

}  // namespace detail

// A std::tuple of distinct singleton atoms.
template <class Registry>
concept SingletonRegistry = detail::is_tuple_<Registry> && detail::is_registry_(static_cast<Registry*>(nullptr));

// A std::tuple of init_edge, each of whose ends the registry holds.
template <class Registry, class Edges>
concept EdgesNameRegisteredSingletons = SingletonRegistry<Registry> && detail::is_tuple_<Edges>
                                     && detail::edges_name_registered_<Registry>(static_cast<Edges*>(nullptr));

// The registry and the edges form a graph with no cycle.
template <class Registry, class Edges>
concept AcyclicInitGraph =
    EdgesNameRegisteredSingletons<Registry, Edges> && detail::is_acyclic_<Registry>(static_cast<Edges*>(nullptr));

// The gate.  Naming init_graph<Registry, Edges> for a graph with a cycle,
// or with an edge to an unregistered singleton, fails the build.
template <class Registry, class Edges>
    requires AcyclicInitGraph<Registry, Edges>
struct init_graph final {
    using registry_type = Registry;
    using edges_type = Edges;
};

using registry = std::tuple<CKernelTableSingleton, HotRegionRegistrySingleton, BpfLogCbSingleton>;

// Empty, because no registered initializer reaches another singleton.
using init_edges = std::tuple<>;

using runtime_init_graph = init_graph<registry, init_edges>;

}  // namespace crucible::singleton_init_graph
