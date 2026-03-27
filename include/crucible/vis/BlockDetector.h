#pragma once

// BlockDetector: coarsen a loaded trace (~12K ops) into ~30-100 semantic blocks.
//
// Four-phase algorithm (faithful port of bench/block_detect.py):
//   1. Phase detection:   forward / backward / optimizer boundaries
//   2. Forward blocks:    residual-add boundaries + op family classification
//   3. Backward blocks:   mirror forward using norm_backward / SDPA_backward anchors
//   4. Optimizer:         single block (~10 ops/parameter AdamW pattern)
//
// Input:  LoadedTrace (ops + metas + schema names)
// Output: vector<Block> with kind, op range, label, output shape, phase
//
// This is a visualization tool -- not hot path. std::vector and std::string
// are appropriate. All safety axioms still apply: InitSafe, TypeSafe,
// NullSafe ([[nodiscard]]), no raw new/delete.

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TraceLoader.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crucible::vis {

// ═══════════════════════════════════════════════════════════════════
// Op family -- coarse classification for block detection
// ═══════════════════════════════════════════════════════════════════

enum class OpFamily : uint8_t {
  GEMM,       // mm, addmm, bmm, matmul, linear, einsum
  CONV,       // convolution, convolution_backward
  NORM,       // native_layer_norm, native_group_norm, batch_norm
  ATTN,       // scaled_dot_product_attention (all variants)
  ACT,        // gelu, silu, relu, sigmoid, tanh, softmax, dropout
  ELEM,       // add, mul, sub, div, neg, exp, log, sqrt, pow, ...
  MOVE,       // view, reshape, permute, transpose, clone, detach, cat, ...
  REDUCE,     // sum, mean, var_mean, argmax, topk, cumsum
  OPTIM,      // _foreach_*, _fused_adam*, addcmul_, addcdiv_, lerp_
  LOSS,       // nll_loss, cross_entropy, mse_loss
  EMBED,      // embedding
  POOL,       // upsample, interpolate, max_pool, avg_pool
  OTHER,      // everything else
};

static constexpr uint8_t NUM_FAMILIES = 13;

[[nodiscard]] constexpr const char* family_name(OpFamily f) {
  switch (f) {
    case OpFamily::GEMM:   return "gemm";
    case OpFamily::CONV:   return "conv";
    case OpFamily::NORM:   return "norm";
    case OpFamily::ATTN:   return "attn";
    case OpFamily::ACT:    return "act";
    case OpFamily::ELEM:   return "elem";
    case OpFamily::MOVE:   return "move";
    case OpFamily::REDUCE: return "reduce";
    case OpFamily::OPTIM:  return "optim";
    case OpFamily::LOSS:   return "loss";
    case OpFamily::EMBED:  return "embed";
    case OpFamily::POOL:   return "pool";
    case OpFamily::OTHER:  return "other";
  }
  std::unreachable();
}

// ═══════════════════════════════════════════════════════════════════
// Block kind -- semantic classification
// ═══════════════════════════════════════════════════════════════════

enum class BlockKind : uint8_t {
  RESBLOCK,     RESBLOCK_BWD,
  SELF_ATTN,    SELF_ATTN_BWD,    ATTN_BWD,
  CROSS_ATTN,   CROSS_ATTN_BWD,
  TRANSFORMER,  TRANSFORMER_BWD,
  MLP,          MLP_BWD,
  CONV_BLOCK,   CONV_BLOCK_BWD,
  DOWNSAMPLE,   DOWNSAMPLE_BWD,
  UPSAMPLE,     UPSAMPLE_BWD,
  TIME_EMBED,   TIME_EMBED_BWD,
  LINEAR_BLOCK, LINEAR_BLOCK_BWD,
  CONV,         CONV_BWD,
  LOSS,         LOSS_BWD,
  OPTIMIZER,
  EPILOGUE,
  GENERIC,      GENERIC_BWD,
};

[[nodiscard]] constexpr const char* block_kind_name(BlockKind k) {
  switch (k) {
    case BlockKind::RESBLOCK:       return "ResBlock";
    case BlockKind::RESBLOCK_BWD:   return "ResBlock BWD";
    case BlockKind::SELF_ATTN:      return "SelfAttn";
    case BlockKind::SELF_ATTN_BWD:  return "SelfAttn BWD";
    case BlockKind::ATTN_BWD:       return "Attn BWD";
    case BlockKind::CROSS_ATTN:     return "CrossAttn";
    case BlockKind::CROSS_ATTN_BWD: return "CrossAttn BWD";
    case BlockKind::TRANSFORMER:    return "Transformer";
    case BlockKind::TRANSFORMER_BWD:return "Transformer BWD";
    case BlockKind::MLP:            return "MLP";
    case BlockKind::MLP_BWD:        return "MLP BWD";
    case BlockKind::CONV_BLOCK:     return "ConvBlock";
    case BlockKind::CONV_BLOCK_BWD: return "ConvBlock BWD";
    case BlockKind::DOWNSAMPLE:     return "Downsample";
    case BlockKind::DOWNSAMPLE_BWD: return "Downsample BWD";
    case BlockKind::UPSAMPLE:       return "Upsample";
    case BlockKind::UPSAMPLE_BWD:   return "Upsample BWD";
    case BlockKind::TIME_EMBED:     return "TimeEmbed";
    case BlockKind::TIME_EMBED_BWD: return "TimeEmbed BWD";
    case BlockKind::LINEAR_BLOCK:   return "Linear";
    case BlockKind::LINEAR_BLOCK_BWD: return "Linear BWD";
    case BlockKind::CONV:           return "Conv";
    case BlockKind::CONV_BWD:       return "Conv BWD";
    case BlockKind::LOSS:           return "Loss";
    case BlockKind::LOSS_BWD:       return "Loss BWD";
    case BlockKind::OPTIMIZER:      return "Optimizer";
    case BlockKind::EPILOGUE:       return "Epilogue";
    case BlockKind::GENERIC:        return "Generic";
    case BlockKind::GENERIC_BWD:    return "Generic BWD";
  }
  std::unreachable();
}

[[nodiscard]] constexpr const char* block_kind_icon(BlockKind k) {
  switch (k) {
    case BlockKind::RESBLOCK: case BlockKind::RESBLOCK_BWD:
      return "\xe2\x96\x88";  // full block
    case BlockKind::SELF_ATTN: case BlockKind::SELF_ATTN_BWD:
    case BlockKind::ATTN_BWD:
      return "\xe2\x97\x86";  // black diamond
    case BlockKind::CROSS_ATTN: case BlockKind::CROSS_ATTN_BWD:
      return "\xe2\x97\x87";  // white diamond
    case BlockKind::TRANSFORMER: case BlockKind::TRANSFORMER_BWD:
      return "\xe2\x96\xa3";  // white square containing black small square
    case BlockKind::MLP: case BlockKind::MLP_BWD:
      return "\xe2\x96\xac";  // black rectangle
    case BlockKind::DOWNSAMPLE: case BlockKind::DOWNSAMPLE_BWD:
      return "\xe2\x96\xbc";  // black down-pointing triangle
    case BlockKind::UPSAMPLE: case BlockKind::UPSAMPLE_BWD:
      return "\xe2\x96\xb2";  // black up-pointing triangle
    case BlockKind::TIME_EMBED: case BlockKind::TIME_EMBED_BWD:
      return "\xe2\x8f\xb1";  // stopwatch
    case BlockKind::OPTIMIZER:
      return "\xe2\x9a\x99";  // gear
    case BlockKind::LOSS: case BlockKind::LOSS_BWD:
      return "\xe2\x9c\x97";  // ballot x
    case BlockKind::CONV: case BlockKind::CONV_BWD:
    case BlockKind::CONV_BLOCK: case BlockKind::CONV_BLOCK_BWD:
      return "\xe2\x96\xa0";  // black square
    case BlockKind::LINEAR_BLOCK: case BlockKind::LINEAR_BLOCK_BWD:
      return "\xe2\x94\x80";  // box drawing light horizontal
    case BlockKind::EPILOGUE:
    case BlockKind::GENERIC: case BlockKind::GENERIC_BWD:
      return "\xc2\xb7";      // middle dot
  }
  std::unreachable();
}

// ═══════════════════════════════════════════════════════════════════
// Training phase
// ═══════════════════════════════════════════════════════════════════

enum class Phase : uint8_t { FORWARD, BACKWARD, OPTIMIZER };

// ═══════════════════════════════════════════════════════════════════
// Detected architecture type
// ═══════════════════════════════════════════════════════════════════

enum class Architecture : uint8_t { UNET, VIT, GENERIC };

// ═══════════════════════════════════════════════════════════════════
// Op -- lightweight per-op view for block detection
// ═══════════════════════════════════════════════════════════════════

struct Op {
  uint32_t idx = 0;             // global index into trace
  SchemaHash schema{};          // op identity
  const char* name = nullptr;   // short name (from SchemaTable, not owned)
  OpFamily family = OpFamily::OTHER;
  uint16_t n_in = 0;
  uint16_t n_out = 0;
  bool grad_enabled = false;
  int64_t out_sizes[8]{};       // first output tensor shape (up to 8D)
  uint8_t out_ndim = 0;
  std::vector<uint64_t> data_ptr_in;   // input data_ptrs (variable count)
  std::vector<uint64_t> data_ptr_out;  // output data_ptrs (variable count)
};

// ═══════════════════════════════════════════════════════════════════
// Block -- one detected semantic block
// ═══════════════════════════════════════════════════════════════════

struct Block {
  BlockKind kind = BlockKind::GENERIC;
  Phase phase = Phase::FORWARD;
  uint32_t start_op = 0;        // first op index (inclusive)
  uint32_t end_op = 0;          // last op index (inclusive)
  uint32_t num_ops = 0;         // end_op - start_op + 1
  std::string label;            // human-readable label
  std::string out_shape;        // representative output shape string

  // Spatial resolution (from 4D output tensors), 0 if not applicable.
  int32_t spatial_h = 0;
  int32_t spatial_w = 0;
};

// ═══════════════════════════════════════════════════════════════════
// Detection result
// ═══════════════════════════════════════════════════════════════════

struct DetectionResult {
  std::vector<Block> blocks;
  Architecture architecture = Architecture::GENERIC;
  uint32_t fwd_end = 0;        // last forward op index
  uint32_t bwd_end = 0;        // last backward op index
  uint32_t optim_start = 0;    // first optimizer op index
};

// ═══════════════════════════════════════════════════════════════════
// Op family classification from full schema name
// Python calls classify_family(full_name) where full_name includes "aten::" prefix.
// We match that: the SchemaTable stores "aten::add.Tensor" etc.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline OpFamily classify_family(std::string_view name) {
  if (name.empty()) return OpFamily::OTHER;

  auto has = [&](std::string_view pat) {
    return name.find(pat) != std::string_view::npos;
  };

  // Order matters: most specific patterns first (mirrors Python exactly).
  if (has("attention") || has("sdp"))   return OpFamily::ATTN;
  if (has("loss") || has("nll") || has("cross_entropy") || has("mse_loss"))
                                        return OpFamily::LOSS;
  if (has("foreach") || has("fused_adam") || has("fused_sgd") ||
      has("addcmul") || has("addcdiv") || has("lerp"))
                                        return OpFamily::OPTIM;
  if (has("mm") || has("matmul") || has("linear") || has("einsum"))
                                        return OpFamily::GEMM;
  if (has("conv"))                      return OpFamily::CONV;
  if (has("norm"))                      return OpFamily::NORM;
  if (has("gelu") || has("silu") || has("relu") || has("sigmoid") ||
      has("tanh") || has("softmax") || has("dropout") || has("mish"))
                                        return OpFamily::ACT;
  if (has("sum") || has("mean") || has("var_mean") ||
      has("argmax") || has("topk") || has("cumsum"))
                                        return OpFamily::REDUCE;
  if (has("add") || has("mul") || has("sub") || has("div") || has("neg") ||
      has("exp") || has("log") || has("sqrt") || has("rsqrt") || has("pow") ||
      has("abs") || has("clamp") || has("where") || has("fill") ||
      has("zero") || has("masked") || has("sin") || has("cos"))
                                        return OpFamily::ELEM;
  if (has("view") || has("reshape") || has("permute") || has("transpose") ||
      has("expand") || has("squeeze") || has("unsqueeze") || has("contiguous") ||
      has("slice") || has("select") || has("cat") || has("stack") ||
      has("clone") || has("detach") || has("narrow") || has("flatten") ||
      has("unfold") || has("unsafe_view") || has("as_strided") ||
      has("index") || has("gather") || has("scatter") || has("alias") ||
      has("copy") || has("pad") || has("split") || has("chunk") ||
      has("unbind") || has("repeat") || has("to_copy") || has("arange") ||
      has("zeros") || has("ones") || has("empty") || has("full") ||
      has("new_zeros") || has("lift_fresh"))
                                        return OpFamily::MOVE;
  if (has("embedding"))                 return OpFamily::EMBED;
  if (has("upsample") || has("interpolate") || has("pool"))
                                        return OpFamily::POOL;
  return OpFamily::OTHER;
}

// ═══════════════════════════════════════════════════════════════════
// Build Op array from LoadedTrace
//
// Python does: classify_family(full_name) where full_name is the ATEN
// hash table lookup (includes "aten::" prefix). The short name
// (stripped of "aten::" and overload suffix ".Tensor" etc) is used
// for exact name matching in phase/block detection.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline std::vector<Op> build_ops(const LoadedTrace& trace) {
  std::vector<Op> ops(trace.num_ops);
  uint32_t meta_cursor = 0;

  for (uint32_t i = 0; i < trace.num_ops; i++) {
    const auto& e = trace.entries[i];
    auto& op = ops[i];

    op.idx = i;
    op.schema = e.schema_hash;

    // Resolve full name from global SchemaTable.
    // Python: full_name = ATEN_HASH_TABLE.get(sh, hex(sh))
    //         short = full_name.replace("aten::", "").split(".")[0]
    //         family = classify_family(full_name)  <-- uses FULL name
    const char* full = schema_name(e.schema_hash);  // full name (e.g. "aten::add.Tensor")
    const char* short_nm = schema_short_name(e.schema_hash);  // stripped "aten::" prefix

    // Family uses the full name (Python matches on full_name which includes "aten::")
    // but since short_name strips aten::, and all the patterns don't depend on "aten::",
    // using short_name is functionally identical. However the full name also
    // matches correctly. We use full name if available, else short name.
    std::string_view classify_str = full ? std::string_view{full} : "";
    op.family = classify_family(classify_str);

    // Short name: strip overload suffix ("add.Tensor" -> "add")
    // Python: full_name.replace("aten::", "").split(".")[0]
    if (short_nm) {
      op.name = short_nm;
      // Note: short_nm already has "aten::" stripped but still has ".Tensor" etc.
      // The SchemaTable::short_name() only strips "aten::" prefix.
      // Python additionally splits on "." and takes [0].
      // For exact name comparisons (op.name == "add"), we need the base name.
      // But we can't modify the SchemaTable string. We'll do name comparisons
      // carefully -- checking with strstr or prefix matching.
    } else {
      op.name = nullptr;
    }

    op.n_in = e.num_inputs;
    op.n_out = e.num_outputs;
    op.grad_enabled = e.grad_enabled;

    // Extract data_ptrs and output shapes from metas.
    // Python reads 144B legacy metas; C++ TraceLoader auto-detects size.
    const auto mi = trace.meta_starts[i];
    if (mi.is_valid()) {
      const uint32_t base = mi.raw();

      for (uint16_t j = 0; j < e.num_inputs; j++) {
        if (base + j < trace.num_metas) {
          auto ptr = reinterpret_cast<uint64_t>(trace.metas[base + j].data_ptr);
          op.data_ptr_in.push_back(ptr);
        }
      }

      for (uint16_t j = 0; j < e.num_outputs; j++) {
        const uint32_t mi_out = base + e.num_inputs + j;
        if (mi_out < trace.num_metas) {
          auto ptr = reinterpret_cast<uint64_t>(trace.metas[mi_out].data_ptr);
          op.data_ptr_out.push_back(ptr);
          if (j == 0) {
            const auto& m = trace.metas[mi_out];
            op.out_ndim = m.ndim;
            uint8_t nd = m.ndim < 8 ? m.ndim : 8;
            for (uint8_t d = 0; d < nd; d++)
              op.out_sizes[d] = m.sizes[d];
          }
        }
      }
    }
    meta_cursor += e.num_inputs + e.num_outputs;
  }
  return ops;
}

// ═══════════════════════════════════════════════════════════════════
// String helpers
// ═══════════════════════════════════════════════════════════════════

// Get the base op name: strip everything from first '.' onwards.
// Python: full_name.replace("aten::", "").split(".")[0]
// SchemaTable::short_name already strips "aten::", so we just need
// to strip from '.' onwards.
[[nodiscard]] inline std::string_view base_name(const char* name) {
  if (!name) return {};
  std::string_view sv{name};
  auto dot = sv.find('.');
  return dot != sv.npos ? sv.substr(0, dot) : sv;
}

// Check if the base name (before '.') exactly equals target.
[[nodiscard]] inline bool name_eq(const char* name, std::string_view target) {
  return base_name(name) == target;
}

// Check if the full short name contains a substring.
[[nodiscard]] inline bool name_has(const char* name, std::string_view sub) {
  if (!name) return false;
  return std::string_view{name}.find(sub) != std::string_view::npos;
}

// Check if the full short name starts with prefix.
[[nodiscard]] inline bool name_starts(const char* name, std::string_view prefix) {
  if (!name) return false;
  return std::string_view{name}.starts_with(prefix);
}

// Shape string from out_sizes (Python: "x".join(str(s) for s in op.out_shape))
[[nodiscard]] inline std::string shape_string(const Op& op) {
  if (op.out_ndim == 0) return {};
  std::string s;
  uint8_t nd = op.out_ndim < 8 ? op.out_ndim : 8;
  for (uint8_t d = 0; d < nd; d++) {
    if (d > 0) s += 'x';
    s += std::to_string(op.out_sizes[d]);
  }
  return s;
}

// ═══════════════════════════════════════════════════════════════════
// Phase detection: forward / backward / optimizer
//
// Python: detect_phases(ops) -> (fwd_end, bwd_end, optim_start)
// ═══════════════════════════════════════════════════════════════════

struct PhaseBoundaries {
  uint32_t fwd_end = 0;
  uint32_t bwd_end = 0;
  uint32_t optim_start = 0;
};

[[nodiscard]] inline PhaseBoundaries detect_phases(
    std::span<const Op> ops) {
  const uint32_t n = static_cast<uint32_t>(ops.size());
  PhaseBoundaries pb{.fwd_end = 0, .bwd_end = n, .optim_start = n};

  // Strategy 1: Find the first *_backward op.
  uint32_t first_bwd = n;
  for (uint32_t i = 0; i < n; i++) {
    if (name_has(ops[i].name, "backward")) {
      first_bwd = i;
      break;
    }
  }
  // Python: fwd_end = max(0, first_bwd - 1)
  pb.fwd_end = first_bwd > 0 ? first_bwd - 1 : 0;

  // Strategy 2: Find optimizer start via profiler hook or addcdiv_ pattern.
  // Look for grad=0->1 transition after fwd_end (profiler enter op).
  bool in_backward = false;
  for (uint32_t i = first_bwd; i < n; i++) {
    if (!ops[i].grad_enabled) {
      in_backward = true;
    } else if (in_backward && ops[i].grad_enabled &&
               name_starts(ops[i].name, "profiler")) {
      pb.optim_start = i;
      pb.bwd_end = i - 1;
      break;
    }
  }

  // Fallback: detect addcdiv_ run (unique to AdamW).
  if (pb.optim_start == n) {
    uint32_t addcdiv_run = 0;
    for (uint32_t i = first_bwd; i < n; i++) {
      if (name_eq(ops[i].name, "addcdiv_")) {
        addcdiv_run++;
        if (addcdiv_run >= 3) {
          // Rewind to find the start of optimizer section.
          uint32_t j = i;
          while (j > first_bwd && addcdiv_run < 100) {
            j--;
            if (name_eq(ops[j].name, "addcdiv_"))
              addcdiv_run++;
          }
          // Find the actual start (first add_ before first addcmul_).
          while (j > first_bwd && !name_eq(ops[j].name, "add_"))
            j--;
          pb.optim_start = j;
          pb.bwd_end = j > 0 ? j - 1 : 0;
          break;
        }
      } else {
        addcdiv_run = 0;
      }
    }
  }

  return pb;
}

// ═══════════════════════════════════════════════════════════════════
// Block classification from op families
//
// Faithfully ports Python classify_block() with all ~30 rules.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline Block classify_block(
    std::span<const Op> ops,
    const std::unordered_map<uint64_t, uint32_t>& ptr_producer,
    Phase phase) {

  Block blk;
  blk.phase = phase;
  blk.start_op = ops.front().idx;
  blk.end_op = ops.back().idx;
  blk.num_ops = static_cast<uint32_t>(ops.size());

  // Count families.
  uint32_t counts[NUM_FAMILIES]{};
  for (const auto& op : ops)
    counts[static_cast<uint8_t>(op.family)]++;

  auto count = [&](OpFamily f) -> uint32_t {
    return counts[static_cast<uint8_t>(f)];
  };

  const uint32_t n_conv = count(OpFamily::CONV);
  const uint32_t n_gemm = count(OpFamily::GEMM);
  const uint32_t n_optim = count(OpFamily::OPTIM);
  const uint32_t n_loss = count(OpFamily::LOSS);

  // Detect specific ops (works for both forward and backward variants).
  bool has_sdpa = false, has_gelu = false, has_silu = false;
  bool has_conv = n_conv > 0;
  bool has_group_norm = false, has_layer_norm = false;
  bool is_backward = false, has_upsample = false;

  for (const auto& op : ops) {
    if (!op.name) continue;
    if (name_has(op.name, "attention"))  has_sdpa = true;
    if (name_eq(op.name, "gelu") || name_eq(op.name, "gelu_backward"))
                                         has_gelu = true;
    if (name_eq(op.name, "silu") || name_eq(op.name, "silu_") ||
        name_eq(op.name, "silu_backward"))
                                         has_silu = true;
    if (name_has(op.name, "group_norm")) has_group_norm = true;
    if (name_has(op.name, "layer_norm")) has_layer_norm = true;
    if (name_has(op.name, "backward"))   is_backward = true;
    if (name_has(op.name, "upsample"))   has_upsample = true;
  }

  // Check for cross-attention: mm ops with inputs from outside the block.
  // Only meaningful in forward pass.
  bool has_cross_input = false;
  if (phase == Phase::FORWARD) {
    std::unordered_set<uint32_t> block_idx_set;
    block_idx_set.reserve(ops.size());
    for (const auto& op : ops)
      block_idx_set.insert(op.idx);

    for (const auto& op : ops) {
      if (name_eq(op.name, "mm") && op.family == OpFamily::GEMM) {
        for (uint64_t ptr : op.data_ptr_in) {
          if (ptr) {
            auto it = ptr_producer.find(ptr);
            if (it != ptr_producer.end() &&
                block_idx_set.find(it->second) == block_idx_set.end()) {
              has_cross_input = true;
              break;
            }
          }
        }
        if (has_cross_input) break;
      }
    }
  }

  // Output shape from last significant op.
  // Python: for op in reversed(ops): if op.out_shape and op.family not in (MOVE, OTHER): ...
  for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
    if (it->out_ndim > 0 && it->family != OpFamily::MOVE &&
        it->family != OpFamily::OTHER) {
      blk.out_shape = shape_string(*it);
      if (it->out_ndim == 4) {
        blk.spatial_h = static_cast<int32_t>(it->out_sizes[2]);
        blk.spatial_w = static_cast<int32_t>(it->out_sizes[3]);
      }
      break;
    }
  }
  if (blk.out_shape.empty() && ops.back().out_ndim > 0)
    blk.out_shape = shape_string(ops.back());

  // Python: suffix = " BWD" if phase == "backward" and is_backward else ""
  const char* suffix = (phase == Phase::BACKWARD && is_backward) ? " BWD" : "";

  // Classification rules (most specific first) -- faithfully matches Python.
  std::string label;

  if (n_optim > blk.num_ops / 4) {
    blk.kind = BlockKind::OPTIMIZER;
    label = "Optimizer";
  } else if (n_loss > 0 && !is_backward) {
    blk.kind = BlockKind::LOSS;
    label = "Loss";
  } else if (n_loss > 0 && is_backward) {
    blk.kind = BlockKind::LOSS_BWD;
    label = "Loss BWD";
  } else if (has_sdpa && has_layer_norm && has_cross_input) {
    blk.kind = is_backward ? BlockKind::CROSS_ATTN_BWD : BlockKind::CROSS_ATTN;
    label = std::string("CrossAttn") + suffix;
  } else if (has_sdpa && has_gelu && has_layer_norm) {
    blk.kind = is_backward ? BlockKind::TRANSFORMER_BWD : BlockKind::TRANSFORMER;
    label = std::string("Transformer") + suffix;
  } else if (has_sdpa && has_layer_norm) {
    blk.kind = is_backward ? BlockKind::SELF_ATTN_BWD : BlockKind::SELF_ATTN;
    label = std::string("SelfAttn") + suffix;
  } else if (has_conv && has_group_norm && has_silu) {
    blk.kind = is_backward ? BlockKind::RESBLOCK_BWD : BlockKind::RESBLOCK;
    label = std::string("ResBlock") + suffix;
  } else if (has_gelu && n_gemm >= 2 && has_layer_norm) {
    blk.kind = is_backward ? BlockKind::MLP_BWD : BlockKind::MLP;
    label = std::string("MLP") + suffix;
    // Python: if any(op.name == "split" for op in ops): label = "GEGLU MLP" + suffix
    for (const auto& op : ops) {
      if (name_eq(op.name, "split")) {
        label = std::string("GEGLU MLP") + suffix;
        break;
      }
    }
  } else if (has_gelu && has_layer_norm) {
    blk.kind = is_backward ? BlockKind::MLP_BWD : BlockKind::MLP;
    label = std::string("MLP") + suffix;
  } else if (has_conv && blk.num_ops <= 6 && !has_group_norm) {
    if (has_upsample) {
      blk.kind = is_backward ? BlockKind::UPSAMPLE_BWD : BlockKind::UPSAMPLE;
      label = std::string("Upsample") + suffix;
    } else {
      blk.kind = is_backward ? BlockKind::DOWNSAMPLE_BWD : BlockKind::DOWNSAMPLE;
      label = std::string("Downsample") + suffix;
    }
  } else if (has_conv && has_group_norm) {
    blk.kind = is_backward ? BlockKind::CONV_BLOCK_BWD : BlockKind::CONV_BLOCK;
    label = std::string("ConvBlock") + suffix;
  } else if ([&]{
    // Python: any(op.name in ("sin", "cos", "arange", "exp") for op in ops) and n_gemm >= 1
    for (const auto& op : ops) {
      if (name_eq(op.name, "sin") || name_eq(op.name, "cos") ||
          name_eq(op.name, "arange") || name_eq(op.name, "exp"))
        return true;
    }
    return false;
  }() && n_gemm >= 1) {
    blk.kind = is_backward ? BlockKind::TIME_EMBED_BWD : BlockKind::TIME_EMBED;
    label = std::string("TimeEmbed") + suffix;
  } else if (has_layer_norm && n_gemm >= 1 && !has_sdpa) {
    blk.kind = is_backward ? BlockKind::LINEAR_BLOCK_BWD : BlockKind::LINEAR_BLOCK;
    label = std::string("Linear") + suffix;
  } else if (has_conv && n_conv == 1) {
    blk.kind = is_backward ? BlockKind::CONV_BWD : BlockKind::CONV;
    label = std::string("Conv") + suffix;
  } else if (has_sdpa && !has_layer_norm) {
    blk.kind = is_backward ? BlockKind::ATTN_BWD : BlockKind::SELF_ATTN;
    label = std::string("Attn") + suffix;
  } else if (has_gelu && !has_layer_norm) {
    blk.kind = is_backward ? BlockKind::MLP_BWD : BlockKind::MLP;
    label = std::string("MLP") + suffix;
  } else if (has_silu && has_conv) {
    blk.kind = is_backward ? BlockKind::RESBLOCK_BWD : BlockKind::RESBLOCK;
    label = std::string("ResBlock") + suffix;
  } else {
    // Python: dom = max(families, key=families.get) if families else "other"
    OpFamily dom = OpFamily::OTHER;
    uint32_t max_count = 0;
    for (uint8_t f = 0; f < NUM_FAMILIES; f++) {
      if (counts[f] > max_count) {
        max_count = counts[f];
        dom = static_cast<OpFamily>(f);
      }
    }
    if (is_backward) {
      blk.kind = BlockKind::GENERIC_BWD;
      // Python: label = dom.upper() + " BWD"
      std::string fn = family_name(dom);
      for (auto& c : fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      label = fn + " BWD";
    } else {
      blk.kind = BlockKind::GENERIC;
      std::string fn = family_name(dom);
      for (auto& c : fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      label = fn;
    }
  }

  // Append output shape.
  if (!blk.out_shape.empty())
    label += " [" + blk.out_shape + "]";

  blk.label = std::move(label);
  return blk;
}

// ═══════════════════════════════════════════════════════════════════
// Forward block detection: residual-add boundaries
//
// Python: detect_forward_blocks(ops, fwd_end)
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline std::vector<Block> detect_forward_blocks(
    std::span<const Op> all_ops, uint32_t fwd_end) {

  if (fwd_end == 0 && all_ops.empty()) return {};
  const uint32_t n = fwd_end + 1;
  if (n > all_ops.size()) return {};

  auto fwd_ops = all_ops.subspan(0, n);

  // Build ptr -> producer map (Python: ptr_producer[ptr] = op.idx).
  // Python overwrites, so last producer wins.
  std::unordered_map<uint64_t, uint32_t> ptr_producer;
  ptr_producer.reserve(n * 2);
  for (uint32_t i = 0; i < n; i++) {
    for (uint64_t p : fwd_ops[i].data_ptr_out) {
      if (p) ptr_producer[p] = fwd_ops[i].idx;
    }
  }

  // Find residual-add boundaries: add ops where one input comes from
  // far earlier (skip connection gap > threshold).
  constexpr uint32_t SKIP_GAP = 8;
  std::vector<uint32_t> block_ends;  // stores op.idx values

  for (const auto& op : fwd_ops) {
    if ((name_eq(op.name, "add") || name_eq(op.name, "add_")) &&
        op.family == OpFamily::ELEM) {
      for (uint64_t p : op.data_ptr_in) {
        if (!p) continue;
        auto it = ptr_producer.find(p);
        if (it != ptr_producer.end()) {
          uint32_t src = it->second;
          if (src < op.idx && op.idx - src > SKIP_GAP) {
            block_ends.push_back(op.idx);
            break;
          }
        }
      }
    }
  }

  // Fallback: fixed-size chunks.
  if (block_ends.empty()) {
    uint32_t chunk = n / 30;
    if (chunk < 20) chunk = 20;
    for (uint32_t i = chunk - 1; i < n; i += chunk)
      block_ends.push_back(i);
  }

  // Split into blocks.
  // Python: uses set lookup on block_ends, iterates enumerate(fwd_ops).
  // Since fwd_ops starts at index 0, i == op.idx for forward ops.
  std::unordered_set<uint32_t> block_ends_set(block_ends.begin(), block_ends.end());
  std::vector<Block> blocks;
  uint32_t block_start = 0;

  for (uint32_t i = 0; i < n; i++) {
    if (block_ends_set.count(i) || i == n - 1) {
      auto block_ops = fwd_ops.subspan(block_start, i - block_start + 1);
      if (!block_ops.empty()) {
        blocks.push_back(classify_block(block_ops, ptr_producer, Phase::FORWARD));
      }
      block_start = i + 1;
    }
  }

  // Remaining ops after last block_end.
  if (block_start < n) {
    auto remaining = fwd_ops.subspan(block_start, n - block_start);
    if (!remaining.empty()) {
      blocks.push_back(classify_block(remaining, ptr_producer, Phase::FORWARD));
    }
  }

  return blocks;
}

// ═══════════════════════════════════════════════════════════════════
// Backward block detection using SDPA / norm_backward anchors
//
// Python: detect_backward_blocks(ops, fwd_end, bwd_end, fwd_blocks)
// Calls _split_backward_by_sdpa or _split_backward_by_adds.
// ═══════════════════════════════════════════════════════════════════

// Split backward by residual-add boundaries (fallback).
[[nodiscard]] inline std::vector<Block> split_backward_by_adds_(
    std::span<const Op> bwd_ops,
    const std::vector<uint32_t>& add_boundaries,
    const std::unordered_map<uint64_t, uint32_t>& ptr_producer) {

  std::unordered_set<uint32_t> boundary_set(add_boundaries.begin(), add_boundaries.end());
  std::vector<Block> blocks;
  uint32_t block_start = 0;
  const uint32_t n = static_cast<uint32_t>(bwd_ops.size());

  for (uint32_t i = 0; i < n; i++) {
    if (boundary_set.count(i) || i == n - 1) {
      auto block_ops = bwd_ops.subspan(block_start, i - block_start + 1);
      if (!block_ops.empty()) {
        blocks.push_back(classify_block(block_ops, ptr_producer, Phase::BACKWARD));
      }
      block_start = i + 1;
    }
  }

  if (block_start < n) {
    auto remaining = bwd_ops.subspan(block_start, n - block_start);
    if (!remaining.empty()) {
      blocks.push_back(classify_block(remaining, ptr_producer, Phase::BACKWARD));
    }
  }

  return blocks;
}

// Split backward using structural norm_backward boundaries.
[[nodiscard]] inline std::vector<Block> split_backward_by_sdpa_(
    std::span<const Op> bwd_ops,
    const std::unordered_map<uint64_t, uint32_t>& ptr_producer) {

  std::vector<Block> blocks;
  const uint32_t n = static_cast<uint32_t>(bwd_ops.size());

  // Find ALL norm_backward ops (both layer_norm and group_norm).
  std::vector<uint32_t> lnorm_bwd;
  std::vector<uint32_t> gnorm_bwd;

  for (uint32_t i = 0; i < n; i++) {
    if (name_eq(bwd_ops[i].name, "native_layer_norm_backward"))
      lnorm_bwd.push_back(i);
    if (name_eq(bwd_ops[i].name, "native_group_norm_backward"))
      gnorm_bwd.push_back(i);
  }

  // Find the preamble: everything before the first structural backward.
  // Python checks: name in ("native_layer_norm_backward",
  //   "native_group_norm_backward",
  //   "_scaled_dot_product_flash_attention_for_cpu_backward")
  uint32_t first_structural = n;
  for (uint32_t i = 0; i < n; i++) {
    if (name_eq(bwd_ops[i].name, "native_layer_norm_backward") ||
        name_eq(bwd_ops[i].name, "native_group_norm_backward") ||
        name_eq(bwd_ops[i].name,
                "_scaled_dot_product_flash_attention_for_cpu_backward")) {
      first_structural = i;
      break;
    }
  }

  // Emit preamble (loss backward / head backward).
  if (first_structural > 0) {
    auto preamble = bwd_ops.subspan(0, first_structural);
    blocks.push_back(classify_block(preamble, ptr_producer, Phase::BACKWARD));
  }

  if (first_structural >= n) return blocks;

  // Collect ALL structural anchor positions (both norm types).
  std::vector<uint32_t> all_norm_bwd;
  all_norm_bwd.reserve(lnorm_bwd.size() + gnorm_bwd.size());
  all_norm_bwd.insert(all_norm_bwd.end(), lnorm_bwd.begin(), lnorm_bwd.end());
  all_norm_bwd.insert(all_norm_bwd.end(), gnorm_bwd.begin(), gnorm_bwd.end());
  std::ranges::sort(all_norm_bwd);

  // Find block boundaries.
  // Python logic: for each norm_backward, check if preceded/followed by 'add'.
  std::vector<uint32_t> block_starts = {first_structural};

  for (uint32_t idx : all_norm_bwd) {
    if (idx <= first_structural) continue;

    // Pattern A (ViT): norm_backward preceded by 'add' -> block starts at norm.
    if (idx > 0 && name_eq(bwd_ops[idx - 1].name, "add"))
      block_starts.push_back(idx);
    else if (idx > 1 && name_eq(bwd_ops[idx - 2].name, "add"))
      block_starts.push_back(idx);

    // Pattern B (UNet): norm_backward followed by 'add' -> block starts AFTER add.
    if (idx + 1 < n && name_eq(bwd_ops[idx + 1].name, "add")) {
      if (idx + 2 < n)
        block_starts.push_back(idx + 2);
    }
  }

  // Deduplicate and sort.
  std::ranges::sort(block_starts);
  block_starts.erase(
      std::ranges::unique(block_starts).begin(), block_starts.end());

  // Remove block starts that are too close together (< 10 ops apart).
  std::vector<uint32_t> filtered = {block_starts[0]};
  for (size_t i = 1; i < block_starts.size(); i++) {
    if (block_starts[i] - filtered.back() >= 10)
      filtered.push_back(block_starts[i]);
  }
  block_starts = std::move(filtered);

  // Split into blocks using these starts.
  for (size_t i = 0; i < block_starts.size(); i++) {
    uint32_t s = block_starts[i];
    uint32_t e = (i + 1 < block_starts.size()) ? block_starts[i + 1] - 1 : n - 1;
    if (s <= e) {
      auto block_ops = bwd_ops.subspan(s, e - s + 1);
      if (!block_ops.empty()) {
        blocks.push_back(classify_block(block_ops, ptr_producer, Phase::BACKWARD));
      }
    }
  }

  return blocks;
}

[[nodiscard]] inline std::vector<Block> detect_backward_blocks(
    std::span<const Op> all_ops,
    uint32_t fwd_end, uint32_t bwd_end,
    std::span<const Block> fwd_blocks) {

  if (bwd_end <= fwd_end) return {};
  const uint32_t bwd_start = fwd_end + 1;
  if (bwd_start >= all_ops.size()) return {};
  const uint32_t n_bwd = bwd_end - bwd_start + 1;
  if (bwd_start + n_bwd > all_ops.size()) return {};
  auto bwd_ops = all_ops.subspan(bwd_start, n_bwd);

  // Build ptr -> producer for ALL ops up to bwd_end (Python does ops[:bwd_end + 1]).
  std::unordered_map<uint64_t, uint32_t> ptr_producer;
  ptr_producer.reserve((bwd_end + 1) * 2);
  for (uint32_t i = 0; i <= bwd_end && i < all_ops.size(); i++) {
    for (uint64_t p : all_ops[i].data_ptr_out) {
      if (p) ptr_producer[p] = all_ops[i].idx;
    }
  }

  // Find SDPA backward ops.
  std::vector<uint32_t> sdpa_bwd_indices;
  for (uint32_t i = 0; i < n_bwd; i++) {
    if (name_has(bwd_ops[i].name, "scaled_dot_product") &&
        name_has(bwd_ops[i].name, "backward"))
      sdpa_bwd_indices.push_back(i);
  }

  // Strategy: use SDPA backward ops as anchors if >= 3.
  if (!sdpa_bwd_indices.empty() && sdpa_bwd_indices.size() >= 3) {
    return split_backward_by_sdpa_(bwd_ops, ptr_producer);
  }

  // Fallback: use add-based boundaries.
  std::vector<uint32_t> bwd_add_boundaries;
  for (uint32_t i = 0; i < n_bwd; i++) {
    const auto& op = bwd_ops[i];
    if (name_eq(op.name, "add") && op.family == OpFamily::ELEM) {
      for (uint64_t p : op.data_ptr_in) {
        if (!p) continue;
        auto it = ptr_producer.find(p);
        if (it != ptr_producer.end()) {
          if (it->second < op.idx && op.idx - it->second > 8) {
            bwd_add_boundaries.push_back(i);  // local index within bwd_ops
            break;
          }
        }
      }
    }
  }

  if (!bwd_add_boundaries.empty()) {
    return split_backward_by_adds_(bwd_ops, bwd_add_boundaries, ptr_producer);
  }

  // Final fallback: single block.
  std::vector<Block> blocks;
  blocks.push_back(classify_block(bwd_ops, ptr_producer, Phase::BACKWARD));
  return blocks;
}

// ═══════════════════════════════════════════════════════════════════
// Architecture detection
//
// Python: detect_architecture(fwd_blocks)
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline Architecture detect_architecture(
    std::span<const Block> fwd_blocks) {

  std::vector<int32_t> resolutions;
  for (const auto& b : fwd_blocks) {
    if (b.spatial_h > 0)
      resolutions.push_back(b.spatial_h);
  }

  if (resolutions.empty()) return Architecture::GENERIC;

  // UNet: resolutions go down then up.
  if (resolutions.size() >= 5) {
    auto min_it = std::ranges::min_element(resolutions);
    int32_t min_res = *min_it;
    auto min_idx = std::distance(resolutions.begin(), min_it);
    auto max_res = *std::ranges::max_element(resolutions);

    if (min_idx > 0 && min_idx < static_cast<ptrdiff_t>(resolutions.size()) - 1) {
      // Python: check that both before_min and after_min have max >= 2 * min_res
      auto before_max = *std::max_element(resolutions.begin(), resolutions.begin() + min_idx + 1);
      auto after_max = *std::max_element(resolutions.begin() + min_idx, resolutions.end());
      if (before_max >= 2 * min_res && after_max >= 2 * min_res)
        return Architecture::UNET;
    }
  }

  // ViT: all blocks have same resolution (or nearly).
  // Python: if len(set(resolutions)) <= 2: return "vit"
  {
    std::unordered_set<int32_t> unique_res(resolutions.begin(), resolutions.end());
    if (unique_res.size() <= 2)
      return Architecture::VIT;
  }

  return Architecture::GENERIC;
}

// ═══════════════════════════════════════════════════════════════════
// Top-level: detect all blocks from a loaded trace
//
// Python: detect_blocks(ops) -> list[Block]
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline DetectionResult detect_blocks(const LoadedTrace& trace) {
  DetectionResult result;

  auto ops = build_ops(trace);
  if (ops.empty()) return result;

  const auto n = static_cast<uint32_t>(ops.size());
  std::span<const Op> ops_span{ops};

  // Phase boundaries.
  auto pb = detect_phases(ops_span);
  result.fwd_end = pb.fwd_end;
  result.bwd_end = pb.bwd_end;
  result.optim_start = pb.optim_start;

  // Forward blocks.
  auto fwd = detect_forward_blocks(ops_span, pb.fwd_end);

  // Backward blocks (needs fwd_blocks for strategy selection).
  auto bwd = detect_backward_blocks(ops_span, pb.fwd_end, pb.bwd_end, fwd);

  // Optimizer block.
  std::vector<Block> optim_blocks;
  if (pb.optim_start < n) {
    auto optim_ops = ops_span.subspan(pb.optim_start);

    // Find last addcdiv_ for core optimizer ops.
    uint32_t last_addcdiv = pb.optim_start;
    for (const auto& op : optim_ops) {
      if (name_eq(op.name, "addcdiv_"))
        last_addcdiv = op.idx;
    }
    uint32_t optim_end = last_addcdiv;

    // Count parameters.
    uint32_t n_params = 0;
    for (const auto& op : optim_ops) {
      if (name_eq(op.name, "addcdiv_") && op.idx <= optim_end)
        n_params++;
    }

    // Core optimizer ops.
    // Python: core_ops = [op for op in optim_ops if op.idx <= optim_end]
    uint32_t core_count = 0;
    for (const auto& op : optim_ops) {
      if (op.idx <= optim_end) core_count++;
      else break;  // ops are sorted by idx
    }

    if (core_count > 0) {
      Block opt;
      opt.kind = BlockKind::OPTIMIZER;
      opt.phase = Phase::OPTIMIZER;
      opt.start_op = pb.optim_start;
      opt.end_op = optim_end;
      opt.num_ops = core_count;
      opt.label = "Optimizer (" + std::to_string(n_params) + " params, " +
                  std::to_string(core_count) + " ops)";
      optim_blocks.push_back(std::move(opt));
    }

    // Trailing ops after optimizer.
    if (optim_end + 1 < n) {
      uint32_t trailing_count = n - optim_end - 1;
      Block epi;
      epi.kind = BlockKind::EPILOGUE;
      epi.phase = Phase::OPTIMIZER;
      epi.start_op = optim_end + 1;
      epi.end_op = n - 1;
      epi.num_ops = trailing_count;
      epi.label = "Epilogue (" + std::to_string(trailing_count) + " ops)";
      optim_blocks.push_back(std::move(epi));
    }
  }

  // Detect architecture from forward blocks.
  result.architecture = detect_architecture(fwd);

  // Combine: forward + backward + optimizer (same order as Python).
  result.blocks.reserve(fwd.size() + bwd.size() + optim_blocks.size());
  for (auto& b : fwd) result.blocks.push_back(std::move(b));
  for (auto& b : bwd) result.blocks.push_back(std::move(b));
  for (auto& b : optim_blocks) result.blocks.push_back(std::move(b));

  return result;
}

} // namespace crucible::vis
