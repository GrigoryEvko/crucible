#pragma once

// Lower: TraceGraph → Graph IR conversion.
//
// Bridges the recording layer (TraceEntry[], CSR property graph)
// and the mutable computation graph (GraphNode, 64B cache-line IR).
//
// One GraphNode per TraceEntry, plus dedicated INPUT nodes for
// external tensors (params, data loader outputs), deduplicated
// by slot_id. Slot IDs propagate through Graph's side-tables
// so the Vessel can serve all tensors from the pre-allocated pool.
//
// Graph inputs  = INPUT nodes (external tensors).
// Graph outputs = ops with no DATA_FLOW consumers in the DFG.

#include <crucible/ExprPool.h>
#include <crucible/Graph.h>
#include <crucible/TraceGraph.h>

#include <cstring>

namespace crucible {

// Lower a recorded TraceGraph into a mutable Graph IR.
//
// Populates `graph` with one GraphNode per TraceEntry:
//   - NodeKind from classify_node_kind(kernel_id)
//   - Symbolic sizes from output TensorMeta
//   - Input/output slot IDs in the Graph's side-tables
//   - Wired to producer nodes via input_trace_indices
//
// POINTWISE ops get add_pointwise (null body, filled by Tier 2+).
// Everything else gets add_extern with ckernel_name as the label,
// then kind is patched to the correct NodeKind.
//
// Null tensor inputs (Optional[Tensor] = None) are filtered out:
// the Graph node will have fewer inputs than the TraceEntry. Slot
// IDs are compacted to match the filtered input list.
inline void lower_trace_to_graph(
    const TraceGraph& tg,
    ExprPool& pool,
    Graph& graph)
{
  const uint32_t num_ops = tg.num_ops;
  const uint32_t num_slots = tg.num_slots;
  if (num_ops == 0) return;

  Arena& arena = graph.arena();

  // ── Phase 1: INPUT nodes for external tensors ─────────────────
  //
  // Scan all op inputs to find externals (input_trace_indices == UINT32_MAX
  // with a valid slot_id). Create one INPUT node per unique slot_id.

  const uint32_t map_size = (num_slots > 0) ? num_slots : 1;
  auto** extern_map = arena.alloc_array<GraphNode*>(map_size);
  std::memset(extern_map, 0, map_size * sizeof(GraphNode*));

  for (uint32_t i = 0; i < num_ops; i++) {
    const TraceEntry& te = tg.ops[i];
    if (!te.input_trace_indices || !te.input_slot_ids) continue;

    for (uint16_t j = 0; j < te.num_inputs; j++) {
      if (te.input_trace_indices[j] != UINT32_MAX) continue;
      const uint32_t sid = te.input_slot_ids[j];
      if (sid == UINT32_MAX || sid >= num_slots) continue;
      if (extern_map[sid]) continue; // already created

      // Create INPUT node with symbolic sizes from the TensorMeta.
      const TensorMeta& m = te.input_metas[j];
      const Expr* sizes[8];
      const uint8_t ndim = (m.ndim <= 8) ? m.ndim : 8;
      for (uint8_t d = 0; d < ndim; d++)
        sizes[d] = pool.integer(m.sizes[d]);

      auto* inp = graph.add_input(m.dtype, m.device_idx, sizes, ndim);
      graph.set_output_slots(inp->id, &sid, 1);
      extern_map[sid] = inp;
    }
  }

  // ── Phase 2: Compute nodes for each TraceEntry ────────────────

  auto** op_to_node = arena.alloc_array<GraphNode*>(num_ops);

  for (uint32_t i = 0; i < num_ops; i++) {
    const TraceEntry& te = tg.ops[i];
    const NodeKind kind = classify_node_kind(te.kernel_id);

    // Output metadata from primary output tensor.
    uint8_t ndim = 0;
    int8_t dtype = 0;
    int8_t dev = -1;
    const Expr* sizes[8] = {};

    if (te.num_outputs > 0 && te.output_metas) {
      const TensorMeta& m = te.output_metas[0];
      ndim = (m.ndim <= 8) ? m.ndim : 8;
      dtype = m.dtype;
      dev = m.device_idx;
      for (uint8_t d = 0; d < ndim; d++)
        sizes[d] = pool.integer(m.sizes[d]);
    }

    // Resolve input dependencies, filtering null inputs.
    // Two passes: count real inputs, then collect.
    uint16_t real_count = 0;
    for (uint16_t j = 0; j < te.num_inputs; j++) {
      const uint32_t tidx = te.input_trace_indices
          ? te.input_trace_indices[j] : UINT32_MAX;
      const uint32_t sid = te.input_slot_ids
          ? te.input_slot_ids[j] : UINT32_MAX;

      if (tidx != UINT32_MAX && tidx < num_ops) {
        real_count++;
      } else if (sid != UINT32_MAX && sid < num_slots && extern_map[sid]) {
        real_count++;
      }
    }

    const uint16_t alloc_n = (real_count > 0) ? real_count : 1;
    auto** deps = arena.alloc_array<GraphNode*>(alloc_n);
    auto* in_slots = arena.alloc_array<uint32_t>(alloc_n);
    uint16_t k = 0;

    for (uint16_t j = 0; j < te.num_inputs; j++) {
      const uint32_t tidx = te.input_trace_indices
          ? te.input_trace_indices[j] : UINT32_MAX;
      const uint32_t sid = te.input_slot_ids
          ? te.input_slot_ids[j] : UINT32_MAX;

      GraphNode* dep = nullptr;
      if (tidx != UINT32_MAX && tidx < num_ops) {
        dep = op_to_node[tidx];
      } else if (sid != UINT32_MAX && sid < num_slots) {
        dep = extern_map[sid];
      }

      if (!dep) continue;
      deps[k] = dep;
      in_slots[k] = sid;
      k++;
    }

    // Create GraphNode.
    GraphNode* node;
    if (kind == NodeKind::POINTWISE) {
      node = graph.add_pointwise(
          sizes, ndim, dtype, dev,
          nullptr, // body built by Tier 2+
          deps, real_count);
    } else {
      // EXTERN for everything else (REDUCTION, NOP, MUTATION, SCAN, EXTERN).
      // add_extern provides the structural shell; kind is patched below.
      node = graph.add_extern(
          ckernel_name(te.kernel_id),
          ckernel_name(te.kernel_id),
          dtype, dev, sizes, ndim,
          deps, real_count);
      node->kind = kind;
    }

    // Multi-output ops (e.g. topk → values + indices).
    if (te.num_outputs > 1)
      node->num_outputs = te.num_outputs;

    // Carry slot IDs into Graph's side-tables.
    if (real_count > 0)
      graph.set_input_slots(node->id, in_slots, real_count);
    if (te.output_slot_ids && te.num_outputs > 0)
      graph.set_output_slots(node->id, te.output_slot_ids, te.num_outputs);

    op_to_node[i] = node;
  }

  // ── Phase 3: Graph inputs and outputs ─────────────────────────

  // Graph inputs: all INPUT nodes, ordered by slot_id.
  auto* input_ids = arena.alloc_array<uint32_t>(map_size);
  uint32_t n_inputs = 0;
  for (uint32_t s = 0; s < num_slots; s++) {
    if (extern_map[s])
      input_ids[n_inputs++] = extern_map[s]->id;
  }
  if (n_inputs > 0)
    graph.set_graph_inputs(input_ids, n_inputs);

  // Graph outputs: ops whose outputs are not consumed by any
  // DATA_FLOW edge within this iteration (terminal values).
  auto* output_ids = arena.alloc_array<uint32_t>(num_ops);
  uint32_t n_outputs = 0;
  for (uint32_t i = 0; i < num_ops; i++) {
    bool has_df_consumer = false;
    for (const Edge* e = tg.fwd_begin(i); e != tg.fwd_end(i); e++) {
      if (e->kind == EdgeKind::DATA_FLOW) {
        has_df_consumer = true;
        break;
      }
    }
    if (!has_df_consumer)
      output_ids[n_outputs++] = op_to_node[i]->id;
  }
  if (n_outputs > 0)
    graph.set_graph_outputs(output_ids, n_outputs);
}

} // namespace crucible
