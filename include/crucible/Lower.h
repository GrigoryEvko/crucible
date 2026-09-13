#pragma once

#include <crucible/ExprPool.h>
#include <crucible/Graph.h>
#include <crucible/TraceGraph.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

#include <concepts>
#include <cstring>

namespace crucible {

template <typename Source>
concept LowerTraceSource =
    std::same_as<Source, fixy::tags::source::Recorded> || std::same_as<Source, fixy::tags::source::Replayed>;

template <LowerTraceSource Source>
using LowerTraceGraph = fixy::wrap::Tagged<const TraceGraph*, Source>;

template <LowerTraceSource Source>
using LoweredGraph = fixy::wrap::Tagged<Graph*, Source>;

using lower_trace_required_row = effects::Row<effects::Effect::Bg, effects::Effect::Alloc>;

// One graph node per recorded op, plus one input node per external tensor,
// which is any tensor with a slot but no producing op in this trace.
//
// An absent optional tensor input is dropped, so a node can end up with fewer
// inputs than the entry it came from. The slot list is compacted to match.
template <typename CallerRow, LowerTraceSource Source>
    requires effects::Subrow<lower_trace_required_row, CallerRow>
[[nodiscard]] inline LoweredGraph<Source> lower_trace_to_graph(effects::Alloc a, LowerTraceGraph<Source> trace,
                                                               ExprPool& pool, Graph& graph)
    pre(trace.value() != nullptr) {
    const TraceGraph& tg = *trace.value();
    const uint32_t num_ops = tg.num_ops.get_assuming_set();
    const uint32_t num_slots = tg.num_slots.get_assuming_set();
    if (num_ops == 0) return LoweredGraph<Source>{&graph};

    Arena& arena = graph.arena();

    // An input with no producing op but a valid slot is external. One node
    // is created per distinct slot.
    const uint32_t map_size = (num_slots > 0) ? num_slots : 1;
    auto** extern_map = arena.alloc_array<GraphNode*>(a, map_size);
    std::memset(extern_map, 0, map_size * sizeof(GraphNode*));

    for (uint32_t i = 0; i < num_ops; i++) {
        const TraceEntry& te = tg.ops[i];
        if (!te.input_trace_indices || !te.input_slot_ids) continue;

        for (uint16_t j = 0; j < te.num_inputs; j++) {
            if (te.input_trace_indices[j].is_valid()) continue;
            const SlotId sid = te.input_slot_ids[j];
            if (!sid.is_valid() || sid.raw() >= num_slots) continue;
            if (extern_map[sid.raw()]) continue;

            const TensorMeta& m = te.input_metas[j];
            fixy::wrap::FixedArray<const Expr*, 8> sizes{};
            const uint8_t ndim = (m.ndim <= 8) ? m.ndim : 8;
            for (uint8_t d = 0; d < ndim; d++)
                sizes[d] = pool.integer(a, raw_tensor_dim(m.sizes[d]));

            auto* inp = graph.add_input(a, m.dtype, m.device_idx, std::span{sizes.data(), ndim});
            graph.set_output_slots(a, inp->id, std::span{&sid, 1u});
            extern_map[sid.raw()] = inp;
        }
    }

    auto** op_to_node = arena.alloc_array<GraphNode*>(a, num_ops);

    for (uint32_t i = 0; i < num_ops; i++) {
        const TraceEntry& te = tg.ops[i];
        const NodeKind kind = classify_node_kind(te.kernel_id);

        // The node's shape comes from the first output.
        uint8_t ndim = 0;
        ScalarType dtype = ScalarType::Undefined;
        int8_t dev = -1;
        fixy::wrap::FixedArray<const Expr*, 8> sizes{};

        if (te.num_outputs > 0 && te.output_metas) {
            const TensorMeta& m = te.output_metas[0];
            ndim = (m.ndim <= 8) ? m.ndim : 8;
            dtype = m.dtype;
            dev = m.device_idx;
            for (uint8_t d = 0; d < ndim; d++)
                sizes[d] = pool.integer(a, raw_tensor_dim(m.sizes[d]));
        }

        // Counted first, then collected, because the count sizes the arrays.
        uint16_t real_count = 0;
        for (uint16_t j = 0; j < te.num_inputs; j++) {
            const OpIndex tidx = te.input_trace_indices ? te.input_trace_indices[j] : OpIndex{};
            const SlotId sid = te.input_slot_ids ? te.input_slot_ids[j] : SlotId{};

            if (tidx.is_valid() && tidx.raw() < num_ops) {
                real_count++;
            } else if (sid.is_valid() && sid.raw() < num_slots && extern_map[sid.raw()]) {
                real_count++;
            }
        }

        const uint16_t alloc_n = (real_count > 0) ? real_count : 1;
        auto** deps = arena.alloc_array<GraphNode*>(a, alloc_n);
        auto* in_slots = arena.alloc_array<SlotId>(a, alloc_n);
        uint16_t k = 0;

        for (uint16_t j = 0; j < te.num_inputs; j++) {
            const OpIndex tidx = te.input_trace_indices ? te.input_trace_indices[j] : OpIndex{};
            const SlotId sid = te.input_slot_ids ? te.input_slot_ids[j] : SlotId{};

            GraphNode* dep = nullptr;
            if (tidx.is_valid() && tidx.raw() < num_ops) {
                dep = op_to_node[tidx.raw()];
            } else if (sid.is_valid() && sid.raw() < num_slots) {
                dep = extern_map[sid.raw()];
            }

            if (!dep) continue;
            deps[k] = dep;
            in_slots[k] = sid;
            k++;
        }

        GraphNode* node;
        if (kind == NodeKind::POINTWISE) {
            node =
                graph.add_pointwise(a, std::span{sizes.data(), ndim}, dtype, dev, nullptr, std::span{deps, real_count});
        } else {
            // Every other kind is built through the external-node factory for
            // its structural shell, then corrected to its real kind.
            node = graph.add_extern(a, ckernel_name(te.kernel_id), ckernel_name(te.kernel_id), dtype, dev,
                                    std::span{sizes.data(), ndim}, std::span{deps, real_count});
            node->kind = kind;
        }

        if (te.num_outputs > 1) node->num_outputs = te.num_outputs;

        if (real_count > 0) graph.set_input_slots(a, node->id, std::span{in_slots, real_count});
        if (te.output_slot_ids && te.num_outputs > 0)
            graph.set_output_slots(a, node->id, std::span<const SlotId>{te.output_slot_ids, te.num_outputs});

        op_to_node[i] = node;
    }

    // The graph's inputs are the input nodes, in slot order.
    auto* input_ids = arena.alloc_array<NodeId>(a, map_size);
    uint32_t n_inputs = 0;
    for (uint32_t s = 0; s < num_slots; s++) {
        if (extern_map[s]) input_ids[n_inputs++] = extern_map[s]->id;
    }
    if (n_inputs > 0) graph.set_graph_inputs(a, std::span{input_ids, n_inputs});

    // The graph's outputs are the ops whose results nothing in this trace
    // consumes.
    auto* output_ids = arena.alloc_array<NodeId>(a, num_ops);
    uint32_t n_outputs = 0;
    for (uint32_t i = 0; i < num_ops; i++) {
        bool has_df_consumer = false;
        for (const Edge* e = tg.fwd_begin(OpIndex{i}); e != tg.fwd_end(OpIndex{i}); e++) {
            if (e->kind == EdgeKind::DATA_FLOW) {
                has_df_consumer = true;
                break;
            }
        }
        if (!has_df_consumer) output_ids[n_outputs++] = op_to_node[i]->id;
    }
    if (n_outputs > 0) graph.set_graph_outputs(a, std::span{output_ids, n_outputs});

    return LoweredGraph<Source>{&graph};
}

}  // namespace crucible
