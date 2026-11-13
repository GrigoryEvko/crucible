#pragma once

#include <crucible/ExprPool.h>
#include <crucible/Graph.h>
#include <crucible/TraceGraph.h>
#include <fixy/FixedArray.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <concepts>

namespace crucible {

template <typename Source>
concept LowerTraceSource =
    std::same_as<Source, ::fixy::tags::source::Recorded> || std::same_as<Source, ::fixy::tags::source::Replayed>;

template <LowerTraceSource Source>
using LowerTraceGraph = ::fixy::Tagged<const TraceGraph*, Source>;

template <LowerTraceSource Source>
using LoweredGraph = ::fixy::Tagged<Graph*, Source>;

using lower_trace_required_row =
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc>;

namespace detail {

// The id at position of an array of an entry, or the none id when the entry
// holds no such array.
template <class Id>
[[nodiscard]] inline Id id_or_none(::fixy::View<const Id> ids, uint16_t position) noexcept {
    return ids.get(position).match([](auto id) { return Id{*id}; }, [] { return Id{}; });
}

}  // namespace detail

// One graph node per recorded op, plus one input node per external tensor,
// which is any tensor with a slot but no producing op in this trace.
//
// An absent optional tensor input is dropped, so a node can end up with fewer
// inputs than the entry it came from. The slot list is compacted to match.
template <typename CallerRow, LowerTraceSource Source>
    requires ::foundation::effects::Subrow<lower_trace_required_row, CallerRow>
[[nodiscard]] inline LoweredGraph<Source>
lower_trace_to_graph(::foundation::effects::Alloc a, LowerTraceGraph<Source> trace, ExprPool& pool, Graph& graph) {
    CRUCIBLE_PRE(trace.value() != nullptr);
    const TraceGraph& tg = *trace.value();
    const uint32_t num_ops = tg.num_ops.get_assuming_set();
    const uint32_t num_slots = tg.num_slots.get_assuming_set();
    if (num_ops == 0) return ::fixy::mint_tagged<Source>(&graph);

    ::fixy::BumpArena& arena = graph.arena();

    // An input with no producing op but a valid slot is external. One node
    // is created per distinct slot.
    const uint32_t map_size = (num_slots > 0) ? num_slots : 1;
    auto** extern_map = detail::alloc_zeroed_array<GraphNode*>(a, arena, map_size);

    for (uint32_t i = 0; i < num_ops; i++) {
        const TraceEntry& te = tg.ops[i];
        // An op with no producers or no slot ids of its inputs adds no input
        // node.
        const ::fixy::View<const OpIndex> producers = te.input_trace_indices();
        const ::fixy::View<const SlotId> slot_ids = te.input_slot_ids();
        if (producers.size() != te.num_inputs() || slot_ids.size() != te.num_inputs()) continue;

        for (uint16_t j = 0; j < te.num_inputs(); j++) {
            if (detail::id_or_none(producers, j).is_valid()) continue;
            const SlotId sid = detail::id_or_none(slot_ids, j);
            if (!sid.is_valid() || sid.raw() >= num_slots) continue;
            if (extern_map[sid.raw()]) continue;

            const TensorMeta& m = *te.input_metas().get(j).expect("an external input of a traced op has its metadata");
            ::fixy::FixedArray<const Expr*, 8> sizes{};
            const uint8_t ndim = (m.ndim <= 8) ? m.ndim : 8;
            for (uint8_t d = 0; d < ndim; d++)
                sizes[d] = pool.integer(a, raw_tensor_dim(m.sizes[d]));

            auto* inp = graph.add_input(a, m.dtype, m.device_idx, std::span{sizes.data(), ndim});
            const ::foundation::core::FixedArray<SlotId, 1> input_slot{sid};
            graph.set_output_slots(a, inp->id, input_slot.view());
            extern_map[sid.raw()] = inp;
        }
    }

    auto** op_to_node = detail::alloc_zeroed_array<GraphNode*>(a, arena, num_ops);

    for (uint32_t i = 0; i < num_ops; i++) {
        const TraceEntry& te = tg.ops[i];
        const NodeKind kind = classify_node_kind(te.kernel_id);

        // The node's shape comes from the first output.
        uint8_t ndim = 0;
        ScalarType dtype = ScalarType::Undefined;
        int8_t dev = -1;
        ::fixy::FixedArray<const Expr*, 8> sizes{};

        for (const TensorMeta& m : te.output_metas()) {
            ndim = (m.ndim <= 8) ? m.ndim : 8;
            dtype = m.dtype;
            dev = m.device_idx;
            for (uint8_t d = 0; d < ndim; d++)
                sizes[d] = pool.integer(a, raw_tensor_dim(m.sizes[d]));
            break;
        }

        // Counted first, then collected, because the count sizes the arrays.
        uint16_t real_count = 0;
        for (uint16_t j = 0; j < te.num_inputs(); j++) {
            const OpIndex tidx = detail::id_or_none(te.input_trace_indices(), j);
            const SlotId sid = detail::id_or_none(te.input_slot_ids(), j);

            if (tidx.is_valid() && tidx.raw() < num_ops) {
                real_count++;
            } else if (sid.is_valid() && sid.raw() < num_slots && extern_map[sid.raw()]) {
                real_count++;
            }
        }

        const uint16_t alloc_n = (real_count > 0) ? real_count : 1;
        auto** deps = detail::alloc_zeroed_array<GraphNode*>(a, arena, alloc_n);
        auto* in_slots = detail::alloc_zeroed_array<SlotId>(a, arena, alloc_n);
        uint16_t k = 0;

        for (uint16_t j = 0; j < te.num_inputs(); j++) {
            const OpIndex tidx = detail::id_or_none(te.input_trace_indices(), j);
            const SlotId sid = detail::id_or_none(te.input_slot_ids(), j);

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

        if (te.num_outputs() > 1) node->num_outputs = te.num_outputs();

        if (real_count > 0) graph.set_input_slots(a, node->id, std::span{in_slots, real_count});
        if (te.output_slot_ids().size() > 0) graph.set_output_slots(a, node->id, te.output_slot_ids());

        op_to_node[i] = node;
    }

    // The graph's inputs are the input nodes, in slot order.
    auto* input_ids = detail::alloc_zeroed_array<NodeId>(a, arena, map_size);
    uint32_t n_inputs = 0;
    for (uint32_t s = 0; s < num_slots; s++) {
        if (extern_map[s]) input_ids[n_inputs++] = extern_map[s]->id;
    }
    if (n_inputs > 0) graph.set_graph_inputs(a, std::span{input_ids, n_inputs});

    // The graph's outputs are the ops whose results nothing in this trace
    // consumes.
    auto* output_ids = detail::alloc_zeroed_array<NodeId>(a, arena, num_ops);
    uint32_t n_outputs = 0;
    for (uint32_t i = 0; i < num_ops; i++) {
        bool has_df_consumer = false;
        for (const Edge& e : tg.fwd_of(OpIndex{i})) {
            if (e.kind == EdgeKind::DATA_FLOW) {
                has_df_consumer = true;
                break;
            }
        }
        if (!has_df_consumer) output_ids[n_outputs++] = op_to_node[i]->id;
    }
    if (n_outputs > 0) graph.set_graph_outputs(a, std::span{output_ids, n_outputs});

    return ::fixy::mint_tagged<Source>(&graph);
}

}  // namespace crucible
