#pragma once

// Minimum-cost rank assignment for a DAG.
//
// Minimize the sum over edges of weight(e) * |rank(head) - rank(tail)|,
// subject to rank(head) - rank(tail) >= minlen(e) for every edge. The solver
// is network simplex on the dual of that rank LP.
//
// This is not a hot path, so std::vector is appropriate.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace crucible::vis {

struct NSEdge {
    uint32_t tail = 0;
    uint32_t head = 0;
    int32_t minlen = 1;
    int32_t weight = 1;  // Higher weight pulls the edge straighter.
};

struct NSResult {
    std::vector<int32_t> rank;
    int32_t min_rank = 0;
    int32_t max_rank = 0;
    bool converged = false;
};

[[nodiscard]] inline NSResult network_simplex(uint32_t num_nodes, const std::vector<NSEdge>& edges,
                                              uint32_t max_iterations = 1000) {
    NSResult result;
    result.rank.resize(num_nodes, 0);

    if (num_nodes == 0 || edges.empty()) {
        result.converged = true;
        return result;
    }

    const uint32_t n = num_nodes;
    const uint32_t m = static_cast<uint32_t>(edges.size());

    std::vector<std::vector<uint32_t>> fwd(n);
    std::vector<std::vector<uint32_t>> rev(n);
    std::vector<uint32_t> in_deg(n, 0);

    for (uint32_t i = 0; i < m; i++) {
        const auto& e = edges[i];
        if (e.tail < n && e.head < n && e.tail != e.head) {
            fwd[e.tail].push_back(i);
            rev[e.head].push_back(i);
            in_deg[e.head]++;
        }
    }

    std::vector<uint32_t> topo;
    {
        std::vector<uint32_t> queue;
        for (uint32_t i = 0; i < n; i++)
            if (in_deg[i] == 0) queue.push_back(i);
        auto deg = in_deg;
        while (!queue.empty()) {
            uint32_t u = queue.back();
            queue.pop_back();
            topo.push_back(u);
            for (uint32_t ei : fwd[u]) {
                uint32_t v = edges[ei].head;
                if (--deg[v] == 0) queue.push_back(v);
            }
        }
    }

    // The order covers every node unless the graph has a cycle. A short
    // order means a cycle, and the rank LP then has no solution.
    if (topo.size() != n) {
        result.converged = false;
        return result;
    }

    // Relaxing in topological order gives each node the longest path from a
    // source. That is the smallest rank assignment satisfying every minlen,
    // so it is a feasible starting point.
    auto& rank = result.rank;
    for (uint32_t u : topo) {
        for (uint32_t ei : fwd[u]) {
            const auto& e = edges[ei];
            rank[e.head] = std::max(rank[e.head], rank[e.tail] + e.minlen);
        }
    }

    // The spanning tree must be built from tight edges, those of zero slack.
    auto slack = [&](uint32_t ei) -> int32_t {
        const auto& e = edges[ei];
        return rank[e.head] - rank[e.tail] - e.minlen;
    };

    std::vector<bool> in_tree(m, false);
    std::vector<bool> node_in_tree(n, false);
    std::vector<uint32_t> tree_edges;

    std::vector<int32_t> parent_edge(n, -1);
    std::vector<int32_t> parent_node(n, -1);

    {
        std::vector<uint32_t> queue;
        for (uint32_t i = 0; i < n; i++) {
            if (in_deg[i] == 0) {
                node_in_tree[i] = true;
                queue.push_back(i);
            }
        }

        while (!queue.empty() && tree_edges.size() < n - 1) {
            uint32_t u = queue.back();
            queue.pop_back();

            for (uint32_t ei : fwd[u]) {
                uint32_t v = edges[ei].head;
                if (!node_in_tree[v] && slack(ei) == 0) {
                    in_tree[ei] = true;
                    node_in_tree[v] = true;
                    tree_edges.push_back(ei);
                    parent_edge[v] = static_cast<int32_t>(ei);
                    parent_node[v] = static_cast<int32_t>(u);
                    queue.push_back(v);
                }
            }
            for (uint32_t ei : rev[u]) {
                uint32_t v = edges[ei].tail;
                if (!node_in_tree[v] && slack(ei) == 0) {
                    in_tree[ei] = true;
                    node_in_tree[v] = true;
                    tree_edges.push_back(ei);
                    parent_edge[v] = static_cast<int32_t>(ei);
                    parent_node[v] = static_cast<int32_t>(u);
                    queue.push_back(v);
                }
            }
        }

        // Tight edges alone need not span the graph. Where they do not, the
        // least-slack crossing edge is made tight by moving ranks.
        for (uint32_t attempts = 0; tree_edges.size() < n - 1 && attempts < n; attempts++) {
            int32_t best_slack = std::numeric_limits<int32_t>::max();
            uint32_t best_ei = 0;
            for (uint32_t ei = 0; ei < m; ei++) {
                if (in_tree[ei]) continue;
                const auto& e = edges[ei];
                bool tail_in = node_in_tree[e.tail];
                bool head_in = node_in_tree[e.head];
                if (tail_in != head_in) {
                    int32_t s = slack(ei);
                    if (s < best_slack) {
                        best_slack = s;
                        best_ei = ei;
                    }
                }
            }
            if (best_slack == std::numeric_limits<int32_t>::max()) break;

            const auto& be = edges[best_ei];
            if (node_in_tree[be.tail] && !node_in_tree[be.head]) {
                rank[be.head] = rank[be.tail] + be.minlen;
                node_in_tree[be.head] = true;
            } else {
                rank[be.tail] = rank[be.head] - be.minlen;
                node_in_tree[be.tail] = true;
            }
            in_tree[best_ei] = true;
            tree_edges.push_back(best_ei);
            parent_edge[be.head] = static_cast<int32_t>(best_ei);
            parent_node[be.head] = static_cast<int32_t>(be.tail);
        }
    }

    // Removing a tree edge splits the tree into a head side and a tail side.
    // The cutvalue of that edge is the summed weight of every edge crossing
    // the split, counted positive when it runs the same way as the tree edge
    // and negative when it runs the other way.
    std::vector<int32_t> cutvalue(m, 0);

    // Each cut is rebuilt from scratch rather than maintained incrementally
    // through the tree. That costs a graph sweep per tree edge, and buys a
    // result that can be checked against the definition directly.
    auto compute_cutvalues = [&]() {
        for (uint32_t ei : tree_edges) {
            const auto& te = edges[ei];
            std::vector<bool> head_side(n, false);
            {
                std::vector<uint32_t> q = {te.head};
                head_side[te.head] = true;
                while (!q.empty()) {
                    uint32_t u = q.back();
                    q.pop_back();
                    for (uint32_t tei : tree_edges) {
                        if (tei == ei) continue;
                        if (!in_tree[tei]) continue;
                        const auto& e2 = edges[tei];
                        if (e2.tail == u && !head_side[e2.head]) {
                            head_side[e2.head] = true;
                            q.push_back(e2.head);
                        }
                        if (e2.head == u && !head_side[e2.tail]) {
                            head_side[e2.tail] = true;
                            q.push_back(e2.tail);
                        }
                    }
                }
            }

            int32_t cv = 0;
            for (uint32_t i = 0; i < m; i++) {
                const auto& e2 = edges[i];
                bool t_head = head_side[e2.tail];
                bool h_head = head_side[e2.head];
                if (!t_head && h_head) cv += e2.weight;  // Same way as the tree edge.
                if (t_head && !h_head) cv -= e2.weight;  // The other way.
            }
            cutvalue[ei] = cv;
        }
    };

    compute_cutvalues();

    for (uint32_t iter = 0; iter < max_iterations; iter++) {
        int32_t worst_cv = 0;
        uint32_t leave_ei = UINT32_MAX;
        for (uint32_t tei : tree_edges) {
            if (cutvalue[tei] < worst_cv) {
                worst_cv = cutvalue[tei];
                leave_ei = tei;
            }
        }

        if (leave_ei == UINT32_MAX) {
            // No negative cutvalue is left, so the tree is optimal.
            result.converged = true;
            break;
        }

        // The entering edge is the non-tree edge of least slack crossing the
        // same cut, and it must run from the tail side to the head side.
        const auto& le = edges[leave_ei];
        std::vector<bool> head_side(n, false);
        {
            std::vector<uint32_t> q = {le.head};
            head_side[le.head] = true;
            while (!q.empty()) {
                uint32_t u = q.back();
                q.pop_back();
                for (uint32_t tei : tree_edges) {
                    if (tei == leave_ei || !in_tree[tei]) continue;
                    const auto& e2 = edges[tei];
                    if (e2.tail == u && !head_side[e2.head]) {
                        head_side[e2.head] = true;
                        q.push_back(e2.head);
                    }
                    if (e2.head == u && !head_side[e2.tail]) {
                        head_side[e2.tail] = true;
                        q.push_back(e2.tail);
                    }
                }
            }
        }

        int32_t best_slack = std::numeric_limits<int32_t>::max();
        uint32_t enter_ei = UINT32_MAX;
        for (uint32_t i = 0; i < m; i++) {
            if (in_tree[i]) continue;
            const auto& e2 = edges[i];
            if (!head_side[e2.tail] && head_side[e2.head]) {
                int32_t s = slack(i);
                if (s < best_slack) {
                    best_slack = s;
                    enter_ei = i;
                }
            }
        }

        if (enter_ei == UINT32_MAX) break;  // Unreachable on a feasible instance.

        // Moving the whole head side by the entering edge's slack makes that
        // edge tight. Every remaining tree edge lies wholly inside one side,
        // so its slack does not change and the tree stays tight.
        if (best_slack != 0) {
            for (uint32_t i = 0; i < n; i++) {
                if (head_side[i]) rank[i] -= best_slack;
            }
        }

        in_tree[leave_ei] = false;
        in_tree[enter_ei] = true;
        tree_edges.erase(std::find(tree_edges.begin(), tree_edges.end(), leave_ei));
        tree_edges.push_back(enter_ei);

        compute_cutvalues();
    }

    result.min_rank = *std::ranges::min_element(rank);
    result.max_rank = *std::ranges::max_element(rank);
    for (auto& r : rank)
        r -= result.min_rank;
    result.max_rank -= result.min_rank;
    result.min_rank = 0;

    return result;
}

}  // namespace crucible::vis
