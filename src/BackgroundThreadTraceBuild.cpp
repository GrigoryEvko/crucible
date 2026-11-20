// The bodies of the trace build of crucible/BackgroundThread.h: the scratch
// buffers of the build, build_trace_from and build_trace_columns_.  The
// build stage of the pipeline calls build_trace_from for each work item.
//
// The trace build is a unit of its own, so its compile runs beside the
// compile of the pipeline unit src/BackgroundThread.cpp.

#include <crucible/BackgroundThread.h>

#include <crucible/CKernel.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceGraph.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <foundation/contracts/Pre.h>

#include <cstddef>
#include <utility>

namespace crucible {

void BackgroundThread::ensure_scratch_buffers(::foundation::effects::Alloc a, uint32_t total_inputs,
                                              uint32_t total_outputs) {
    // Power-of-two capacity at a load factor below one half.  The unique
    // pointer count is the output count plus a small external fraction.
    uint32_t raw_map_size = ::fixy::max(
        MIN_PTR_MAP_CAP, ::fixy::saturating_mul(::fixy::saturating_add(total_outputs, uint32_t{256}), uint32_t{2}));
    uint32_t needed_map =
        (raw_map_size >= MAX_PTR_MAP_CAP)
            ? MAX_PTR_MAP_CAP
            : ::fixy::bit_ceil(raw_map_size).expect("a size below MAX_PTR_MAP_CAP has a power of two in 32 bits");

    // Unique storages are bounded by the outputs plus external headroom.
    uint32_t needed_slots = ::fixy::max(MIN_SCRATCH_SLOT_CAP, ::fixy::saturating_add(total_outputs, uint32_t{1024}));

    // Data-flow edges are bounded by the inputs, alias edges by the
    // outputs.
    uint32_t needed_edges = ::fixy::max(MIN_SCRATCH_EDGE_CAP, ::fixy::saturating_add(total_inputs, total_outputs));

    if (needed_map > map_cap_.get()) {
        map_cap_.advance(needed_map);
        scratch_map_ = ::fixy::mint_masked_array<PtrSlot>(a, map_cap_.get());
        // Each fresh slot holds generation zero, so generation zero is unused.
        map_gen_ = 0;
    }

    if (needed_slots > slot_cap_max_.get()) {
        slot_cap_max_.advance(needed_slots);
        scratch_slots_ = ::fixy::mint_heap_array(a, slot_cap_max_.get(), SlotInfo{});
    }

    if (needed_edges > edge_cap_max_.get()) {
        edge_cap_max_.advance(needed_edges);
        scratch_edges_ = ::fixy::mint_heap_array(a, edge_cap_max_.get(), Edge{});
    }
}

BackgroundThread::TraceBuild BackgroundThread::build_trace_from(
    ::foundation::effects::Alloc a, ::fixy::BumpArena& scratch, ::fixy::View<const TraceRing::Entry> trace,
    ::fixy::View<const MetaIndex> meta_starts, ::fixy::View<const ScopeHash> scope_hashes,
    ::fixy::View<const CallsiteHash> callsite_hashes, ::fixy::View<const OpDepth> depths,
    NumericsHash numerics) noexcept {
    CRUCIBLE_PRE(meta_starts.size() == trace.size());
    CRUCIBLE_PRE(scope_hashes.size() == trace.size());
    CRUCIBLE_PRE(callsite_hashes.size() == trace.size());
    CRUCIBLE_PRE(depths.size() == trace.size());
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(numerics));
    CRUCIBLE_PRE(!numerics.is_sentinel());

    // The five arrays take the brand of one scope over the ops.  The scope
    // ends the process for more than 0xFFFFFFFF ops.
    return ::fixy::mint_branded_scope(trace, [&](auto trace_data) noexcept -> TraceBuild {
        return build_trace_columns_(
            a, scratch, trace_data,
            trace_data.parallel(meta_starts).expect("the metadata starts are parallel to the ops"),
            trace_data.parallel(scope_hashes).expect("the scope hashes are parallel to the ops"),
            trace_data.parallel(callsite_hashes).expect("the callsite hashes are parallel to the ops"),
            trace_data.parallel(depths).expect("the depths are parallel to the ops"), numerics);
    });
}

template <class TraceColumn, class MetaColumn, class ScopeColumn, class CallsiteColumn, class DepthColumn>
BackgroundThread::TraceBuild BackgroundThread::build_trace_columns_(
    ::foundation::effects::Alloc a, ::fixy::BumpArena& scratch, TraceColumn trace_data, MetaColumn meta_data,
    ScopeColumn scope_data, CallsiteColumn callsite_data, DepthColumn depth_data, NumericsHash numerics) noexcept {
    // An op index has 32 bits, and a branded scope holds at most 0xFFFFFFFF
    // ops.
    const uint32_t count = ::fixy::narrow<uint32_t>(trace_data.size()).expect("a branded count fits in 32 bits");

    // Scan for totals and for metadata-log overflow.  A metadata index is a
    // count of records with 64 bits, and the arithmetic on it keeps 64 bits.
    // The loop has one exit, so the optimizer removes the check of each read
    // of the cursor.  An overflow is rare, so the full scan costs nothing.
    uint64_t max_meta_end = 0;
    MetaIndex first_meta = MetaIndex::none();
    uint32_t total_inputs = 0;
    uint32_t total_outputs = 0;
    uint32_t total_scalars = 0;
    bool has_overflow = false;
    bool has_unrecorded_nest = false;
    for (const auto index : trace_data.indices()) {
        MetaIndex ms = meta_data.at(index);
        const TraceRing::Entry& re = trace_data.at(index);
        // An overflow is an op that has tensors and no metadata index.
        has_overflow |= !ms.is_valid() && (re.num_inputs + re.num_outputs) > 0;
        has_unrecorded_nest |= re.schema_hash == kUnrecordedNestSchema;
        if (ms.is_valid()) {
            if (!first_meta.is_valid()) first_meta = ms;
            const uint64_t meta_end = ms.raw() + re.num_inputs + re.num_outputs;
            if (meta_end > max_meta_end) max_meta_end = meta_end;
        }
        total_inputs += re.num_inputs;
        total_outputs += re.num_outputs;
        total_scalars += ::fixy::min(uint32_t{re.num_scalar_args}, kScalarSlots);
    }
    if (has_overflow) {
        return ::fixy::err(BuildRefusal::MetaLogOverflow);
    }
    // A nest whose op did not reach the Vigil holds ops of a computation that
    // the trace does not finish, so a region of it must not compile.
    if (has_unrecorded_nest) {
        return ::fixy::err(BuildRefusal::UnrecordedNest);
    }

    ensure_scratch_buffers(a, total_inputs, total_outputs);

    // The ops keep their tensor metadata in a buffer of the region, beside
    // their other arrays.  A published region lives as long as its memory.
    // Activation, the region cache and the store read its ops after the
    // publish stage releases this run of the metadata log.  The foreground
    // then writes new records over the run.  The build copies the run, and
    // no op points into the log.  Each read below reads the copy.
    //
    // The tail stays at or before the first record of a work until the
    // release of that work.  The log held each record of the run at one
    // time, and the run is at most CAPACITY records.  The check is armed in
    // a release build too: the count of the copy has 32 bits, and a longer
    // run loses its high bits there.
    uint32_t total_metas = 0;
    if (first_meta.is_valid()) {
        const uint64_t run_length = max_meta_end - first_meta.raw();
        CRUCIBLE_FATAL_INVARIANT(run_length <= MetaLog::CAPACITY);
        total_metas = static_cast<uint32_t>(run_length);
    }

    // The ops of the region, one for each entry of the trace.  The column of
    // the ops takes the brand of the scope, so one index of an op reads it
    // with no check.
    ::fixy::HeapArray<TraceEntry> op_buffer = ::fixy::mint_heap_array(a, count, TraceEntry{});
    const auto op_data = trace_data.parallel(op_buffer.view()).expect("the op buffer holds one op for each entry");

    ::fixy::HeapArray<TensorMeta> meta_buffer = ::fixy::mint_heap_array(a, total_metas, TensorMeta{});
    if (first_meta.is_valid()) {
        meta_log_.get()->copy_run(first_meta.raw(), meta_buffer.view());
    }

    // The other arrays of the ops: the scalars, the producer of each input,
    // and the slot ids of the inputs and then the outputs of each op.  Each
    // op takes its windows in order, so a cursor for each buffer gives the
    // start of the next window.
    ::fixy::HeapArray<int64_t> scalar_buffer = ::fixy::mint_heap_array(a, total_scalars, int64_t{0});
    ::fixy::HeapArray<OpIndex> producer_buffer = ::fixy::mint_heap_array(a, total_inputs, OpIndex{});
    ::fixy::HeapArray<SlotId> slot_buffer = ::fixy::mint_heap_array(a, total_inputs + total_outputs, SlotId{});
    const ::fixy::View<TensorMeta> meta_run = meta_buffer.view();
    const ::fixy::View<int64_t> scalar_run = scalar_buffer.view();
    const ::fixy::View<OpIndex> producer_run = producer_buffer.view();
    const ::fixy::View<SlotId> slot_run = slot_buffer.view();
    size_t scalar_cursor = 0;
    size_t producer_cursor = 0;
    size_t slot_cursor = 0;

    map_gen_++;
    if (map_gen_ == 0) [[unlikely]] {
        // Generation wrapped, so stale slots would read as current.
        for (PtrSlot& slot : scratch_map_.view()) {
            slot = PtrSlot{};
        }
        map_gen_ = 1;
    }

    // Value-initialize only the portion this call uses.  The window of the
    // slots is exactly the slot_cap slots of this call, so a slot id below
    // slot_cap reads its slot with the one compare of the window.
    uint32_t slot_cap = ::fixy::min(slot_cap_max_.get(), ::fixy::max(uint32_t{256}, total_inputs + total_outputs));
    const ::fixy::View<SlotInfo> local_slots =
        scratch_slots_.view().window(0, slot_cap).expect("the slot scratch holds slot_cap_max_ slots");
    for (SlotInfo& slot_info : local_slots) {
        slot_info = SlotInfo{};
    }
    uint32_t next_slot_raw = 0;

    // The edge buffer needs no initialization: num_edges bounds every
    // read.
    uint32_t num_edges = 0;

    // Hoisted into locals because the compiler cannot prove the writes
    // below into the buffers of the region do not alias *this, and would
    // otherwise reload each member on every access.
    const ::fixy::View<Edge> local_edges = scratch_edges_.view();
    const uint8_t local_gen = map_gen_;

    // The ops whose nest is open, for the rule of the same storage below.
    // Each op pushes one mark, so the stack holds at most count marks.
    struct NestMark {
        uint32_t op = 0;
        OpDepth depth{};
    };
    ::fixy::HeapArray<NestMark> nest_stack = ::fixy::mint_heap_array(a, count, NestMark{});
    uint32_t nest_top = 0;

    // The fold object owns the seed, the per-op mix and the finalizer,
    // so this streaming producer cannot drift from the span producer in
    // MerkleDag.h.  Spelling the seed here instead is what used to make
    // a change to one of them silent.
    //
    // The content hash of a region is the key of the kernel cache, so it
    // separates two regions whose numerics differ.  The fold already reads
    // the dtype and the device of each tensor.  The rest of the numerics is
    // the policy of the run and the eager word of the front end, and their
    // hash folds in first (compute_numerics_hash).  The recipe of one op is
    // the policy, the dtype and the device together, and the compile of a
    // kernel derives it, so no op carries a recipe.  The numerics are the
    // numerics of the stream at the cut of the work (BgBuildWork::numerics).
    ContentHashFold content_fold{numerics};

    for (const auto index : trace_data.indices()) {
        const uint32_t i = ::fixy::narrow<uint32_t>(index.raw()).expect("a branded index fits in 32 bits");
        const TraceRing::Entry& re = trace_data.at(index);
        MetaIndex ms = meta_data.at(index);
        TraceEntry& te = op_data.at(index);

        te.schema_hash = re.schema_hash;
        te.shape_hash = re.shape_hash;
        te.scope_hash = scope_data.at(index);
        te.callsite_hash = callsite_data.at(index);
        te.depth = depth_data.at(index);
        te.grad_enabled = (re.op_flags & op_flag::GRAD_ENABLED) != 0;
        te.kernel_id = classify_kernel(re.schema_hash);

        // The flag bits are ground truth captured at the dispatch site
        // from the schema, thread-local state and dispatch keys, not
        // reconstructed here by heuristic.
        const uint8_t flags = re.op_flags;
        te.inference_mode = (flags & op_flag::INFERENCE_MODE) != 0;
        te.is_mutable = (flags & op_flag::IS_MUTABLE) != 0;
        te.training_phase = static_cast<TrainingPhase>((flags & op_flag::PHASE_MASK) >> op_flag::PHASE_SHIFT);
        te.torch_function = (flags & op_flag::TORCH_FUNCTION) != 0;
        // The scalar array is fixed at five entries, so a larger count
        // means the recorder wrote past it or a stored count is corrupt.
        // The check ends the process in each build, before a copy can
        // read past the array.
        CRUCIBLE_FATAL_INVARIANT(re.num_scalar_args <= kScalarSlots);
        const uint16_t n_scalars = re.num_scalar_args;

        // The scalars of each op go into its entry, also for an op with no
        // tensors, so they fold into the content hash.
        const ::fixy::View<int64_t> scalars =
            scalar_run.window(scalar_cursor, n_scalars).expect("the scan counted the scalars of each op");
        scalar_cursor += n_scalars;
        ::fixy::copy(scalars, re.scalar_values.view().window(0, n_scalars).expect("an entry stores five scalars"))
            .expect("the two runs hold the scalars of the op");
        te.set_scalar_args(scalars);
        // The tags and the data slots of the copied slots.
        te.scalar_kinds = re.scalar_kinds();

        // The windows of this op.  An op with no metadata has no tensors,
        // because the scan refused an op with tensors and no metadata index.
        // It keeps the empty Views and the counts of zero, and its loops
        // below run no step.
        ::fixy::View<TensorMeta> input_metas{};
        ::fixy::View<TensorMeta> output_metas{};
        ::fixy::View<OpIndex> producers{};
        ::fixy::View<SlotId> input_slot_ids{};
        ::fixy::View<SlotId> output_slot_ids{};
        if (ms.is_valid()) {
            uint16_t n_in = re.num_inputs;
            uint16_t n_out = re.num_outputs;

            const uint64_t meta_offset = ms.raw() - first_meta.raw();
            input_metas = meta_run.window(meta_offset, n_in).expect("the copied run holds the metadata of each op");
            output_metas =
                meta_run.window(meta_offset + n_in, n_out).expect("the copied run holds the metadata of each op");

            producers = producer_run.window(producer_cursor, n_in).expect("the scan counted the inputs of each op");
            producer_cursor += n_in;
            input_slot_ids = slot_run.window(slot_cursor, n_in).expect("the scan counted the inputs of each op");
            slot_cursor += n_in;
            output_slot_ids = slot_run.window(slot_cursor, n_out).expect("the scan counted the outputs of each op");
            slot_cursor += n_out;

            te.set_inputs(input_metas, producers, input_slot_ids);
            te.set_outputs(output_metas, output_slot_ids);
        }

        // Prefetch the map slots the next op will probe.  Processing the
        // current op gives the lines time to arrive.  The last op has no
        // next op.
        trace_data.index(index.raw() + 1)
            .match(
                [&](auto next) noexcept {
                    MetaIndex next_ms = meta_data.at(next);
                    if (!next_ms.is_valid()) return;
                    const uint64_t next_off = next_ms.raw() - first_meta.raw();
                    const TraceRing::Entry& next_re = trace_data.at(next);
                    // The scan bounds each offset of a metadata index by
                    // the copied run, so each record below is in the run.
                    if (next_re.num_inputs > 0) {
                        const TensorMeta& next_input =
                            *meta_run.get(next_off).expect("the copied run holds the metadata of each op");
                        ::fixy::prefetch<::fixy::PrefetchAccess::read, 1>(
                            scratch_map_.at_masked(hash_ptr(storage_key(next_input))));
                    }
                    if (next_re.num_outputs > 0) {
                        const TensorMeta& next_output = *meta_run.get(next_off + next_re.num_inputs)
                                                             .expect("the copied run holds the metadata of each op");
                        ::fixy::prefetch<::fixy::PrefetchAccess::write, 1>(
                            scratch_map_.at_masked(hash_ptr(storage_key(next_output))));
                    }
                },
                [] noexcept {});

        // The first op of the nest of op i.  The record is in post-order, so
        // the ops that op i called come just before it, each deeper than op
        // i, and the op before them has the depth of op i or less.  The
        // stack keeps the ops whose nest is open, with depths that do not
        // decrease to its top.  O(1) amortized for each op.
        while (nest_top > 0 && nest_stack.view().get(nest_top - 1).expect("the stack holds its top")->depth > te.depth)
            --nest_top;
        const uint32_t nest_start =
            nest_top == 0 ? 0 : nest_stack.view().get(nest_top - 1).expect("the stack holds its top")->op + 1;
        *nest_stack.view().get(nest_top).expect("the stack holds at most one mark for each op") =
            NestMark{.op = i, .depth = te.depth};
        ++nest_top;

        for (uint16_t j = 0; j < te.num_inputs(); j++) {
            const TensorMeta& input_meta = *input_metas.get(j).expect("the window holds each input of the op");
            OpIndex& producer = *producers.get(j).expect("the window holds each input of the op");
            SlotId& input_slot = *input_slot_ids.get(j).expect("the window holds each input of the op");
            producer = OpIndex{};
            input_slot = SlotId{};
            if (input_meta.data_ptr.value() == nullptr) continue;
            const PtrMapKey input_key = storage_key(input_meta);
            const uint64_t input_span = span_bytes_of_(input_meta);
            const ::fixy::WeakRef<PtrSlot> entry = ptr_map_find(scratch_map_, local_gen, input_key);
            if (entry) {
                // A storage that an op of the region wrote, or an external
                // storage that an op read before.  The op is a reader of the
                // storage, so an output of the op on it is the same storage.
                entry->reader = OpIndex{i};
                producer = entry->op_index;
                input_slot = entry->slot_id;
                if (entry->op_index.is_valid()) {
                    // The edge scratch holds an edge for each input and each
                    // output, and the check of the element ends the process
                    // in each build when the count reaches its end.  The
                    // check does not depend on the contract evaluation
                    // semantic, which is a per-target build option.
                    *local_edges.get(num_edges).expect(
                        "the edge scratch holds an edge for each input and each output") =
                        Edge{.src = OpIndex{entry->op_index.raw()},
                             .dst = OpIndex{i},
                             .src_port = entry->port,
                             .dst_port = static_cast<uint8_t>(j),
                             .kind = EdgeKind::DATA_FLOW,
                             .pad = 0};
                    ++num_edges;
                }
                // A slot id at or past slot_cap names no slot of this call.
                for (auto slot_info : local_slots.get(entry->slot_id.raw())) {
                    if (slot_info->death_op < OpIndex{i}) slot_info->death_op = OpIndex{i};
                    slot_info->nbytes = ::fixy::max(slot_info->nbytes, input_span);
                }
            } else if (next_slot_raw < slot_cap) {
                // First sight of an external storage, such as a parameter or
                // a data-loader output.  The birth holds the op of the first
                // sight while the build runs, for the rule of a storage that
                // a nest makes below, and the region gives an external slot
                // the birth zero.
                SlotId new_slot{next_slot_raw++};
                input_slot = new_slot;
                SlotInfo& slot_info = *local_slots.get(new_slot.raw()).expect("a new slot is below slot_cap");
                slot_info.birth_op = OpIndex{i};
                slot_info.death_op = OpIndex{i};
                slot_info.is_external = true;
                slot_info.nbytes = input_span;
                slot_info.dtype = input_meta.dtype;
                slot_info.device_type = input_meta.device_type;
                slot_info.device_idx = input_meta.device_idx;
                slot_info.layout = input_meta.layout;
                const InsertResult inserted =
                    ptr_map_insert(scratch_map_, local_gen, input_key, OpIndex{}, 0, new_slot);
                if (inserted.slot) inserted.slot->reader = OpIndex{i};
            }
        }

        for (uint16_t j = 0; j < te.num_outputs(); j++) {
            const TensorMeta& output_meta = *output_metas.get(j).expect("the window holds each output of the op");
            SlotId& output_slot = *output_slot_ids.get(j).expect("the window holds each output of the op");
            output_slot = SlotId{};
            if (output_meta.data_ptr.value() == nullptr) continue;
            const PtrMapKey output_key = storage_key(output_meta);
            const uint64_t output_span = span_bytes_of_(output_meta);
            const ::fixy::WeakRef<PtrSlot> entry = ptr_map_find(scratch_map_, local_gen, output_key);

            // A key with a known generation names one storage of the process,
            // so a key of the map with a known generation is the same storage.
            //
            // A key with no known generation is the address of the first byte
            // of the storage, in one period of the allocator for an unknown
            // generation.  The allocator can give that address to a new
            // storage after a free.  So such a key in the map names the same
            // storage only when the op shows that the storage lives across
            // it: the op reads the
            // storage, which keeps it alive during the op (an in-place op,
            // an out= op, a view or an alias of an input); the output is a
            // view, which never owns a new storage; or an op in the nest of
            // the op, or the op itself, wrote it, as addmm writes the result
            // that linear returns.  Each other output at a known key is a new
            // storage at the address of a dead one, and takes a slot of its
            // own.  The mutable flag of the op tells nothing more: an in-place
            // or out= op reads the tensor that it writes, and an op such as
            // batch norm writes its running statistics in place and returns
            // new tensors.  An input with no generation at the address of a
            // dead storage still reads the slot of the dead storage, because
            // a read shows no allocation.  An input with an unknown generation
            // does so only in the period of the dead storage, and in one
            // period the front end records no new storage with no generation
            // at the address of a dead one.
            const bool is_written_in_the_nest =
                entry && entry->op_index.is_valid() && entry->op_index.raw() >= nest_start;
            const bool is_same_storage = entry && entry->slot_id.is_valid()
                                      && (output_key.generation.is_known() || entry->reader == OpIndex{i}
                                          || (output_meta.flags & meta_flags::IS_VIEW) != 0 || is_written_in_the_nest);
            if (is_same_storage) {
                output_slot = entry->slot_id;
                // A slot id at or past slot_cap names no slot of this call.
                for (auto slot_info : local_slots.get(entry->slot_id.raw())) {
                    if (slot_info->death_op < OpIndex{i}) slot_info->death_op = OpIndex{i};
                    slot_info->nbytes = ::fixy::max(slot_info->nbytes, output_span);
                    // An op of the nest met the storage first, as an input,
                    // and the op returns it with no read of its own.  A
                    // storage that lived before the op reaches its nest only
                    // through the inputs of the op, so the op made this
                    // storage, as addmm makes the result that its nested
                    // copy_ writes in place.  The storage is internal, and it
                    // is born at its first sight.
                    const bool is_made_in_the_nest = slot_info->is_external && entry->reader != OpIndex{i}
                                                  && slot_info->birth_op.raw() >= nest_start;
                    if (is_made_in_the_nest) slot_info->is_external = false;
                }
                if (entry->op_index.is_valid() && entry->op_index != OpIndex{i}) {
                    // The same bound as the data-flow edge above, with the
                    // same check of the element.
                    *local_edges.get(num_edges).expect(
                        "the edge scratch holds an edge for each input and each output") =
                        Edge{.src = OpIndex{entry->op_index.raw()},
                             .dst = OpIndex{i},
                             .src_port = entry->port,
                             .dst_port = static_cast<uint8_t>(j),
                             .kind = EdgeKind::ALIAS,
                             .pad = 0};
                    ++num_edges;
                }
                entry->op_index = OpIndex{i};
                entry->port = static_cast<uint8_t>(j);
                continue;
            }

            if (next_slot_raw < slot_cap) {
                const SlotId new_slot{next_slot_raw++};
                SlotInfo& slot_info = *local_slots.get(new_slot.raw()).expect("a new slot is below slot_cap");
                slot_info.birth_op = OpIndex{i};
                slot_info.death_op = OpIndex{i};
                slot_info.is_external = false;
                slot_info.nbytes = output_span;
                slot_info.dtype = output_meta.dtype;
                slot_info.device_type = output_meta.device_type;
                slot_info.device_idx = output_meta.device_idx;
                slot_info.layout = output_meta.layout;
                output_slot = new_slot;
            }
            if (entry) {
                entry->op_index = OpIndex{i};
                entry->port = static_cast<uint8_t>(j);
                entry->slot_id = output_slot;
                entry->reader = OpIndex{};
            } else {
                (void)ptr_map_insert(scratch_map_, local_gen, output_key, OpIndex{i}, static_cast<uint8_t>(j),
                                     output_slot);
            }
        }

        // The entry holds each of its arrays now, with the producer and the
        // slot of each input and the slot of each output, so one fold covers
        // the tensor and no-tensor cases alike.  This is the same object the
        // canonical whole-region hash drives, so the streaming result is
        // bit-identical to that pass by construction and needs no second walk.
        content_fold.fold(te);
    }

    // Each slot of the build becomes a TensorSlot of the region, in the
    // order of the slot ids.  compute_memory_plan sets the offset later.  An
    // external slot lives before the region, so its birth is op zero.
    uint32_t num_slots = ::fixy::min(next_slot_raw, slot_cap);
    const ::fixy::View<SlotInfo const> built_slots =
        local_slots.window(0, num_slots).expect("the slots of the build are below slot_cap");
    ::fixy::HeapArray<TensorSlot> slots =
        ::fixy::mint_heap_array_with<TensorSlot>(a, num_slots, [built_slots](size_t slot_idx) noexcept {
            SlotInfo const& info = *built_slots.get(slot_idx).expect("the slot of each position is built");
            return TensorSlot{.offset_bytes = 0,
                              .nbytes = info.nbytes,
                              .birth_op = info.is_external ? OpIndex{0} : info.birth_op,
                              .death_op = info.death_op,
                              .dtype = info.dtype,
                              .device_type = info.device_type,
                              .device_idx = info.device_idx,
                              .layout = info.layout,
                              .is_external = info.is_external,
                              .pad = {},
                              .slot_id = SlotId{static_cast<uint32_t>(slot_idx)},
                              .pad2 = {}};
        });

    const ::fixy::Ref<TraceGraph> graph = alloc_trace_graph(a, scratch);
    graph->ops = op_buffer.view();
    graph->slots = slots.view();
    graph->num_slots.set(num_slots);
    graph->content_hash = content_fold.finish();
    graph->max_meta_end.set(max_meta_end);
    build_csr(a, scratch, *graph,
              local_edges.window(0, num_edges).expect("the edge scratch holds each edge of the build"), count);

    return BuiltTrace{.graph = ::fixy::WeakRef<TraceGraph>{*graph},
                      .region = BuiltRegion{.ops = std::move(op_buffer),
                                            .metas = std::move(meta_buffer),
                                            .scalars = std::move(scalar_buffer),
                                            .producers = std::move(producer_buffer),
                                            .slot_ids = std::move(slot_buffer),
                                            .slots = std::move(slots),
                                            .content_hash = graph->content_hash,
                                            .numerics = numerics}};
}

}  // namespace crucible
