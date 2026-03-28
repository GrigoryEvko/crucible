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
    case BlockKind::MODULE:
      return {palette::BLOCK_MLP, Color::hex(0x1E40AF)};
    case BlockKind::MODULE_BWD:
      return {palette::BLOCK_BWD_MLP, Color::hex(0x1E40AF)};
    case BlockKind::OPTIMIZER: case BlockKind::EPILOGUE:
      return {palette::BLOCK_OPTIM, Color::hex(0x9D174D)};
    case BlockKind::ROOT:
      return {palette::BLOCK_GENERIC, Color::hex(0x6B7280)};
  }
  return {palette::BLOCK_GENERIC, Color::hex(0x6B7280)};
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

// Detect encoder/mid/decoder within a phase's blocks using spatial resolution.
// Returns (encoder_end, decoder_start) indices into the phase_idx array.
// encoder = [0..encoder_end], mid = [encoder_end+1..decoder_start-1],
// decoder = [decoder_start..end]
struct UShapeSplit {
  uint32_t enc_end = 0;      // last encoder index
  uint32_t dec_start = 0;    // first decoder index
  bool is_unet = false;
};

[[nodiscard]] inline UShapeSplit detect_u_shape(
    const std::vector<Block>& blocks,
    const std::vector<uint32_t>& phase_idx) {

  if (phase_idx.size() < 6) return {0, 0, false};

  // Get spatial resolution per block (0 if not 4D)
  std::vector<int32_t> res(phase_idx.size(), 0);
  for (uint32_t i = 0; i < phase_idx.size(); i++)
    res[i] = blocks[phase_idx[i]].spatial_h;

  // Find blocks with valid resolution
  std::vector<std::pair<uint32_t, int32_t>> valid_res;
  for (uint32_t i = 0; i < res.size(); i++)
    if (res[i] > 0) valid_res.push_back({i, res[i]});

  if (valid_res.size() < 4) return {0, 0, false};

  // Find minimum resolution (bottleneck)
  int32_t min_res = valid_res[0].second;
  for (const auto& [idx, r] : valid_res) {
    if (r < min_res) min_res = r;
  }

  // Check if there's a U-shape: resolution decreases then increases
  int32_t max_res = valid_res[0].second;
  if (max_res < 2 * min_res) return {0, 0, false};

  // Find last block before bottleneck with max resolution (encoder end region)
  // and first block after bottleneck with increasing resolution (decoder start)
  uint32_t enc_end = 0;
  uint32_t dec_start = static_cast<uint32_t>(phase_idx.size()) - 1;

  // Encoder: find where resolution first reaches minimum
  for (uint32_t i = 0; i < phase_idx.size(); i++) {
    if (res[i] == min_res || (res[i] > 0 && res[i] <= min_res)) {
      enc_end = (i > 0) ? i - 1 : 0;
      break;
    }
  }

  // Decoder: find where resolution starts increasing after bottleneck
  bool past_mid = false;
  for (uint32_t i = enc_end + 1; i < phase_idx.size(); i++) {
    if (res[i] == min_res) {
      past_mid = true;
      continue;
    }
    if (past_mid && res[i] > min_res) {
      dec_start = i;
      break;
    }
  }

  return {enc_end, dec_start, true};
}

[[nodiscard]] inline std::vector<GridPos> grid_layout(
    const std::vector<Block>& blocks,
    const std::vector<BlockEdge>& /*edges*/,
    Architecture arch) {

  // Separate by phase
  std::vector<uint32_t> fwd_idx, bwd_idx, opt_idx;
  for (uint32_t i = 0; i < blocks.size(); i++) {
    switch (blocks[i].phase) {
      case Phase::FORWARD:  fwd_idx.push_back(i); break;
      case Phase::BACKWARD: bwd_idx.push_back(i); break;
      case Phase::OPTIMIZER: opt_idx.push_back(i); break;
    }
  }

  // Geometry
  constexpr float SUB_COL_W = 220;   // sub-column width (encoder/decoder)
  constexpr float SUB_GAP = 10;      // gap between encoder and decoder sub-columns
  constexpr float PHASE_GAP = 50;    // gap between forward U and backward U
  constexpr float ROW_GAP = 3;
  constexpr float PAD = 25;
  constexpr float HEADER = 50;

  constexpr float ROW_H = 26;

  std::vector<GridPos> pos(blocks.size());

  auto label_width = [&](uint32_t bi) -> float {
    return std::min(SUB_COL_W,
        std::max(90.0f, static_cast<float>(blocks[bi].label.size()) * 7.0f + 12));
  };

  // ── UNet layout: two U-shapes side by side ──────────────────────────
  if (arch == Architecture::UNET) {
    auto fwd_u = detect_u_shape(blocks, fwd_idx);

    // Layout one U-shape phase into sub-columns.
    // Encoder: left sub-column, going DOWN (highest res at top).
    // Decoder: right sub-column, going DOWN in REVERSED order
    //          (highest res at top, matching encoder rows).
    // Mid: centered below both, at the bottom.
    // The U-shape is visual: skip connections between matching
    // resolution rows create horizontal lines.
    auto layout_u = [&](const std::vector<uint32_t>& idx,
                        const UShapeSplit& u, float base_x) -> uint32_t {
      if (!u.is_unet || idx.empty()) {
        // Fallback: single column
        for (uint32_t r = 0; r < idx.size(); r++) {
          float lw = label_width(idx[r]);
          pos[idx[r]] = {
            .x = base_x + (SUB_COL_W - lw) / 2,
            .y = HEADER + r * (ROW_H + ROW_GAP),
            .w = lw, .h = ROW_H, .col = 0, .row = r,
          };
        }
        return static_cast<uint32_t>(idx.size());
      }

      uint32_t enc_count = u.enc_end + 1;
      uint32_t dec_count = static_cast<uint32_t>(idx.size()) - u.dec_start;

      // Encoder and decoder go DOWN in parallel columns.
      // Use max(enc, dec) rows for the paired section, then mid below.
      uint32_t paired_rows = std::max(enc_count, dec_count);

      // Encoder: left sub-column, rows 0..enc_count-1 (top to bottom)
      for (uint32_t i = 0; i < enc_count && i < idx.size(); i++) {
        float lw = label_width(idx[i]);
        pos[idx[i]] = {
          .x = base_x + (SUB_COL_W - lw) / 2,
          .y = HEADER + i * (ROW_H + ROW_GAP),
          .w = lw, .h = ROW_H, .col = 0, .row = i,
        };
      }

      // Decoder: right sub-column, REVERSED order so highest resolution
      // is at top (row 0) matching encoder's first blocks.
      // decoder_blocks = [dec_start..end], reversed = [end..dec_start]
      for (uint32_t i = 0; i < dec_count; i++) {
        uint32_t block_i = u.dec_start + dec_count - 1 - i;  // reverse
        float lw = label_width(idx[block_i]);
        pos[idx[block_i]] = {
          .x = base_x + SUB_COL_W + SUB_GAP + (SUB_COL_W - lw) / 2,
          .y = HEADER + i * (ROW_H + ROW_GAP),
          .w = lw, .h = ROW_H, .col = 1, .row = i,
        };
      }

      // Mid: centered below both columns
      uint32_t mid_row = paired_rows;
      float mid_center = base_x + SUB_COL_W + SUB_GAP / 2;
      for (uint32_t i = u.enc_end + 1; i < u.dec_start && i < idx.size(); i++) {
        float lw = label_width(idx[i]);
        pos[idx[i]] = {
          .x = mid_center - lw / 2,
          .y = HEADER + mid_row * (ROW_H + ROW_GAP),
          .w = lw, .h = ROW_H, .col = 0, .row = mid_row,
        };
        mid_row++;
      }

      return mid_row;  // total rows used by this U
    };

    // Forward U: left side
    float fwd_base = PAD;
    uint32_t fwd_rows = layout_u(fwd_idx, fwd_u, fwd_base);

    // Backward U: right side
    float bwd_base = PAD + 2 * SUB_COL_W + SUB_GAP + PHASE_GAP;
    auto bwd_u = detect_u_shape(blocks, bwd_idx);
    uint32_t bwd_rows = layout_u(bwd_idx, bwd_u, bwd_base);

    // Optimizer: centered below everything
    uint32_t max_used_rows = std::max(fwd_rows, bwd_rows);
    float opt_y = HEADER + max_used_rows * (ROW_H + ROW_GAP) + 20;
    float total_w = 2 * (2 * SUB_COL_W + SUB_GAP) + PHASE_GAP;
    float center_x = PAD + total_w / 2;
    for (uint32_t r = 0; r < opt_idx.size(); r++) {
      uint32_t bi = opt_idx[r];
      float lw = std::min(total_w, std::max(180.0f, label_width(bi)));
      pos[bi] = {
        .x = center_x - lw / 2,
        .y = opt_y + r * (ROW_H + ROW_GAP),
        .w = lw, .h = ROW_H, .col = 2, .row = 0,
      };
    }

    return pos;
  }

  // ── Default layout: two columns (forward + backward) ────────────────
  uint32_t n_fwd = static_cast<uint32_t>(fwd_idx.size());
  uint32_t n_bwd = static_cast<uint32_t>(bwd_idx.size());
  uint32_t max_rows = std::max(n_fwd, n_bwd);
  constexpr float COL_WIDTH = 240;
  constexpr float COL_GAP = 60;

  for (uint32_t r = 0; r < n_fwd; r++) {
    float lw = label_width(fwd_idx[r]);
    pos[fwd_idx[r]] = {
      .x = PAD + (COL_WIDTH - lw) / 2,
      .y = HEADER + r * (ROW_H + ROW_GAP),
      .w = lw, .h = ROW_H, .col = 0, .row = r,
    };
  }
  // Backward column: REVERSED order. Autograd generates backward ops
  // in reverse order of forward. backward[0] = Loss BWD (mirrors
  // forward[-1] = Loss). Reversing puts Loss BWD at the bottom,
  // matching Loss on the left. Skip connections become horizontal.
  for (uint32_t r = 0; r < n_bwd; r++) {
    uint32_t rev_r = n_bwd - 1 - r;  // reverse: last bwd block at row 0
    float lw = label_width(bwd_idx[r]);
    pos[bwd_idx[r]] = {
      .x = PAD + COL_WIDTH + COL_GAP + (COL_WIDTH - lw) / 2,
      .y = HEADER + rev_r * (ROW_H + ROW_GAP),
      .w = lw, .h = ROW_H, .col = 1, .row = rev_r,
    };
  }

  float opt_y = HEADER + max_rows * (ROW_H + ROW_GAP) + 16;
  float center_x = PAD + COL_WIDTH + COL_GAP / 2;
  for (uint32_t r = 0; r < opt_idx.size(); r++) {
    uint32_t bi = opt_idx[r];
    float lw = std::min(COL_WIDTH * 2 + COL_GAP, std::max(180.0f, label_width(bi)));
    pos[bi] = {
      .x = center_x - lw / 2,
      .y = opt_y + r * (ROW_H + ROW_GAP),
      .w = lw, .h = ROW_H, .col = 2, .row = max_rows + r,
    };
  }

  return pos;
}

// ═══════════════════════════════════════════════════════════════════
// Block-level rendering with phase-aware grid layout
// ═══════════════════════════════════════════════════════════════════

// ── Rendering sub-steps ─────────────────────────────────────────────
// Each function handles one visual layer, keeping render_block_svg clean.

inline void render_skip_edges(
    SvgRenderer& svg, const DetectionResult& detection,
    const std::vector<Block>& blocks,
    const std::vector<GridPos>& pos,
    const std::vector<BlockEdge>& block_edges) {

  svg.begin_group("skip-edges");
  if (detection.architecture == Architecture::UNET) {
    // Resolution-paired skip connections within each U
    auto draw_u_skips = [&](const std::vector<uint32_t>& phase_idx) {
      std::unordered_map<int32_t, std::vector<uint32_t>> enc_by_res, dec_by_res;
      for (uint32_t bi : phase_idx) {
        if (blocks[bi].spatial_h <= 0) continue;
        (pos[bi].col == 0 ? enc_by_res : dec_by_res)
            [blocks[bi].spatial_h].push_back(bi);
      }
      for (auto& [res, enc] : enc_by_res) {
        auto dit = dec_by_res.find(res);
        if (dit == dec_by_res.end()) continue;
        uint32_t a = enc.back(), b = dit->second.front();
        float x1 = pos[a].x + pos[a].w + 3, y1 = pos[a].y + pos[a].h / 2;
        float x2 = pos[b].x - 3,             y2 = pos[b].y + pos[b].h / 2;
        float mx = (x1 + x2) / 2;
        svg.bezier_arrow(x1, y1, mx, y1, mx, y2, x2, y2,
                          palette::EDGE_SKIP, 0.6f, true);
        // Shape label at midpoint
        if (!blocks[a].out_shape.empty()) {
          float label_y = (y1 + y2) / 2 - 2;
          svg.text_mono(mx, label_y, blocks[a].out_shape, 5.0f,
                        Color::hex(0xB0B0B0));
        }
      }
    };
    std::vector<uint32_t> fwd_phase, bwd_phase;
    for (uint32_t i = 0; i < blocks.size(); i++) {
      if (blocks[i].phase == Phase::FORWARD)  fwd_phase.push_back(i);
      if (blocks[i].phase == Phase::BACKWARD) bwd_phase.push_back(i);
    }
    draw_u_skips(fwd_phase);
    draw_u_skips(bwd_phase);

    // Cross-phase saved-activation edges (limited)
    std::unordered_set<uint32_t> drawn;
    uint32_t count = 0;
    for (const auto& e : block_edges) {
      if (count >= 8) break;
      if (blocks[e.src_block].phase != Phase::FORWARD) continue;
      if (blocks[e.dst_block].phase != Phase::BACKWARD) continue;
      // Only draw from forward MODULE blocks (not ROOT/EPILOGUE)
      if (blocks[e.src_block].kind != BlockKind::MODULE) continue;
      if (!drawn.insert(e.src_block).second) continue;
      svg.orthogonal_edge(
          pos[e.src_block].x + pos[e.src_block].w + 2,
          pos[e.src_block].y + pos[e.src_block].h / 2,
          pos[e.dst_block].x - 2,
          pos[e.dst_block].y + pos[e.dst_block].h / 2,
          Color::hex(0xA78BFA), 0.4f, true);
      count++;
    }
  } else {
    // Non-UNet: filtered data-flow edges
    std::unordered_set<uint32_t> drawn;
    for (const auto& e : block_edges) {
      if (pos[e.src_block].col != 0 || pos[e.dst_block].col != 1) continue;
      if (blocks[e.src_block].kind != BlockKind::MODULE) continue;
      if (!drawn.insert(e.src_block).second) continue;
      svg.orthogonal_edge(
          pos[e.src_block].x + pos[e.src_block].w + 2,
          pos[e.src_block].y + pos[e.src_block].h / 2,
          pos[e.dst_block].x - 2,
          pos[e.dst_block].y + pos[e.dst_block].h / 2,
          palette::EDGE_SKIP, 0.5f, true);
    }
  }
  svg.end_group();
}

inline void render_seq_connectors(
    SvgRenderer& svg,
    const std::vector<Block>& blocks,
    const std::vector<GridPos>& pos) {

  svg.begin_group("seq-edges");
  for (uint32_t col = 0; col < 2; col++) {
    std::vector<uint32_t> col_blocks;
    for (uint32_t i = 0; i < blocks.size(); i++)
      if (pos[i].col == col) col_blocks.push_back(i);
    std::ranges::sort(col_blocks, [&](uint32_t a, uint32_t b) {
      return pos[a].row < pos[b].row;
    });
    for (uint32_t j = 0; j + 1 < col_blocks.size(); j++) {
      uint32_t a = col_blocks[j], b = col_blocks[j + 1];
      float x = pos[a].x + pos[a].w / 2;
      if (pos[b].y > pos[a].y + pos[a].h)
        svg.line(x, pos[a].y + pos[a].h, x, pos[b].y,
                 Color::hex(0xD1D5DB), 0.4f);
    }
  }
  svg.end_group();
}

inline void render_resolution_bands(
    SvgRenderer& svg, float svg_w,
    const std::vector<Block>& blocks,
    const std::vector<GridPos>& pos) {

  svg.begin_group("res-bands");
  struct ResBand { int32_t res; float y_min; float y_max; };
  std::vector<ResBand> bands;
  int32_t prev_res = 0;
  for (uint32_t i = 0; i < blocks.size(); i++) {
    if (blocks[i].phase != Phase::FORWARD) continue;
    int32_t r = blocks[i].spatial_h;
    if (r <= 0) continue;
    if (r != prev_res) {
      bands.push_back({r, pos[i].y - 2, pos[i].y + pos[i].h + 2});
      prev_res = r;
    } else if (!bands.empty()) {
      bands.back().y_max = pos[i].y + pos[i].h + 2;
    }
  }
  constexpr Color band_colors[] = {
    Color::hex(0xFEFCE8), Color::hex(0xEFF6FF),
  };
  for (uint32_t i = 0; i < bands.size(); i++) {
    const auto& band = bands[i];
    svg.rect(2, band.y_min, svg_w - 4, band.y_max - band.y_min,
             band_colors[i % 2], Color::hex(0xE5E7EB), 0, 0, false);
    std::string label = std::to_string(band.res) + "x" + std::to_string(band.res);
    svg.text(4, (band.y_min + band.y_max) / 2 + 3,
             label, 6.0f, Color::hex(0xC0C0C0), "start");
  }
  svg.end_group();
}

inline void render_block_nodes(
    SvgRenderer& svg,
    const std::vector<Block>& blocks,
    const std::vector<GridPos>& pos) {

  svg.begin_group("blocks");
  for (uint32_t i = 0; i < blocks.size(); i++) {
    const auto& b = blocks[i];
    const auto& p = pos[i];
    auto [fill, border] = colors_for_block(b.kind);

    std::string info = b.label + " | " + std::to_string(b.num_ops) + " ops";
    if (!b.out_shape.empty()) info += " | " + b.out_shape;
    svg.begin_block_group(info);

    svg.rect(p.x, p.y, p.w, p.h, fill, border, 4, 0.7f, true);
    svg.text(p.x + p.w / 2, p.y + p.h / 2 + 1,
             b.label, 7.0f, Color::hex(0x1F2937), "middle", true);
    std::string ops_str = std::to_string(b.num_ops);
    svg.text(p.x + p.w - 3, p.y + p.h - 3,
             ops_str, 5.0f, Color::hex(0xA0A0A0), "end");
    svg.end_group();
  }
  svg.end_group();
}

inline void render_phase_borders(
    SvgRenderer& svg,
    const std::vector<Block>& blocks,
    const std::vector<GridPos>& pos) {

  svg.begin_group("clusters");
  auto draw_cluster = [&](Phase phase, Color border_color) {
    float cx_min = 1e9f, cy_min = 1e9f, cx_max = 0, cy_max = 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < blocks.size(); i++) {
      if (blocks[i].phase != phase) continue;
      cx_min = std::min(cx_min, pos[i].x - 6);
      cy_min = std::min(cy_min, pos[i].y - 6);
      cx_max = std::max(cx_max, pos[i].x + pos[i].w + 6);
      cy_max = std::max(cy_max, pos[i].y + pos[i].h + 6);
      count++;
    }
    if (count == 0) return;
    svg.rect_dashed(cx_min, cy_min, cx_max - cx_min, cy_max - cy_min,
                     border_color, 8, 0.6f);
  };
  draw_cluster(Phase::FORWARD,  Color::hex(0x93C5FD));
  draw_cluster(Phase::BACKWARD, Color::hex(0xFCA5A5));
  draw_cluster(Phase::OPTIMIZER,Color::hex(0xF9A8D4));
  svg.end_group();
}

inline void render_legend(SvgRenderer& svg, float lx, float ly) {
  svg.begin_group("legend");
  svg.text(lx, ly, "Legend", 9, Color::hex(0x6B7280), "start", true);
  ly += 14;

  struct Entry { const char* label; Color fill; Color border; };
  Entry entries[] = {
    {"Module (fwd)", palette::BLOCK_MLP,      Color::hex(0x1E40AF)},
    {"Module (bwd)", palette::BLOCK_BWD_MLP,  Color::hex(0x1E40AF)},
    {"Optimizer",    palette::BLOCK_OPTIM,    Color::hex(0x9D174D)},
  };
  for (const auto& e : entries) {
    svg.rect(lx, ly - 7, 12, 10, e.fill, e.border, 2, 0.5f);
    svg.text(lx + 16, ly + 1, e.label, 7, Color::hex(0x6B7280));
    ly += 13;
  }
  ly += 4;
  svg.line(lx, ly - 3, lx + 12, ly - 3, palette::EDGE_SKIP, 0.6f, true);
  svg.text(lx + 16, ly, "Skip connection", 7, Color::hex(0x6B7280));
  ly += 13;
  svg.line(lx, ly - 3, lx + 12, ly - 3, Color::hex(0xD1D5DB), 0.5f);
  svg.text(lx + 16, ly, "Sequential flow", 7, Color::hex(0x6B7280));
  svg.end_group();
}

// ═══════════════════════════════════════════════════════════════════
// Block-level rendering — orchestrator
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline std::string render_block_svg(
    const DetectionResult& detection,
    const std::vector<Op>& ops,
    std::string_view title = "Crucible Trace") {

  const auto& blocks = detection.blocks;
  if (blocks.empty()) return {};

  auto block_edges = build_block_edges(blocks, ops);
  auto pos = grid_layout(blocks, block_edges, detection.architecture);

  // Compute total size
  float max_x = 0, max_y = 0;
  for (uint32_t i = 0; i < blocks.size(); i++) {
    max_x = std::max(max_x, pos[i].x + pos[i].w);
    max_y = std::max(max_y, pos[i].y + pos[i].h);
  }
  float svg_w = max_x + 30;
  float svg_h = max_y + 30;

  SvgRenderer svg;
  svg.begin(svg_w, svg_h, std::string{title});

  // Column headers — computed from actual block positions per phase
  auto phase_center_x = [&](Phase phase) -> float {
    float xmin = 1e9f, xmax = 0;
    for (uint32_t i = 0; i < blocks.size(); i++) {
      if (blocks[i].phase != phase) continue;
      xmin = std::min(xmin, pos[i].x);
      xmax = std::max(xmax, pos[i].x + pos[i].w);
    }
    return (xmin + xmax) / 2;
  };
  float fwd_cx = phase_center_x(Phase::FORWARD);
  float bwd_cx = phase_center_x(Phase::BACKWARD);
  if (fwd_cx < 1e8f)
    svg.text(fwd_cx, 42, "FORWARD", 12, Color::hex(0x1E40AF), "middle", true);
  if (bwd_cx < 1e8f)
    svg.text(bwd_cx, 42, "BACKWARD", 12, Color::hex(0x9A3412), "middle", true);

  // Render layers (back to front)
  if (detection.architecture == Architecture::UNET)
    render_resolution_bands(svg, svg_w, blocks, pos);
  render_skip_edges(svg, detection, blocks, pos, block_edges);
  render_seq_connectors(svg, blocks, pos);
  render_block_nodes(svg, blocks, pos);
  render_phase_borders(svg, blocks, pos);
  render_legend(svg, max_x - 180, max_y + 12);
  svg.embed_interactivity();

  svg.end();
  return svg.take();
}

} // namespace crucible::vis
