#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/Saturate.h>
#include <crucible/SchemaTable.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/OneShotFlag.h>
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

  // Thread control — relaxed ordering.
  // std::thread ctor (in start()) provides happens-before for init data.
  // thread::join() (in stop()) provides happens-before for teardown.
  // The flag is just a loop-termination signal with no data dependencies;
  // worst case the bg thread spins one extra iteration before seeing false.
  std::atomic<bool> running{false};
  std::thread thread;

  // Total entries fully processed by the bg thread (monotonic).
  // Release on fetch_add: fg must see all prior bg writes (pending_region_,
  // active_region, region data) when flush()'s acquire load sees the count.
  // Not acq_rel: bg doesn't need to acquire anything from fg at this point.
  // Own cache line: fg reads, bg writes — false sharing otherwise.
  alignas(64) std::atomic<uint64_t> total_processed{0};

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

  // Start the background thread. ring/meta_log must be set first.
  //
  // Seals the global registration tables as part of start(): after this
  // point, register_schema_name() / register_schema_hash() abort via the
  // mint_mutable_view() contract rather than racing with the lookups the
  // bg worker performs on this thread.  This matches the documented
  // lifecycle — all registrations complete before bg starts — and turns
  // it into a load-bearing rule.
  void start(TraceRing* r, MetaLog* ml,
             int32_t rank_ = -1, int32_t world_size_ = 0,
             uint64_t device_cap = 0) CRUCIBLE_NO_THREAD_SAFETY {
    global_schema_table().seal();
    global_ckernel_table().seal();
    // Wrap raw pointers in NonNull at the set-once boundary.  Refined's
    // ctor contract fires if either is null — the only way into the
    // bg worker's ring.get().value() / meta_log.get().value() reads.
    ring.set(RingPtr{r});
    meta_log.set(MetaLogPtr{ml});
    rank = rank_;
    world_size = world_size_;
    device_capability = device_cap;
    running.store(true, std::memory_order_relaxed);
    thread = std::thread([this] { run(); });
  }

  // Signal the thread to stop and join.
  void stop() CRUCIBLE_NO_THREAD_SAFETY {
    running.store(false, std::memory_order_relaxed);
    if (thread.joinable()) {
      thread.join();
    }
  }

  ~BackgroundThread() CRUCIBLE_NO_THREAD_SAFETY {
    stop();
    std::free(scratch_map_);
    std::free(scratch_slots_);
    std::free(scratch_edges_);
  }

  BackgroundThread() = default;
  BackgroundThread(const BackgroundThread&) = delete("BackgroundThread owns a std::thread");
  BackgroundThread& operator=(const BackgroundThread&) = delete("BackgroundThread owns a std::thread");
  BackgroundThread(BackgroundThread&&) = delete("BackgroundThread owns a std::thread with captured this");
  BackgroundThread& operator=(BackgroundThread&&) = delete("BackgroundThread owns a std::thread with captured this");

#ifdef CRUCIBLE_BENCH
 public:  // Bench needs access to scratch buffers for isolated sub-phase timing.
#else
 private:
#endif
  static constexpr uint32_t BATCH_SIZE = 4096;

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
    uint32_t raw_map = std::max(MIN_PTR_MAP_CAP,
        crucible::sat::mul_sat(crucible::sat::add_sat(total_outputs, uint32_t{256}), uint32_t{2}));
    uint32_t needed_map = (raw_map >= MAX_PTR_MAP_CAP) ?
        MAX_PTR_MAP_CAP : std::bit_ceil(raw_map);

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
  [[nodiscard, gnu::const]] static uint32_t hash_ptr(void* p) noexcept {
    return static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(p) * 0x9E3779B97F4A7C15ULL >> 32);
  }

  [[nodiscard]] static InsertResult ptr_map_insert(
      PtrSlot* map, uint8_t gen, uint32_t mask,
      void* key, OpIndex op_index, uint8_t port, SlotId slot_id) {
    uint32_t idx = hash_ptr(key) & mask;
    for (uint32_t probe = 0; probe <= mask; probe++) {
      auto& s = map[(idx + probe) & mask];
      if (s.gen != gen) {
        // Stale generation → empty slot. Claim it.
        s.key = key;
        s.op_index = op_index;
        s.port = port;
        s.slot_id = slot_id;
        s.gen = gen;
        return {.slot = &s, .was_alias = false, .old_op = {}, .old_port = 0, .old_slot = {}};
      }
      if (s.key == key) {
        // Existing entry. Alias if op differs.
        InsertResult r{.slot = &s, .was_alias = (s.op_index != op_index),
                       .old_op = s.op_index, .old_port = s.port, .old_slot = s.slot_id};
        s.op_index = op_index;
        s.port = port;
        // Keep the same slot_id for aliases (shared storage)
        return r;
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
    uint32_t idx = hash_ptr(key) & mask;
    for (uint32_t probe = 0; probe <= mask; probe++) {
      auto& s = map[(idx + probe) & mask];
      if (s.gen == gen && s.key == key)
        return {.op_index = s.op_index, .slot_id = s.slot_id, .port = s.port};
      if (s.gen != gen)
        return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0}; // empty → miss
    }
    return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};
  }

  // ── Main loop ──

  void run() CRUCIBLE_NO_THREAD_SAFETY {
    fx::Bg bg;
    TraceRing::Entry batch[BATCH_SIZE];
    MetaIndex meta_batch[BATCH_SIZE];
    ScopeHash scope_batch[BATCH_SIZE];
    CallsiteHash callsite_batch[BATCH_SIZE];

    // Reset-on-divergence body — same code for the top-of-loop check
    // and the inner-loop check.  OneShotFlag::check_and_run fuses the
    // (relaxed test → acquire fence → body → release clear) dance.
    auto do_reset = [&]() noexcept {
        detector.reset();
        current_trace.clear();
        current_meta_starts.clear();
        current_scope_hashes.clear();
        current_callsite_hashes.clear();
    };

    while (running.load(std::memory_order_relaxed)) {
      // Check for divergence reset signal from fg thread.
      (void)reset_requested.check_and_run(do_reset);

      uint32_t n = ring.get().value()->drain(
          batch, BATCH_SIZE, meta_batch, scope_batch, callsite_batch);
      if (n == 0) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }

      for (uint32_t i = 0; i < n; i++) {
        // Check for divergence reset INSIDE the drain loop.
        //
        // Race without this: fg sets reset_requested AFTER the top-of-loop
        // check but BEFORE/DURING this drain batch. The bg thread then
        // processes new post-divergence entries with stale retained-tail
        // entries and an old detector state. The detector fires a false
        // boundary from the stale + new entries, building a region whose
        // ops are shifted from the true iteration start.
        //
        // Cost: one relaxed atomic load per entry (~1ns). Acceptable on
        // the bg thread (~50-100ns/entry). Acquire fence deferred to the
        // rare case where the flag is true — OneShotFlag handles that.
        (void)reset_requested.check_and_run(do_reset);

        current_trace.push_back(batch[i]);
        current_meta_starts.push_back(meta_batch[i]);
        current_scope_hashes.push_back(scope_batch[i]);
        current_callsite_hashes.push_back(callsite_batch[i]);

        if (detector.check(batch[i].schema_hash)) {
          on_iteration_boundary(bg.alloc);
        }
      }

      // Signal: all entries in this batch are fully processed, including
      // any on_iteration_boundary() calls (build_trace, make_region,
      // region_ready_cb). Release pairs with acquire in Vigil::flush().
      // Release: publishes all bg side effects (pending_region_,
      // active_region, region data) to fg thread's acquire in flush().
      // Not acq_rel: bg has no reads that depend on fg writes here.
      total_processed.fetch_add(n, std::memory_order_release);
    }

    // Drain remaining on shutdown.
    uint32_t n = ring.get().value()->drain(
        batch, BATCH_SIZE, meta_batch, scope_batch, callsite_batch);
    for (uint32_t i = 0; i < n; i++) {
      current_trace.push_back(batch[i]);
      current_meta_starts.push_back(meta_batch[i]);
      current_scope_hashes.push_back(scope_batch[i]);
      current_callsite_hashes.push_back(callsite_batch[i]);
    }
  }

  void on_iteration_boundary(fx::Alloc a) CRUCIBLE_NO_THREAD_SAFETY {
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
  [[nodiscard]] TraceGraph* build_trace(fx::Alloc a, uint32_t count)
      CRUCIBLE_NO_THREAD_SAFETY {
    // ── Hoist vector data pointers into locals ─────────────────────
    //
    // vector::operator[] reloads base+end from this->member on every
    // iteration because the compiler can't prove arena writes don't
    // alias vector storage.  Local pointers live on the stack — the
    // compiler keeps them in registers and eliminates bounds checks.
    // Intel PT showed this saves ~22% of build_trace cycles.
    const TraceRing::Entry* trace_data = current_trace.data();
    const MetaIndex* meta_data = current_meta_starts.data();
    const ScopeHash* scope_data = current_scope_hashes.data();
    const CallsiteHash* callsite_data = current_callsite_hashes.data();

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
        uint32_t end = ms.raw() + re.num_inputs + re.num_outputs;
        if (end > max_meta_end) max_meta_end = end;
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
        for (uint32_t m = 0; m < total_metas; m++)
          meta_base[m] = meta_log.get().value()->at(first_meta + m);
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

    uint64_t content_h = 0x9E3779B97F4A7C15ULL;

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

        content_h = detail::wymix(content_h, te.schema_hash.raw());
        for (uint16_t j = 0; j < n_in; j++) {
          const TensorMeta& m = te.input_metas[j];
          uint64_t dim_h = 0;
          for (uint8_t d = 0; d < m.ndim; d++) {
            dim_h ^= static_cast<uint64_t>(m.sizes[d]) * detail::kDimMix[d];
            dim_h ^= static_cast<uint64_t>(m.strides[d]) * detail::kDimMix[d + 8];
          }
          uint64_t meta_packed =
              static_cast<uint64_t>(std::to_underlying(m.dtype)) |
              (static_cast<uint64_t>(std::to_underlying(m.device_type)) << 8) |
              (static_cast<uint64_t>(static_cast<uint8_t>(m.device_idx)) << 16);
          content_h = detail::wymix(content_h ^ dim_h, meta_packed);
        }
        if (n_scalars > 0) {
          for (uint16_t s = 0; s < n_scalars; s++) {
            content_h ^= static_cast<uint64_t>(te.scalar_args[s]);
            content_h *= 0x100000001b3ULL;
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
        content_h = detail::wymix(content_h, te.schema_hash.raw());
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
            uint32_t idx = hash_ptr(meta_base[next_off].data_ptr) & local_mask;
            __builtin_prefetch(&local_map[idx], 0, 1);
          }
          if (next_re.num_outputs > 0) {
            uint32_t out_off = next_off + next_re.num_inputs;
            uint32_t idx = hash_ptr(meta_base[out_off].data_ptr) & local_mask;
            __builtin_prefetch(&local_map[idx], 1, 1);
          }
        }
      }

      // ── DFG edges + input slot tracking ──

      for (uint16_t j = 0; j < te.num_inputs; j++) {
        void* ptr = te.input_metas[j].data_ptr;
        auto lookup = ptr_map_lookup(local_map, local_gen, local_mask, ptr);
        te.input_trace_indices[j] = lookup.op_index;
        if (lookup.op_index.is_valid()) {
          te.input_slot_ids[j] = lookup.slot_id;
          assert(num_edges < edge_cap_max_.get());
          local_edges[num_edges++] = {
              .src = OpIndex{lookup.op_index.raw()}, .dst = OpIndex{i},
              .src_port = lookup.port, .dst_port = static_cast<uint8_t>(j),
              .kind = EdgeKind::DATA_FLOW, .pad = 0};
          if (lookup.slot_id.raw() < slot_cap) {
            auto& si = local_slots[lookup.slot_id.raw()];
            si.death_op = std::max(si.death_op, OpIndex{i});
          }
        } else if (ptr != nullptr && next_slot_raw < slot_cap) {
          // External tensor (param, data loader output): first encounter.
          SlotId sid{next_slot_raw++};
          te.input_slot_ids[j] = sid;
          auto& si = local_slots[sid.raw()];
          si.birth_op = OpIndex{0};
          si.death_op = OpIndex{i};
          si.is_external = true;
          si.nbytes = compute_storage_nbytes(te.input_metas[j]);
          si.dtype = te.input_metas[j].dtype;
          si.device_type = te.input_metas[j].device_type;
          si.device_idx = te.input_metas[j].device_idx;
          si.layout = te.input_metas[j].layout;
          (void)ptr_map_insert(local_map, local_gen, local_mask,
              ptr, OpIndex{}, 0, sid);
        } else {
          te.input_slot_ids[j] = SlotId{};
        }
      }

      // ── Output slot tracking + alias detection ──

      for (uint16_t j = 0; j < te.num_outputs; j++) {
        void* ptr = te.output_metas[j].data_ptr;
        if (!ptr) {
          te.output_slot_ids[j] = SlotId{};
          continue;
        }

        auto result = ptr_map_insert(local_map, local_gen, local_mask,
            ptr, OpIndex{i}, static_cast<uint8_t>(j), SlotId{0});

        if (result.was_alias) {
          te.output_slot_ids[j] = result.old_slot;
          if (result.old_slot.raw() < slot_cap) {
            auto& si = local_slots[result.old_slot.raw()];
            si.death_op = std::max(si.death_op, OpIndex{i});
            uint64_t nb = compute_storage_nbytes(te.output_metas[j]);
            si.nbytes = std::max(si.nbytes, nb);
          }
          if (result.old_op.is_valid()) {
            assert(num_edges < edge_cap_max_.get());
            local_edges[num_edges++] = {
                .src = OpIndex{result.old_op.raw()}, .dst = OpIndex{i},
                .src_port = result.old_port, .dst_port = static_cast<uint8_t>(j),
                .kind = EdgeKind::ALIAS, .pad = 0};
          }
        } else if (next_slot_raw < slot_cap) {
          SlotId sid{next_slot_raw++};
          auto& si = local_slots[sid.raw()];
          si.birth_op = OpIndex{i};
          si.death_op = OpIndex{i};
          si.is_external = false;
          si.nbytes = compute_storage_nbytes(te.output_metas[j]);
          si.dtype = te.output_metas[j].dtype;
          si.device_type = te.output_metas[j].device_type;
          si.device_idx = te.output_metas[j].device_idx;
          si.layout = te.output_metas[j].layout;
          te.output_slot_ids[j] = sid;
          // Patch the PtrMap slot's slot_id via returned pointer.
          if (result.slot) result.slot->slot_id = sid;
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
      for (uint32_t s = 0; s < num_slots; s++) {
        slots[s].offset_bytes = 0;  // assigned by compute_memory_plan()
        // Bulk-copy 24B: nbytes|birth|death|dtype|dev|idx|layout|ext|pad
        std::memcpy(&slots[s].nbytes, &local_slots[s], sizeof(SlotInfo));
        slots[s].slot_id = SlotId{s};
        // pad2 is zero from NSDMI + arena alloc_array returns unzeroed,
        // but TensorSlot has NSDMI pad2[4]{} so placement-new is fine.
        std::memset(slots[s].pad2, 0, sizeof(slots[s].pad2));
      }
    }

    // Build CSR property graph.
    auto* graph = arena.alloc_obj<TraceGraph>(a);
    graph->ops = ops;
    graph->num_ops = count;
    graph->slots = slots;
    graph->num_slots = num_slots;
    graph->content_hash = ContentHash{detail::fmix64(content_h)};
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
      fx::Alloc a, TensorSlot* slots, uint32_t num_slots)
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
    // only touched on hit. NOT zero-init: num_free bounds all access.
    uint64_t fl_sizes[MAX_FREE];
    uint64_t fl_offsets[MAX_FREE];
    uint32_t num_free = 0;
    uint64_t pool_end = 0;

    // Free: O(1) append.
    auto free_block = [&](uint64_t offset, uint64_t size) {
      if (num_free < MAX_FREE) [[likely]] {
        fl_offsets[num_free] = offset;
        fl_sizes[num_free] = size;
        ++num_free;
      }
    };

    // Alloc: first-fit scan over sizes[] + O(1) swap-remove.
    auto alloc_slot = [&](uint32_t s, uint64_t aligned_size) {
      for (uint32_t f = 0; f < num_free; f++) {
        if (fl_sizes[f] >= aligned_size) {
          slots[s].offset_bytes = fl_offsets[f];
          if (fl_sizes[f] == aligned_size) {
            // Swap-remove: move last entry into this slot.
            fl_sizes[f] = fl_sizes[--num_free];
            fl_offsets[f] = fl_offsets[num_free];
          } else {
            fl_offsets[f] += aligned_size;
            fl_sizes[f] -= aligned_size;
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
      uint32_t d_beg = death_off[op], d_end = death_off[op + 1];
      uint32_t b_beg = birth_off[op], b_end = birth_off[op + 1];
      uint32_t nd = d_end - d_beg;
      uint32_t nb = b_end - b_beg;

      // ── Direct reuse: match dying→born to skip free list ──
      uint32_t nd_cap = nd < MAX_PER_OP ? nd : MAX_PER_OP;
      uint32_t nb_cap = nb < MAX_PER_OP ? nb : MAX_PER_OP;
      bool d_used[MAX_PER_OP]{};
      bool b_used[MAX_PER_OP]{};

      if (nd_cap > 0 && nb_cap > 0) {
        DyingInfo d_info[MAX_PER_OP];
        for (uint32_t i = 0; i < nd_cap; i++) {
          uint32_t ds = dead_slots[d_beg + i];
          d_info[i] = {
            .aligned_size = (slots[ds].nbytes + ALIGNMENT - 1) &
                            ~uint64_t(ALIGNMENT - 1),
            .offset = slots[ds].offset_bytes
          };
        }

        for (uint32_t bi = 0; bi < nb_cap; bi++) {
          uint32_t bs = born_slots[b_beg + bi];
          uint64_t b_sz = (slots[bs].nbytes + ALIGNMENT - 1) &
                          ~uint64_t(ALIGNMENT - 1);

          uint32_t best_d = UINT32_MAX;
          uint64_t best_waste = UINT64_MAX;
          for (uint32_t di = 0; di < nd_cap; di++) {
            if (!d_used[di] && d_info[di].aligned_size >= b_sz) {
              uint64_t w = d_info[di].aligned_size - b_sz;
              if (w < best_waste) { best_d = di; best_waste = w; }
            }
          }
          if (best_d != UINT32_MAX) {
            slots[bs].offset_bytes = d_info[best_d].offset;
            d_used[best_d] = true;
            b_used[bi] = true;
            if (best_waste > 0)
              free_block(d_info[best_d].offset + b_sz, best_waste);
          }
        }
      }

      // ── Free unmatched dying slots ──
      for (uint32_t i = 0; i < nd; i++) {
        if (i < nd_cap && d_used[i]) continue;
        uint32_t ds = dead_slots[d_beg + i];
        free_block(slots[ds].offset_bytes,
                   (slots[ds].nbytes + ALIGNMENT - 1) &
                       ~uint64_t(ALIGNMENT - 1));
      }

      // ── Alloc unmatched born slots ──
      for (uint32_t i = 0; i < nb; i++) {
        if (i < nb_cap && b_used[i]) continue;
        uint32_t bs = born_slots[b_beg + i];
        alloc_slot(bs, (slots[bs].nbytes + ALIGNMENT - 1) &
                           ~uint64_t(ALIGNMENT - 1));
      }
    }

    plan->pool_bytes = pool_end;
    return plan;
  }
};

} // namespace crucible
