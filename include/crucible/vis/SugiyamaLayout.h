#pragma once

// Layered drawing of a directed graph after Sugiyama, Tagawa and Toda,
// "Methods for Visual Understanding of Hierarchical System Structures", IEEE
// SMC 11(2), 1981, with the x placement of Gansner, Koutsofios, North and Vo,
// "A Technique for Drawing Directed Graphs", IEEE TSE 19(3), 1993, section 4.2.
//
// The layout has five steps:
//  1. A topological order breaks each cycle. When no node is ready, the
//     unplaced node of least index goes next. Each edge then points from the
//     earlier node to the later one.
//  2. Longest-path layering puts each node one layer below its lowest
//     predecessor.
//  3. An edge that spans more than one layer becomes a chain through thin
//     virtual nodes, one in each layer between its ends. The virtual nodes
//     take part in crossing minimization and x placement, so a long edge goes
//     through the layers it crosses.
//  4. Sweeps down and up sort each layer by the median position of the
//     neighbors in the adjacent layer. The median resists outliers better
//     than the mean of the usual barycenter heuristic.
//  5. Network simplex on an auxiliary graph gives the x coordinates. The naive
//     placement with a median nudge is the fallback.
//
// This is not a hot path, so std::vector is appropriate.  The five steps are
// in src/vis/SugiyamaLayout.cpp.

#include <cstdint>
#include <span>
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

// The largest half width, height, gap or padding that the layout accepts, in
// pixels. It keeps each separation constraint of the simplex in int32_t.
inline constexpr float kMaxLayoutExtent = 1.0e6f;

// Each dimension and each parameter is in [0, kMaxLayoutExtent]. A NaN fails,
// because each comparison with a NaN is false.
[[nodiscard]] bool is_well_formed_layout_input(std::span<const LayoutNode> nodes, const LayoutParams& params) noexcept;

// The precondition is is_well_formed_layout_input(nodes, params), and the
// definition checks it.
[[nodiscard]] LayoutResult sugiyama_layout(std::span<const LayoutNode> nodes, std::span<const LayoutEdge> edges,
                                           const LayoutParams& params = {});

}  // namespace crucible::vis
