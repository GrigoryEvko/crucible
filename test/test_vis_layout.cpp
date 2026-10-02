// Checks of the rank solver and the layered layout of include/crucible/vis.
//
// Each check prints a line for each property that does not hold, and main
// returns nonzero if one check fails. The solver checks compare its cost with
// an exhaustive search over small graphs.

#include <crucible/vis/NetworkSimplex.h>
#include <crucible/vis/SugiyamaLayout.h>
#include <crucible/vis/TraceVisualizer.h>

#include "test_abort_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <utility>
#include <vector>
#include "test_assert.h"

namespace {

namespace vis = crucible::vis;

class Checker {
public:
    void expect(bool is_true, const char* check, const char* property) {
        if (is_true) return;
        std::fprintf(stderr, "FAIL: %s: %s\n", check, property);
        ++failed_;
    }

    [[nodiscard]] int exit_code() const { return failed_ == 0 ? 0 : 1; }

private:
    uint32_t failed_ = 0;
};

[[nodiscard]] bool is_near(float lhs, float rhs) { return std::abs(lhs - rhs) < 1.0e-3f; }

// A deterministic generator for the random instances. The seed is fixed.
class SplitMix64 {
public:
    explicit SplitMix64(uint64_t seed) : state_{seed} {}

    [[nodiscard]] uint32_t below(uint32_t bound) { return static_cast<uint32_t>(next() % bound); }

private:
    [[nodiscard]] uint64_t next() {
        uint64_t mixed = (state_ += 0x9E3779B97F4A7C15ULL);
        mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
        return mixed ^ (mixed >> 31);
    }

    uint64_t state_ = 0;
};

// ── Rank solver helpers ─────────────────────────────────────────────

template <typename Rank>
[[nodiscard]] bool is_feasible(const std::vector<Rank>& rank, std::span<const vis::NSEdge> edges) {
    return std::ranges::all_of(edges, [&](const vis::NSEdge& edge) {
        return static_cast<int64_t>(rank[edge.head]) - static_cast<int64_t>(rank[edge.tail]) >= edge.minlen;
    });
}

template <typename Rank>
[[nodiscard]] int64_t cost_of(const std::vector<Rank>& rank, std::span<const vis::NSEdge> edges) {
    int64_t total_cost = 0;
    for (const auto& edge : edges) {
        total_cost +=
            int64_t{edge.weight} * (static_cast<int64_t>(rank[edge.head]) - static_cast<int64_t>(rank[edge.tail]));
    }
    return total_cost;
}

template <typename Rank>
[[nodiscard]] bool has_zero_minimum(const std::vector<Rank>& rank) {
    return rank.empty() || *std::ranges::min_element(rank) == 0;
}

// Visits every feasible assignment with ranks in [0, bound], in topological
// order, and keeps the least cost. Each rank starts at the least value that
// its assigned predecessors permit.
class ExhaustiveRanking {
public:
    ExhaustiveRanking(std::span<const vis::NSEdge> edges, std::span<const uint32_t> topo_order, int64_t bound)
        : edges_{edges}, topo_order_{topo_order}, bound_{bound}, rank_(topo_order.size(), 0) {}

    [[nodiscard]] int64_t least_cost() {
        search(0);
        return best_cost_;
    }

private:
    void search(size_t depth) {
        if (depth == topo_order_.size()) {
            best_cost_ = std::min(best_cost_, cost_of(rank_, edges_));
            return;
        }
        const uint32_t node = topo_order_[depth];
        int64_t lower = 0;
        for (const auto& edge : edges_) {
            if (edge.head == node) lower = std::max(lower, rank_[edge.tail] + edge.minlen);
        }
        for (int64_t candidate = lower; candidate <= bound_; ++candidate) {
            rank_[node] = candidate;
            search(depth + 1);
        }
    }

    std::span<const vis::NSEdge> edges_;
    std::span<const uint32_t> topo_order_;
    int64_t bound_ = 0;
    std::vector<int64_t> rank_;
    int64_t best_cost_ = std::numeric_limits<int64_t>::max();
};

// ── Rank solver checks ──────────────────────────────────────────────

// Node 3 connects to node 1 with weight 2 and to node 0 with weight 1, and a
// separation of 10 keeps 0 and 1 apart. The first tight tree puts 3 under 0,
// so the solver must pivot one time to put 3 under 1. Nodes 2 and 4 are the
// auxiliary nodes of the two connections.
[[nodiscard]] std::vector<vis::NSEdge> pull_instance() {
    return {
        {.tail = 0, .head = 1, .minlen = 10, .weight = 0},  // The separation of 0 and 1.
        {.tail = 2, .head = 0, .minlen = 0, .weight = 1},  // The connection of 3 to 0.
        {.tail = 2, .head = 3, .minlen = 0, .weight = 1},
        {.tail = 4, .head = 1, .minlen = 0, .weight = 2},  // The connection of 3 to 1.
        {.tail = 4, .head = 3, .minlen = 0, .weight = 2},
    };
}

void check_simplex_pivots_to_the_optimum(Checker& checker) {
    const auto edges = pull_instance();
    const auto result = vis::network_simplex(5, edges);
    constexpr const char* check = "simplex_pivots_to_the_optimum";
    checker.expect(result.status == vis::NSStatus::Optimal, check, "the solver reports the optimum");
    checker.expect(is_feasible(result.rank, edges), check, "every minlen holds");
    checker.expect(result.rank[3] == result.rank[1], check, "the heavier connection is straight");
    checker.expect(cost_of(result.rank, edges) == 10, check, "the cost is the optimum 10");
    checker.expect(result.max_rank == *std::ranges::max_element(result.rank), check, "max_rank is the largest rank");
}

// The pull instance needs one pivot, so a limit of zero pivots stops the
// solver at the first feasible tree. The two-source instance needs none.
void check_simplex_stops_at_the_pivot_limit(Checker& checker) {
    constexpr const char* check = "simplex_stops_at_the_pivot_limit";
    const auto edges = pull_instance();
    const auto limited = vis::network_simplex(5, edges, 0);
    checker.expect(limited.status == vis::NSStatus::PivotLimit, check,
                   "a needed pivot over the limit reports PivotLimit");
    checker.expect(is_feasible(limited.rank, edges), check, "the ranks at the limit satisfy every minlen");

    const std::vector<vis::NSEdge> no_pivot_edges{
        {.tail = 0, .head = 2, .minlen = 1, .weight = 1},
        {.tail = 1, .head = 2, .minlen = 3, .weight = 1},
    };
    checker.expect(vis::network_simplex(3, no_pivot_edges, 0).status == vis::NSStatus::Optimal, check,
                   "an optimal first tree needs no pivot");
}

// Two sources feed one node. Longest-path ranking puts both sources at 0, so
// the edge of minlen 1 has a slack of 2. One tight tree from one root must
// shift as a whole to take that edge in.
void check_simplex_spans_each_component_with_one_tree(Checker& checker) {
    const std::vector<vis::NSEdge> edges{
        {.tail = 0, .head = 2, .minlen = 1, .weight = 1},
        {.tail = 1, .head = 2, .minlen = 3, .weight = 1},
    };
    const auto result = vis::network_simplex(3, edges);
    constexpr const char* check = "simplex_spans_each_component_with_one_tree";
    checker.expect(result.status == vis::NSStatus::Optimal, check, "the solver reports the optimum");
    checker.expect(is_feasible(result.rank, edges), check, "every minlen holds");
    checker.expect(cost_of(result.rank, edges) == 4, check, "each edge has its minlen, so the cost is 4");
}

void check_simplex_matches_exhaustive_search(Checker& checker) {
    constexpr const char* check = "simplex_matches_exhaustive_search";
    constexpr int64_t kMaxTotalMinlen = 6;
    SplitMix64 random{42};
    uint32_t mismatches = 0;
    for (uint32_t instance = 0; instance < 400; ++instance) {
        const uint32_t num_nodes = 1 + random.below(5);
        std::vector<uint32_t> topo_order(num_nodes);
        for (uint32_t node = 0; node < num_nodes; ++node)
            topo_order[node] = node;
        for (uint32_t slot = num_nodes; slot > 1; --slot)
            std::swap(topo_order[slot - 1], topo_order[random.below(slot)]);
        std::vector<uint32_t> position(num_nodes);
        for (uint32_t slot = 0; slot < num_nodes; ++slot)
            position[topo_order[slot]] = slot;

        std::vector<vis::NSEdge> edges;
        int64_t total_minlen = 0;
        const uint32_t num_edges = num_nodes < 2 ? 0 : random.below(8);
        for (uint32_t edge = 0; edge < num_edges; ++edge) {
            const uint32_t first = random.below(num_nodes);
            uint32_t second = random.below(num_nodes - 1);
            if (second >= first) ++second;
            const bool is_first_earlier = position[first] < position[second];
            int32_t minlen = static_cast<int32_t>(random.below(3));
            if (total_minlen + minlen > kMaxTotalMinlen) minlen = 0;
            total_minlen += minlen;
            edges.push_back({
                .tail = is_first_earlier ? first : second,
                .head = is_first_earlier ? second : first,
                .minlen = minlen,
                .weight = static_cast<int32_t>(random.below(4)),
            });
        }

        const auto result = vis::network_simplex(num_nodes, edges);
        const int64_t least_cost = ExhaustiveRanking{edges, topo_order, total_minlen}.least_cost();
        const bool is_correct = result.status == vis::NSStatus::Optimal && is_feasible(result.rank, edges)
                             && has_zero_minimum(result.rank) && cost_of(result.rank, edges) == least_cost;
        if (!is_correct) ++mismatches;
    }
    if (mismatches != 0) std::fprintf(stderr, "  %u of 400 instances differ from the exhaustive search\n", mismatches);
    checker.expect(mismatches == 0, check, "each instance is feasible, starts at rank 0 and has the least cost");
}

void check_simplex_refuses_a_cycle(Checker& checker) {
    constexpr const char* check = "simplex_refuses_a_cycle";
    const std::vector<vis::NSEdge> two_cycle{
        {.tail = 0, .head = 1, .minlen = 1, .weight = 1},
        {.tail = 1, .head = 0, .minlen = 1, .weight = 1},
    };
    const auto cyclic = vis::network_simplex(2, two_cycle);
    checker.expect(cyclic.status == vis::NSStatus::NoRanking, check, "a two-node cycle has no ranking");
    checker.expect(std::ranges::all_of(cyclic.rank, [](int64_t rank) { return rank == 0; }), check,
                   "a result with no ranking has each rank zero");

    const std::vector<vis::NSEdge> self_loop{{.tail = 1, .head = 1, .minlen = 1, .weight = 1}};
    checker.expect(vis::network_simplex(2, self_loop).status == vis::NSStatus::NoRanking, check,
                   "a self-loop is a cycle");
}

// Each component gets its own tree. The isolated node and the graph with no
// edge are optimal at rank zero.
void check_simplex_solves_each_component(Checker& checker) {
    constexpr const char* check = "simplex_solves_each_component";
    const std::vector<vis::NSEdge> edges{
        {.tail = 0, .head = 1, .minlen = 1, .weight = 1},
        {.tail = 2, .head = 3, .minlen = 2, .weight = 1},
    };
    const auto result = vis::network_simplex(5, edges);
    checker.expect(result.status == vis::NSStatus::Optimal, check, "the solver reports the optimum");
    checker.expect(is_feasible(result.rank, edges) && cost_of(result.rank, edges) == 3, check,
                   "each component has the least cost");
    checker.expect(vis::network_simplex(0, {}).status == vis::NSStatus::Optimal, check, "no node is optimal");
    const auto no_edges = vis::network_simplex(3, {});
    checker.expect(no_edges.status == vis::NSStatus::Optimal && no_edges.max_rank == 0, check,
                   "no edge puts each node at rank zero");
}

void check_simplex_refuses_malformed_input(Checker& checker) {
    constexpr const char* check = "simplex_refuses_malformed_input";
    const std::vector<vis::NSEdge> out_of_range{{.tail = 0, .head = 2, .minlen = 1, .weight = 1}};
    const std::vector<vis::NSEdge> negative_weight{{.tail = 0, .head = 1, .minlen = 1, .weight = -1}};
    checker.expect(vis::is_well_formed_ns_input(5, pull_instance()), check, "the pull instance is well formed");
    checker.expect(!vis::is_well_formed_ns_input(2, out_of_range) && !vis::is_well_formed_ns_input(2, negative_weight),
                   check, "the predicate refuses an endpoint out of range and a negative weight");
    checker.expect(crucible::test::aborts([&] { static_cast<void>(vis::network_simplex(2, out_of_range)); }), check,
                   "an endpoint out of range stops the process");
    checker.expect(crucible::test::aborts([&] { static_cast<void>(vis::network_simplex(2, negative_weight)); }), check,
                   "a negative weight stops the process");
}

// ── Layered layout helpers ──────────────────────────────────────────

[[nodiscard]] vis::LayoutNode sized_node(float half_width, float min_height = 28.0f) {
    vis::LayoutNode node;
    node.lw = half_width;
    node.rw = half_width;
    node.min_height = min_height;
    return node;
}

// In each layer, sorted by x, the gap between two adjacent boxes is at least
// node_h_gap, and the order field agrees with the x order.
[[nodiscard]] bool has_separated_layers(const vis::LayoutResult& layout, float node_h_gap) {
    for (uint32_t layer = 0; layer < layout.num_layers; ++layer) {
        std::vector<uint32_t> members;
        for (uint32_t node = 0; node < layout.nodes.size(); ++node) {
            if (layout.nodes[node].layer == layer) members.push_back(node);
        }
        std::ranges::sort(members,
                          [&](uint32_t lhs, uint32_t rhs) { return layout.nodes[lhs].x < layout.nodes[rhs].x; });
        for (size_t index = 1; index < members.size(); ++index) {
            const auto& left = layout.nodes[members[index - 1]];
            const auto& right = layout.nodes[members[index]];
            if ((right.x - right.lw) - (left.x + left.rw) < node_h_gap - 1.0e-3f) return false;
            if (right.order <= left.order) return false;
        }
    }
    return true;
}

// ── Layered layout checks ───────────────────────────────────────────

// Layer 1 holds b and c. A layout from the simplex puts d straight under c,
// keeps b and c at their least separation, and puts a between them. The
// naive placement centers d under layer 1, so the two layouts differ.
void check_layout_runs_the_simplex(Checker& checker) {
    constexpr const char* check = "layout_runs_the_simplex";
    const std::vector<vis::LayoutNode> nodes{sized_node(30), sized_node(30), sized_node(30), sized_node(50)};
    const std::vector<vis::LayoutEdge> edges{{.src = 0, .dst = 1}, {.src = 0, .dst = 2}, {.src = 2, .dst = 3}};

    const vis::LayoutParams simplex_params;
    const auto simplex = vis::sugiyama_layout(nodes, edges, simplex_params);
    vis::LayoutParams naive_params;
    naive_params.use_network_simplex = false;
    const auto naive = vis::sugiyama_layout(nodes, edges, naive_params);

    const auto& node_a = simplex.nodes[0];
    const auto& node_b = simplex.nodes[1];
    const auto& node_c = simplex.nodes[2];
    const auto& node_d = simplex.nodes[3];
    checker.expect(node_b.layer == 1 && node_c.layer == 1, check, "b and c share layer 1");
    checker.expect(is_near(node_d.x, node_c.x), check, "d is straight under c");
    checker.expect(is_near(std::abs(node_c.x - node_b.x), 30.0f + 30.0f + simplex_params.node_h_gap), check,
                   "b and c are at their least separation");
    const bool is_a_between =
        node_a.x >= std::min(node_b.x, node_c.x) - 1.0e-3f && node_a.x <= std::max(node_b.x, node_c.x) + 1.0e-3f;
    checker.expect(is_a_between, check, "a is between b and c");
    checker.expect(!is_near(simplex.nodes[3].x, naive.nodes[3].x), check,
                   "the simplex layout differs from the naive one");
    checker.expect(has_separated_layers(simplex, simplex_params.node_h_gap), check, "no two boxes of a layer overlap");
}

// A long edge a->c goes through one virtual node. The same graph with a real
// node of the virtual node's size in its place must give a, b and c the same
// place, because the layered graph is the same. The naive placement keeps the
// comparison free of the simplex weights, which differ for virtual nodes.
void check_layout_counts_each_short_edge_once(Checker& checker) {
    constexpr const char* check = "layout_counts_each_short_edge_once";
    vis::LayoutParams params;
    params.use_network_simplex = false;

    const std::vector<vis::LayoutNode> long_nodes{sized_node(30), sized_node(30), sized_node(30)};
    const std::vector<vis::LayoutEdge> long_edges{{.src = 0, .dst = 1}, {.src = 1, .dst = 2}, {.src = 0, .dst = 2}};
    const auto with_long_edge = vis::sugiyama_layout(long_nodes, long_edges, params);

    const std::vector<vis::LayoutNode> chain_nodes{sized_node(30), sized_node(30), sized_node(30), sized_node(2, 4)};
    const std::vector<vis::LayoutEdge> chain_edges{
        {.src = 0, .dst = 1}, {.src = 1, .dst = 2}, {.src = 0, .dst = 3}, {.src = 3, .dst = 2}};
    const auto with_chain = vis::sugiyama_layout(chain_nodes, chain_edges, params);

    bool is_same = with_long_edge.num_layers == with_chain.num_layers;
    for (uint32_t node = 0; node < 3; ++node) {
        const auto& long_node = with_long_edge.nodes[node];
        const auto& chain_node = with_chain.nodes[node];
        is_same = is_same && long_node.layer == chain_node.layer && is_near(long_node.x, chain_node.x)
               && is_near(long_node.y, chain_node.y);
    }
    checker.expect(is_same, check, "a long edge lays out as a chain through a real node");
}

// A three-node cycle has no source. Each node must still get its own layer,
// and each edge must join two layers.
void check_layout_breaks_a_cycle(Checker& checker) {
    constexpr const char* check = "layout_breaks_a_cycle";
    const std::vector<vis::LayoutNode> nodes{sized_node(30), sized_node(30), sized_node(30)};
    const std::vector<vis::LayoutEdge> edges{{.src = 0, .dst = 1}, {.src = 1, .dst = 2}, {.src = 2, .dst = 0}};
    const auto layout = vis::sugiyama_layout(nodes, edges);
    checker.expect(layout.num_layers == 3, check, "the cycle takes three layers");
    const bool is_each_edge_between_layers = std::ranges::all_of(edges, [&](const vis::LayoutEdge& edge) {
        return layout.nodes[edge.src].layer != layout.nodes[edge.dst].layer;
    });
    checker.expect(is_each_edge_between_layers, check, "each edge joins two layers");
}

// A random DAG. Each edge must point down, and no two boxes of a layer may
// overlap, with and without the simplex. The simplex layout also keeps the
// left margin and fits in total_width.
void check_layout_invariants_on_a_random_dag(Checker& checker, uint32_t num_nodes, uint32_t num_edges) {
    constexpr const char* check = "layout_invariants_on_a_random_dag";
    SplitMix64 random{42};
    std::vector<vis::LayoutNode> nodes;
    for (uint32_t node = 0; node < num_nodes; ++node)
        nodes.push_back(sized_node(static_cast<float>(10 + random.below(40))));
    std::vector<vis::LayoutEdge> edges;
    for (uint32_t edge = 0; edge < num_edges; ++edge) {
        const uint32_t first = random.below(num_nodes);
        uint32_t second = random.below(num_nodes - 1);
        if (second >= first) ++second;
        edges.push_back({.src = std::min(first, second), .dst = std::max(first, second)});
    }

    for (const bool use_network_simplex : {true, false}) {
        vis::LayoutParams params;
        params.use_network_simplex = use_network_simplex;
        const auto layout = vis::sugiyama_layout(nodes, edges, params);
        const bool is_each_edge_down = std::ranges::all_of(edges, [&](const vis::LayoutEdge& edge) {
            return layout.nodes[edge.dst].layer > layout.nodes[edge.src].layer;
        });
        checker.expect(is_each_edge_down, check, "each edge points down");
        checker.expect(has_separated_layers(layout, params.node_h_gap), check, "no two boxes of a layer overlap");
        if (!use_network_simplex) continue;
        const bool is_inside = std::ranges::all_of(layout.nodes, [&](const vis::LayoutNode& node) {
            return node.x - node.lw >= params.padding - 1.0e-3f
                && node.x + node.rw + params.padding <= layout.total_width + 1.0e-3f;
        });
        checker.expect(is_inside, check, "each box is inside the padding and total_width");
    }
}

void check_layout_refuses_malformed_input(Checker& checker) {
    constexpr const char* check = "layout_refuses_malformed_input";
    const std::vector<vis::LayoutNode> nan_nodes{sized_node(30), sized_node(std::numeric_limits<float>::quiet_NaN())};
    vis::LayoutParams negative_gap;
    negative_gap.node_h_gap = -1.0f;
    const std::vector<vis::LayoutNode> one_node{sized_node(30)};
    checker.expect(vis::is_well_formed_layout_input(one_node, {}), check, "one node of half width 30 is well formed");
    checker.expect(!vis::is_well_formed_layout_input(nan_nodes, {}), check, "the predicate refuses a NaN half width");
    checker.expect(!vis::is_well_formed_layout_input(one_node, negative_gap), check,
                   "the predicate refuses a negative gap");
    checker.expect(crucible::test::aborts([&] { static_cast<void>(vis::sugiyama_layout(nan_nodes, {})); }), check,
                   "a NaN half width stops the process");
    checker.expect(crucible::test::aborts([&] { static_cast<void>(vis::sugiyama_layout(one_node, {}, negative_gap)); }),
                   check, "a negative gap stops the process");
}

// ── U-net detection ─────────────────────────────────────────────────

// The first block has a resolution of 6, less than two times the bottleneck
// of 4, but later blocks reach 32. The encoder ends before the bottleneck and
// the decoder starts after it.
void check_u_shape_uses_the_largest_resolution(Checker& checker) {
    constexpr const char* check = "u_shape_uses_the_largest_resolution";
    const std::vector<int32_t> resolutions{6, 32, 16, 8, 4, 8, 16, 32};
    std::vector<vis::Block> blocks(resolutions.size());
    std::vector<uint32_t> phase_idx(resolutions.size());
    for (uint32_t index = 0; index < resolutions.size(); ++index) {
        blocks[index].spatial_h = resolutions[index];
        phase_idx[index] = index;
    }
    const auto split = vis::detect_u_shape(blocks, phase_idx);
    checker.expect(split.is_unet, check, "the resolution range makes a U");
    checker.expect(split.enc_end == 3 && split.dec_start == 5, check,
                   "the encoder ends at 3 and the decoder starts at 5");

    const std::vector<int32_t> flat{8, 8, 12, 8, 12, 8};
    std::vector<vis::Block> flat_blocks(flat.size());
    std::vector<uint32_t> flat_idx(flat.size());
    for (uint32_t index = 0; index < flat.size(); ++index) {
        flat_blocks[index].spatial_h = flat[index];
        flat_idx[index] = index;
    }
    checker.expect(!vis::detect_u_shape(flat_blocks, flat_idx).is_unet, check,
                   "a range less than a factor of two is not a U");
}

}  // namespace

[[gnu::cold]] int main() {
    Checker checker;
    check_simplex_pivots_to_the_optimum(checker);
    check_simplex_stops_at_the_pivot_limit(checker);
    check_simplex_spans_each_component_with_one_tree(checker);
    check_simplex_matches_exhaustive_search(checker);
    check_simplex_refuses_a_cycle(checker);
    check_simplex_solves_each_component(checker);
    check_simplex_refuses_malformed_input(checker);
    check_layout_runs_the_simplex(checker);
    check_layout_counts_each_short_edge_once(checker);
    check_layout_breaks_a_cycle(checker);
    check_layout_invariants_on_a_random_dag(checker, 14, 22);
    check_layout_invariants_on_a_random_dag(checker, 60, 110);
    check_layout_refuses_malformed_input(checker);
    check_u_shape_uses_the_largest_resolution(checker);
    if (checker.exit_code() == 0) crucible::test::pass("test_vis_layout: all tests passed\n");
    return checker.exit_code();
}
