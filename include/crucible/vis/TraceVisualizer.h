#pragma once

// The block view of a Merkle DAG and its SVG rendering.  The bodies are in
// src/vis/TraceVisualizer.cpp.

#include <crucible/vis/BlockDetector.h>
#include <crucible/vis/SvgRenderer.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crucible {
struct LoopNode;
struct RegionNode;
struct TensorMeta;
struct TraceNode;
}  // namespace crucible

namespace crucible::vis {

enum class DagEdgeKind : uint8_t {
    SEQUENTIAL,
    BRANCH_ARM,
    LOOP_BODY,
    LOOP_FEEDBACK,
};

[[nodiscard]] constexpr const char* dag_edge_kind_name(DagEdgeKind kind) {
    switch (kind) {
        case DagEdgeKind::SEQUENTIAL:
            return "seq";
        case DagEdgeKind::BRANCH_ARM:
            return "branch";
        case DagEdgeKind::LOOP_BODY:
            return "loop-body";
        case DagEdgeKind::LOOP_FEEDBACK:
            return "loop-feedback";
        default:
            return "unknown";
    }
}

struct DagBlockEdge {
    uint32_t src_block = 0;
    uint32_t dst_block = 0;
    DagEdgeKind kind = DagEdgeKind::SEQUENTIAL;
    std::string label;
};

struct MerkleDagBlockView {
    DetectionResult detection;
    std::vector<DagBlockEdge> edges;
};

[[nodiscard]] std::string hex64_short(uint64_t value);

[[nodiscard]] std::string tensor_shape_string(const TensorMeta& meta);

[[nodiscard]] std::string loop_label(const LoopNode& loop);

[[nodiscard]] MerkleDagBlockView extract_block_view(const TraceNode* root);

[[nodiscard]] MerkleDagBlockView extract_block_view(const TraceNode& root);

[[nodiscard]] MerkleDagBlockView extract_block_view(const RegionNode& root);

[[nodiscard]] DetectionResult extract_blocks(const TraceNode* root);

[[nodiscard]] DetectionResult extract_blocks(const TraceNode& root);

[[nodiscard]] DetectionResult extract_blocks(const RegionNode& root);

struct NodeColors {
    Color fill;
    Color border;
};

[[nodiscard]] NodeColors colors_for_family(OpFamily f);

[[nodiscard]] NodeColors colors_for_block(BlockKind k);

[[nodiscard]] Color color_for_dag_edge(DagEdgeKind kind);

struct BlockEdge {
    uint32_t src_block = 0;
    uint32_t dst_block = 0;
    bool is_skip = false;  // True when the edge spans more than one block.
};

[[nodiscard]] std::vector<BlockEdge> build_block_edges(const std::vector<Block>& blocks, const std::vector<Op>& ops);

// No graph solver is needed here. Blocks already arrive in execution order,
// and autograd makes the backward sequence mirror the forward one, so rows
// follow directly from those two orders.

struct GridPos {
    float x = 0;  // Left edge.
    float y = 0;  // Top edge.
    float w = 0;
    float h = 0;
    uint32_t col = 0;  // 0 is forward, 1 is backward, 2 is optimizer.
    uint32_t row = 0;
};

// The two indices split phase_idx into an encoder over [0, enc_end], a mid
// section over [enc_end + 1, dec_start - 1] and a decoder over
// [dec_start, end].
struct UShapeSplit {
    uint32_t enc_end = 0;
    uint32_t dec_start = 0;
    bool is_unet = false;
};

[[nodiscard]] UShapeSplit detect_u_shape(const std::vector<Block>& blocks, const std::vector<uint32_t>& phase_idx);

[[nodiscard]] std::vector<GridPos> grid_layout(const std::vector<Block>& blocks, const std::vector<BlockEdge>& edges,
                                               Architecture arch);

void render_skip_edges(SvgRenderer& svg, const DetectionResult& detection, const std::vector<Block>& blocks,
                       const std::vector<GridPos>& pos, const std::vector<BlockEdge>& block_edges);

void render_seq_connectors(SvgRenderer& svg, const std::vector<Block>& blocks, const std::vector<GridPos>& pos);

void render_resolution_bands(SvgRenderer& svg, float svg_w, const std::vector<Block>& blocks,
                             const std::vector<GridPos>& pos);

void render_block_nodes(SvgRenderer& svg, const std::vector<Block>& blocks, const std::vector<GridPos>& pos);

void render_phase_borders(SvgRenderer& svg, const std::vector<Block>& blocks, const std::vector<GridPos>& pos);

void render_legend(SvgRenderer& svg, float lx, float ly);

[[nodiscard]] std::string render_block_svg(const DetectionResult& detection, const std::vector<Op>& ops,
                                           std::string_view title = "Crucible Trace");

[[nodiscard]] std::string render_live_block_svg(const MerkleDagBlockView& view,
                                                std::string_view title = "Crucible Live Trace");

}  // namespace crucible::vis
