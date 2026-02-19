#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/TraceGraph.h>

namespace crucible {

// Background thread that drains the ring buffer and builds traces.
//
// The foreground thread records ops into the TraceRing at ~5ns each.
// This thread wakes up periodically, drains all pending entries,
// feeds them to the IterationDetector, and accumulates the linear
// trace for each iteration.
//
// Phases implemented here:
//   1. Drain ring buffer (batch of up to 4096 entries)
//   2. Append to linear trace + feed IterationDetector
//   3. On iteration boundary: finalize trace
//   4. Build TraceGraph (CSR property graph with DFG + alias edges)
//   5. Create RegionNode, compute content + Merkle hashes
//
// Phases 5-7 (compilation, verification, activation) will be added
// when those components are built.
struct BackgroundThread {
  // The ring buffer to drain from. Not owned.
  TraceRing* ring = nullptr;

  // The MetaLog to read tensor metadata from. Not owned.
  MetaLog* meta_log = nullptr;

  // Distributed context (set by Vessel adapter at start).
  int32_t rank = -1;
  int32_t world_size = 0;
  uint64_t device_capability = 0;

  // Active region pointer (written by background, read by foreground).
  // In standalone mode, the Vessel adapter polls this.
  std::atomic<RegionNode*> active_region{nullptr};

  // Optional callback invoked on the background thread whenever a new
  // RegionNode becomes available. Used by Vigil to update transactions
  // and trigger persistence without polling.
  std::function<void(RegionNode*)> region_ready_cb;

  // Iteration detection.
  IterationDetector detector;

  // Accumulated ring entries for the current iteration.
  std::vector<TraceRing::Entry> current_trace;
  // Parallel: MetaLog start index for each entry in current_trace.
  std::vector<uint32_t> current_meta_starts;
  // Parallel: module scope hash for each entry in current_trace.
  std::vector<uint64_t> current_scope_hashes;
  // Parallel: Python callsite hash for each entry in current_trace.
  std::vector<uint64_t> current_callsite_hashes;

  // Completed iteration stats.
  uint32_t iterations_completed = 0;
  uint32_t last_iteration_length = 0;

  // Arena for DAG allocations (TraceEntry, TensorMeta arrays,
  // edges, CSR structures, RegionNodes).
  Arena arena{1 << 20}; // 1MB blocks

  // Regions created but not yet compiled.
  std::vector<RegionNode*> uncompiled_regions;

  // Per-iteration property graphs (for future fusion/scheduling).
  std::vector<TraceGraph*> iteration_graphs;

  // Thread control.
  std::atomic<bool> running{false};
  std::thread thread;

  // Start the background thread. ring/meta_log must be set first.
  void start(TraceRing* r, MetaLog* ml,
             int32_t rank_ = -1, int32_t world_size_ = 0,
             uint64_t device_cap = 0) {
    ring = r;
    meta_log = ml;
    rank = rank_;
    world_size = world_size_;
    device_capability = device_cap;
    running.store(true, std::memory_order_relaxed);
    thread = std::thread([this] { run(); });
  }

  // Signal the thread to stop and join.
  void stop() {
    running.store(false, std::memory_order_relaxed);
    if (thread.joinable()) {
      thread.join();
    }
  }

  ~BackgroundThread() {
    stop();
  }

  BackgroundThread() = default;
  BackgroundThread(const BackgroundThread&) = delete;
  BackgroundThread& operator=(const BackgroundThread&) = delete;

 private:
  static constexpr uint32_t BATCH_SIZE = 4096;

  // ── PtrMap: open-addressing hash map for dataflow + alias tracking ──
  //
  // Maps data_ptr → (op_index, output_port). When a collision occurs
  // (same data_ptr, different op), that's an alias — the old entry
  // is returned for alias edge emission before being overwritten.

  static constexpr uint32_t PTR_MAP_CAP = 8192;

  struct PtrSlot {
    void* key;          // 8B
    uint32_t op_index;  // 4B
    uint32_t slot_id;   // 4B — tensor slot assigned to this storage
    uint8_t port;       // 1B
    uint8_t pad[7];     // 7B — align to 24B
  };

  static_assert(sizeof(PtrSlot) == 24, "PtrSlot should be 24 bytes");

  static uint32_t hash_ptr(void* p) {
    return static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(p) * 0x9E3779B97F4A7C15ULL >> 32);
  }

  // Insert into PtrMap. If key already existed with a different op,
  // sets old_op/old_port/old_slot to the previous entry (for alias detection)
  // and returns true. Otherwise returns false.
  static bool ptr_map_insert(
      PtrSlot* map,
      void* key,
      uint32_t op_index,
      uint8_t port,
      uint32_t slot_id,
      uint32_t& old_op,
      uint8_t& old_port,
      uint32_t& old_slot) {
    uint32_t idx = hash_ptr(key) & (PTR_MAP_CAP - 1);
    for (uint32_t probe = 0; probe < PTR_MAP_CAP; probe++) {
      auto& s = map[(idx + probe) & (PTR_MAP_CAP - 1)];
      if (s.key == nullptr) {
        s.key = key;
        s.op_index = op_index;
        s.port = port;
        s.slot_id = slot_id;
        return false;
      }
      if (s.key == key) {
        // Collision: same data_ptr, possibly different op → alias
        bool is_alias = (s.op_index != op_index);
        old_op = s.op_index;
        old_port = s.port;
        old_slot = s.slot_id;
        s.op_index = op_index;
        s.port = port;
        // Keep the same slot_id for aliases (shared storage)
        return is_alias;
      }
    }
    return false; // table full — shouldn't happen
  }

  struct PtrLookup {
    uint32_t op_index;
    uint32_t slot_id;
    uint8_t port;
  };

  static PtrLookup ptr_map_lookup(const PtrSlot* map, void* key) {
    if (!key) return {UINT32_MAX, 0, 0};
    uint32_t idx = hash_ptr(key) & (PTR_MAP_CAP - 1);
    for (uint32_t probe = 0; probe < PTR_MAP_CAP; probe++) {
      auto& s = map[(idx + probe) & (PTR_MAP_CAP - 1)];
      if (s.key == key) return {s.op_index, s.slot_id, s.port};
      if (s.key == nullptr) return {UINT32_MAX, 0, 0};
    }
    return {UINT32_MAX, 0, 0};
  }

  // ── Main loop ──

  void run() {
    TraceRing::Entry batch[BATCH_SIZE];
    uint32_t meta_batch[BATCH_SIZE];
    uint64_t scope_batch[BATCH_SIZE];
    uint64_t callsite_batch[BATCH_SIZE];

    while (running.load(std::memory_order_relaxed)) {
      uint32_t n = ring->drain(
          batch, BATCH_SIZE, meta_batch, scope_batch, callsite_batch);
      if (n == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        continue;
      }

      for (uint32_t i = 0; i < n; i++) {
        current_trace.push_back(batch[i]);
        current_meta_starts.push_back(meta_batch[i]);
        current_scope_hashes.push_back(scope_batch[i]);
        current_callsite_hashes.push_back(callsite_batch[i]);

        if (detector.check(batch[i].schema_hash)) {
          on_iteration_boundary();
        }
      }
    }

    // Drain remaining on shutdown.
    uint32_t n = ring->drain(
        batch, BATCH_SIZE, meta_batch, scope_batch, callsite_batch);
    for (uint32_t i = 0; i < n; i++) {
      current_trace.push_back(batch[i]);
      current_meta_starts.push_back(meta_batch[i]);
      current_scope_hashes.push_back(scope_batch[i]);
      current_callsite_hashes.push_back(callsite_batch[i]);
    }
  }

  void on_iteration_boundary() {
    uint32_t total = static_cast<uint32_t>(current_trace.size());
    uint32_t completed_len = total - IterationDetector::K;

    last_iteration_length = completed_len;
    iterations_completed++;

    // Build property graph, compute memory plan, create DAG region.
    if (meta_log && completed_len > 0) {
      TraceGraph* graph = build_trace(completed_len);
      if (graph) {
        iteration_graphs.push_back(graph);

        auto* region = make_region(arena, graph->ops, graph->num_ops);

        // Phase 3: compute memory plan from slot liveness data.
        if (graph->slots && graph->num_slots > 0) {
          region->plan = compute_memory_plan(
              graph->slots, graph->num_slots);
        }

        recompute_merkle(region);
        uncompiled_regions.push_back(region);

        // Signal foreground: stable iteration detected.
        active_region.store(region, std::memory_order_release);

        // Notify Vigil (or any other observer) that a region is ready.
        if (region_ready_cb) region_ready_cb(region);
      }
    }

    // TODO Phase 5: Compile new regions.
    // TODO Phase 6: Verify compiled kernels.
    // TODO Phase 7: Atomic swap to activate compiled DAG.

    // Keep the K signature ops as the start of the new iteration.
    std::vector<TraceRing::Entry> new_trace(
        current_trace.end() - IterationDetector::K,
        current_trace.end());
    std::vector<uint32_t> new_meta_starts(
        current_meta_starts.end() - IterationDetector::K,
        current_meta_starts.end());
    std::vector<uint64_t> new_scope_hashes(
        current_scope_hashes.end() - IterationDetector::K,
        current_scope_hashes.end());
    std::vector<uint64_t> new_callsite_hashes(
        current_callsite_hashes.end() - IterationDetector::K,
        current_callsite_hashes.end());
    current_trace = std::move(new_trace);
    current_meta_starts = std::move(new_meta_starts);
    current_scope_hashes = std::move(new_scope_hashes);
    current_callsite_hashes = std::move(new_callsite_hashes);
  }

  // Maximum tensor slots per iteration. Generous cap for large MoE models.
  static constexpr uint32_t MAX_SLOTS = 65536;

  // ── Build TraceGraph: ring entries + MetaLog → CSR property graph ──
  //
  // Single pass builds TraceEntries, collects DFG + ALIAS edges,
  // assigns tensor slot IDs, and tracks liveness (birth/death ops).
  // Returns nullptr on MetaLog overflow.
  TraceGraph* build_trace(uint32_t count) {
    // Check for MetaLog overflow.
    uint32_t max_meta_end = 0;
    uint32_t total_inputs = 0;
    uint32_t total_outputs = 0;
    for (uint32_t i = 0; i < count; i++) {
      uint32_t ms = current_meta_starts[i];
      if (ms == UINT32_MAX) {
        if (max_meta_end > 0)
          meta_log->advance_tail(max_meta_end);
        return nullptr;
      }
      const auto& re = current_trace[i];
      uint32_t end = ms + re.num_inputs + re.num_outputs;
      if (end > max_meta_end) max_meta_end = end;
      total_inputs += re.num_inputs;
      total_outputs += re.num_outputs;
    }

    // Allocate TraceEntries.
    auto* ops = arena.alloc_array<TraceEntry>(count);

    // Overallocate edge buffer (max: one DFG per input + one alias per output).
    uint32_t max_edges = total_inputs + total_outputs;
    auto* edge_buf = arena.alloc_array<Edge>(max_edges);
    uint32_t num_edges = 0;

    // PtrMap for dataflow + alias tracking.
    PtrSlot map[PTR_MAP_CAP];
    std::memset(map, 0, sizeof(map));

    // Slot tracking: arena-allocated parallel arrays for liveness analysis.
    uint32_t next_slot_id = 0;
    auto* slot_birth = arena.alloc_array<uint32_t>(MAX_SLOTS);
    auto* slot_death = arena.alloc_array<uint32_t>(MAX_SLOTS);
    auto* slot_nbytes_arr = arena.alloc_array<uint64_t>(MAX_SLOTS);
    auto* slot_dtype = arena.alloc_array<int8_t>(MAX_SLOTS);
    auto* slot_device_type = arena.alloc_array<int8_t>(MAX_SLOTS);
    auto* slot_device_idx = arena.alloc_array<int8_t>(MAX_SLOTS);
    auto* slot_layout = arena.alloc_array<int8_t>(MAX_SLOTS);
    auto* slot_external = arena.alloc_array<bool>(MAX_SLOTS);
    std::memset(slot_birth, 0, MAX_SLOTS * sizeof(uint32_t));
    std::memset(slot_death, 0, MAX_SLOTS * sizeof(uint32_t));
    std::memset(slot_nbytes_arr, 0, MAX_SLOTS * sizeof(uint64_t));
    std::memset(slot_external, 0, MAX_SLOTS * sizeof(bool));

    for (uint32_t i = 0; i < count; i++) {
      const auto& re = current_trace[i];
      uint32_t ms = current_meta_starts[i];
      auto& te = ops[i];

      te.schema_hash = re.schema_hash;
      te.shape_hash = re.shape_hash;
      te.scope_hash = current_scope_hashes[i];
      te.callsite_hash = current_callsite_hashes[i];
      te.num_inputs = re.num_inputs;
      te.num_outputs = re.num_outputs;
      te.num_scalar_args = re.num_scalar_args;
      te.grad_enabled = re.grad_enabled;
      te.inference_mode = re.inference_mode;
      // Copy scalar values from ring entry to arena-allocated array.
      uint16_t n_scalars = std::min(re.num_scalar_args, uint16_t(5));
      if (n_scalars > 0) {
        te.scalar_args = arena.alloc_array<int64_t>(n_scalars);
        std::memcpy(te.scalar_args, re.scalar_values, n_scalars * sizeof(int64_t));
      } else {
        te.scalar_args = nullptr;
      }
      te.num_scalar_args = n_scalars;

      // Read TensorMetas from MetaLog.
      te.input_metas = arena.alloc_array<TensorMeta>(re.num_inputs);
      te.output_metas = arena.alloc_array<TensorMeta>(re.num_outputs);
      for (uint16_t j = 0; j < re.num_inputs; j++)
        te.input_metas[j] = meta_log->at(ms + j);
      for (uint16_t j = 0; j < re.num_outputs; j++)
        te.output_metas[j] = meta_log->at(ms + re.num_inputs + j);

      // Build DFG edges + track input liveness.
      te.input_trace_indices = arena.alloc_array<uint32_t>(re.num_inputs);
      for (uint16_t j = 0; j < re.num_inputs; j++) {
        void* ptr = te.input_metas[j].data_ptr;
        auto lookup = ptr_map_lookup(map, ptr);
        te.input_trace_indices[j] = lookup.op_index;
        if (lookup.op_index != UINT32_MAX) {
          // Known tensor: emit DFG edge and extend liveness.
          edge_buf[num_edges++] = {
              lookup.op_index, i, lookup.port, static_cast<uint8_t>(j),
              EdgeKind::DATA_FLOW, 0};
          if (lookup.slot_id < MAX_SLOTS)
            slot_death[lookup.slot_id] = std::max(slot_death[lookup.slot_id], i);
        } else if (ptr != nullptr && next_slot_id < MAX_SLOTS) {
          // External tensor (param, data loader output): first encounter.
          uint32_t sid = next_slot_id++;
          slot_birth[sid] = 0;
          slot_death[sid] = i;
          slot_external[sid] = true;
          slot_nbytes_arr[sid] = compute_storage_nbytes(te.input_metas[j]);
          slot_dtype[sid] = te.input_metas[j].dtype;
          slot_device_type[sid] = te.input_metas[j].device_type;
          slot_device_idx[sid] = te.input_metas[j].device_idx;
          slot_layout[sid] = te.input_metas[j].layout;
          // Insert with op_index=UINT32_MAX to mark as external producer.
          uint32_t dummy_op;
          uint8_t dummy_port;
          uint32_t dummy_slot;
          ptr_map_insert(map, ptr, UINT32_MAX, 0, sid,
                         dummy_op, dummy_port, dummy_slot);
        }
      }

      // Register outputs: assign slot IDs + detect aliases.
      te.output_slot_ids = arena.alloc_array<uint32_t>(re.num_outputs);
      for (uint16_t j = 0; j < re.num_outputs; j++) {
        void* ptr = te.output_metas[j].data_ptr;
        if (!ptr) {
          te.output_slot_ids[j] = UINT32_MAX;
          continue;
        }

        uint32_t old_op;
        uint8_t old_port;
        uint32_t old_slot;
        // Insert with temporary slot_id=0; patched below for new keys.
        bool alias = ptr_map_insert(
            map, ptr, i, static_cast<uint8_t>(j), 0,
            old_op, old_port, old_slot);

        if (alias) {
          // Same data_ptr from a different op → alias. Reuse slot_id.
          te.output_slot_ids[j] = old_slot;
          if (old_slot < MAX_SLOTS) {
            slot_death[old_slot] = std::max(slot_death[old_slot], i);
            uint64_t nb = compute_storage_nbytes(te.output_metas[j]);
            slot_nbytes_arr[old_slot] = std::max(slot_nbytes_arr[old_slot], nb);
          }
          // Only emit ALIAS edge if the prior entry was from a real op
          // (not an external with op_index=UINT32_MAX).
          if (old_op != UINT32_MAX) {
            edge_buf[num_edges++] = {
                old_op, i, old_port, static_cast<uint8_t>(j),
                EdgeKind::ALIAS, 0};
          }
        } else if (next_slot_id < MAX_SLOTS) {
          // New unique storage: assign fresh slot and patch PtrMap.
          uint32_t sid = next_slot_id++;
          slot_birth[sid] = i;
          slot_death[sid] = i;
          slot_external[sid] = false;
          slot_nbytes_arr[sid] = compute_storage_nbytes(te.output_metas[j]);
          slot_dtype[sid] = te.output_metas[j].dtype;
          slot_device_type[sid] = te.output_metas[j].device_type;
          slot_device_idx[sid] = te.output_metas[j].device_idx;
          slot_layout[sid] = te.output_metas[j].layout;
          te.output_slot_ids[j] = sid;
          // Patch PtrMap entry with correct slot_id.
          uint32_t pidx = hash_ptr(ptr) & (PTR_MAP_CAP - 1);
          for (uint32_t probe = 0; probe < PTR_MAP_CAP; probe++) {
            auto& s = map[(pidx + probe) & (PTR_MAP_CAP - 1)];
            if (s.key == ptr) {
              s.slot_id = sid;
              break;
            }
          }
        } else {
          te.output_slot_ids[j] = UINT32_MAX;
        }
      }
    }

    // Advance MetaLog tail.
    meta_log->advance_tail(max_meta_end);

    // Build arena-allocated TensorSlot array.
    uint32_t num_slots = std::min(next_slot_id, MAX_SLOTS);
    TensorSlot* slots = nullptr;
    if (num_slots > 0) {
      slots = arena.alloc_array<TensorSlot>(num_slots);
      for (uint32_t s = 0; s < num_slots; s++) {
        slots[s].offset_bytes = 0; // assigned by compute_memory_plan()
        slots[s].nbytes = slot_nbytes_arr[s];
        slots[s].slot_id = s;
        slots[s].birth_op = slot_birth[s];
        slots[s].death_op = slot_death[s];
        slots[s].dtype = slot_dtype[s];
        slots[s].device_type = slot_device_type[s];
        slots[s].device_idx = slot_device_idx[s];
        slots[s].layout = slot_layout[s];
        slots[s].is_external = slot_external[s];
        std::memset(slots[s].pad, 0, sizeof(slots[s].pad));
      }
    }

    // Build CSR property graph.
    auto* graph = arena.alloc_obj<TraceGraph>();
    graph->ops = ops;
    graph->num_ops = count;
    graph->slots = slots;
    graph->num_slots = num_slots;
    build_csr(arena, graph, edge_buf, num_edges, count);

    return graph;
  }

 public:
  // ── Compute memory plan: sweep-line offset assignment ──
  //
  // For each internal (non-external) slot, compute a byte offset
  // in a single pre-allocated pool using best-fit allocation.
  // Alignment: 256 bytes (CUDA coalescing).
  MemoryPlan* compute_memory_plan(TensorSlot* slots, uint32_t num_slots) {
    static constexpr uint32_t ALIGNMENT = 256;

    auto* plan = arena.alloc_obj<MemoryPlan>();
    plan->slots = slots;
    plan->num_slots = num_slots;
    plan->num_external = 0;
    plan->pool_bytes = 0;

    // Populate device/distributed context.
    plan->rank = rank;
    plan->world_size = world_size;
    plan->device_capability = device_capability;
    // Infer target device from the first non-external slot.
    plan->device_type = 0;  // CPU default
    plan->device_idx = -1;
    std::memset(plan->pad0, 0, sizeof(plan->pad0));

    if (num_slots == 0)
      return plan;

    // Count externals and find target device.
    for (uint32_t s = 0; s < num_slots; s++) {
      if (slots[s].is_external) {
        plan->num_external++;
      } else if (plan->device_type == 0 && slots[s].device_type != 0) {
        // First non-CPU internal slot determines the plan's target device.
        plan->device_type = slots[s].device_type;
        plan->device_idx = slots[s].device_idx;
      }
    }

    // Build event list for sweep-line.
    // Each internal slot generates two events: ALLOC at birth, FREE at death+1.
    struct Event {
      uint32_t op;        // sweep position
      bool is_free;       // true = FREE, false = ALLOC
      uint32_t slot_id;   // which slot
    };

    uint32_t num_internal = num_slots - plan->num_external;
    if (num_internal == 0)
      return plan;

    auto* events = arena.alloc_array<Event>(num_internal * 2);
    uint32_t num_events = 0;
    for (uint32_t s = 0; s < num_slots; s++) {
      if (slots[s].is_external)
        continue;
      events[num_events++] = {slots[s].birth_op, false, s};
      events[num_events++] = {slots[s].death_op + 1, true, s};
    }

    // Sort: by op_index, ties: FREE before ALLOC.
    // Simple insertion sort — num_events is typically < 1000.
    for (uint32_t i = 1; i < num_events; i++) {
      Event tmp = events[i];
      uint32_t j = i;
      while (j > 0 && (events[j - 1].op > tmp.op ||
                        (events[j - 1].op == tmp.op &&
                         !events[j - 1].is_free && tmp.is_free))) {
        events[j] = events[j - 1];
        j--;
      }
      events[j] = tmp;
    }

    // Free-list: sorted by offset for best-fit.
    struct FreeBlock {
      uint64_t offset;
      uint64_t size;
    };
    // Stack-allocated free list (generous upper bound).
    constexpr uint32_t MAX_FREE = 4096;
    FreeBlock free_list[MAX_FREE];
    uint32_t num_free = 0;

    uint64_t pool_end = 0;

    // Sweep through events.
    for (uint32_t e = 0; e < num_events; e++) {
      auto& ev = events[e];
      if (ev.is_free) {
        // Return slot to free list.
        uint64_t offset = slots[ev.slot_id].offset_bytes;
        uint64_t size = (slots[ev.slot_id].nbytes + ALIGNMENT - 1) & ~uint64_t(ALIGNMENT - 1);

        // Try to merge with adjacent blocks.
        bool merged = false;
        for (uint32_t f = 0; f < num_free; f++) {
          if (free_list[f].offset + free_list[f].size == offset) {
            // Merge: extend existing block forward.
            free_list[f].size += size;
            // Check if we can also merge with the next block.
            for (uint32_t g = 0; g < num_free; g++) {
              if (g != f && free_list[f].offset + free_list[f].size == free_list[g].offset) {
                free_list[f].size += free_list[g].size;
                // Remove block g.
                free_list[g] = free_list[--num_free];
                break;
              }
            }
            merged = true;
            break;
          }
          if (offset + size == free_list[f].offset) {
            // Merge: extend existing block backward.
            free_list[f].offset = offset;
            free_list[f].size += size;
            merged = true;
            break;
          }
        }
        if (!merged && num_free < MAX_FREE) {
          free_list[num_free++] = {offset, size};
        }
      } else {
        // Allocate: best-fit from free list.
        uint64_t aligned_size = (slots[ev.slot_id].nbytes + ALIGNMENT - 1) & ~uint64_t(ALIGNMENT - 1);
        uint32_t best = UINT32_MAX;
        uint64_t best_waste = UINT64_MAX;
        for (uint32_t f = 0; f < num_free; f++) {
          if (free_list[f].size >= aligned_size) {
            uint64_t waste = free_list[f].size - aligned_size;
            if (waste < best_waste) {
              best = f;
              best_waste = waste;
            }
          }
        }
        if (best != UINT32_MAX) {
          // Use best-fit block.
          slots[ev.slot_id].offset_bytes = free_list[best].offset;
          if (best_waste == 0) {
            // Exact fit: remove block.
            free_list[best] = free_list[--num_free];
          } else {
            // Shrink block.
            free_list[best].offset += aligned_size;
            free_list[best].size -= aligned_size;
          }
        } else {
          // Extend pool.
          slots[ev.slot_id].offset_bytes = pool_end;
          pool_end += aligned_size;
        }
      }
    }

    plan->pool_bytes = pool_end;
    return plan;
  }
};

} // namespace crucible
