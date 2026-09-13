#pragma once

// This is not a hot path, so std::vector is appropriate.

#include <crucible/vis/NetworkSimplex.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace crucible::vis {

struct LayoutEdge {
    uint32_t src = 0;
    uint32_t dst = 0;
};

struct LayoutNode {
    // Filled by the caller.
    float lw = 30;  // Distance from the center to the left edge.
    float rw = 30;  // Distance from the center to the right edge.
    float min_height = 28;

    [[nodiscard]] float width() const { return lw + rw; }

    // Filled by the layout.
    float x = 0;  // Center of the node.
    float y = 0;  // Top of the node.
    uint32_t layer = 0;  // Layer 0 is the top.
    uint32_t order = 0;  // Position in the layer. Position 0 is leftmost.
};

struct LayoutResult {
    std::vector<LayoutNode> nodes;
    float total_width = 0;
    float total_height = 0;
    uint32_t num_layers = 0;
};

struct LayoutParams {
    float node_h_gap = 16;
    float layer_v_gap = 36;
    float padding = 20;
    bool use_network_simplex = true;
};

[[nodiscard]] inline LayoutResult sugiyama_layout(std::vector<LayoutNode> nodes, const std::vector<LayoutEdge>& edges,
                                                  const LayoutParams& params = {}) {
    const uint32_t n = static_cast<uint32_t>(nodes.size());
    if (n == 0) return {.nodes = {}, .total_width = 0, .total_height = 0};

    std::vector<std::vector<uint32_t>> fwd(n), rev(n);
    std::vector<uint32_t> in_deg(n, 0);
    for (const auto& e : edges) {
        if (e.src < n && e.dst < n && e.src != e.dst) {
            fwd[e.src].push_back(e.dst);
            rev[e.dst].push_back(e.src);
            in_deg[e.dst]++;
        }
    }

    std::vector<uint32_t> topo;
    {
        std::vector<uint32_t> queue;
        for (uint32_t i = 0; i < n; i++)
            if (in_deg[i] == 0) queue.push_back(i);

        std::vector<uint32_t> deg = in_deg;
        while (!queue.empty()) {
            uint32_t u = queue.back();
            queue.pop_back();
            topo.push_back(u);
            for (uint32_t v : fwd[u]) {
                if (--deg[v] == 0) queue.push_back(v);
            }
        }
    }

    for (uint32_t u : topo) {
        uint32_t max_pred = 0;
        for (uint32_t p : rev[u])
            max_pred = std::max(max_pred, nodes[p].layer + 1);
        nodes[u].layer = max_pred;
    }

    uint32_t num_layers = 0;
    for (const auto& nd : nodes)
        num_layers = std::max(num_layers, nd.layer + 1);

    // An edge spanning more than one layer is replaced by a chain of
    // single-layer edges through thin virtual nodes, one per intermediate
    // layer. Virtual nodes take part in crossing minimization and coordinate
    // assignment, so a long edge is routed through the layers it crosses
    // instead of cutting across them.

    const uint32_t original_n = static_cast<uint32_t>(nodes.size());
    uint32_t num_virtual = 0;
    for (const auto& e : edges) {
        if (e.src >= original_n || e.dst >= original_n) continue;
        int32_t span = static_cast<int32_t>(nodes[e.dst].layer) - static_cast<int32_t>(nodes[e.src].layer);
        if (span > 1) num_virtual += static_cast<uint32_t>(span - 1);
    }

    std::vector<std::vector<uint32_t>> fwd2, rev2;
    fwd2.resize(original_n + num_virtual);
    rev2.resize(original_n + num_virtual);

    uint32_t vnode_id = original_n;
    for (const auto& e : edges) {
        if (e.src >= original_n || e.dst >= original_n) continue;
        if (e.src == e.dst) continue;

        uint32_t src_layer = nodes[e.src].layer;
        uint32_t dst_layer = nodes[e.dst].layer;
        if (dst_layer <= src_layer) continue;

        int32_t span = static_cast<int32_t>(dst_layer - src_layer);
        if (span <= 1) {
            fwd2[e.src].push_back(e.dst);
            rev2[e.dst].push_back(e.src);
        } else {
            uint32_t prev = e.src;
            for (int32_t k = 1; k < span; k++) {
                LayoutNode vn{};
                vn.lw = 2;
                vn.rw = 2;
                vn.min_height = 4;
                vn.layer = src_layer + static_cast<uint32_t>(k);
                nodes.push_back(vn);
                uint32_t vid = vnode_id++;
                fwd2[prev].push_back(vid);
                rev2[vid].push_back(prev);
                prev = vid;
            }
            fwd2[prev].push_back(e.dst);
            rev2[e.dst].push_back(prev);
        }
    }

    for (const auto& e : edges) {
        if (e.src >= original_n || e.dst >= original_n) continue;
        if (e.src == e.dst) continue;
        uint32_t src_layer = nodes[e.src].layer;
        uint32_t dst_layer = nodes[e.dst].layer;
        if (dst_layer == src_layer + 1) {
            fwd2[e.src].push_back(e.dst);
            rev2[e.dst].push_back(e.src);
        }
    }

    fwd = std::move(fwd2);
    rev = std::move(rev2);

    const uint32_t total_n = static_cast<uint32_t>(nodes.size());

    std::vector<std::vector<uint32_t>> layers(num_layers);
    for (uint32_t i = 0; i < total_n; i++)
        layers[nodes[i].layer].push_back(i);

    for (auto& layer : layers) {
        for (uint32_t pos = 0; pos < layer.size(); pos++)
            nodes[layer[pos]].order = pos;
    }

    // The median of the neighbours' positions resists outliers better than
    // their mean, which is what the usual barycenter heuristic uses.
    auto neighbor_median = [&](uint32_t node_id, bool use_pred) -> float {
        const auto& adj = use_pred ? rev[node_id] : fwd[node_id];
        if (adj.empty()) return static_cast<float>(nodes[node_id].order);
        if (adj.size() == 1) return static_cast<float>(nodes[adj[0]].order);
        std::vector<float> positions;
        for (uint32_t a : adj)
            positions.push_back(static_cast<float>(nodes[a].order));
        std::ranges::sort(positions);
        size_t mid = positions.size() / 2;
        if (positions.size() % 2 == 0) return (positions[mid - 1] + positions[mid]) / 2;
        return positions[mid];
    };

    constexpr uint32_t MAX_SWEEPS = 8;
    for (uint32_t sweep = 0; sweep < MAX_SWEEPS; sweep++) {
        for (uint32_t l = 1; l < num_layers; l++) {
            auto& layer = layers[l];
            std::ranges::sort(
                layer, [&](uint32_t a, uint32_t b) { return neighbor_median(a, true) < neighbor_median(b, true); });
            for (uint32_t pos = 0; pos < layer.size(); pos++)
                nodes[layer[pos]].order = pos;
        }

        for (uint32_t l = num_layers - 1; l > 0; l--) {
            auto& layer = layers[l - 1];
            std::ranges::sort(
                layer, [&](uint32_t a, uint32_t b) { return neighbor_median(a, false) < neighbor_median(b, false); });
            for (uint32_t pos = 0; pos < layer.size(); pos++)
                nodes[layer[pos]].order = pos;
        }
    }

    // X placement is encoded as a rank problem on an auxiliary graph.
    // Adjacent nodes in a layer get a hard separation constraint and each
    // graph edge becomes a zero-length spring, so the solver minimizes
    // weighted edge bending subject to non-overlap.

    for (uint32_t l = 0; l < num_layers; l++) {
        std::ranges::sort(layers[l], [&](uint32_t a, uint32_t b) { return nodes[a].order < nodes[b].order; });
    }

    float total_width = 0;

    if (params.use_network_simplex) {
        std::vector<NSEdge> aux_edges;

        for (uint32_t l = 0; l < num_layers; l++) {
            const auto& layer = layers[l];
            for (uint32_t j = 1; j < layer.size(); j++) {
                uint32_t left = layer[j - 1];
                uint32_t right = layer[j];
                int32_t sep = static_cast<int32_t>(nodes[left].rw + nodes[right].lw + params.node_h_gap);
                aux_edges.push_back({left, right, sep, 0});  // Weight zero: a constraint with no pull.
            }
        }

        for (const auto& e : edges) {
            if (e.src < total_n && e.dst < total_n && e.src != e.dst) {
                aux_edges.push_back({e.src, e.dst, 0, 1});
            }
        }

        for (uint32_t l = 0; l < num_layers; l++) {
            if (layers[l].size() >= 2) {
                uint32_t first = layers[l].front();
                uint32_t last = layers[l].back();
                aux_edges.push_back({last, first, 0, 1});
            }
        }

        auto ns_result = network_simplex(total_n, aux_edges, 200);
        if (ns_result.converged) {
            for (uint32_t i = 0; i < total_n; i++) {
                nodes[i].x = params.padding + static_cast<float>(ns_result.rank[i]) + nodes[i].lw;
            }
            float max_x = 0;
            for (uint32_t i = 0; i < total_n; i++)
                max_x = std::max(max_x, nodes[i].x + nodes[i].rw);
            total_width = max_x + params.padding;
        } else {
            // Fall through to the naive placement below.
        }
    }

    // total_width is still at its zero initializer on both the disabled path
    // and the non-converged path. A <= 0 test is the sentinel for "untouched"
    // and does not trip -Wfloat-equal.
    if (total_width <= 0.0f) {
        std::vector<float> layer_widths(num_layers, 0);
        for (uint32_t l = 0; l < num_layers; l++) {
            float w = 0;
            for (uint32_t idx : layers[l])
                w += nodes[idx].width();
            if (!layers[l].empty()) w += params.node_h_gap * static_cast<float>(layers[l].size() - 1);
            layer_widths[l] = w;
        }
        float max_layer_width = *std::ranges::max_element(layer_widths);
        total_width = max_layer_width + 2 * params.padding;
        for (uint32_t l = 0; l < num_layers; l++) {
            float offset = params.padding + (max_layer_width - layer_widths[l]) / 2;
            float x = offset;
            for (uint32_t idx : layers[l]) {
                nodes[idx].x = x + nodes[idx].lw;
                x += nodes[idx].width() + params.node_h_gap;
            }
        }
    }

    std::vector<float> layer_heights(num_layers, 0);
    for (uint32_t i = 0; i < total_n; i++)
        layer_heights[nodes[i].layer] = std::max(layer_heights[nodes[i].layer], nodes[i].min_height);

    float y = params.padding;
    std::vector<float> layer_y(num_layers);
    for (uint32_t l = 0; l < num_layers; l++) {
        layer_y[l] = y;
        for (uint32_t idx : layers[l])
            nodes[idx].y = y;
        y += layer_heights[l] + params.layer_v_gap;
    }

    float total_height = y - params.layer_v_gap + params.padding;

    for (uint32_t sweep = 0; sweep < 4; sweep++) {
        for (uint32_t l = 1; l < num_layers; l++) {
            for (uint32_t idx : layers[l]) {
                if (rev[idx].empty()) continue;
                std::vector<float> pred_x;
                for (uint32_t p : rev[idx])
                    pred_x.push_back(nodes[p].x);
                std::ranges::sort(pred_x);
                float median = pred_x[pred_x.size() / 2];
                nodes[idx].x = 0.7f * nodes[idx].x + 0.3f * median;
            }
        }

        for (uint32_t l = num_layers - 1; l > 0; l--) {
            for (uint32_t idx : layers[l - 1]) {
                if (fwd[idx].empty()) continue;
                std::vector<float> succ_x;
                for (uint32_t s : fwd[idx])
                    succ_x.push_back(nodes[s].x);
                std::ranges::sort(succ_x);
                float median = succ_x[succ_x.size() / 2];
                nodes[idx].x = 0.7f * nodes[idx].x + 0.3f * median;
            }
        }

        // After this pass no two nodes in a layer sit closer than
        // node_h_gap. One left-to-right sweep is enough because a node is
        // only ever pushed right.
        for (uint32_t l = 0; l < num_layers; l++) {
            auto& layer = layers[l];
            std::ranges::sort(layer, [&](uint32_t a, uint32_t b) { return nodes[a].x < nodes[b].x; });
            for (uint32_t j = 1; j < layer.size(); j++) {
                uint32_t prev = layer[j - 1];
                uint32_t curr = layer[j];
                float min_x = nodes[prev].x + nodes[prev].rw + params.node_h_gap + nodes[curr].lw;
                if (nodes[curr].x < min_x) nodes[curr].x = min_x;
            }
            for (uint32_t pos = 0; pos < layer.size(); pos++)
                nodes[layer[pos]].order = pos;
        }
    }

    // The nudge and the overlap pass can widen the layout.
    float actual_max_x = 0;
    for (uint32_t i = 0; i < total_n; i++)
        actual_max_x = std::max(actual_max_x, nodes[i].x + nodes[i].rw);
    total_width = actual_max_x + params.padding;

    // The virtual nodes sit after the original ones, so truncating here
    // drops exactly them.
    nodes.resize(original_n);

    return LayoutResult{
        .nodes = std::move(nodes),
        .total_width = total_width,
        .total_height = total_height,
        .num_layers = num_layers,
    };
}

}  // namespace crucible::vis
