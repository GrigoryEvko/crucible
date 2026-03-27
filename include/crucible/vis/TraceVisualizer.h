#pragma once

// TraceVisualizer: end-to-end trace → SVG rendering.
//
// Pipeline: load .crtrace → detect blocks → build edges → layout → render SVG
//
// Two rendering modes:
//   - Block-level: blocks as containers with labels, inter-block edges
//   - Op-level: individual ops as nodes inside block containers
//
// Uses SugiyamaLayout for both intra-block and inter-block positioning.

#include <crucible/vis/BlockDetector.h>
#include <crucible/vis/SugiyamaLayout.h>
#include <crucible/vis/SvgRenderer.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crucible::vis {

// ═══════════════════════════════════════════════════════════════════
// Color selection by op family / block kind
// ═══════════════════════════════════════════════════════════════════

struct NodeColors {
  Color fill;
  Color border;
};

[[nodiscard]] inline NodeColors colors_for_family(OpFamily f) {
  switch (f) {
    case OpFamily::GEMM:   return {palette::GEMM_FILL, palette::GEMM_BORDER};
    case OpFamily::CONV:   return {palette::CONV_FILL, palette::CONV_BORDER};
    case OpFamily::NORM:   return {palette::NORM_FILL, palette::NORM_BORDER};
    case OpFamily::ATTN:   return {palette::ATTN_FILL, palette::ATTN_BORDER};
    case OpFamily::ACT:    return {palette::ACT_FILL, palette::ACT_BORDER};
    case OpFamily::ELEM:   return {palette::ELEM_FILL, palette::ELEM_BORDER};
    case OpFamily::MOVE:   return {palette::MOVE_FILL, palette::MOVE_BORDER};
    case OpFamily::REDUCE: return {palette::REDUCE_FILL, palette::REDUCE_BORDER};
    case OpFamily::OPTIM:  return {palette::OPTIM_FILL, palette::OPTIM_BORDER};
    case OpFamily::LOSS:   return {palette::LOSS_FILL, palette::LOSS_BORDER};
    case OpFamily::EMBED:  return {palette::NORM_FILL, palette::NORM_BORDER};
    case OpFamily::POOL:   return {palette::CONV_FILL, palette::CONV_BORDER};
    case OpFamily::OTHER:  return {palette::OTHER_FILL, palette::OTHER_BORDER};
  }
  return {palette::OTHER_FILL, palette::OTHER_BORDER};
}

[[nodiscard]] inline NodeColors colors_for_block(BlockKind k) {
  switch (k) {
    case BlockKind::RESBLOCK: case BlockKind::RESBLOCK_BWD:
      return {palette::BLOCK_RESBLOCK, Color::hex(0x065F46)};
    case BlockKind::SELF_ATTN: case BlockKind::SELF_ATTN_BWD:
    case BlockKind::CROSS_ATTN: case BlockKind::CROSS_ATTN_BWD:
    case BlockKind::TRANSFORMER: case BlockKind::TRANSFORMER_BWD:
    case BlockKind::ATTN_BWD:
      return {palette::BLOCK_ATTN, Color::hex(0x92400E)};
    case BlockKind::MLP: case BlockKind::MLP_BWD:
      return {palette::BLOCK_MLP, Color::hex(0x1E40AF)};
    case BlockKind::CONV: case BlockKind::CONV_BWD:
    case BlockKind::CONV_BLOCK: case BlockKind::CONV_BLOCK_BWD:
    case BlockKind::DOWNSAMPLE: case BlockKind::DOWNSAMPLE_BWD:
    case BlockKind::UPSAMPLE: case BlockKind::UPSAMPLE_BWD:
      return {palette::BLOCK_CONV, Color::hex(0x0C4A6E)};
    case BlockKind::LOSS: case BlockKind::LOSS_BWD:
      return {palette::BLOCK_LOSS, Color::hex(0xBE123C)};
    case BlockKind::OPTIMIZER: case BlockKind::EPILOGUE:
      return {palette::BLOCK_OPTIM, Color::hex(0x9D174D)};
    case BlockKind::TIME_EMBED: case BlockKind::TIME_EMBED_BWD:
      return {palette::BLOCK_GENERIC, Color::hex(0x374151)};
    default:
      return {palette::BLOCK_GENERIC, Color::hex(0x6B7280)};
  }
}

// ═══════════════════════════════════════════════════════════════════
// Inter-block edge construction
// ═══════════════════════════════════════════════════════════════════

struct BlockEdge {
  uint32_t src_block = 0;
  uint32_t dst_block = 0;
  bool is_skip = false;  // skip connection (spans >1 block)
};

[[nodiscard]] inline std::vector<BlockEdge> build_block_edges(
    const std::vector<Block>& blocks,
    const std::vector<Op>& ops) {

  // Map op_idx → block_idx
  std::unordered_map<uint32_t, uint32_t> op_to_block;
  for (uint32_t bi = 0; bi < blocks.size(); bi++) {
    for (uint32_t oi = blocks[bi].start_op; oi <= blocks[bi].end_op; oi++)
      op_to_block[oi] = bi;
  }

  // Map data_ptr → producing op_idx
  std::unordered_map<uint64_t, uint32_t> ptr_producer;
  for (const auto& op : ops) {
    for (uint32_t j = 0; j < op.n_out && j < 4; j++) {
      if (op.data_ptr_out[j])
        ptr_producer[op.data_ptr_out[j]] = op.idx;
    }
  }

  // Find block-level edges
  std::unordered_set<uint64_t> seen_edges;
  std::vector<BlockEdge> edges;

  for (const auto& op : ops) {
    auto dit = op_to_block.find(op.idx);
    if (dit == op_to_block.end()) continue;
    uint32_t dst_bi = dit->second;

    for (uint32_t j = 0; j < op.n_in && j < 8; j++) {
      uint64_t ptr = op.data_ptr_in[j];
      if (!ptr) continue;
      auto pit = ptr_producer.find(ptr);
      if (pit == ptr_producer.end()) continue;

      auto sit = op_to_block.find(pit->second);
      if (sit == op_to_block.end()) continue;
      uint32_t src_bi = sit->second;

      if (src_bi == dst_bi) continue;  // intra-block

      // Deduplicate
      uint64_t key = (static_cast<uint64_t>(src_bi) << 32) | dst_bi;
      if (!seen_edges.insert(key).second) continue;

      bool skip = (dst_bi > src_bi + 1);
      edges.push_back({src_bi, dst_bi, skip});
    }
  }

  // Add implicit sequential edges: block N → block N+1.
  // These form the "spine" that creates vertical ordering in the layout.
  // Without them, blocks with no data-flow connection get assigned to
  // the same layer and pile up horizontally.
  for (uint32_t i = 0; i + 1 < blocks.size(); i++) {
    uint64_t key = (static_cast<uint64_t>(i) << 32) | (i + 1);
    if (seen_edges.insert(key).second)
      edges.push_back({i, i + 1, false});
  }

  return edges;
}

// ═══════════════════════════════════════════════════════════════════
// Phase-aware grid layout
//
// Two columns: forward (left) + backward (right, reversed to mirror).
// Optimizer centered below. Skip connections as horizontal arrows.
// No Sugiyama needed — blocks are in execution order, backward
// mirrors forward by autograd construction.
// ═══════════════════════════════════════════════════════════════════

struct GridPos {
  float x = 0;   // left edge
  float y = 0;   // top edge
  float w = 0;   // width
  float h = 0;   // height
  uint32_t col = 0;  // 0=forward, 1=backward, 2=optimizer
  uint32_t row = 0;
};

[[nodiscard]] inline std::vector<GridPos> grid_layout(
    const std::vector<Block>& blocks,
    const std::vector<BlockEdge>& edges) {

  // Separate by phase
  std::vector<uint32_t> fwd_idx, bwd_idx, opt_idx;
  for (uint32_t i = 0; i < blocks.size(); i++) {
    switch (blocks[i].phase) {
      case Phase::FORWARD:  fwd_idx.push_back(i); break;
      case Phase::BACKWARD: bwd_idx.push_back(i); break;
      case Phase::OPTIMIZER: opt_idx.push_back(i); break;
    }
  }

  // Geometry constants
  constexpr float COL_WIDTH = 260;
  constexpr float COL_GAP = 80;     // gap between forward and backward columns
  constexpr float ROW_HEIGHT = 32;
  constexpr float ROW_GAP = 4;
  constexpr float PAD = 40;
  constexpr float HEADER = 50;      // space for title

  uint32_t max_rows = std::max(
      static_cast<uint32_t>(fwd_idx.size()),
      static_cast<uint32_t>(bwd_idx.size()));

  std::vector<GridPos> pos(blocks.size());

  // Forward: column 0, sequential rows
  for (uint32_t r = 0; r < fwd_idx.size(); r++) {
    uint32_t bi = fwd_idx[r];
    float lw = std::min(COL_WIDTH,
        std::max(120.0f, static_cast<float>(blocks[bi].label.size()) * 8.0f + 16));
    pos[bi] = {
        .x = PAD + (COL_WIDTH - lw) / 2,
        .y = HEADER + r * (ROW_HEIGHT + ROW_GAP),
        .w = lw,
        .h = ROW_HEIGHT,
        .col = 0,
        .row = r,
    };
  }

  // Backward: column 1, REVERSED rows to mirror forward.
  // Backward block 0 (Loss BWD) pairs with last forward block.
  // Backward block N pairs with forward block (n_fwd - 1 - N).
  for (uint32_t r = 0; r < bwd_idx.size(); r++) {
    uint32_t bi = bwd_idx[r];
    // Reverse: first backward block gets row = n_fwd-1, last gets row 0
    uint32_t mirror_row = (bwd_idx.size() > fwd_idx.size())
        ? r   // more backward blocks than forward — just stack sequentially
        : static_cast<uint32_t>(fwd_idx.size()) - 1 - r;
    // Clamp to valid range
    if (mirror_row >= max_rows) mirror_row = max_rows - 1;

    float lw = std::min(COL_WIDTH,
        std::max(120.0f, static_cast<float>(blocks[bi].label.size()) * 8.0f + 16));
    pos[bi] = {
        .x = PAD + COL_WIDTH + COL_GAP + (COL_WIDTH - lw) / 2,
        .y = HEADER + mirror_row * (ROW_HEIGHT + ROW_GAP),
        .w = lw,
        .h = ROW_HEIGHT,
        .col = 1,
        .row = mirror_row,
    };
  }

  // Optimizer: centered below both columns
  float opt_y = HEADER + max_rows * (ROW_HEIGHT + ROW_GAP) + 20;
  float center_x = PAD + COL_WIDTH + COL_GAP / 2;
  for (uint32_t r = 0; r < opt_idx.size(); r++) {
    uint32_t bi = opt_idx[r];
    float lw = std::min(COL_WIDTH * 2 + COL_GAP,
        std::max(200.0f, static_cast<float>(blocks[bi].label.size()) * 8.0f + 16));
    pos[bi] = {
        .x = center_x - lw / 2,
        .y = opt_y + r * (ROW_HEIGHT + ROW_GAP),
        .w = lw,
        .h = ROW_HEIGHT,
        .col = 2,
        .row = max_rows + r,
    };
  }

  return pos;
}

// ═══════════════════════════════════════════════════════════════════
// Block-level rendering with phase-aware grid layout
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline std::string render_block_svg(
    const DetectionResult& detection,
    const std::vector<Op>& ops,
    std::string_view title = "Crucible Trace") {

  const auto& blocks = detection.blocks;
  if (blocks.empty()) return {};

  auto block_edges = build_block_edges(blocks, ops);
  auto pos = grid_layout(blocks, block_edges);

  // Compute total size
  float max_x = 0, max_y = 0;
  for (uint32_t i = 0; i < blocks.size(); i++) {
    max_x = std::max(max_x, pos[i].x + pos[i].w);
    max_y = std::max(max_y, pos[i].y + pos[i].h);
  }
  float svg_w = max_x + 40;
  float svg_h = max_y + 40;

  SvgRenderer svg;
  svg.begin(svg_w, svg_h, std::string{title});

  // Column headers
  float col0_center = 40 + 130;
  float col1_center = 40 + 260 + 80 + 130;
  svg.text(col0_center, 42, "FORWARD", 12, Color::hex(0x1E40AF), "middle", true);
  svg.text(col1_center, 42, "BACKWARD", 12, Color::hex(0x9A3412), "middle", true);

  // Draw skip edges first (behind blocks)
  svg.begin_group("skip-edges");
  for (const auto& e : block_edges) {
    if (!e.is_skip) continue;
    // Only draw cross-column skip edges (forward→backward)
    if (pos[e.src_block].col == 0 && pos[e.dst_block].col == 1) {
      float x1 = pos[e.src_block].x + pos[e.src_block].w;
      float y1 = pos[e.src_block].y + pos[e.src_block].h / 2;
      float x2 = pos[e.dst_block].x;
      float y2 = pos[e.dst_block].y + pos[e.dst_block].h / 2;
      float dx = x2 - x1;
      float dy = y2 - y1;
      float cx1 = x1 + dx * 0.3f;
      float cy1 = y1;
      float cx2 = x2 - dx * 0.3f;
      float cy2 = y2;
      svg.bezier_arrow(x1, y1, cx1, cy1, cx2, cy2, x2, y2,
                        palette::EDGE_SKIP, 0.6f, true);
    }
  }
  svg.end_group();

  // Draw sequential edges within columns
  svg.begin_group("seq-edges");
  for (const auto& e : block_edges) {
    if (e.is_skip) continue;
    if (pos[e.src_block].col == pos[e.dst_block].col) {
      float x1 = pos[e.src_block].x + pos[e.src_block].w / 2;
      float y1 = pos[e.src_block].y + pos[e.src_block].h;
      float x2 = pos[e.dst_block].x + pos[e.dst_block].w / 2;
      float y2 = pos[e.dst_block].y;
      if (y2 > y1) // only draw downward
        svg.arrow(x1, y1, x2, y2, palette::EDGE_DATA_FLOW, 0.35f);
    }
  }
  svg.end_group();

  // Draw blocks
  svg.begin_group("blocks");
  for (uint32_t i = 0; i < blocks.size(); i++) {
    const auto& b = blocks[i];
    const auto& p = pos[i];
    auto [fill, border] = colors_for_block(b.kind);

    svg.rect(p.x, p.y, p.w, p.h, fill, border, 5, 0.8f, true);
    svg.text(p.x + p.w / 2, p.y + p.h / 2 + 4,
             b.label, 8, Color::hex(0x1F2937), "middle", true);
  }
  svg.end_group();

  svg.end();
  return svg.take();
}

// ═══════════════════════════════════════════════════════════════════
// Full rendering with ops inside blocks
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline std::string render_full_svg(
    const DetectionResult& detection,
    const std::vector<Op>& ops,
    std::string_view title = "Crucible Trace") {

  const auto& blocks = detection.blocks;
  if (blocks.empty()) return {};

  // ── Step 1: Layout ops inside each block ──────────────────────────

  struct IntraBlockLayout {
    LayoutResult layout;
    float width = 0;
    float height = 0;
  };

  std::vector<IntraBlockLayout> intra(blocks.size());

  // Map data_ptr → producing op (for intra-block edges)
  std::unordered_map<uint64_t, uint32_t> ptr_producer;
  for (const auto& op : ops) {
    for (uint32_t j = 0; j < op.n_out && j < 4; j++) {
      if (op.data_ptr_out[j])
        ptr_producer[op.data_ptr_out[j]] = op.idx;
    }
  }

  for (uint32_t bi = 0; bi < blocks.size(); bi++) {
    const auto& b = blocks[bi];
    uint32_t n_ops = b.end_op - b.start_op + 1;

    // For large blocks (optimizer), don't layout individual ops
    if (n_ops > 200 || b.kind == BlockKind::OPTIMIZER ||
        b.kind == BlockKind::EPILOGUE) {
      intra[bi].width = 160;
      intra[bi].height = 44;
      continue;
    }

    // Build local op nodes and edges
    std::vector<LayoutNode> op_nodes(n_ops);
    std::vector<LayoutEdge> op_edges;

    for (uint32_t i = 0; i < n_ops; i++) {
      const auto& op = ops[b.start_op + i];
      float label_w = op.name ? static_cast<float>(std::strlen(op.name)) * 6.5f + 16
                              : 60;
      op_nodes[i].min_width = std::max(50.0f, std::min(label_w, 180.0f));
      op_nodes[i].min_height = 24;
    }

    // Intra-block edges from data_ptr matching
    for (uint32_t i = 0; i < n_ops; i++) {
      const auto& op = ops[b.start_op + i];
      for (uint32_t j = 0; j < op.n_in && j < 8; j++) {
        uint64_t ptr = op.data_ptr_in[j];
        if (!ptr) continue;
        auto it = ptr_producer.find(ptr);
        if (it == ptr_producer.end()) continue;
        uint32_t src_global = it->second;
        if (src_global >= b.start_op && src_global <= b.end_op) {
          uint32_t src_local = src_global - b.start_op;
          uint32_t dst_local = i;
          if (src_local != dst_local)
            op_edges.push_back({src_local, dst_local});
        }
      }
    }

    LayoutParams op_params{
        .node_h_gap = 10,
        .layer_v_gap = 8,
        .padding = 8,
    };
    intra[bi].layout = sugiyama_layout(
        std::move(op_nodes), op_edges, op_params);
    intra[bi].width = std::max(100.0f, intra[bi].layout.total_width);
    intra[bi].height = std::max(44.0f, intra[bi].layout.total_height);
  }

  // ── Step 2: Layout blocks using their computed sizes ──────────────

  auto block_edges = build_block_edges(blocks, ops);

  std::vector<LayoutNode> block_nodes(blocks.size());
  for (uint32_t i = 0; i < blocks.size(); i++) {
    // Block container = intra layout + header (28px for title)
    float label_w = static_cast<float>(blocks[i].label.size()) * 8.0f + 20;
    block_nodes[i].min_width = std::max(intra[i].width + 12, label_w);
    block_nodes[i].min_height = intra[i].height + 32;  // +32 for header
  }

  std::vector<LayoutEdge> block_layout_edges;
  block_layout_edges.reserve(block_edges.size());
  for (const auto& e : block_edges)
    block_layout_edges.push_back({e.src_block, e.dst_block});

  LayoutParams block_params{
      .node_h_gap = 28,
      .layer_v_gap = 16,
      .padding = 50,
  };
  auto block_layout = sugiyama_layout(
      std::move(block_nodes), block_layout_edges, block_params);

  // ── Step 3: Render SVG ────────────────────────────────────────────

  SvgRenderer svg;
  svg.begin(block_layout.total_width,
            block_layout.total_height + 50,
            std::string{title});

  // Inter-block edges (drawn first, under blocks)
  svg.begin_group("inter-edges");
  for (const auto& e : block_edges) {
    const auto& src = block_layout.nodes[e.src_block];
    const auto& dst = block_layout.nodes[e.dst_block];
    float x1 = src.x;
    float y1 = src.y + block_layout.nodes[e.src_block].min_height;
    float x2 = dst.x;
    float y2 = dst.y;

    float dy = y2 - y1;
    float dx = x2 - x1;

    if (e.is_skip && std::abs(dx) > 10) {
      float cx1 = x1 + dx * 0.2f;
      float cy1 = y1 + dy * 0.4f;
      float cx2 = x2 - dx * 0.2f;
      float cy2 = y2 - dy * 0.4f;
      svg.bezier_arrow(x1, y1, cx1, cy1, cx2, cy2, x2, y2,
                        palette::EDGE_SKIP, 0.8f, true);
    } else {
      svg.arrow(x1, y1, x2, y2,
                palette::EDGE_DATA_FLOW, 0.4f);
    }
  }
  svg.end_group();

  // Block containers + internal ops
  svg.begin_group("blocks");
  for (uint32_t bi = 0; bi < blocks.size(); bi++) {
    const auto& b = blocks[bi];
    const auto& bnd = block_layout.nodes[bi];
    auto [fill, border] = colors_for_block(b.kind);

    float bx = bnd.x - bnd.min_width / 2;
    float by = bnd.y;
    float bw = bnd.min_width;
    float bh = bnd.min_height;

    // Block container
    svg.block_container(bx, by, bw, bh, b.label, fill, border,
                        b.out_shape);

    // Draw internal ops (if layout was computed)
    const auto& il = intra[bi];
    if (!il.layout.nodes.empty()) {
      float ox = bx + 6;  // offset for internal ops
      float oy = by + 30; // below header

      svg.begin_group();

      // Intra-block edges
      for (uint32_t oi = b.start_op; oi <= b.end_op; oi++) {
        const auto& op = ops[oi];
        uint32_t local_dst = oi - b.start_op;
        if (local_dst >= il.layout.nodes.size()) continue;

        for (uint32_t j = 0; j < op.n_in && j < 8; j++) {
          uint64_t ptr = op.data_ptr_in[j];
          if (!ptr) continue;
          auto it = ptr_producer.find(ptr);
          if (it == ptr_producer.end()) continue;
          uint32_t src_global = it->second;
          if (src_global < b.start_op || src_global > b.end_op) continue;
          uint32_t local_src = src_global - b.start_op;
          if (local_src >= il.layout.nodes.size()) continue;
          if (local_src == local_dst) continue;

          const auto& sn = il.layout.nodes[local_src];
          const auto& dn = il.layout.nodes[local_dst];
          svg.line(ox + sn.x, oy + sn.y + sn.min_height,
                   ox + dn.x, oy + dn.y,
                   Color::hex(0xCBD5E1), 0.3f);
        }
      }

      // Op nodes
      for (uint32_t i = 0; i < il.layout.nodes.size(); i++) {
        uint32_t oi = b.start_op + i;
        if (oi >= ops.size()) break;
        const auto& op = ops[oi];
        const auto& nd = il.layout.nodes[i];
        auto [of, ob] = colors_for_family(op.family);

        float nx = ox + nd.x - nd.min_width / 2;
        float ny = oy + nd.y;
        std::string_view label = op.name ? op.name : "?";
        svg.op_node(nx, ny, nd.min_width, nd.min_height,
                    label, of, ob, 7);

        // Shape subtitle
        std::string shp = shape_string(op);
        if (!shp.empty()) {
          svg.text_mono(ox + nd.x, ny + nd.min_height + 8,
                        shp, 5, Color::hex(0x9CA3AF));
        }
      }

      svg.end_group();
    }
  }
  svg.end_group();

  svg.end();
  return svg.take();
}

} // namespace crucible::vis
