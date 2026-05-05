#pragma once

// MerkleDagAdapter: live Merkle DAG -> visualization blocks.
//
// The runtime does not expose a `MerkleDag` owner class today. The live
// structure is rooted at TraceNode / RegionNode pointers published by Vigil and
// BackgroundThread, so this adapter accepts that real surface directly.

#include <crucible/MerkleDag.h>
#include <crucible/vis/BlockDetector.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crucible::vis {

enum class DagEdgeKind : uint8_t {
  SEQUENTIAL,
  BRANCH_ARM,
  LOOP_BODY,
  LOOP_FEEDBACK,
};

[[nodiscard]] constexpr const char* dag_edge_kind_name(DagEdgeKind kind) {
  switch (kind) {
    case DagEdgeKind::SEQUENTIAL:    return "seq";
    case DagEdgeKind::BRANCH_ARM:    return "branch";
    case DagEdgeKind::LOOP_BODY:     return "loop-body";
    case DagEdgeKind::LOOP_FEEDBACK: return "loop-feedback";
    default:                         return "unknown";
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

[[nodiscard]] inline std::string hex64_short(uint64_t value) {
  char buf[17]{};
  std::snprintf(buf, sizeof(buf), "%08llx",
                value & 0xFFFFFFFFULL);
  return std::string{buf};
}

[[nodiscard]] inline std::string tensor_shape_string(const TensorMeta& meta) {
  if (meta.ndim == 0) return {};
  std::string out;
  for (uint8_t d = 0; d < meta.ndim && d < 4; ++d) {
    if (d != 0) out += 'x';
    out += std::to_string(meta.sizes[d]);
  }
  return out;
}

[[nodiscard]] inline const char* guard_kind_name(Guard::Kind kind) {
  switch (kind) {
    case Guard::Kind::SHAPE_DIM:    return "shape";
    case Guard::Kind::SCALAR_VALUE: return "scalar";
    case Guard::Kind::DTYPE:        return "dtype";
    case Guard::Kind::DEVICE:       return "device";
    case Guard::Kind::OP_SEQUENCE:  return "opseq";
    default:                        return "unknown";
  }
}

[[nodiscard]] inline std::string loop_label(const LoopNode& loop) {
  std::string label = "loop ";
  if (loop.term_kind == LoopTermKind::REPEAT) {
    label += "x" + std::to_string(loop.repeat_count);
  } else {
    label += "until ";
    label += std::to_string(loop.epsilon);
  }
  if (loop.num_feedback != 0) {
    label += " fb=" + std::to_string(static_cast<unsigned>(loop.num_feedback));
  }
  return label;
}

namespace detail {

struct IncomingEdge {
  uint32_t src_block = UINT32_MAX;
  DagEdgeKind kind = DagEdgeKind::SEQUENTIAL;
  std::string label;
};

struct WalkResult {
  std::vector<IncomingEdge> tails;
  uint32_t first_block = UINT32_MAX;
};

struct AdapterState {
  MerkleDagBlockView view;
  std::unordered_map<const TraceNode*, uint32_t> node_to_block;
  std::unordered_set<std::string> edge_keys;
  uint32_t next_op = 0;

  void add_edge(uint32_t src, uint32_t dst, DagEdgeKind kind, std::string label) {
    if (src == UINT32_MAX || dst == UINT32_MAX) return;
    std::string key = std::to_string(src) + ":" + std::to_string(dst) + ":"
        + std::to_string(static_cast<uint32_t>(kind)) + ":" + label;
    if (!edge_keys.insert(std::move(key)).second) return;
    view.edges.push_back(DagBlockEdge{
        .src_block = src,
        .dst_block = dst,
        .kind = kind,
        .label = std::move(label),
    });
  }

  void connect_to(uint32_t dst, std::span<const IncomingEdge> incoming) {
    for (const auto& edge : incoming) {
      add_edge(edge.src_block, dst, edge.kind, edge.label);
    }
  }

  [[nodiscard]] Block make_region_block(const RegionNode& region) {
    Block block;
    block.kind = BlockKind::MODULE;
    block.phase = Phase::FORWARD;
    block.start_op = next_op;
    block.num_ops = region.num_ops;
    block.end_op = region.num_ops == 0 ? next_op : next_op + region.num_ops - 1;
    block.scope_path = "merkle.region." + hex64_short(region.content_hash.raw());
    block.label = "region " + hex64_short(region.content_hash.raw());

    if (region.num_ops != 0 && region.ops != nullptr) {
      for (uint32_t i = region.num_ops; i > 0; --i) {
        const TraceEntry& op = region.ops[i - 1];
        if (op.output_metas != nullptr && op.num_outputs != 0) {
          block.out_shape = tensor_shape_string(op.output_metas[0]);
          break;
        }
      }
    }
    if (!block.out_shape.empty()) {
      block.label += " [" + block.out_shape + "]";
    }

    next_op += std::max(region.num_ops, uint32_t{1});
    return block;
  }

  [[nodiscard]] Block make_branch_block(const BranchNode& branch) {
    Block block;
    block.kind = BlockKind::BRANCH;
    block.phase = Phase::FORWARD;
    block.start_op = next_op;
    block.end_op = next_op++;
    block.num_ops = 0;
    block.scope_path = "merkle.branch." + hex64_short(branch.guard.hash());
    block.label = std::string{"branch "} + guard_kind_name(branch.guard.kind)
        + " arms=" + std::to_string(branch.num_arms);
    return block;
  }

  [[nodiscard]] Block make_loop_block(const LoopNode& loop) {
    Block block;
    block.kind = BlockKind::LOOP;
    block.phase = Phase::FORWARD;
    block.start_op = next_op;
    block.end_op = next_op++;
    block.num_ops = 0;
    block.scope_path = "merkle.loop." + hex64_short(loop.body_content_hash.raw());
    block.label = loop_label(loop);
    return block;
  }

  [[nodiscard]] uint32_t push_block(const TraceNode* node, Block block) {
    const uint32_t index = static_cast<uint32_t>(view.detection.blocks.size());
    node_to_block.emplace(node, index);
    view.detection.blocks.push_back(std::move(block));
    return index;
  }

  [[nodiscard]] WalkResult walk_chain(
      const TraceNode* node,
      const TraceNode* stop,
      std::vector<IncomingEdge> incoming) {
    uint32_t first = UINT32_MAX;

    while (node != nullptr && node != stop) {
      if (node->kind == TraceNodeKind::TERMINAL) {
        break;
      }

      if (auto it = node_to_block.find(node); it != node_to_block.end()) {
        connect_to(it->second, incoming);
        if (first == UINT32_MAX) first = it->second;
        return {.tails = {IncomingEdge{it->second, DagEdgeKind::SEQUENTIAL, {}}},
                .first_block = first};
      }

      switch (node->kind) {
        case TraceNodeKind::REGION: {
          auto* region = static_cast<const RegionNode*>(node);
          const uint32_t block_id = push_block(node, make_region_block(*region));
          connect_to(block_id, incoming);
          if (first == UINT32_MAX) first = block_id;
          incoming = {IncomingEdge{block_id, DagEdgeKind::SEQUENTIAL, {}}};
          node = node->next;
          break;
        }

        case TraceNodeKind::BRANCH: {
          auto* branch = static_cast<const BranchNode*>(node);
          const uint32_t block_id = push_block(node, make_branch_block(*branch));
          connect_to(block_id, incoming);
          if (first == UINT32_MAX) first = block_id;

          std::vector<IncomingEdge> branch_tails;
          for (uint32_t i = 0; i < branch->num_arms; ++i) {
            std::vector<IncomingEdge> arm_in{
                IncomingEdge{block_id, DagEdgeKind::BRANCH_ARM,
                             "guard=" + std::to_string(branch->arms[i].value)}};
            WalkResult arm = walk_chain(branch->arms[i].target, branch->next,
                                        std::move(arm_in));
            branch_tails.insert(branch_tails.end(),
                                std::make_move_iterator(arm.tails.begin()),
                                std::make_move_iterator(arm.tails.end()));
          }
          if (branch_tails.empty()) {
            branch_tails.push_back(IncomingEdge{block_id, DagEdgeKind::SEQUENTIAL, {}});
          }
          incoming = std::move(branch_tails);
          node = branch->next;
          break;
        }

        case TraceNodeKind::LOOP: {
          auto* loop = static_cast<const LoopNode*>(node);
          const uint32_t block_id = push_block(node, make_loop_block(*loop));
          connect_to(block_id, incoming);
          if (first == UINT32_MAX) first = block_id;

          WalkResult body = walk_chain(
              loop->body, nullptr,
              {IncomingEdge{block_id, DagEdgeKind::LOOP_BODY, "body"}});
          for (const auto& tail : body.tails) {
            add_edge(tail.src_block, body.first_block, DagEdgeKind::LOOP_FEEDBACK,
                     loop->num_feedback == 0 ? "repeat" : "feedback");
          }

          incoming = body.tails.empty()
              ? std::vector<IncomingEdge>{IncomingEdge{block_id, DagEdgeKind::SEQUENTIAL, {}}}
              : std::move(body.tails);
          node = node->next;
          break;
        }

        case TraceNodeKind::TERMINAL:
          break;
        default:
          std::unreachable();
      }
    }

    return {.tails = std::move(incoming), .first_block = first};
  }
};

} // namespace detail

[[nodiscard]] inline MerkleDagBlockView extract_block_view(const TraceNode* root) {
  detail::AdapterState state;
  if (root == nullptr) return {};
  (void)state.walk_chain(root, nullptr, {});
  state.view.detection.architecture = Architecture::GENERIC;
  if (!state.view.detection.blocks.empty()) {
    state.view.detection.fwd_end =
        static_cast<uint32_t>(state.view.detection.blocks.size() - 1);
    state.view.detection.bwd_end = state.view.detection.fwd_end;
    state.view.detection.optim_start =
        static_cast<uint32_t>(state.view.detection.blocks.size());
  }
  return std::move(state.view);
}

[[nodiscard]] inline MerkleDagBlockView extract_block_view(const TraceNode& root) {
  return extract_block_view(&root);
}

[[nodiscard]] inline MerkleDagBlockView extract_block_view(const RegionNode& root) {
  return extract_block_view(static_cast<const TraceNode*>(&root));
}

[[nodiscard]] inline DetectionResult extract_blocks(const TraceNode* root) {
  return extract_block_view(root).detection;
}

[[nodiscard]] inline DetectionResult extract_blocks(const TraceNode& root) {
  return extract_blocks(&root);
}

[[nodiscard]] inline DetectionResult extract_blocks(const RegionNode& root) {
  return extract_blocks(static_cast<const TraceNode*>(&root));
}

} // namespace crucible::vis
