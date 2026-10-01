// The layout steps of crucible/vis/SugiyamaLayout.h.

#include <crucible/vis/SugiyamaLayout.h>

#include <crucible/vis/NetworkSimplex.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace crucible::vis {

namespace {

// The input nodes, then the virtual nodes, and the edges between adjacent
// layers. Each edge of the input appears once, as one edge or as one chain.
struct LayeredGraph {
    std::vector<LayoutNode> nodes;
    std::vector<std::vector<uint32_t>> successors;  // In the layer below.
    std::vector<std::vector<uint32_t>> predecessors;  // In the layer above.
    std::vector<std::vector<uint32_t>> layers;  // Left to right.
    uint32_t num_input_nodes = 0;
    uint32_t num_layers = 0;

    [[nodiscard]] bool is_virtual(uint32_t node) const { return node >= num_input_nodes; }
};

// Kahn's algorithm, with one change for a cycle: when no node is ready, the
// unplaced node of least index goes next. An unplaced node with no unplaced
// predecessor is always ready, so the forced node has one, and each edge from
// an unplaced predecessor into it then points back in the order. O(V + E).
[[nodiscard]] std::vector<uint32_t> order_breaking_cycles(uint32_t num_nodes, std::span<const LayoutEdge> edges) {
    std::vector<std::vector<uint32_t>> successors(num_nodes);
    std::vector<uint32_t> in_degree(num_nodes, 0);
    for (const auto& edge : edges) {
        successors[edge.src].push_back(edge.dst);
        ++in_degree[edge.dst];
    }
    std::vector<uint8_t> is_placed(num_nodes, 0);
    std::vector<uint32_t> ready;
    for (uint32_t node = 0; node < num_nodes; ++node) {
        if (in_degree[node] == 0) ready.push_back(node);
    }
    std::vector<uint32_t> order(num_nodes, 0);
    uint32_t next_cycle_candidate = 0;
    for (uint32_t position = 0; position < num_nodes; ++position) {
        if (ready.empty()) {
            while (is_placed[next_cycle_candidate] != 0)
                ++next_cycle_candidate;
            ready.push_back(next_cycle_candidate);
        }
        const uint32_t node = ready.back();
        ready.pop_back();
        is_placed[node] = 1;
        order[position] = node;
        for (const uint32_t successor : successors[node]) {
            if (is_placed[successor] == 0 && --in_degree[successor] == 0) ready.push_back(successor);
        }
    }
    return order;
}

// Orients each edge from the earlier node to the later one in the order,
// gives each node its longest-path layer, and replaces each edge that spans
// k > 1 layers with a chain through k - 1 virtual nodes. It drops an edge
// with an end out of range, and an edge with its two ends on one node.
// O(V + E + number of virtual nodes).
[[nodiscard]] LayeredGraph build_layered_graph(std::span<const LayoutNode> nodes, std::span<const LayoutEdge> edges) {
    const auto num_input = static_cast<uint32_t>(nodes.size());
    std::vector<LayoutEdge> valid_edges;
    for (const auto& edge : edges) {
        if (edge.src < num_input && edge.dst < num_input && edge.src != edge.dst) valid_edges.push_back(edge);
    }

    const std::vector<uint32_t> order = order_breaking_cycles(num_input, valid_edges);
    std::vector<uint32_t> position(num_input, 0);
    for (uint32_t index = 0; index < num_input; ++index)
        position[order[index]] = index;

    std::vector<std::vector<uint32_t>> later(num_input);
    for (auto& edge : valid_edges) {
        if (position[edge.src] > position[edge.dst]) std::swap(edge.src, edge.dst);
        later[edge.src].push_back(edge.dst);
    }
    std::vector<uint32_t> layer_of(num_input, 0);
    for (const uint32_t node : order) {
        for (const uint32_t successor : later[node])
            layer_of[successor] = std::max(layer_of[successor], layer_of[node] + 1);
    }

    LayeredGraph graph;
    graph.num_input_nodes = num_input;
    graph.num_layers = *std::ranges::max_element(layer_of) + 1;
    uint64_t num_virtual = 0;
    for (const auto& edge : valid_edges)
        num_virtual += layer_of[edge.dst] - layer_of[edge.src] - 1;
    // A graph whose virtual nodes do not fit a uint32_t index has no layout.
    contract_assert(num_input + num_virtual < std::numeric_limits<uint32_t>::max());

    LayoutNode virtual_node;
    virtual_node.lw = 2;
    virtual_node.rw = 2;
    virtual_node.min_height = 4;
    graph.nodes.assign(num_input + num_virtual, virtual_node);
    std::ranges::copy(nodes, graph.nodes.begin());
    for (uint32_t node = 0; node < num_input; ++node)
        graph.nodes[node].layer = layer_of[node];
    const auto num_nodes = static_cast<uint32_t>(graph.nodes.size());
    graph.successors.resize(num_nodes);
    graph.predecessors.resize(num_nodes);

    const auto link = [&graph](uint32_t upper, uint32_t lower) {
        graph.successors[upper].push_back(lower);
        graph.predecessors[lower].push_back(upper);
    };
    uint32_t next_virtual = num_input;
    for (const auto& edge : valid_edges) {
        uint32_t upper = edge.src;
        for (uint32_t layer = graph.nodes[edge.src].layer + 1; layer < graph.nodes[edge.dst].layer; ++layer) {
            graph.nodes[next_virtual].layer = layer;
            link(upper, next_virtual);
            upper = next_virtual++;
        }
        link(upper, edge.dst);
    }

    graph.layers.resize(graph.num_layers);
    for (uint32_t node = 0; node < num_nodes; ++node)
        graph.layers[graph.nodes[node].layer].push_back(node);
    for (const auto& layer : graph.layers) {
        for (uint32_t index = 0; index < layer.size(); ++index)
            graph.nodes[layer[index]].order = index;
    }
    return graph;
}

// Eight sweeps. A sweep down sorts each layer by the median order of the
// predecessors of its nodes, and a sweep up sorts by the successors. A node
// with no neighbor on that side keeps its own order as its key. The sort is
// stable, so equal keys keep their order.
// O(sweeps * (V log V + E log E)).
void minimize_crossings(LayeredGraph& graph) {
    std::vector<float> key(graph.nodes.size(), 0.0f);
    std::vector<float> neighbor_orders;
    const auto median_order = [&](uint32_t node, const std::vector<uint32_t>& neighbors) -> float {
        if (neighbors.empty()) return static_cast<float>(graph.nodes[node].order);
        neighbor_orders.clear();
        for (const uint32_t neighbor : neighbors)
            neighbor_orders.push_back(static_cast<float>(graph.nodes[neighbor].order));
        std::ranges::sort(neighbor_orders);
        const size_t middle = neighbor_orders.size() / 2;
        if (neighbor_orders.size() % 2 == 0) return (neighbor_orders[middle - 1] + neighbor_orders[middle]) / 2;
        return neighbor_orders[middle];
    };
    const auto sort_layer = [&](std::vector<uint32_t>& layer, const std::vector<std::vector<uint32_t>>& neighbors) {
        for (const uint32_t node : layer)
            key[node] = median_order(node, neighbors[node]);
        std::ranges::stable_sort(layer, [&key](uint32_t lhs, uint32_t rhs) { return key[lhs] < key[rhs]; });
        for (uint32_t index = 0; index < layer.size(); ++index)
            graph.nodes[layer[index]].order = index;
    };

    constexpr uint32_t kSweeps = 8;
    for (uint32_t sweep = 0; sweep < kSweeps; ++sweep) {
        for (uint32_t layer = 1; layer < graph.num_layers; ++layer)
            sort_layer(graph.layers[layer], graph.predecessors);
        for (uint32_t layer = graph.num_layers - 1; layer > 0; --layer)
            sort_layer(graph.layers[layer - 1], graph.successors);
    }
}

// The auxiliary graph of Gansner et al. Each pair of adjacent nodes in a
// layer gets a separation edge with no weight. Each edge (u, v) of the
// layered graph gets an auxiliary node a, with the edges a -> u and a -> v.
// At the optimum a sits at min(x(u), x(v)), so the two edges cost
// weight * |x(u) - x(v)|. The weight is 1 between two input nodes, 2 with one
// virtual end and 8 with two, which keeps a long edge straight. Each
// auxiliary node is a source and each separation edge runs left to right in
// one layer, so the graph is acyclic.
//
// Returns false, with no x set, when the auxiliary graph is too large for
// the solver or the solver does not get to the optimum.
[[nodiscard]] bool place_by_network_simplex(LayeredGraph& graph, const LayoutParams& params) {
    const auto num_nodes = static_cast<uint32_t>(graph.nodes.size());
    uint64_t num_layered_edges = 0;
    for (const auto& successors : graph.successors)
        num_layered_edges += successors.size();
    const uint64_t num_aux_nodes = uint64_t{num_nodes} + num_layered_edges;
    const uint64_t max_aux_edges = uint64_t{num_nodes} + 2 * num_layered_edges;
    if (num_aux_nodes >= std::numeric_limits<uint32_t>::max()
        || max_aux_edges > std::numeric_limits<uint32_t>::max() / 2) {
        return false;
    }

    std::vector<NSEdge> aux_edges;
    for (const auto& layer : graph.layers) {
        for (size_t index = 1; index < layer.size(); ++index) {
            const float separation =
                graph.nodes[layer[index - 1]].rw + graph.nodes[layer[index]].lw + params.node_h_gap;
            aux_edges.push_back({
                .tail = layer[index - 1],
                .head = layer[index],
                .minlen = static_cast<int32_t>(std::ceil(separation)),
                .weight = 0,
            });
        }
    }
    uint32_t aux_node = num_nodes;
    for (uint32_t upper = 0; upper < num_nodes; ++upper) {
        for (const uint32_t lower : graph.successors[upper]) {
            const int32_t num_virtual_ends = (graph.is_virtual(upper) ? 1 : 0) + (graph.is_virtual(lower) ? 1 : 0);
            const int32_t weight = num_virtual_ends == 0 ? 1 : (num_virtual_ends == 1 ? 2 : 8);
            aux_edges.push_back({.tail = aux_node, .head = upper, .minlen = 0, .weight = weight});
            aux_edges.push_back({.tail = aux_node, .head = lower, .minlen = 0, .weight = weight});
            ++aux_node;
        }
    }

    const NSResult result = network_simplex(aux_node, aux_edges);
    if (result.status != NSStatus::Optimal) return false;

    // A rank is the center of a node. The leftmost left edge goes to the
    // padding, so each center moves by one common offset.
    float least_left = std::numeric_limits<float>::max();
    for (uint32_t node = 0; node < num_nodes; ++node)
        least_left = std::min(least_left, static_cast<float>(result.rank[node]) - graph.nodes[node].lw);
    for (uint32_t node = 0; node < num_nodes; ++node)
        graph.nodes[node].x = params.padding + (static_cast<float>(result.rank[node]) - least_left);
    return true;
}

// Each layer is packed left to right and centered on the widest layer.
void place_naive(LayeredGraph& graph, const LayoutParams& params) {
    std::vector<float> layer_widths(graph.num_layers, 0.0f);
    for (uint32_t layer = 0; layer < graph.num_layers; ++layer) {
        float width = 0;
        for (const uint32_t node : graph.layers[layer])
            width += graph.nodes[node].width();
        if (!graph.layers[layer].empty())
            width += params.node_h_gap * static_cast<float>(graph.layers[layer].size() - 1);
        layer_widths[layer] = width;
    }
    const float max_layer_width = *std::ranges::max_element(layer_widths);
    for (uint32_t layer = 0; layer < graph.num_layers; ++layer) {
        float left = params.padding + (max_layer_width - layer_widths[layer]) / 2;
        for (const uint32_t node : graph.layers[layer]) {
            graph.nodes[node].x = left + graph.nodes[node].lw;
            left += graph.nodes[node].width() + params.node_h_gap;
        }
    }
}

// Four sweeps move each node 30% of the distance to the upper median x of its
// neighbors in the layer above, then in the layer below. After each sweep no
// two nodes of a layer sit closer than node_h_gap. One pass left to right is
// enough for that, because a node only moves right. The pass sorts each layer
// by x and sets the order again.
void nudge_to_medians(LayeredGraph& graph, const LayoutParams& params) {
    std::vector<float> neighbor_x;
    const auto pull_layer = [&](const std::vector<uint32_t>& layer,
                                const std::vector<std::vector<uint32_t>>& neighbors) {
        for (const uint32_t node : layer) {
            if (neighbors[node].empty()) continue;
            neighbor_x.clear();
            for (const uint32_t neighbor : neighbors[node])
                neighbor_x.push_back(graph.nodes[neighbor].x);
            std::ranges::sort(neighbor_x);
            graph.nodes[node].x = 0.7f * graph.nodes[node].x + 0.3f * neighbor_x[neighbor_x.size() / 2];
        }
    };
    const auto separate_layer = [&](std::vector<uint32_t>& layer) {
        std::ranges::stable_sort(
            layer, [&graph](uint32_t lhs, uint32_t rhs) { return graph.nodes[lhs].x < graph.nodes[rhs].x; });
        for (size_t index = 1; index < layer.size(); ++index) {
            const auto& left = graph.nodes[layer[index - 1]];
            auto& right = graph.nodes[layer[index]];
            right.x = std::max(right.x, left.x + left.rw + params.node_h_gap + right.lw);
        }
        for (uint32_t index = 0; index < layer.size(); ++index)
            graph.nodes[layer[index]].order = index;
    };

    constexpr uint32_t kSweeps = 4;
    for (uint32_t sweep = 0; sweep < kSweeps; ++sweep) {
        for (uint32_t layer = 1; layer < graph.num_layers; ++layer)
            pull_layer(graph.layers[layer], graph.predecessors);
        for (uint32_t layer = graph.num_layers - 1; layer > 0; --layer)
            pull_layer(graph.layers[layer - 1], graph.successors);
        for (auto& layer : graph.layers)
            separate_layer(layer);
    }
}

// Each layer is as tall as its tallest node, and y is the top of the layer.
// Returns the total height.
[[nodiscard]] float place_layers_vertically(LayeredGraph& graph, const LayoutParams& params) {
    std::vector<float> layer_heights(graph.num_layers, 0.0f);
    for (const auto& node : graph.nodes)
        layer_heights[node.layer] = std::max(layer_heights[node.layer], node.min_height);
    float top = params.padding;
    for (uint32_t layer = 0; layer < graph.num_layers; ++layer) {
        for (const uint32_t node : graph.layers[layer])
            graph.nodes[node].y = top;
        top += layer_heights[layer] + params.layer_v_gap;
    }
    return top - params.layer_v_gap + params.padding;
}

}  // namespace

bool is_well_formed_layout_input(std::span<const LayoutNode> nodes, const LayoutParams& params) noexcept {
    const auto is_extent = [](float value) { return value >= 0.0f && value <= kMaxLayoutExtent; };
    const auto is_node_sized = [&is_extent](const LayoutNode& node) {
        return is_extent(node.lw) && is_extent(node.rw) && is_extent(node.min_height);
    };
    const bool are_params_extents =
        is_extent(params.node_h_gap) && is_extent(params.layer_v_gap) && is_extent(params.padding);
    return nodes.size() < std::numeric_limits<uint32_t>::max() && are_params_extents
        && std::ranges::all_of(nodes, is_node_sized);
}

LayoutResult sugiyama_layout(std::span<const LayoutNode> nodes, std::span<const LayoutEdge> edges,
                             const LayoutParams& params) {
    if (nodes.empty()) return {};
    const auto num_input = static_cast<uint32_t>(nodes.size());
    LayeredGraph graph = build_layered_graph(nodes, edges);
    minimize_crossings(graph);

    // The simplex placement is optimal for its objective, so only the naive
    // placement gets the median nudge.
    const bool is_simplex_placed = params.use_network_simplex && place_by_network_simplex(graph, params);
    if (!is_simplex_placed) {
        place_naive(graph, params);
        nudge_to_medians(graph, params);
    }
    const float total_height = place_layers_vertically(graph, params);
    float max_right = 0;
    for (const auto& node : graph.nodes)
        max_right = std::max(max_right, node.x + node.rw);

    // The virtual nodes follow the input nodes, so the resize drops exactly
    // them.
    graph.nodes.resize(num_input);
    return LayoutResult{
        .nodes = std::move(graph.nodes),
        .total_width = max_right + params.padding,
        .total_height = total_height,
        .num_layers = graph.num_layers,
    };
}

}  // namespace crucible::vis
