// The network simplex solver of crucible/vis/NetworkSimplex.h.

#include <crucible/vis/NetworkSimplex.h>

#include <foundation/contracts/Pre.h>

#include <algorithm>
#include <utility>

namespace crucible::vis {

namespace {

constexpr uint32_t kNoIndex = std::numeric_limits<uint32_t>::max();

// The edges that touch each node, in compressed form. An edge is in the list
// of its tail and in the list of its head.
class IncidenceLists {
public:
    IncidenceLists(uint32_t num_nodes, std::span<const NSEdge> edges) : offsets_(size_t{num_nodes} + 1, 0) {
        for (const auto& edge : edges) {
            ++offsets_[edge.tail + 1];
            ++offsets_[edge.head + 1];
        }
        for (uint32_t node = 0; node < num_nodes; ++node)
            offsets_[node + 1] += offsets_[node];
        edge_ids_.resize(offsets_[num_nodes]);
        std::vector<uint32_t> fill_position(offsets_.begin(), offsets_.end() - 1);
        for (uint32_t edge_id = 0; edge_id < edges.size(); ++edge_id) {
            edge_ids_[fill_position[edges[edge_id].tail]++] = edge_id;
            edge_ids_[fill_position[edges[edge_id].head]++] = edge_id;
        }
    }

    [[nodiscard]] std::span<const uint32_t> of(uint32_t node) const {
        return std::span{edge_ids_}.subspan(offsets_[node], offsets_[node + 1] - offsets_[node]);
    }

private:
    std::vector<uint32_t> offsets_;
    std::vector<uint32_t> edge_ids_;
};

class NetworkSimplexSolver {
public:
    NetworkSimplexSolver(uint32_t num_nodes, std::span<const NSEdge> edges)
        : num_nodes_{num_nodes},
          num_edges_{static_cast<uint32_t>(edges.size())},
          edges_{edges},
          incidence_{num_nodes, edges},
          rank_(num_nodes, 0),
          is_tree_edge_(edges.size(), 0),
          cut_value_(edges.size(), 0),
          parent_edge_(num_nodes, kNoIndex),
          low_(num_nodes, 0),
          lim_(num_nodes, 0),
          post_order_(num_nodes, 0),
          net_weight_(num_nodes, 0) {}

    [[nodiscard]] NSResult solve(uint32_t max_pivots) {
        if (!rank_by_longest_path()) {
            std::ranges::fill(rank_, 0);
            return finish(NSStatus::NoRanking);
        }
        for (const auto& edge : edges_) {
            net_weight_[edge.tail] += edge.weight;
            net_weight_[edge.head] -= edge.weight;
        }
        build_feasible_tree();
        index_tree();
        compute_cut_values();

        for (uint32_t pivots = 0;; ++pivots) {
            const uint32_t leaving = find_leaving_edge();
            if (leaving == kNoIndex) return finish(NSStatus::Optimal);
            if (pivots >= max_pivots) return finish(NSStatus::PivotLimit);
            const uint32_t entering = find_entering_edge(leaving);
            // A negative cut value needs an edge of positive weight from the
            // head component to the tail component, and that edge is not in the
            // tree. With no negative weight, an entering edge always exists.
            contract_assert(entering != kNoIndex);
            exchange(leaving, entering);
        }
    }

private:
    [[nodiscard]] int64_t slack(uint32_t edge_id) const {
        const auto& edge = edges_[edge_id];
        return rank_[edge.head] - rank_[edge.tail] - edge.minlen;
    }

    [[nodiscard]] uint32_t other_end(uint32_t edge_id, uint32_t node) const {
        const auto& edge = edges_[edge_id];
        return edge.tail == node ? edge.head : edge.tail;
    }

    // The nodes of the subtree below `root` have the lim numbers
    // low_[root] through lim_[root].
    [[nodiscard]] bool is_in_subtree(uint32_t root, uint32_t node) const {
        return low_[root] <= lim_[node] && lim_[node] <= lim_[root];
    }

    // Kahn's algorithm gives a topological order, and each node gets the
    // longest path from a source. That satisfies each minlen. Returns false
    // when a cycle leaves nodes out of the order. O(V + E).
    [[nodiscard]] bool rank_by_longest_path() {
        std::vector<uint32_t> in_degree(num_nodes_, 0);
        for (const auto& edge : edges_)
            ++in_degree[edge.head];
        std::vector<uint32_t> ready;
        for (uint32_t node = 0; node < num_nodes_; ++node) {
            if (in_degree[node] == 0) ready.push_back(node);
        }
        uint32_t num_ordered = 0;
        while (!ready.empty()) {
            const uint32_t node = ready.back();
            ready.pop_back();
            ++num_ordered;
            for (const uint32_t edge_id : incidence_.of(node)) {
                const auto& edge = edges_[edge_id];
                if (edge.tail != node) continue;
                rank_[edge.head] = std::max(rank_[edge.head], rank_[node] + edge.minlen);
                if (--in_degree[edge.head] == 0) ready.push_back(edge.head);
            }
        }
        return num_ordered == num_nodes_;
    }

    // One tree for each connected component. A tree starts at the component's
    // node of least index and takes each tight edge to a node outside it. When
    // no tight edge is left, the non-tree edge of least slack with one end in
    // the tree becomes tight: the whole tree moves by that slack, which keeps
    // each other edge feasible. O(V * E) in the worst case.
    void build_feasible_tree() {
        std::vector<uint8_t> is_in_tree(num_nodes_, 0);
        std::vector<uint32_t> members;
        std::vector<uint32_t> frontier;
        for (uint32_t root = 0; root < num_nodes_; ++root) {
            if (is_in_tree[root] != 0) continue;
            roots_.push_back(root);
            members.assign(1, root);
            is_in_tree[root] = 1;
            frontier.assign(1, root);
            for (;;) {
                grow_tight_tree(is_in_tree, members, frontier);
                const uint32_t edge_id = find_least_slack_crossing_edge(is_in_tree);
                if (edge_id == kNoIndex) break;
                const auto& edge = edges_[edge_id];
                const bool is_head_in_tree = is_in_tree[edge.head] != 0;
                const int64_t shift = is_head_in_tree ? -slack(edge_id) : slack(edge_id);
                for (const uint32_t member : members)
                    rank_[member] += shift;
                frontier.push_back(is_head_in_tree ? edge.head : edge.tail);
            }
        }
    }

    void grow_tight_tree(std::vector<uint8_t>& is_in_tree, std::vector<uint32_t>& members,
                         std::vector<uint32_t>& frontier) {
        while (!frontier.empty()) {
            const uint32_t node = frontier.back();
            frontier.pop_back();
            for (const uint32_t edge_id : incidence_.of(node)) {
                const uint32_t other = other_end(edge_id, node);
                if (is_in_tree[other] != 0 || slack(edge_id) != 0) continue;
                is_in_tree[other] = 1;
                is_tree_edge_[edge_id] = 1;
                members.push_back(other);
                frontier.push_back(other);
            }
        }
    }

    // Each earlier tree spans its whole component, so an edge with exactly
    // one end in a tree has that end in the tree that grows now. O(E).
    [[nodiscard]] uint32_t find_least_slack_crossing_edge(const std::vector<uint8_t>& is_in_tree) const {
        uint32_t best_edge = kNoIndex;
        int64_t best_slack = std::numeric_limits<int64_t>::max();
        for (uint32_t edge_id = 0; edge_id < num_edges_; ++edge_id) {
            const auto& edge = edges_[edge_id];
            if (is_in_tree[edge.tail] == is_in_tree[edge.head]) continue;
            const int64_t edge_slack = slack(edge_id);
            if (edge_slack < best_slack) {
                best_slack = edge_slack;
                best_edge = edge_id;
            }
        }
        return best_edge;
    }

    // A depth-first walk of each tree from its root. It records the tree edge
    // to each node's parent, and it numbers the nodes in postorder, so that a
    // subtree holds a contiguous range of numbers. O(V + E).
    void index_tree() {
        struct Frame {
            uint32_t node = 0;
            uint32_t next_slot = 0;
            uint32_t low = 0;
        };
        std::ranges::fill(parent_edge_, kNoIndex);
        std::vector<Frame> frames;
        uint32_t next_lim = 0;
        for (const uint32_t root : roots_) {
            frames.push_back({.node = root, .next_slot = 0, .low = next_lim});
            while (!frames.empty()) {
                Frame& frame = frames.back();
                const auto incident = incidence_.of(frame.node);
                if (frame.next_slot < incident.size()) {
                    const uint32_t edge_id = incident[frame.next_slot++];
                    if (is_tree_edge_[edge_id] == 0 || edge_id == parent_edge_[frame.node]) continue;
                    const uint32_t child = other_end(edge_id, frame.node);
                    parent_edge_[child] = edge_id;
                    frames.push_back({.node = child, .next_slot = 0, .low = next_lim});
                    continue;
                }
                low_[frame.node] = frame.low;
                lim_[frame.node] = next_lim;
                post_order_[next_lim] = frame.node;
                ++next_lim;
                frames.pop_back();
            }
        }
    }

    // The cut value of the tree edge above a node follows from the subtree
    // below the node. Edges inside the subtree cancel, so the weight out of
    // the subtree minus the weight into it is the sum of the net weights of
    // its nodes. The cut value is that sum when the node is the tail of the
    // tree edge, and its negation when the node is the head. O(V).
    void compute_cut_values() {
        std::vector<int64_t> subtree_net = net_weight_;
        for (const uint32_t node : post_order_) {
            const uint32_t edge_id = parent_edge_[node];
            if (edge_id == kNoIndex) continue;
            const bool is_node_tail = edges_[edge_id].tail == node;
            cut_value_[edge_id] = is_node_tail ? subtree_net[node] : -subtree_net[node];
            subtree_net[other_end(edge_id, node)] += subtree_net[node];
        }
    }

    // Bland's rule: the tree edge of least index with a negative cut value.
    // O(E).
    [[nodiscard]] uint32_t find_leaving_edge() const {
        for (uint32_t edge_id = 0; edge_id < num_edges_; ++edge_id) {
            if (is_tree_edge_[edge_id] != 0 && cut_value_[edge_id] < 0) return edge_id;
        }
        return kNoIndex;
    }

    // The subtree below the leaving edge is its head component when the child
    // end is the head, and its tail component when the child end is the tail.
    // The entering edge runs from the head component to the tail component and
    // has the least slack, the least index first. O(E).
    [[nodiscard]] uint32_t find_entering_edge(uint32_t leaving) const {
        const auto& leaving_edge = edges_[leaving];
        const bool is_child_head = parent_edge_[leaving_edge.head] == leaving;
        const uint32_t child = is_child_head ? leaving_edge.head : leaving_edge.tail;
        uint32_t best_edge = kNoIndex;
        int64_t best_slack = std::numeric_limits<int64_t>::max();
        for (uint32_t edge_id = 0; edge_id < num_edges_; ++edge_id) {
            if (is_tree_edge_[edge_id] != 0) continue;
            const auto& edge = edges_[edge_id];
            const bool is_tail_below = is_in_subtree(child, edge.tail);
            const bool is_head_below = is_in_subtree(child, edge.head);
            const bool is_head_to_tail =
                is_child_head ? (is_tail_below && !is_head_below) : (!is_tail_below && is_head_below);
            if (!is_head_to_tail) continue;
            const int64_t edge_slack = slack(edge_id);
            if (edge_slack < best_slack) {
                best_slack = edge_slack;
                best_edge = edge_id;
            }
        }
        return best_edge;
    }

    // The head component moves up by the slack of the entering edge. That
    // makes the entering edge tight and keeps each other edge feasible. The
    // subtree moves up when it is the head component, and down when it is the
    // tail component. O(V + E) with the new index and cut values.
    void exchange(uint32_t leaving, uint32_t entering) {
        const auto& leaving_edge = edges_[leaving];
        const bool is_child_head = parent_edge_[leaving_edge.head] == leaving;
        const uint32_t child = is_child_head ? leaving_edge.head : leaving_edge.tail;
        const int64_t entering_slack = slack(entering);
        const int64_t shift = is_child_head ? entering_slack : -entering_slack;
        for (uint32_t number = low_[child]; number <= lim_[child]; ++number)
            rank_[post_order_[number]] += shift;
        is_tree_edge_[leaving] = 0;
        is_tree_edge_[entering] = 1;
        index_tree();
        compute_cut_values();
    }

    [[nodiscard]] NSResult finish(NSStatus status) {
        NSResult result;
        result.status = status;
        if (num_nodes_ != 0) {
            const int64_t least_rank = *std::ranges::min_element(rank_);
            for (auto& node_rank : rank_)
                node_rank -= least_rank;
            result.max_rank = *std::ranges::max_element(rank_);
        }
        result.rank = std::move(rank_);
        return result;
    }

    uint32_t num_nodes_ = 0;
    uint32_t num_edges_ = 0;
    std::span<const NSEdge> edges_;
    IncidenceLists incidence_;
    std::vector<int64_t> rank_;
    std::vector<uint8_t> is_tree_edge_;
    std::vector<int64_t> cut_value_;  // Only the values of tree edges are current.
    std::vector<uint32_t> roots_;  // One root for each tree, and so for each connected component.
    std::vector<uint32_t> parent_edge_;
    std::vector<uint32_t> low_;
    std::vector<uint32_t> lim_;
    std::vector<uint32_t> post_order_;  // The node of each lim number.
    std::vector<int64_t> net_weight_;  // The weight out of each node minus the weight into it.
};

}  // namespace

bool is_well_formed_ns_input(uint32_t num_nodes, std::span<const NSEdge> edges) noexcept {
    if (edges.size() > std::numeric_limits<uint32_t>::max() / 2) return false;
    return std::ranges::all_of(edges, [num_nodes](const NSEdge& edge) {
        return edge.tail < num_nodes && edge.head < num_nodes && edge.weight >= 0;
    });
}

NSResult network_simplex(uint32_t num_nodes, std::span<const NSEdge> edges, uint32_t max_pivots) {
    CRUCIBLE_PRE(is_well_formed_ns_input(num_nodes, edges));
    return NetworkSimplexSolver{num_nodes, edges}.solve(max_pivots);
}

}  // namespace crucible::vis
