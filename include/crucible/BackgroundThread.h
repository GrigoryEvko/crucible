#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <crucible/DimHash.h>
#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/Saturate.h>
#include <crucible/SchemaTable.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Mutation.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/safety/Refined.h>
#include <crucible/TraceGraph.h>

namespace crucible {

// Background thread that drains the ring buffer and builds traces.
//
// The foreground thread records ops into the TraceRing at ~5ns each.
// This thread wakes up periodically, drains all pending entries,
// feeds them to the IterationDetector, and accumulates the linear
// trace for each iteration.
//
// Performance design:
//   - Scratch buffers (PtrMap, SlotInfo, Edge) are allocated ONCE
//     and reused across iterations. Zero per-call allocation.
//   - PtrMap uses a generation counter: no memset between calls.
//   - MetaLog access is true zero-copy: pointers reach directly into
//     the MetaLog buffer. No arena alloc, no memcpy (contiguous case).
//   - Content hash is fused into the main loop (streaming accumulator),
//     eliminating the redundant second pass in make_region.
//   - Memory plan uses counting sort O(n+k) instead of insertion sort O(n²).
struct BackgroundThread {
  // The ring buffer to drain from. Not owned.  The composition is:
  //   WriteOnce<NonNull<T*>>
  //     — WriteOnce: installed exactly once per BackgroundThread; a
  //       second set() is a contract fire.  has_value() / operator bool
  //       exposes the "set" state for defensive code paths.
  //     — NonNull (= Refined<non_null, T*>): the installed pointer is
  //       proven non-null at construction, so downstream bg-worker
  //       code dereferences without guards and the optimizer can
  //       [[assume]] it.  `set(nullptr)` is a contract fire.
  using RingPtr    = crucible::safety::NonNull<TraceRing*>;
  using MetaLogPtr = crucible::safety::NonNull<MetaLog*>;
  crucible::safety::WriteOnce<RingPtr>    ring;

  // The MetaLog to read tensor metadata from.  Not owned.  Same
  // installed-once + non-null discipline as ring.  `if (meta_log)`
  // continues to work via WriteOnce::operator bool (checks the
  // has-been-set state, not the pointer itself — the pointer is
  // non-null once set, by construction).
  crucible::safety::WriteOnce<MetaLogPtr> meta_log;

  // Distributed context (set by Vessel adapter at start).
  int32_t rank = -1;
  int32_t world_size = 0;
  uint64_t device_capability = 0;

  // Active region pointer (written by background, read by foreground).
  // NOT relaxed: store(release) publishes region data (ops, plan, merkle
  // hash) written before it. Fg's load(acquire) must see that data.
  // Relaxed = fg dereferences pointer to region with stale/garbage fields.
  //
  // Own cache line: fg reads, bg writes.  Co-locating with adjacent
  // bg-only fields below would have every bg write to those fields
  // invalidate the fg's cached copy of active_region.
  alignas(64) std::atomic<RegionNode*> active_region{nullptr};

  // Optional callback invoked on the background thread whenever a new
  // RegionNode becomes available. Used by Vigil to update transactions
  // and trigger persistence without polling.
  //
  // Own cache line: bg-only state.  Separating it from active_region
  // (fg-touched) prevents fg's acquire load from dragging the callback
  // object into fg's L1 every iteration.
  alignas(64) std::function<void(RegionNode*)> region_ready_cb;

  // Iteration detection.
  IterationDetector detector;

  // Accumulated ring entries for the current iteration.
  std::vector<TraceRing::Entry> current_trace;
  // Parallel: MetaLog start index for each entry in current_trace.
  // MetaIndex::none() for ops with zero tensor arguments.
  std::vector<MetaIndex> current_meta_starts;
  // Parallel: module scope hash for each entry in current_trace.
  std::vector<ScopeHash> current_scope_hashes;
  // Parallel: Python callsite hash for each entry in current_trace.
  std::vector<CallsiteHash> current_callsite_hashes;

  // Completed iteration stats.  iterations_completed is structurally
  // monotonic (only increments at iteration boundary); wrap so the
  // invariant is enforced by the type rather than by convention.
  //
  // Own cache line: bg writes, fg reads for diagnostics (via size() /
  // iteration count queries).  Co-locating with the preceding per-
  // iteration vectors would have every push_back invalidate fg's copy
  // of iterations_completed.
  alignas(64) crucible::safety::Monotonic<uint32_t> iterations_completed {0};
  uint32_t last_iteration_length = 0;

  // Arena for DAG allocations (TraceEntry, TensorMeta arrays,
  // edges, CSR structures, RegionNodes).
  Arena arena{1 << 20}; // 1MB blocks

  // Regions created but not yet compiled.  Pure push-only — drained
  // by the foreground thread once a region is associated with a
  // compiled plan.  AppendOnly enforces the "no in-place removal"
  // invariant in the type.
  crucible::safety::AppendOnly<RegionNode*> uncompiled_regions;

  // Per-iteration property graphs (for future fusion/scheduling).
  // Same append-only lifecycle.
  crucible::safety::AppendOnly<TraceGraph*> iteration_graphs;

  // Thread control.  stop_requested is a one-way signal for one run()
  // invocation; start() / tests re-arm it under quiescence with
  // reset_unsafe() before launching the next pipeline.  The signal has
  // no data dependency beyond termination, but using OneShotFlag keeps
  // the release/acquire discipline structural instead of hand-rolled
  // atomic<bool> loads and stores.
  alignas(64) crucible::safety::OneShotFlag stop_requested;
  std::jthread pipeline_thread;

  // Total entries fully processed by the bg thread.  AtomicMonotonic
  // lifts the counter's monotonicity into the type system: no decrement,
  // no reset, no stale CAS.  bump_by(drained_count) uses acq_rel — one
  // extra ARM dmb per drain batch (BATCH_SIZE = 4096) compared to the
  // pre-migration release-only fetch_add.  Cost amortized across the
  // batch is far below measurement noise.
  //
  // Synchronization: fg must see all prior bg writes (pending_region_,
  // active_region, region data) when flush()'s acquire load sees the
  // count — bump_by's acq_rel covers the release half of that pairing.
  //
  // Own cache line: fg reads via flush()/flush_complete(), bg writes
  // here.  Without alignas(64) the line would ping-pong on every drain.
  alignas(64) safety::AtomicMonotonic<uint64_t> total_processed{0};

  // Divergence signal: fg thread signals this when compiled replay
  // diverges.  bg thread checks at the start of each drain cycle and
  // on every op inside the drain loop; on observed signal, resets its
  // accumulated trace + detector so leftover signature ops from the
  // pre-divergence iteration don't poison the next region.
  //
  // OneShotFlag fuses the (relaxed load → acquire fence → body →
  // release clear) dance into a single check_and_run call so the
  // memory-ordering discipline is structural, not per-site convention.
  //
  // Own cache line: fg writes (request side), bg reads (at each drain
  // cycle).  Without alignas, a line containing reset_requested + the
  // adjacent bg-private fields (arena, uncompiled_regions, thread)
  // would ping-pong every time fg signals — bg would lose its cached
  // copies of the bg-only fields on the same line.
  alignas(64) crucible::safety::OneShotFlag reset_requested;

  static constexpr uint32_t BATCH_SIZE = 4096;

  struct BgTraceBatch {
    BackgroundThread* owner = nullptr;
    uint32_t count = 0;
    TraceRing::Entry entries[BATCH_SIZE]{};
    MetaIndex meta_starts[BATCH_SIZE]{};
    ScopeHash scope_hashes[BATCH_SIZE]{};
    CallsiteHash callsite_hashes[BATCH_SIZE]{};
  };

  struct BgBuildWork {
    BackgroundThread* owner = nullptr;
    bool commit_only = false;
    uint32_t commit_count = 0;
    uint32_t completed_len = 0;
    std::vector<TraceRing::Entry> trace;
    std::vector<MetaIndex> meta_starts;
    std::vector<ScopeHash> scope_hashes;
    std::vector<CallsiteHash> callsite_hashes;
  };

  struct BgRegionWork {
    BackgroundThread* owner = nullptr;
    bool commit_only = false;
    uint32_t commit_count = 0;
    TraceGraph* graph = nullptr;
  };

  struct BgPipelineDone {};
  struct BgPipelineStart {
    BackgroundThread* owner = nullptr;
  };
  struct BgPipelineStartTag {};
  struct BgTraceBatchTag {};
  struct BgBuildWorkTag {};
  struct BgRegionWorkTag {};

  using StartChannel =
      concurrent::PermissionedSpscChannel<BgPipelineStart, 1, BgPipelineStartTag>;
  using TraceBatchChannel =
      concurrent::PermissionedSpscChannel<BgTraceBatch*, 64, BgTraceBatchTag>;
  using BuildWorkChannel =
      concurrent::PermissionedSpscChannel<BgBuildWork*, 64, BgBuildWorkTag>;
  using RegionWorkChannel =
      concurrent::PermissionedSpscChannel<BgRegionWork*, 64, BgRegionWorkTag>;

  struct BgSinkProducerHandle {
    [[nodiscard]] bool try_push(BgPipelineDone* const& done) noexcept {
      delete done;
      return true;
    }
  };

  std::mutex bg_pipeline_arena_mutex_;

  template <class Producer, class T>
  static void push_pipeline(Producer& producer, T const& value) {
    while (!producer.try_push(value)) {
      CRUCIBLE_SPIN_PAUSE;
    }
  }

  [[nodiscard]] BgBuildWork* make_commit_work(uint32_t count) {
    auto* work = new BgBuildWork{};
    work->owner = this;
    work->commit_only = true;
    work->commit_count = count;
    return work;
  }

  [[nodiscard]] BgRegionWork* make_commit_region_work(uint32_t count) {
    auto* work = new BgRegionWork{};
    work->owner = this;
    work->commit_only = true;
    work->commit_count = count;
    return work;
  }

  [[nodiscard]] BgBuildWork* prepare_iteration_build_work() {
    uint32_t total = static_cast<uint32_t>(current_trace.size());
    const uint32_t iter_len = detector.last_completed_len;

    const uint32_t warmup = crucible::sat::sub_sat(
        crucible::sat::sub_sat(total, IterationDetector::K),
        iter_len);
    if (warmup > 0) [[unlikely]] {
      auto shift = [warmup](auto& vec) {
        const auto n = vec.size();
        if (warmup < n) {
          std::memmove(vec.data(), vec.data() + warmup,
                       (n - warmup) * sizeof(vec[0]));
          vec.resize(n - warmup);
        } else {
          vec.clear();
        }
      };
      shift(current_trace);
      shift(current_meta_starts);
      shift(current_scope_hashes);
      shift(current_callsite_hashes);
      total = crucible::sat::sub_sat(total, warmup);
    }

    const uint32_t completed_len =
        crucible::sat::sub_sat(total, IterationDetector::K);
    last_iteration_length = completed_len;
    iterations_completed.bump();

    BgBuildWork* work = nullptr;
    if (meta_log && completed_len > 0) {
      work = new BgBuildWork{};
      work->owner = this;
      work->completed_len = completed_len;
      work->trace.assign(current_trace.begin(),
                         current_trace.begin() + completed_len);
      work->meta_starts.assign(current_meta_starts.begin(),
                               current_meta_starts.begin() + completed_len);
      work->scope_hashes.assign(current_scope_hashes.begin(),
                                current_scope_hashes.begin() + completed_len);
      work->callsite_hashes.assign(current_callsite_hashes.begin(),
                                   current_callsite_hashes.begin() + completed_len);
    }

    auto retain_tail = [](auto& vec) {
      constexpr uint32_t K = IterationDetector::K;
      const auto n = vec.size();
      if (n > K) {
        std::memmove(vec.data(), vec.data() + n - K,
                     K * sizeof(vec[0]));
        vec.resize(K);
      }
    };
    retain_tail(current_trace);
    retain_tail(current_meta_starts);
    retain_tail(current_scope_hashes);
    retain_tail(current_callsite_hashes);

    return work;
  }

  void publish_trace_graph(effects::Alloc a, TraceGraph* graph)
      CRUCIBLE_NO_THREAD_SAFETY {
    if (!graph) return;

    iteration_graphs.append(graph);

    auto* region = make_region(
        a, arena, graph->ops, graph->num_ops, graph->content_hash);

    if (graph->slots && graph->num_slots > 0) {
      region->plan = compute_memory_plan(a, graph->slots, graph->num_slots);
    }

    if (graph->max_meta_end > 0) {
      meta_log.get().value()->advance_tail(graph->max_meta_end);
    }

    recompute_merkle(region);
    uncompiled_regions.append(region);

    active_region.store(region, std::memory_order_release);
    if (region_ready_cb) region_ready_cb(region);
  }

  static void DrainTraceRingFn(typename StartChannel::ConsumerHandle&& in,
                               typename TraceBatchChannel::ProducerHandle&& out) {
    BackgroundThread* owner = nullptr;
    while (!owner) {
      auto start = in.try_pop();
      if (!start) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }
      owner = start->owner;
    }

    while (true) {
      if (owner->stop_requested.peek()) break;

      auto batch = std::make_unique<BgTraceBatch>();
      batch->owner = owner;
      batch->count = owner->ring.get().value()->try_pop_batch(
          batch->entries, batch->meta_starts, batch->scope_hashes,
          batch->callsite_hashes, BATCH_SIZE);

      if (batch->count == 0) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }

      push_pipeline(out, batch.get());
      batch.release();
    }

    auto final_batch = std::make_unique<BgTraceBatch>();
    final_batch->owner = owner;
    final_batch->count = owner->ring.get().value()->try_pop_batch(
        final_batch->entries, final_batch->meta_starts,
        final_batch->scope_hashes, final_batch->callsite_hashes,
        BATCH_SIZE);
    if (final_batch->count > 0) {
      push_pipeline(out, final_batch.get());
      final_batch.release();
    }

    BgTraceBatch* stop = nullptr;
    push_pipeline(out, stop);
  }

  static void DetectIterationFn(typename TraceBatchChannel::ConsumerHandle&& in,
                                typename BuildWorkChannel::ProducerHandle&& out) {
    effects::Bg bg;

    while (true) {
      auto maybe_batch = in.try_pop();
      if (!maybe_batch) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }

      std::unique_ptr<BgTraceBatch> batch{*maybe_batch};
      if (!batch) {
        BgBuildWork* stop = nullptr;
        push_pipeline(out, stop);
        return;
      }

      BackgroundThread* owner = batch->owner;
      auto do_reset = [&]() noexcept {
        owner->detector.reset();
        owner->current_trace.clear();
        owner->current_meta_starts.clear();
        owner->current_scope_hashes.clear();
        owner->current_callsite_hashes.clear();
      };

      (void)owner->reset_requested.check_and_run(do_reset);

      for (uint32_t i = 0; i < batch->count; ++i) {
        (void)owner->reset_requested.check_and_run(do_reset);

        owner->current_trace.push_back(batch->entries[i]);
        owner->current_meta_starts.push_back(batch->meta_starts[i]);
        owner->current_scope_hashes.push_back(batch->scope_hashes[i]);
        owner->current_callsite_hashes.push_back(batch->callsite_hashes[i]);

        if (owner->detector.check(batch->entries[i].schema_hash)) {
          if (auto work = std::unique_ptr<BgBuildWork>(
                  owner->prepare_iteration_build_work())) {
            push_pipeline(out, work.get());
            work.release();
          }
        }
      }

      auto commit = std::unique_ptr<BgBuildWork>(
          owner->make_commit_work(batch->count));
      push_pipeline(out, commit.get());
      commit.release();
      (void)bg;
    }
  }

  static void BuildTraceFn(typename BuildWorkChannel::ConsumerHandle&& in,
                           typename RegionWorkChannel::ProducerHandle&& out) {
    effects::Bg bg;

    while (true) {
      auto maybe_work = in.try_pop();
      if (!maybe_work) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }

      std::unique_ptr<BgBuildWork> work{*maybe_work};
      if (!work) {
        BgRegionWork* stop = nullptr;
        push_pipeline(out, stop);
        return;
      }

      BackgroundThread* owner = work->owner;
      if (work->commit_only) {
        auto region_work = std::unique_ptr<BgRegionWork>(
            owner->make_commit_region_work(work->commit_count));
        push_pipeline(out, region_work.get());
        region_work.release();
        continue;
      }

      TraceGraph* graph = nullptr;
      {
        std::lock_guard lock(owner->bg_pipeline_arena_mutex_);
        graph = owner->build_trace_from(
            bg.alloc, work->completed_len,
            work->trace.data(), work->meta_starts.data(),
            work->scope_hashes.data(), work->callsite_hashes.data());
      }

      auto region_work = std::make_unique<BgRegionWork>();
      region_work->owner = owner;
      region_work->graph = graph;
      push_pipeline(out, region_work.get());
      region_work.release();
    }
  }

  static void MakeRegionFn(typename RegionWorkChannel::ConsumerHandle&& in,
                           BgSinkProducerHandle&& out) {
    effects::Bg bg;

    while (true) {
      auto maybe_work = in.try_pop();
      if (!maybe_work) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }

      std::unique_ptr<BgRegionWork> work{*maybe_work};
      if (!work) {
        auto done = std::make_unique<BgPipelineDone>();
        push_pipeline(out, done.get());
        done.release();
        return;
      }

      BackgroundThread* owner = work->owner;
      if (work->commit_only) {
        (void)owner->total_processed.bump_by(work->commit_count);
        continue;
      }

      {
        std::lock_guard lock(owner->bg_pipeline_arena_mutex_);
        owner->publish_trace_graph(bg.alloc, work->graph);
      }
    }
  }

  // Start the background thread. ring/meta_log must be set first.
  //
  // Seals the global registration tables as part of start(): after this
  // point, register_schema_name() / register_schema_hash() abort via the
  // mint_mutable_view() contract rather than racing with the lookups the
  // bg worker performs on this thread.  This matches the documented
  // lifecycle — all registrations complete before bg starts — and turns
  // it into a load-bearing rule.
  void start(TraceRing* ring_ptr, MetaLog* meta_log_ptr,
             int32_t rank_ = -1, int32_t world_size_ = 0,
             uint64_t device_cap = 0) CRUCIBLE_NO_THREAD_SAFETY {
    global_schema_table().seal();
    global_ckernel_table().seal();
    // Wrap raw pointers in NonNull at the set-once boundary.  Refined's
    // ctor contract fires if either is null — the only way into the
    // bg worker's ring.get().value() / meta_log.get().value() reads.
    ring.set(RingPtr{ring_ptr});
    meta_log.set(MetaLogPtr{meta_log_ptr});
    rank = rank_;
    world_size = world_size_;
    device_capability = device_cap;
    stop_requested.reset_unsafe();
    pipeline_thread = std::jthread([this](std::stop_token) noexcept {
      run_in_row<run_required_row>();
    });
  }

  // Signal the thread to stop and join.
  void stop() CRUCIBLE_NO_THREAD_SAFETY {
    stop_requested.signal();
    if (pipeline_thread.joinable()) {
      pipeline_thread.join();
    }
  }

  ~BackgroundThread() CRUCIBLE_NO_THREAD_SAFETY {
    stop();
    std::free(scratch_map_);
    std::free(scratch_slots_);
    std::free(scratch_edges_);
  }

  BackgroundThread() = default;
  BackgroundThread(const BackgroundThread&) = delete("BackgroundThread owns a pipeline jthread");
  BackgroundThread& operator=(const BackgroundThread&) = delete("BackgroundThread owns a pipeline jthread");
  BackgroundThread(BackgroundThread&&) = delete("BackgroundThread owns a pipeline jthread with captured this");
  BackgroundThread& operator=(BackgroundThread&&) = delete("BackgroundThread owns a pipeline jthread with captured this");

#ifdef CRUCIBLE_BENCH
 public:  // Bench needs access to scratch buffers for isolated sub-phase timing.
#else
 private:
#endif
  // ── Scratch buffer capacities ──────────────────────────────────────
  //
  // Dynamically sized to the workload. Grown (never shrunk) as needed.
  // PtrMap: gen-counter cleared (zero memset per call).
  // SlotInfo: memset only the used portion each call.
  // Edges: no init needed (written before read).

  static constexpr uint32_t MIN_PTR_MAP_CAP = 4096;    // minimum PtrMap (small models)
  static constexpr uint32_t MIN_SCRATCH_SLOT_CAP = 8192;
  static constexpr uint32_t MIN_SCRATCH_EDGE_CAP = 16384;
  static constexpr uint32_t MAX_PTR_MAP_CAP = uint32_t{1} << 30; // 1B entries; bit_ceil UB at 2^31

  // ── PtrMap: open-addressing hash map with generation counter ──────
  //
  // Maps data_ptr → (op_index, output_port, slot_id).
  // Generation counter eliminates memset: a slot is "empty" if
  // slot.gen != map_gen_. Bumping map_gen_ clears the entire table
  // in O(1). Every 255 calls, wrap-around triggers a real memset.

  struct PtrSlot {
    void* key = nullptr;      // 8B
    OpIndex op_index;          // 4B — default = none (UINT32_MAX)
    SlotId slot_id;            // 4B — default = none (UINT32_MAX)
    uint8_t port = 0;         // 1B
    uint8_t gen = 0;          // 1B — generation counter
    uint8_t pad[6]{};         // 6B — align to 24B
  };

  static_assert(sizeof(PtrSlot) == 24, "PtrSlot should be 24 bytes");

  // ── SlotInfo: compact per-slot metadata for liveness tracking ─────

  // Layout matches TensorSlot's bulk-copyable region (offset 8..31):
  //   nbytes | birth_op | death_op | dtype | device_type |
  //   device_idx | layout | is_external | pad
  // Enables single memcpy(24) in Phase 3 instead of 10 field copies.
  struct SlotInfo {
    uint64_t nbytes = 0;        // 8B — matches TensorSlot::nbytes
    OpIndex birth_op;            // 4B — matches TensorSlot::birth_op
    OpIndex death_op;            // 4B — matches TensorSlot::death_op
    ScalarType dtype = ScalarType::Undefined; // 1B
    DeviceType device_type = DeviceType::CPU; // 1B
    int8_t device_idx = -1;     // 1B
    Layout layout = Layout::Strided; // 1B
    bool is_external = false;   // 1B
    uint8_t pad[3]{};           // 3B
  };

  static_assert(sizeof(SlotInfo) == 24, "SlotInfo: matches TensorSlot bulk region");

  // ── PtrMap insert result ──────────────────────────────────────────

  struct InsertResult {
    PtrSlot* slot = nullptr;   // the inserted/matched slot (for patching)
    bool was_alias = false;    // true if key existed with different op
    OpIndex old_op;            // previous op (valid only if was_alias)
    uint8_t old_port = 0;     // previous port (valid only if was_alias)
    SlotId old_slot;           // previous slot_id (valid only if was_alias)
  };

  // ── Scratch buffers (dynamically sized, grown as needed) ──────────

  PtrSlot* scratch_map_ = nullptr;
  SlotInfo* scratch_slots_ = nullptr;
  Edge* scratch_edges_ = nullptr;
  uint8_t map_gen_ = 0;

  // Current capacities (0 = not yet allocated).  The three *_cap_max_
  // fields grow monotonically — comment in ensure_scratch_buffers says
  // "never shrinks".  Wrapped so that promise is type-enforced.
  // ptr_mask_ is a derived view of map_cap_ kept raw for the inner-loop
  // load on line 608.
  crucible::safety::Monotonic<uint32_t> map_cap_      {0};  // PtrMap capacity (power of 2)
  uint32_t                              ptr_mask_     = 0;  // map_cap_ - 1
  crucible::safety::Monotonic<uint32_t> slot_cap_max_ {0};  // SlotInfo buffer capacity
  crucible::safety::Monotonic<uint32_t> edge_cap_max_ {0};  // Edge buffer capacity

  // Ensure scratch buffers are large enough for the given workload.
  // Called after Phase 0 scan, which provides exact counts.
  // Grows buffers if needed (never shrinks — amortized over iterations).
  void ensure_scratch_buffers(uint32_t total_inputs, uint32_t total_outputs) {
    // PtrMap: power-of-two, load factor < 50%.
    // Unique ptrs ≈ total_outputs + external_inputs (small fraction).
    uint32_t raw_map_size = std::max(MIN_PTR_MAP_CAP,
        crucible::sat::mul_sat(crucible::sat::add_sat(total_outputs, uint32_t{256}), uint32_t{2}));
    uint32_t needed_map = (raw_map_size >= MAX_PTR_MAP_CAP) ?
        MAX_PTR_MAP_CAP : std::bit_ceil(raw_map_size);

    // Slots: unique storages bounded by outputs + external headroom.
    uint32_t needed_slots = std::max(MIN_SCRATCH_SLOT_CAP,
        crucible::sat::add_sat(total_outputs, uint32_t{1024}));

    // Edges: DATA_FLOW (≤ total_inputs) + ALIAS (≤ total_outputs).
    uint32_t needed_edges = std::max(MIN_SCRATCH_EDGE_CAP,
        crucible::sat::add_sat(total_inputs, total_outputs));

    if (needed_map > map_cap_.get()) {
      std::free(scratch_map_);
      map_cap_.advance(needed_map);
      ptr_mask_ = map_cap_.get() - 1;
      scratch_map_ = static_cast<PtrSlot*>(
          std::calloc(map_cap_.get(), sizeof(PtrSlot)));
      if (!scratch_map_) [[unlikely]] std::abort();
      map_gen_ = 0; // fresh buffer, reset gen
    }

    if (needed_slots > slot_cap_max_.get()) {
      std::free(scratch_slots_);
      slot_cap_max_.advance(needed_slots);
      scratch_slots_ = static_cast<SlotInfo*>(
          std::calloc(slot_cap_max_.get(), sizeof(SlotInfo)));
      if (!scratch_slots_) [[unlikely]] std::abort();
    }

    if (needed_edges > edge_cap_max_.get()) {
      std::free(scratch_edges_);
      edge_cap_max_.advance(needed_edges);
      scratch_edges_ = static_cast<Edge*>(
          std::calloc(edge_cap_max_.get(), sizeof(Edge)));
      if (!scratch_edges_) [[unlikely]] std::abort();
    }
  }

  // ── PtrMap operations ─────────────────────────────────────────────

  // gnu::const: depends only on the pointer value, no memory access.
  [[nodiscard, gnu::const]] static uint32_t hash_ptr(void* ptr) noexcept {
    return static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ptr) * 0x9E3779B97F4A7C15ULL >> 32);
  }

  [[nodiscard]] static InsertResult ptr_map_insert(
      PtrSlot* map, uint8_t gen, uint32_t mask,
      void* key, OpIndex op_index, uint8_t port, SlotId slot_id) {
    uint32_t bucket_idx = hash_ptr(key) & mask;
    for (uint32_t probe = 0; probe <= mask; probe++) {
      auto& slot = map[(bucket_idx + probe) & mask];
      if (slot.gen != gen) {
        // Stale generation → empty slot. Claim it.
        slot.key = key;
        slot.op_index = op_index;
        slot.port = port;
        slot.slot_id = slot_id;
        slot.gen = gen;
        return {.slot = &slot, .was_alias = false, .old_op = {}, .old_port = 0, .old_slot = {}};
      }
      if (slot.key == key) {
        // Existing entry. Alias if op differs.
        InsertResult result{.slot = &slot, .was_alias = (slot.op_index != op_index),
                            .old_op = slot.op_index, .old_port = slot.port, .old_slot = slot.slot_id};
        slot.op_index = op_index;
        slot.port = port;
        // Keep the same slot_id for aliases (shared storage)
        return result;
      }
    }
    return {.slot = nullptr, .was_alias = false, .old_op = {}, .old_port = 0, .old_slot = {}}; // table full
  }

  struct PtrLookup {
    OpIndex op_index;
    SlotId slot_id;
    uint8_t port = 0;
  };

  [[nodiscard]] static PtrLookup ptr_map_lookup(
      const PtrSlot* map, uint8_t gen, uint32_t mask, void* key) {
    if (!key) return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};
    uint32_t bucket_idx = hash_ptr(key) & mask;
    for (uint32_t probe = 0; probe <= mask; probe++) {
      auto& slot = map[(bucket_idx + probe) & mask];
      if (slot.gen == gen && slot.key == key)
        return {.op_index = slot.op_index, .slot_id = slot.slot_id, .port = slot.port};
      if (slot.gen != gen)
        return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0}; // empty → miss
    }
    return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};
  }

  // ── Main loop ──
 public:
  //
  // FOUND-I20: row-typed compile-time fence for the bg-thread main loop.
  //
  // The bg drain runs until stop_requested is signaled.  By construction
  // it performs:
  //   • Bg     — every line executes inside the bg-thread context tag.
  //   • Alloc  — `current_trace.push_back` grows; `on_iteration_boundary`
  //              calls `make_region` which arena-allocates RegionNode
  //              + per-op metadata.
  //   • IO     — `region_ready_cb` is fired with the freshly-built
  //              region; the callback is the bg's only externally
  //              observable side effect (audit log, KernelCache publish,
  //              federated-cache enqueue).  IO is also conservatively
  //              charged for the ring drain (MetaLog reads cross the
  //              SPSC fence).
  //   • Block  — the SPSC drain spin-pauses on empty rings, and the
  //              std::thread itself parks the OS scheduler.  Both are
  //              observable blocking effects.
  //
  // The required row is therefore `Row<Bg, Alloc, IO, Block>`.  Callers
  // (Vigil setup, test harnesses, future Keeper init) must declare a
  // CallerRow that is a SUPERSET — i.e. the bg admits at most these
  // four atoms; declaring fewer is a compile error.
  //
  // This is the structural inverse of FOUND-I16/I17/I19 (those use
  // `IsPure<CallerRow>` — caller declares AT MOST nothing).  Here the
  // direction is `Subrow<required, CallerRow>` — caller declares AT
  // LEAST {Bg, Alloc, IO, Block}.  Same algebraic machinery, opposite
  // polarity.  Mirrors Cipher::record_event's row fence (FOUND-I09).
  using run_required_row =
      ::crucible::effects::Row<
          ::crucible::effects::Effect::Bg,
          ::crucible::effects::Effect::Alloc,
          ::crucible::effects::Effect::IO,
          ::crucible::effects::Effect::Block>;

  // ── Required-row content fence (FOUND-I20-AUDIT, finding mirrored
  // from Cipher::record_event_required_row) ────────────────────────
  //
  // Pin the EXACT contents so a future refactor that "improves" the
  // typedef (drops Block, adds Init, swaps to a different alias)
  // fails loudly here rather than silently widening or narrowing
  // the fence.  Header-level: every TU that includes
  // BackgroundThread.h verifies the contract, NOT just the bg-row
  // test TU.
  static_assert(
      std::is_same_v<
          run_required_row,
          ::crucible::effects::Row<
              ::crucible::effects::Effect::Bg,
              ::crucible::effects::Effect::Alloc,
              ::crucible::effects::Effect::IO,
              ::crucible::effects::Effect::Block>>,
      "BackgroundThread::run_required_row MUST be exactly "
      "Row<Bg, Alloc, IO, Block>.  Adding/removing atoms is a "
      "deliberate API tightening — every callsite spawning a bg "
      "thread (Vigil setup, Keeper init, tests) must update its "
      "CallerRow declaration first.");
  static_assert(
      ::crucible::effects::row_size_v<run_required_row> == 4u,
      "run_required_row size MUST be exactly 4 atoms "
      "(Bg + Alloc + IO + Block).");
  static_assert(
      ::crucible::effects::row_contains_v<
          run_required_row,
          ::crucible::effects::Effect::Bg>,
      "run_required_row MUST contain Effect::Bg — the bg thread "
      "by definition runs in the Bg context.");
  static_assert(
      ::crucible::effects::row_contains_v<
          run_required_row,
          ::crucible::effects::Effect::Alloc>,
      "run_required_row MUST contain Effect::Alloc — region "
      "construction and current_trace growth are allocations.");
  static_assert(
      ::crucible::effects::row_contains_v<
          run_required_row,
          ::crucible::effects::Effect::IO>,
      "run_required_row MUST contain Effect::IO — region_ready_cb "
      "fires with the freshly-built region (audit log, federated "
      "cache enqueue).");
  static_assert(
      ::crucible::effects::row_contains_v<
          run_required_row,
          ::crucible::effects::Effect::Block>,
      "run_required_row MUST contain Effect::Block — the SPSC "
      "drain spin-pauses on empty rings and the OS scheduler "
      "parks the std::thread.");

  // Templated wrapper.  CallerRow is the row the caller declares it
  // holds; Subrow<required, CallerRow> checks
  // {Bg, Alloc, IO, Block} ⊆ CallerRow at substitution time.
  //
  // Single-line forwarder to run() — the entire fence lives in the
  // requires-clause.  Zero runtime cost; the bg loop is the same
  // instruction sequence either way.
  //
  // Use case: a callsite that explicitly demands the bg thread admits
  // at most a declared budget of effects can spawn the thread via
  //   bg.run_in_row<effects::Row<Bg, Alloc, IO, Block>>()
  // or any superset (e.g. AllRow).  Pure / Tot / Div / ST contexts
  // cannot satisfy the constraint and the template substitution fails
  // with the standard "constraints not satisfied" diagnostic.
  template <typename CallerRow>
      requires ::crucible::effects::Subrow<
          run_required_row, CallerRow>
  void run_in_row() noexcept CRUCIBLE_NO_THREAD_SAFETY {
      run();
  }

#ifdef CRUCIBLE_BENCH
 public:
#else
 private:
#endif
  void run() noexcept CRUCIBLE_NO_THREAD_SAFETY {
    using namespace crucible::concurrent;
    namespace saf = crucible::safety;

    TraceBatchChannel trace_batches;
    BuildWorkChannel build_work;
    RegionWorkChannel region_work;

    StartChannel start;
    auto start_whole = saf::mint_permission_root<
        spsc_tag::Whole<BgPipelineStartTag>>();
    auto [start_prod_perm, start_cons_perm] =
        saf::mint_permission_split<
            spsc_tag::Producer<BgPipelineStartTag>,
            spsc_tag::Consumer<BgPipelineStartTag>>(std::move(start_whole));

    auto trace_whole = saf::mint_permission_root<
        spsc_tag::Whole<BgTraceBatchTag>>();
    auto [trace_prod_perm, trace_cons_perm] =
        saf::mint_permission_split<
            spsc_tag::Producer<BgTraceBatchTag>,
            spsc_tag::Consumer<BgTraceBatchTag>>(std::move(trace_whole));

    auto build_whole = saf::mint_permission_root<
        spsc_tag::Whole<BgBuildWorkTag>>();
    auto [build_prod_perm, build_cons_perm] =
        saf::mint_permission_split<
            spsc_tag::Producer<BgBuildWorkTag>,
            spsc_tag::Consumer<BgBuildWorkTag>>(std::move(build_whole));

    auto region_whole = saf::mint_permission_root<
        spsc_tag::Whole<BgRegionWorkTag>>();
    auto [region_prod_perm, region_cons_perm] =
        saf::mint_permission_split<
            spsc_tag::Producer<BgRegionWorkTag>,
            spsc_tag::Consumer<BgRegionWorkTag>>(std::move(region_whole));

    auto start_prod = start.producer(std::move(start_prod_perm));
    auto start_cons = start.consumer(std::move(start_cons_perm));
    auto trace_prod = trace_batches.producer(std::move(trace_prod_perm));
    auto trace_cons = trace_batches.consumer(std::move(trace_cons_perm));
    auto build_prod = build_work.producer(std::move(build_prod_perm));
    auto build_cons = build_work.consumer(std::move(build_cons_perm));
    auto region_prod = region_work.producer(std::move(region_prod_perm));
    auto region_cons = region_work.consumer(std::move(region_cons_perm));

    auto ctx = effects::BgDrainCtx{}.template in_row<run_required_row>();
    while (!start_prod.try_push(BgPipelineStart{this})) {
      CRUCIBLE_SPIN_PAUSE;
    }
    auto drain_stage = concurrent::mint_stage<&DrainTraceRingFn>(
        ctx, std::move(start_cons), std::move(trace_prod));
    auto detect_stage = concurrent::mint_stage<&DetectIterationFn>(
        ctx, std::move(trace_cons), std::move(build_prod));
    auto build_stage = concurrent::mint_stage<&BuildTraceFn>(
        ctx, std::move(build_cons), std::move(region_prod));
    auto region_stage = concurrent::mint_stage<&MakeRegionFn>(
        ctx, std::move(region_cons), BgSinkProducerHandle{});

    auto pipeline = concurrent::mint_pipeline(
        ctx,
        std::move(drain_stage),
        std::move(detect_stage),
        std::move(build_stage),
        std::move(region_stage));
    std::move(pipeline).run();
  }

  void on_iteration_boundary(effects::Alloc a) CRUCIBLE_NO_THREAD_SAFETY {
    uint32_t total = static_cast<uint32_t>(current_trace.size());
    uint32_t iter_len = detector.last_completed_len;

    uint32_t warmup = crucible::sat::sub_sat(
        crucible::sat::sub_sat(total, IterationDetector::K),
        iter_len);
    if (warmup > 0) [[unlikely]] {
      // Shift data: memmove remaining to front (no alloc).
      auto shift = [warmup](auto& vec) {
        auto n = vec.size();
        if (warmup < n) {
          std::memmove(vec.data(), vec.data() + warmup,
                       (n - warmup) * sizeof(vec[0]));
          vec.resize(n - warmup);
        } else {
          vec.clear();
        }
      };
      shift(current_trace);
      shift(current_meta_starts);
      shift(current_scope_hashes);
      shift(current_callsite_hashes);
      total = crucible::sat::sub_sat(total, warmup);
    }

    uint32_t completed_len = crucible::sat::sub_sat(total, IterationDetector::K);
    last_iteration_length = completed_len;
    iterations_completed.bump();

    if (meta_log && completed_len > 0) {
      TraceGraph* graph = build_trace(a, completed_len);
      if (graph) {
        iteration_graphs.append(graph);

        // Use pre-computed content hash — no redundant second pass.
        auto* region = make_region(
            a, arena, graph->ops, graph->num_ops, graph->content_hash);

        if (graph->slots && graph->num_slots > 0) {
          region->plan = compute_memory_plan(
              a, graph->slots, graph->num_slots);
        }

        // Advance MetaLog tail AFTER all reads are done (zero-copy safety).
        if (graph->max_meta_end > 0)
          meta_log.get().value()->advance_tail(graph->max_meta_end);

        recompute_merkle(region);
        uncompiled_regions.append(region);

        active_region.store(region, std::memory_order_release);
        if (region_ready_cb) region_ready_cb(region);
      }
    }

    // Keep the K signature ops. Memmove to front + resize (no alloc).
    auto retain_tail = [](auto& vec) {
      constexpr uint32_t K = IterationDetector::K;
      auto n = vec.size();
      if (n > K) {
        std::memmove(vec.data(), vec.data() + n - K,
                     K * sizeof(vec[0]));
        vec.resize(K);
      }
    };
    retain_tail(current_trace);
    retain_tail(current_meta_starts);
    retain_tail(current_scope_hashes);
    retain_tail(current_callsite_hashes);
  }

 public:
  // Maximum tensor slots per iteration. Generous cap for large MoE models.
  static constexpr uint32_t MAX_SLOTS = 65536;

  // ── Build TraceGraph: ring entries + MetaLog → CSR property graph ──
  //
  // Squatted scratch buffers + gen-counter PtrMap + zero-copy MetaLog
  // + streaming content hash + zero per-op arena calls = minimum
  // possible instructions on the hot path.
  //
  // Returns nullptr on MetaLog overflow.
  //
  // Public: called by on_iteration_boundary() and benchmarks.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] TraceGraph* build_trace(effects::Alloc a, uint32_t count)
      CRUCIBLE_NO_THREAD_SAFETY {
    return build_trace_from(
        a, count,
        current_trace.data(), current_meta_starts.data(),
        current_scope_hashes.data(), current_callsite_hashes.data());
  }

  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] TraceGraph* build_trace_from(
      effects::Alloc a,
      uint32_t count,
      const TraceRing::Entry* trace_data,
      const MetaIndex* meta_data,
      const ScopeHash* scope_data,
      const CallsiteHash* callsite_data)
      CRUCIBLE_NO_THREAD_SAFETY {

    // ── Phase 0: Scan — compute totals, check MetaLog overflow ──

    uint32_t max_meta_end = 0;
    uint32_t first_meta = UINT32_MAX;
    uint32_t total_inputs = 0;
    uint32_t total_outputs = 0;
    uint32_t total_scalars = 0;
    for (uint32_t i = 0; i < count; i++) {
      MetaIndex ms = meta_data[i];
      const auto& re = trace_data[i];
      if (!ms.is_valid() && (re.num_inputs + re.num_outputs) > 0) {
        // Genuine MetaLog overflow on an op that had tensors.
        if (max_meta_end > 0)
          meta_log.get().value()->advance_tail(max_meta_end);
        return nullptr;
      }
      if (ms.is_valid()) {
        if (first_meta == UINT32_MAX) first_meta = ms.raw();
        uint32_t meta_end = ms.raw() + re.num_inputs + re.num_outputs;
        if (meta_end > max_meta_end) max_meta_end = meta_end;
      }
      total_inputs += re.num_inputs;
      total_outputs += re.num_outputs;
      total_scalars += std::min(re.num_scalar_args, uint16_t(5));
    }

    // ── Size scratch buffers to this workload (grow-only, no shrink) ──

    ensure_scratch_buffers(total_inputs, total_outputs);

    // ── Phase 1: Bulk allocations (2 arena allocs + zero-copy metas) ──

    // 1. TraceEntry array.
    auto* ops = arena.alloc_array<TraceEntry>(a, count);

    // 2. Zero-copy MetaLog: point directly into the circular buffer.
    //    No arena alloc, no memcpy — just pointer arithmetic per op.
    //    Pointers valid until meta_log.get().value()->advance_tail() is called
    //    (deferred to on_iteration_boundary after all processing).
    //    For the rare wrap case (< 0.01%), fall back to arena copy.
    TensorMeta* meta_base = nullptr;
    if (first_meta != UINT32_MAX) {
      uint32_t total_metas = max_meta_end - first_meta;
      meta_base = meta_log.get().value()->try_contiguous(first_meta, total_metas);
      if (!meta_base) [[unlikely]] {
        // Wrap case: copy to arena (extremely rare with 1M capacity).
        meta_base = arena.alloc_array<TensorMeta>(a, total_metas);
        for (uint32_t meta_idx = 0; meta_idx < total_metas; meta_idx++)
          meta_base[meta_idx] = meta_log.get().value()->at(first_meta + meta_idx);
      }
    }

    // 3. Bulk auxiliary block: scalars + trace indices + slot IDs.
    //    Layout (all naturally aligned):
    //      [int64_t scalars]  align 8
    //      [OpIndex indices]  align 4
    //      [SlotId in_slots]  align 4
    //      [SlotId out_slots] align 4
    size_t aux_bytes =
        static_cast<size_t>(total_scalars) * sizeof(int64_t) +
        static_cast<size_t>(total_inputs) * sizeof(OpIndex) +
        static_cast<size_t>(total_inputs) * sizeof(SlotId) +
        static_cast<size_t>(total_outputs) * sizeof(SlotId);
    char* aux_cursor = (aux_bytes > 0) ?
        static_cast<char*>(arena.alloc(a,
            crucible::safety::Positive<size_t>{aux_bytes},
            crucible::safety::PowerOfTwo<size_t>{alignof(int64_t)})) :
        nullptr;

    // ── PtrMap: bump generation (zero memset) ──

    map_gen_++;
    if (map_gen_ == 0) [[unlikely]] {
      // Wrap-around every 255 calls: full reset.
      std::fill_n(scratch_map_, map_cap_.get(), PtrSlot{});
      map_gen_ = 1;
    }

    // ── SlotInfo: value-init the portion we'll use ──

    uint32_t slot_cap = std::min(slot_cap_max_.get(),
        std::max(uint32_t{256}, total_inputs + total_outputs));
    std::fill_n(scratch_slots_, slot_cap, SlotInfo{});
    uint32_t next_slot_raw = 0;

    // ── Edge buffer: scratch, no init needed ──

    uint32_t num_edges = 0;

    // ── Hoist scratch buffer pointers into locals ──────────────────
    //
    // Same aliasing issue as the vector data pointers: the compiler
    // reloads scratch_map_/scratch_slots_/scratch_edges_/map_gen_
    // from this->member on every access because it can't prove arena
    // writes don't alias *this.  Local copies → register-resident.
    PtrSlot* const local_map = scratch_map_;
    SlotInfo* const local_slots = scratch_slots_;
    Edge* const local_edges = scratch_edges_;
    const uint8_t local_gen = map_gen_;
    const uint32_t local_mask = ptr_mask_;

    // ── Streaming content hash ──

    uint64_t content_h_local = 0x9E3779B97F4A7C15ULL;

    // ── Phase 2: Main loop — zero arena allocs, zero MetaLog copies ──

    for (uint32_t i = 0; i < count; i++) {
      const auto& re = trace_data[i];
      MetaIndex ms = meta_data[i];
      auto& te = ops[i];

      te.schema_hash = re.schema_hash;
      te.shape_hash = re.shape_hash;
      te.scope_hash = scope_data[i];
      te.callsite_hash = callsite_data[i];
      te.num_inputs = re.num_inputs;
      te.num_outputs = re.num_outputs;
      te.grad_enabled = re.grad_enabled();
      te.kernel_id = classify_kernel(re.schema_hash);

      // Unpack op_flags: 5 bits set by the Vessel fallback at dispatch time.
      // Ground truth from the dispatch site (schema, TLS, dispatch keys),
      // not heuristics. See TraceRing.h op_flag:: constants.
      const uint8_t flags = re.op_flags;
      te.inference_mode  = (flags & op_flag::INFERENCE_MODE) != 0;
      te.is_mutable      = (flags & op_flag::IS_MUTABLE) != 0;
      te.training_phase  = static_cast<TrainingPhase>(
          (flags & op_flag::PHASE_MASK) >> op_flag::PHASE_SHIFT);
      te.torch_function  = (flags & op_flag::TORCH_FUNCTION) != 0;
      // Structural invariant: TraceRing::Entry::scalar_values is a
      // fixed-5 array; num_scalar_args > 5 implies the foreground
      // recorder wrote past the array (UB) or the on-disk format
      // stored a corrupt count.  contract_assert fires at the bg
      // boundary so the first downstream memcpy doesn't inherit
      // garbage.  The std::min on the next line retains the defensive
      // clamp for release builds where contracts compile out.
      contract_assert(re.num_scalar_args <= 5);
      uint16_t n_scalars = std::min(re.num_scalar_args, uint16_t(5));
      te.num_scalar_args = n_scalars;

      if (ms.is_valid()) {
        uint16_t n_in = re.num_inputs;
        uint16_t n_out = re.num_outputs;

        // Meta pointers: pure arithmetic into bulk arena copy.
        uint32_t meta_offset = ms.raw() - first_meta;
        te.input_metas = meta_base + meta_offset;
        te.output_metas = meta_base + meta_offset + n_in;

        // Aux pointers: cursor advance into bulk block.
        te.scalar_args = (n_scalars > 0) ?
            reinterpret_cast<int64_t*>(aux_cursor) : nullptr;
        aux_cursor += n_scalars * sizeof(int64_t);
        te.input_trace_indices = reinterpret_cast<OpIndex*>(aux_cursor);
        aux_cursor += n_in * sizeof(OpIndex);
        te.input_slot_ids = reinterpret_cast<SlotId*>(aux_cursor);
        aux_cursor += n_in * sizeof(SlotId);
        te.output_slot_ids = reinterpret_cast<SlotId*>(aux_cursor);
        aux_cursor += n_out * sizeof(SlotId);

        if (n_scalars > 0)
          std::memcpy(te.scalar_args, re.scalar_values,
                      n_scalars * sizeof(int64_t));

        // ── Streaming content hash: XOR-fold dimensions ──
        //
        // Independent multiplies (sizes[d] * kDimMix[d]) break the serial
        // wymix chain. CPU pipelines all multiplies in parallel; XOR chain
        // at 1 cy/op is the critical path. One wymix per tensor merges.
        //
        // REFL-4: this is an inlined-fused copy of compute_content_hash
        // running directly on the build_trace path (avoiding a redundant
        // second pass over the ops).  Same intentional-manual rationale
        // as compute_content_hash itself: heavily perf-tuned XOR-fold
        // (40% faster than per-dim wymix), Family-A bit-stable, must
        // produce IDENTICAL bits to compute_content_hash so cache lookups
        // composed via either path agree.  reflect_hash on TraceEntry
        // would (a) break that bit identity and (b) lose the optimized
        // XOR-fold pattern.

        content_h_local = detail::wymix(content_h_local, te.schema_hash.raw());
        for (uint16_t j = 0; j < n_in; j++) {
          const TensorMeta& meta = te.input_metas[j];
          // SIMD dim-hash: bit-identical to the prior inline XOR-fold over
          // sizes[d]*kDimMix[d] + strides[d]*kDimMix[d+8].  Locked by
          // test_dim_hash_equivalence_handcoded in test_simd.cpp.
          const uint64_t dim_h_local = detail::dim_hash_simd(meta);
          uint64_t meta_packed =
              static_cast<uint64_t>(std::to_underlying(meta.dtype)) |
              (static_cast<uint64_t>(std::to_underlying(meta.device_type)) << 8) |
              (static_cast<uint64_t>(static_cast<uint8_t>(meta.device_idx)) << 16);
          content_h_local = detail::wymix(content_h_local ^ dim_h_local, meta_packed);
        }
        if (n_scalars > 0) {
          for (uint16_t scalar_idx = 0; scalar_idx < n_scalars; scalar_idx++) {
            content_h_local ^= static_cast<uint64_t>(te.scalar_args[scalar_idx]);
            content_h_local *= 0x100000001b3ULL;
          }
        }
      } else {
        te.input_metas = nullptr;
        te.output_metas = nullptr;
        te.scalar_args = nullptr;
        te.input_trace_indices = nullptr;
        te.input_slot_ids = nullptr;
        te.output_slot_ids = nullptr;
        te.num_inputs = 0;
        te.num_outputs = 0;

        // Still hash schema for no-tensor ops (profiler hooks).
        content_h_local = detail::wymix(content_h_local, te.schema_hash.raw());
      }

      // ── PtrMap prefetch: hide L3 latency for next op's probes ──
      //
      // Prefetch the PtrMap slot for op i+1's first input and first
      // output. ~200 cycles of current-op processing gives the
      // prefetch time to bring the cache lines from L3 into L2.

      if (i + 1 < count) {
        MetaIndex next_ms = meta_data[i + 1];
        if (next_ms.is_valid()) {
          uint32_t next_off = next_ms.raw() - first_meta;
          const auto& next_re = trace_data[i + 1];
          if (next_re.num_inputs > 0) {
            uint32_t bucket_idx = hash_ptr(meta_base[next_off].data_ptr) & local_mask;
            __builtin_prefetch(&local_map[bucket_idx], 0, 1);
          }
          if (next_re.num_outputs > 0) {
            uint32_t out_off = next_off + next_re.num_inputs;
            uint32_t bucket_idx = hash_ptr(meta_base[out_off].data_ptr) & local_mask;
            __builtin_prefetch(&local_map[bucket_idx], 1, 1);
          }
        }
      }

      // ── DFG edges + input slot tracking ──

      for (uint16_t j = 0; j < te.num_inputs; j++) {
        void* input_ptr = te.input_metas[j].data_ptr;
        auto lookup = ptr_map_lookup(local_map, local_gen, local_mask, input_ptr);
        te.input_trace_indices[j] = lookup.op_index;
        if (lookup.op_index.is_valid()) {
          te.input_slot_ids[j] = lookup.slot_id;
          assert(num_edges < edge_cap_max_.get());
          local_edges[num_edges++] = {
              .src = OpIndex{lookup.op_index.raw()}, .dst = OpIndex{i},
              .src_port = lookup.port, .dst_port = static_cast<uint8_t>(j),
              .kind = EdgeKind::DATA_FLOW, .pad = 0};
          if (lookup.slot_id.raw() < slot_cap) {
            auto& slot_info = local_slots[lookup.slot_id.raw()];
            slot_info.death_op = std::max(slot_info.death_op, OpIndex{i});
          }
        } else if (input_ptr != nullptr && next_slot_raw < slot_cap) {
          // External tensor (param, data loader output): first encounter.
          SlotId new_slot{next_slot_raw++};
          te.input_slot_ids[j] = new_slot;
          auto& slot_info = local_slots[new_slot.raw()];
          slot_info.birth_op = OpIndex{0};
          slot_info.death_op = OpIndex{i};
          slot_info.is_external = true;
          // compute_storage_nbytes returns Saturated<uint64_t>; .value()
          // strips the clamped flag.  Saturation propagates as
          // UINT64_MAX, which fails downstream pool allocation cleanly
          // — same behavior as the pre-#1018 bare-uint64_t API.
          slot_info.nbytes = compute_storage_nbytes(te.input_metas[j]).value();
          slot_info.dtype = te.input_metas[j].dtype;
          slot_info.device_type = te.input_metas[j].device_type;
          slot_info.device_idx = te.input_metas[j].device_idx;
          slot_info.layout = te.input_metas[j].layout;
          (void)ptr_map_insert(local_map, local_gen, local_mask,
              input_ptr, OpIndex{}, 0, new_slot);
        } else {
          te.input_slot_ids[j] = SlotId{};
        }
      }

      // ── Output slot tracking + alias detection ──

      for (uint16_t j = 0; j < te.num_outputs; j++) {
        void* output_ptr = te.output_metas[j].data_ptr;
        if (!output_ptr) {
          te.output_slot_ids[j] = SlotId{};
          continue;
        }

        auto result = ptr_map_insert(local_map, local_gen, local_mask,
            output_ptr, OpIndex{i}, static_cast<uint8_t>(j), SlotId{0});

        if (result.was_alias) {
          te.output_slot_ids[j] = result.old_slot;
          if (result.old_slot.raw() < slot_cap) {
            auto& slot_info = local_slots[result.old_slot.raw()];
            slot_info.death_op = std::max(slot_info.death_op, OpIndex{i});
            const uint64_t output_nbytes =
                compute_storage_nbytes(te.output_metas[j]).value();
            slot_info.nbytes = std::max(slot_info.nbytes, output_nbytes);
          }
          if (result.old_op.is_valid()) {
            assert(num_edges < edge_cap_max_.get());
            local_edges[num_edges++] = {
                .src = OpIndex{result.old_op.raw()}, .dst = OpIndex{i},
                .src_port = result.old_port, .dst_port = static_cast<uint8_t>(j),
                .kind = EdgeKind::ALIAS, .pad = 0};
          }
        } else if (next_slot_raw < slot_cap) {
          SlotId new_slot{next_slot_raw++};
          auto& slot_info = local_slots[new_slot.raw()];
          slot_info.birth_op = OpIndex{i};
          slot_info.death_op = OpIndex{i};
          slot_info.is_external = false;
          slot_info.nbytes = compute_storage_nbytes(te.output_metas[j]).value();
          slot_info.dtype = te.output_metas[j].dtype;
          slot_info.device_type = te.output_metas[j].device_type;
          slot_info.device_idx = te.output_metas[j].device_idx;
          slot_info.layout = te.output_metas[j].layout;
          te.output_slot_ids[j] = new_slot;
          // Patch the PtrMap slot's slot_id via returned pointer.
          if (result.slot) result.slot->slot_id = new_slot;
        } else {
          te.output_slot_ids[j] = SlotId{};
        }
      }
    }

    // ── Phase 3: Build outputs ──

    // Build arena-allocated TensorSlot array from scratch slots.
    uint32_t num_slots = std::min(next_slot_raw, slot_cap);
    TensorSlot* slots = nullptr;
    if (num_slots > 0) {
      slots = arena.alloc_array<TensorSlot>(a, num_slots);
      static_assert(sizeof(SlotInfo) == 24);
      static_assert(offsetof(TensorSlot, nbytes) == 8);
      static_assert(offsetof(SlotInfo, nbytes) == 0);
      for (uint32_t slot_idx = 0; slot_idx < num_slots; slot_idx++) {
        slots[slot_idx].offset_bytes = 0;  // assigned by compute_memory_plan()
        // Bulk-copy 24B: nbytes|birth|death|dtype|dev|idx|layout|ext|pad
        std::memcpy(&slots[slot_idx].nbytes, &local_slots[slot_idx], sizeof(SlotInfo));
        slots[slot_idx].slot_id = SlotId{slot_idx};
        // pad2 is zero from NSDMI + arena alloc_array returns unzeroed,
        // but TensorSlot has NSDMI pad2[4]{} so placement-new is fine.
        std::memset(slots[slot_idx].pad2, 0, sizeof(slots[slot_idx].pad2));
      }
    }

    // Build CSR property graph.
    auto* graph = arena.alloc_obj<TraceGraph>(a);
    graph->ops = ops;
    graph->num_ops = count;
    graph->slots = slots;
    graph->num_slots = num_slots;
    graph->content_hash = ContentHash{detail::fmix64(content_h_local)};
    graph->max_meta_end = max_meta_end;
    build_csr(a, arena, graph, local_edges, num_edges, count);

    return graph;
  }

 public:
  // ── Compute memory plan: counting sort + sweep-line offset ────────
  //
  // O(n + k) counting sort replaces O(n²) insertion sort for event
  // ordering. Sweep-line best-fit allocation is unchanged.
  // Alignment: 256 bytes (CUDA coalescing).
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] MemoryPlan* compute_memory_plan(
      effects::Alloc a, TensorSlot* slots, uint32_t num_slots)
      CRUCIBLE_NO_THREAD_SAFETY {
    static constexpr uint32_t ALIGNMENT = 256;

    auto* plan = arena.alloc_obj<MemoryPlan>(a);
    plan->slots = slots;
    plan->num_slots = num_slots;
    plan->num_external = 0;
    plan->pool_bytes = 0;

    plan->rank = rank;
    plan->world_size = world_size;
    plan->device_capability = device_capability;
    plan->device_type = DeviceType::CPU;
    plan->device_idx = -1;
    std::memset(plan->pad0, 0, sizeof(plan->pad0));

    if (num_slots == 0)
      return plan;

    // Count externals, find target device, and compute max op index.
    uint32_t max_op = 0;
    for (uint32_t s = 0; s < num_slots; s++) {
      if (slots[s].is_external) {
        plan->num_external++;
      } else {
        if (plan->device_type == DeviceType::CPU &&
            slots[s].device_type != DeviceType::CPU) {
          plan->device_type = slots[s].device_type;
          plan->device_idx = slots[s].device_idx;
        }
        if (slots[s].death_op.raw() + 1 > max_op)
          max_op = slots[s].death_op.raw() + 1;
      }
    }

    uint32_t num_internal = num_slots - plan->num_external;
    if (num_internal == 0)
      return plan;

    // ── Counting sort: bucket slots by birth_op and death_op+1 ──

    uint32_t num_ops = max_op + 1; // sweep range [0, max_op]
    auto* birth_count = arena.alloc_array<uint32_t>(a, num_ops);
    auto* death_count = arena.alloc_array<uint32_t>(a, num_ops);
    std::memset(birth_count, 0, num_ops * sizeof(uint32_t));
    std::memset(death_count, 0, num_ops * sizeof(uint32_t));

    for (uint32_t s = 0; s < num_slots; s++) {
      if (slots[s].is_external) continue;
      birth_count[slots[s].birth_op.raw()]++;
      uint32_t free_at = slots[s].death_op.raw() + 1;
      if (free_at < num_ops) death_count[free_at]++;
    }

    // Prefix sum → offsets.
    auto* birth_off = arena.alloc_array<uint32_t>(a, num_ops + 1);
    auto* death_off = arena.alloc_array<uint32_t>(a, num_ops + 1);
    birth_off[0] = 0;
    death_off[0] = 0;
    for (uint32_t o = 0; o < num_ops; o++) {
      birth_off[o + 1] = birth_off[o] + birth_count[o];
      death_off[o + 1] = death_off[o] + death_count[o];
    }

    // Scatter slot indices into sorted order.
    auto* born_slots = arena.alloc_array<uint32_t>(a, num_internal);
    auto* dead_slots = arena.alloc_array<uint32_t>(a, num_internal);
    // Cursor arrays: copy of offsets, incremented during scatter.
    auto* birth_cur = arena.alloc_array<uint32_t>(a, num_ops);
    auto* death_cur = arena.alloc_array<uint32_t>(a, num_ops);
    std::memcpy(birth_cur, birth_off, num_ops * sizeof(uint32_t));
    std::memcpy(death_cur, death_off, num_ops * sizeof(uint32_t));

    for (uint32_t s = 0; s < num_slots; s++) {
      if (slots[s].is_external) continue;
      born_slots[birth_cur[slots[s].birth_op.raw()]++] = s;
      uint32_t free_at = slots[s].death_op.raw() + 1;
      if (free_at < num_ops)
        dead_slots[death_cur[free_at]++] = s;
    }

    // ── Sweep-line: process ops in order ──
    //
    // Unsorted SoA free list: sizes[] and offsets[] as separate arrays
    // for cache-friendly first-fit scanning (8 sizes per cache line).
    // O(1) free (append), O(f) first-fit scan, O(1) swap-remove.
    // No merge, no memmove, no sorted insertion.
    //
    // Direct reuse at each op boundary captures most same-size matches,
    // keeping f small. First-fit scan handles the rest.

    constexpr uint32_t MAX_FREE = 4096;
    // SoA layout: sizes[] contiguous for fast scanning, offsets[]
    // only touched on hit. NOT zero-init: free_list_count bounds all access.
    uint64_t free_list_sizes[MAX_FREE];
    uint64_t free_list_offsets[MAX_FREE];
    uint32_t free_list_count = 0;
    uint64_t pool_end = 0;

    // Free: O(1) append.
    auto free_block = [&](uint64_t offset, uint64_t size) {
      if (free_list_count < MAX_FREE) [[likely]] {
        free_list_offsets[free_list_count] = offset;
        free_list_sizes[free_list_count] = size;
        ++free_list_count;
      }
    };

    // Alloc: first-fit scan over sizes[] + O(1) swap-remove.
    auto alloc_slot = [&](uint32_t s, uint64_t aligned_size) {
      for (uint32_t f = 0; f < free_list_count; f++) {
        if (free_list_sizes[f] >= aligned_size) {
          slots[s].offset_bytes = free_list_offsets[f];
          if (free_list_sizes[f] == aligned_size) {
            // Swap-remove: move last entry into this slot.
            free_list_sizes[f] = free_list_sizes[--free_list_count];
            free_list_offsets[f] = free_list_offsets[free_list_count];
          } else {
            free_list_offsets[f] += aligned_size;
            free_list_sizes[f] -= aligned_size;
          }
          return;
        }
      }
      slots[s].offset_bytes = pool_end;
      pool_end += aligned_size;
    };

    // ── Direct reuse matching (per-op, stack-allocated) ──

    struct DyingInfo {
      uint64_t aligned_size = 0;  // InitSafe
      uint64_t offset = 0;        // InitSafe
    };
    constexpr uint32_t MAX_PER_OP = 64;

    for (uint32_t op = 0; op < num_ops; op++) {
      uint32_t dying_begin = death_off[op], dying_end = death_off[op + 1];
      uint32_t born_begin = birth_off[op], born_end = birth_off[op + 1];
      uint32_t num_dying = dying_end - dying_begin;
      uint32_t num_born = born_end - born_begin;

      // ── Direct reuse: match dying→born to skip free list ──
      uint32_t dying_count_capped = num_dying < MAX_PER_OP ? num_dying : MAX_PER_OP;
      uint32_t born_count_capped = num_born < MAX_PER_OP ? num_born : MAX_PER_OP;
      bool dying_consumed[MAX_PER_OP]{};
      bool born_assigned[MAX_PER_OP]{};

      if (dying_count_capped > 0 && born_count_capped > 0) {
        DyingInfo dying_slot_info[MAX_PER_OP];
        for (uint32_t i = 0; i < dying_count_capped; i++) {
          uint32_t dying_slot_id = dead_slots[dying_begin + i];
          dying_slot_info[i] = {
            .aligned_size = (slots[dying_slot_id].nbytes + ALIGNMENT - 1) &
                            ~uint64_t(ALIGNMENT - 1),
            .offset = slots[dying_slot_id].offset_bytes
          };
        }

        for (uint32_t born_idx = 0; born_idx < born_count_capped; born_idx++) {
          uint32_t born_slot_id = born_slots[born_begin + born_idx];
          uint64_t born_aligned_size = (slots[born_slot_id].nbytes + ALIGNMENT - 1) &
                          ~uint64_t(ALIGNMENT - 1);

          uint32_t best_dying_match_idx = UINT32_MAX;
          uint64_t best_waste_bytes = UINT64_MAX;
          for (uint32_t dying_idx = 0; dying_idx < dying_count_capped; dying_idx++) {
            if (!dying_consumed[dying_idx] && dying_slot_info[dying_idx].aligned_size >= born_aligned_size) {
              uint64_t waste_bytes = dying_slot_info[dying_idx].aligned_size - born_aligned_size;
              if (waste_bytes < best_waste_bytes) { best_dying_match_idx = dying_idx; best_waste_bytes = waste_bytes; }
            }
          }
          if (best_dying_match_idx != UINT32_MAX) {
            slots[born_slot_id].offset_bytes = dying_slot_info[best_dying_match_idx].offset;
            dying_consumed[best_dying_match_idx] = true;
            born_assigned[born_idx] = true;
            if (best_waste_bytes > 0)
              free_block(dying_slot_info[best_dying_match_idx].offset + born_aligned_size, best_waste_bytes);
          }
        }
      }

      // ── Free unmatched dying slots ──
      for (uint32_t i = 0; i < num_dying; i++) {
        if (i < dying_count_capped && dying_consumed[i]) continue;
        uint32_t dying_slot_id = dead_slots[dying_begin + i];
        free_block(slots[dying_slot_id].offset_bytes,
                   (slots[dying_slot_id].nbytes + ALIGNMENT - 1) &
                       ~uint64_t(ALIGNMENT - 1));
      }

      // ── Alloc unmatched born slots ──
      for (uint32_t i = 0; i < num_born; i++) {
        if (i < born_count_capped && born_assigned[i]) continue;
        uint32_t born_slot_id = born_slots[born_begin + i];
        alloc_slot(born_slot_id, (slots[born_slot_id].nbytes + ALIGNMENT - 1) &
                           ~uint64_t(ALIGNMENT - 1));
      }
    }

    plan->pool_bytes = pool_end;
    return plan;
  }
};

} // namespace crucible
