#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
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
#include <crucible/concurrent/SpinLock.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/permissions/Permission.h>
#include <crucible/fixy/Handle.h>     // FIXY-U-095: AlignedBuffer / OneShotFlag / PublishCommitCell
#include <crucible/fixy/Source.h>     // FIXY-U-095: tags::source::External
#include <crucible/fixy/Wrap.h>       // FIXY-U-095: NonNull / WriteOnce / Monotonic / AppendOnly / Tagged / Positive / PowerOfTwo
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
//   - PtrMap keys are typed `Tagged<void*, source::External>` (regime-1
//     EBO collapse to sizeof(void*)) — a foreign-provenance witness on
//     every observed tensor data_ptr that prevents the address from
//     being routed into internal-trust APIs without an explicit retag.
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
  using RingPtr    = crucible::fixy::wrap::NonNull<TraceRing*>;
  using MetaLogPtr = crucible::fixy::wrap::NonNull<MetaLog*>;
  crucible::fixy::wrap::WriteOnce<RingPtr>    ring;

  // The MetaLog to read tensor metadata from.  Not owned.  Same
  // installed-once + non-null discipline as ring.  `if (meta_log)`
  // continues to work via WriteOnce::operator bool (checks the
  // has-been-set state, not the pointer itself — the pointer is
  // non-null once set, by construction).
  crucible::fixy::wrap::WriteOnce<MetaLogPtr> meta_log;

  // Distributed context (set by Vessel adapter at start).
  int32_t rank = -1;
  int32_t world_size = 0;
  // WRAP-BgThread-4 #875 (Tagged half, 2026-05-24): device_capability is
  // a vendor-encoded hardware identity (NVIDIA SM version sm_50..sm_120,
  // AMD gfx target, Intel XMX tier, etc.) measured by the Meridian
  // startup calibration pass and propagated through the Vessel adapter
  // at init().  source::Meridian provenance encodes "this was measured
  // from real silicon, not synthesized or defaulted".  The default
  // value 0 marks the pre-init transient state (set() at init() time
  // overwrites with the calibrated value).  The "+ Refined" half of
  // WRAP-BgThread-4 (a value-range predicate) is deferred because the
  // canonical predicate would require knowing the vendor-specific
  // encoded-value range, which is opaque at this layer (the Vessel
  // adapter knows the encoding; BgThread does not).  A future task
  // tightens with vendor-routed source::VendorSpec × Refined pairs.
  using DeviceCapability = ::crucible::fixy::wrap::Tagged<
      uint64_t, ::crucible::fixy::tags::source::Meridian>;
  DeviceCapability device_capability{0};

  // Active region pointer (written by background, read by foreground).
  // NOT relaxed: store(release) publishes region data (ops, plan, merkle
  // hash) written before it. Fg's load(acquire) must see that data.
  // Relaxed = fg dereferences pointer to region with stale/garbage fields.
  //
  // Own cache line: fg reads, bg writes.  Co-locating with adjacent
  // bg-only fields below would have every bg write to those fields
  // invalidate the fg's cached copy of active_region.
  alignas(64) std::atomic<RegionNode*> active_region{nullptr};

  struct RegionReadyCallback {
    // FIXY-V-086: `noexcept` on the typedef is load-bearing.  The
    // callback fires on the BG thread inside drain_for_one_iteration_
    // → if it threw, the unwinder would tear through Cipher persistence
    // + tx_log commit + DeadlineWatchdog observe, leaving the runtime
    // in a half-applied state.  With -fno-exceptions globally, throws
    // cannot occur; the `noexcept` here pins the type-system invariant
    // so a future refactor that drops -fno-exceptions or that wires in
    // a throwing callable reddens at the callback assignment site.
    using Fn = void (*)(void*, RegionNode*) noexcept;

    void* ctx = nullptr;
    Fn fn = nullptr;

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
      return fn != nullptr;
    }

    void operator()(RegionNode* region) const noexcept {
      fn(ctx, region);
    }
  };

  // Optional callback invoked on the background thread whenever a new
  // RegionNode becomes available. Used by Vigil to update transactions
  // and trigger persistence without polling.
  //
  // Own cache line: bg-only state.  Separating it from active_region
  // (fg-touched) prevents fg's acquire load from dragging the callback
  // object into fg's L1 every iteration.
  alignas(64) RegionReadyCallback region_ready_cb;

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
  alignas(64) crucible::fixy::wrap::Monotonic<uint32_t> iterations_completed {0};
  uint32_t last_iteration_length = 0;

  // Arena for DAG allocations (TraceEntry, TensorMeta arrays,
  // edges, CSR structures, RegionNodes).
  //
  // BuildTraceFn and MakeRegionFn are separate pipeline threads.  Arena
  // storage is append-only, so already-returned graph pointers remain
  // stable, but the bump cursor itself is mutable.  This spin-only gate
  // serializes owner->arena allocation windows without parking in the
  // kernel or weakening the permissioned SPSC stage topology.
  alignas(64) concurrent::SpinLock arena_alloc_gate_;
  Arena arena{1 << 20}; // 1MB blocks

  // Regions created but not yet compiled.  Pure push-only — drained
  // by the foreground thread once a region is associated with a
  // compiled plan.  AppendOnly enforces the "no in-place removal"
  // invariant in the type.
  crucible::fixy::wrap::AppendOnly<RegionNode*> uncompiled_regions;

  // Per-iteration property graphs (for future fusion/scheduling).
  // Same append-only lifecycle.
  crucible::fixy::wrap::AppendOnly<TraceGraph*> iteration_graphs;

  // Thread control.  stop_requested is a one-way signal for one run()
  // invocation; start() / tests re-arm it under quiescence with
  // reset_in_quiescent_context(QuiescenceProof{}) before launching the
  // next pipeline (fixy-A1-032 — passkey-gated rename of reset_unsafe).
  // The signal has no data dependency beyond termination, but using
  // OneShotFlag keeps the release/acquire discipline structural instead
  // of hand-rolled atomic<bool> loads and stores.
  alignas(64) crucible::fixy::handle::OneShotFlag stop_requested;
  std::jthread pipeline_thread;

  // Total entries fully processed by the bg thread.  AtomicMonotonic-
  // shaped acquire-load API; the WRITE surface is friend-gated to a
  // single nested authority type (BackgroundThread::PublishStageAuth)
  // so only MakeRegionFn — the publishing stage — can advance the
  // counter.
  //
  // GAPS-FLUSH-RACE history: an earlier shape allowed BuildTraceFn
  // (stage 3) to bump this counter for commit-only markers, racing
  // with the publish callback in MakeRegionFn (stage 4).  The fix
  // forwards commit markers downstream so bumping happens in
  // MakeRegionFn; the type-level harness here pins the discipline
  // structurally — a future refactor that tries to bump from any
  // other stage gets a "private member" diagnostic naming the missing
  // friend declaration.
  //
  // Synchronization: fg must see all prior bg writes (pending_region_,
  // active_region, region data) when flush()'s acquire load sees the
  // count — bump_by's acq_rel covers the release half of that pairing.
  //
  // Own cache line: fg reads via flush()/flush_complete(), bg writes
  // here.  Without alignas(64) the line would ping-pong on every drain.
  // The cell type itself carries alignas(64) on its atomic.
  struct PublishStageAuth;  // friend-only authority; defined below.
  struct PublishStageTag {};
  using TotalProcessedCell =
      fixy::handle::PublishCommitCell<PublishStageTag, PublishStageAuth>;
  TotalProcessedCell total_processed;

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
  alignas(64) crucible::fixy::handle::OneShotFlag reset_requested;

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

  struct BgPipelineDone {};
  struct BgPipelineStart {
    BackgroundThread* owner = nullptr;
  };

  // GAPS-099: 4-stage pipeline message — handed from BuildTraceFn to
  // MakeRegionFn.  Carries the freshly-built TraceGraph* plus the owner
  // pointer (same pattern as BgTraceBatch / BgBuildWork) so the
  // downstream stage can call back into the per-Vigil publish path
  // without depending on a captured BackgroundThread*.
  //
  // Lifetime: owned by the producer (BuildTraceFn) until pushed; from
  // there owned by the consumer (MakeRegionFn) which deletes via
  // unique_ptr after publish_trace_graph() consumes the graph.  The
  // graph itself is arena-allocated by build_trace_from() and outlives
  // the BgGraphPublish wrapper (its lifetime is tied to the bg arena).
  //
  // Two shapes flow through this channel:
  //   (a) graph != nullptr, commit_only == false — a real iteration
  //       region to publish via on_region_ready.
  //   (b) graph == nullptr, commit_only == true — a commit marker
  //       carrying the entry count for total_processed bookkeeping.
  //
  // The commit marker MUST flow downstream through MakeRegionFn so that
  // total_processed only advances after every region produced UPSTREAM
  // of it has been published (publish_trace_graph + on_region_ready
  // fully executed).  Bumping total_processed inline at stage 3 races
  // with stage 4: flush() can return while a region is still queued in
  // graph_publish, leaving callers to observe (mode_=COMPILED ∧
  // pending_region_=null) or worse (active_region pointing at a region
  // whose plan hasn't finished computing).  Forwarding commit markers
  // through stage 4 closes that gap by construction.
  struct BgGraphPublish {
    BackgroundThread* owner = nullptr;
    TraceGraph* graph = nullptr;
    bool commit_only = false;
    uint32_t commit_count = 0;
  };

  struct BgPipelineStartTag {};
  struct BgTraceBatchTag {};
  struct BgBuildWorkTag {};
  struct BgGraphPublishTag {};

  using StartChannel =
      concurrent::PermissionedSpscChannel<BgPipelineStart, 1, BgPipelineStartTag>;
  using TraceBatchChannel =
      concurrent::PermissionedSpscChannel<BgTraceBatch*, 64, BgTraceBatchTag>;
  using BuildWorkChannel =
      concurrent::PermissionedSpscChannel<BgBuildWork*, 64, BgBuildWorkTag>;
  // GAPS-099: stage-3→stage-4 channel.  Build-trace stage produces fresh
  // TraceGraph*, MakeRegion stage consumes and publishes them.  Capacity 64
  // matches the upstream channels — backpressure happens at the build step
  // when the publisher falls behind.
  using GraphPublishChannel =
      concurrent::PermissionedSpscChannel<BgGraphPublish*, 64, BgGraphPublishTag>;

  struct BgSinkProducerHandle {
    [[nodiscard]] bool try_push(BgPipelineDone* const& done) noexcept {
      delete done;
      return true;
    }
  };

  template <class Producer, class T>
  static void push_pipeline(Producer& producer, T const& value) {
    while (!producer.try_push(value)) {
      CRUCIBLE_SPIN_PAUSE;
    }
  }

  void reserve_iteration_buffers_() {
    constexpr uint32_t kInitialTraceCapacity = BATCH_SIZE * 2;
    current_trace.reserve(kInitialTraceCapacity);
    current_meta_starts.reserve(kInitialTraceCapacity);
    current_scope_hashes.reserve(kInitialTraceCapacity);
    current_callsite_hashes.reserve(kInitialTraceCapacity);
  }

  [[nodiscard]] BgBuildWork* make_commit_work(uint32_t count) {
    auto* work = new BgBuildWork{};
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

    const uint32_t num_ops = graph->num_ops.get_assuming_set();
    const uint32_t num_slots = graph->num_slots.get_assuming_set();
    const uint32_t max_meta_end = graph->max_meta_end.get_assuming_set();

    auto* region = make_region(
        a, arena, graph->ops, num_ops, graph->content_hash);

    if (graph->slots && num_slots > 0) {
      region->plan = compute_memory_plan(a, graph->slots, num_slots);
    }

    if (max_meta_end > 0) {
      meta_log.get().value()->advance_tail(max_meta_end);
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

    auto batch = std::make_unique<BgTraceBatch>();
    batch->owner = owner;

    while (true) {
      if (owner->stop_requested.peek()) break;

      batch->count = owner->ring.get().value()->try_pop_batch(
          batch->entries, batch->meta_starts, batch->scope_hashes,
          batch->callsite_hashes, BATCH_SIZE);

      if (batch->count == 0) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }

      push_pipeline(out, batch.get());
      batch.release();
      batch = std::make_unique<BgTraceBatch>();
      batch->owner = owner;
    }

    batch->count = owner->ring.get().value()->try_pop_batch(
        batch->entries, batch->meta_starts,
        batch->scope_hashes, batch->callsite_hashes,
        BATCH_SIZE);
    if (batch->count > 0) {
      push_pipeline(out, batch.get());
      batch.release();
    }

    BgTraceBatch* stop = nullptr;
    push_pipeline(out, stop);
  }

  static void DetectIterationFn(typename TraceBatchChannel::ConsumerHandle&& in,
                                typename BuildWorkChannel::ProducerHandle&& out) {
    // fixy-A3-005: Bg ctor is private; BackgroundThread is friended on
    // bg_key so we mint via the passkey path.
    [[maybe_unused]] auto bg =
        effects::mint_bg_context(effects::detail::ctx_mint::bg_key{});

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

  // GAPS-099 Phase 1 — stage 3 of 4.  Consumes BgBuildWork (commit-only or
  // real iteration work).  Commit-only work bumps total_processed inline
  // and never produces downstream output (commit messages don't advance
  // the publish stream).  Real iteration work calls build_trace_from and
  // hands the resulting TraceGraph* to the next stage via BgGraphPublish.
  // Stop sentinel (null BgBuildWork*) is forwarded as null BgGraphPublish*.
  static void BuildTraceFn(typename BuildWorkChannel::ConsumerHandle&& in,
                           typename GraphPublishChannel::ProducerHandle&& out) {
    // fixy-A3-005: Bg ctor private; mint via passkey.
    [[maybe_unused]] auto bg =
        effects::mint_bg_context(effects::detail::ctx_mint::bg_key{});

    while (true) {
      auto maybe_work = in.try_pop();
      if (!maybe_work) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }

      std::unique_ptr<BgBuildWork> work{*maybe_work};
      if (!work) {
        BgGraphPublish* stop = nullptr;
        push_pipeline(out, stop);
        return;
      }

      BackgroundThread* owner = work->owner;
      if (work->commit_only) {
        // Forward commit marker to stage 4 so total_processed only
        // advances AFTER every preceding graph has been published.
        // Bumping inline here would race with publish_trace_graph: the
        // foreground thread can observe total_processed catching up,
        // call dispatch_op, and find pending_region_ empty (or
        // active_region pointing at a half-finished region).  Two-test
        // failure modes traceable to this race:
        //   * test_vigil_dispatch align_and_activate(17) — mode_ flips
        //     COMPILED via on_region_ready, but a NEW region published
        //     mid-alignment resets alignment_pos_ to 0; with the inline
        //     bump, flush() can return before any pending_region_ is
        //     visible.
        //   * test_end_to_end test_pipeline_basic — wait_processed
        //     returns, then active_region.load() is null.
        // The single-region test scenarios pass without this fix because
        // their N-op feed has only one boundary; the multi-region cases
        // fail every time under load.
        auto publish = std::make_unique<BgGraphPublish>();
        publish->owner = owner;
        publish->graph = nullptr;
        publish->commit_only = true;
        publish->commit_count = work->commit_count;
        push_pipeline(out, publish.get());
        publish.release();
        continue;
      }

      TraceGraph* graph = nullptr;
      {
        concurrent::SpinGuard guard{owner->arena_alloc_gate_};
        graph = owner->build_trace_from(
            bg.alloc, work->completed_len,
            work->trace.data(), work->meta_starts.data(),
            work->scope_hashes.data(), work->callsite_hashes.data());
      }

      // build_trace_from returns nullptr on edge cases (zero-op iteration);
      // forward only well-formed graphs downstream so MakeRegion's
      // contract stays "graph != nullptr ⇒ publishable".
      if (!graph) continue;

      auto publish = std::make_unique<BgGraphPublish>();
      publish->owner = owner;
      publish->graph = graph;
      publish->commit_only = false;
      publish->commit_count = 0;
      push_pipeline(out, publish.get());
      publish.release();
    }
  }

  // GAPS-099 Phase 1 — stage 4 of 4.  Consumes BgGraphPublish*, calls
  // publish_trace_graph (iteration_graphs.append, make_region, memory plan,
  // merkle hash, region_ready_cb).  Forwards stop sentinel (null
  // BgGraphPublish*) as BgPipelineDone to the sink.  Owning this stage
  // separately is the structural prerequisite for FOUND-F03's
  // RecordingSessionHandle wrapping: the publish-side delegate doesn't see
  // the build-side machinery, so a crash-stop on publish (e.g. cipher
  // freeze, callback throw) doesn't taint build's owner pointer.
  //
  // GAPS-FLUSH-RACE (this fix): commit_only markers ride the same
  // channel and bump total_processed AFTER any region(s) ahead of them
  // in the stream have been fully published.  The order guarantee is
  // structural: the channel is SPSC FIFO, so a commit_count for a
  // batch can only land here after every region produced from that
  // batch (and from preceding batches) has gone through
  // publish_trace_graph.  flush() returning therefore implies "all
  // regions published, mode_ = COMPILED, pending_region_ holds the
  // latest one" — the precondition every test depended on but the
  // pre-fix pipeline could violate by 5–500 µs under load.
  static void MakeRegionFn(typename GraphPublishChannel::ConsumerHandle&& in,
                           BgSinkProducerHandle&& out) {
    // fixy-A3-005: Bg ctor private; mint via passkey.
    [[maybe_unused]] auto bg =
        effects::mint_bg_context(effects::detail::ctx_mint::bg_key{});

    while (true) {
      auto maybe_publish = in.try_pop();
      if (!maybe_publish) {
        CRUCIBLE_SPIN_PAUSE;
        continue;
      }

      std::unique_ptr<BgGraphPublish> publish{*maybe_publish};
      if (!publish) {
        auto done = std::make_unique<BgPipelineDone>();
        push_pipeline(out, done.get());
        done.release();
        return;
      }

      BackgroundThread* owner = publish->owner;
      if (publish->commit_only) {
        // FIFO ordering on graph_publish: the commit marker sits
        // strictly AFTER every BgGraphPublish from the same and earlier
        // batches.  Bumping here therefore happens-after every
        // publish_trace_graph above; the release on total_processed
        // pairs with flush()'s acquire load on the foreground.
        //
        // Bump goes through PublishStageAuth::commit, the only legal
        // bumper of total_processed (friend-gated by the cell type).
        // A future regression that tries to bump from BuildTraceFn or
        // any other stage will get a "private member" diagnostic
        // pointing at this site — the structural fence against the
        // GAPS-FLUSH-RACE bug class.
        (void)PublishStageAuth::commit(owner->total_processed,
                                        publish->commit_count);
        continue;
      }
      {
        concurrent::SpinGuard guard{owner->arena_alloc_gate_};
        owner->publish_trace_graph(bg.alloc, publish->graph);
      }
    }
  }

  // ─── PublishStageAuth — sole authority for total_processed writes ─
  //
  // Befriended by TotalProcessedCell.  The static commit method is
  // the ONLY legal site that can call bump_by on total_processed.
  // MakeRegionFn invokes PublishStageAuth::commit; any other call
  // site fails to compile (private bump_by inaccessible).
  //
  // Definition LATE in the class body so the friend declaration in
  // total_processed sees the forward-declared name above.  The
  // method is a thin friend-side forwarder; the call elides
  // completely under -O3.
  struct PublishStageAuth {
    [[nodiscard]] static uint64_t commit(TotalProcessedCell& cell,
                                          uint64_t delta) noexcept {
      return cell.bump_by(delta);
    }
  };

  // Start the background thread. ring/meta_log must be set first.
  //
  // Seals the global registration tables as part of start(): after this
  // point, schema / kernel registrations require already-minted mutable
  // views, so late mutation is rejected by the typed-table contracts
  // rather than racing with the lookups the bg worker performs on this
  // thread. This matches the documented lifecycle — all registrations
  // complete before bg starts — and turns it into a load-bearing rule.
  void start(TraceRing* ring_ptr, MetaLog* meta_log_ptr,
             int32_t rank_ = -1, int32_t world_size_ = 0,
             uint64_t device_cap = 0) CRUCIBLE_NO_THREAD_SAFETY {
    global_schema_table().seal();
    global_ckernel_table().value()->seal();
    // Wrap raw pointers in NonNull at the set-once boundary.  Refined's
    // ctor contract fires if either is null — the only way into the
    // bg worker's ring.get().value() / meta_log.get().value() reads.
    ring.set(RingPtr{ring_ptr});
    meta_log.set(MetaLogPtr{meta_log_ptr});
    rank = rank_;
    world_size = world_size_;
    // WRAP-BgThread-4 #875: typed construction at the Vessel-adapter
    // boundary; raw `device_capability = device_cap;` is rejected by
    // the explicit ctor (see neg fixture neg_device_capability_raw_assignment.cpp).
    device_capability = DeviceCapability{device_cap};
    reserve_iteration_buffers_();
    stop_requested.reset_in_quiescent_context(
        ::crucible::fixy::handle::OneShotFlag::QuiescenceProof{});
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
    // #876 WRAP-BgThread-5: scratch buffers are AlignedBuffer-owned;
    // their dtors run on member destruction.  No explicit free needed.
  }

  BackgroundThread() = default;
  BackgroundThread(const BackgroundThread&) = delete("BackgroundThread owns a pipeline jthread");
  BackgroundThread& operator=(const BackgroundThread&) = delete("BackgroundThread owns a pipeline jthread");
  BackgroundThread(BackgroundThread&&) = delete("BackgroundThread owns a pipeline jthread with captured this");
  BackgroundThread& operator=(BackgroundThread&&) = delete("BackgroundThread owns a pipeline jthread with captured this");

  void set_region_ready_callback(void* ctx, RegionReadyCallback::Fn fn) noexcept {
    region_ready_cb = RegionReadyCallback{.ctx = ctx, .fn = fn};
  }

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
  //
  // GAPS-097: the key is a Tagged<void*, source::External> — every
  // PtrMap key originates as a tensor data_ptr observed by the fg
  // thread inside a TraceEntry, i.e. a foreign address whose lifetime
  // and aliasing semantics are owned by PyTorch.  The bg thread treats
  // it as an opaque cookie (hash + equality only); the External tag
  // forbids it being passed to internal-trust APIs (PoolAllocator
  // slot tables, KernelCache patch sites) without an explicit retag,
  // catching any future refactor that tries to launder external
  // pointers as internal-pool offsets.  Regime-1 EBO collapse
  // preserves sizeof(PtrSlot) == 24.

  using PtrMapKey = ::crucible::fixy::wrap::Tagged<void*,
                       ::crucible::fixy::tags::source::External>;

  struct PtrSlot {
    PtrMapKey key{nullptr};   // 8B (Tagged regime-1 EBO collapse)
    OpIndex op_index;          // 4B — default = none (UINT32_MAX)
    SlotId slot_id;            // 4B — default = none (UINT32_MAX)
    uint8_t port = 0;         // 1B
    uint8_t gen = 0;          // 1B — generation counter
    uint8_t pad[6]{};         // 6B — align to 24B
  };

  static_assert(sizeof(PtrSlot) == 24, "PtrSlot should be 24 bytes");
  static_assert(sizeof(PtrMapKey) == sizeof(void*),
                "Tagged<void*,External> must collapse to sizeof(void*) "
                "(regime-1 EBO) so PtrSlot stays at 24 bytes");

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
  //
  // #876 WRAP-BgThread-5: each raw `T* scratch_*_ = nullptr` paired
  // with a manual std::calloc + std::free is wrapped in
  // fixy::handle::AlignedBuffer<T> for move-only RAII.  The hot drain path
  // continues to read raw pointers via `local_map = scratch_map_.data()`
  // / `scratch_slots_.data()` / `scratch_edges_.data()` — same single-
  // load shape, no indirection.  Replacement on growth is a buffer move
  // (AlignedBuffer::allocate); the old buffer's dtor frees the prior
  // allocation, eliminating the explicit std::free pairs.

  ::crucible::fixy::handle::AlignedBuffer<PtrSlot>  scratch_map_;
  ::crucible::fixy::handle::AlignedBuffer<SlotInfo> scratch_slots_;
  ::crucible::fixy::handle::AlignedBuffer<Edge>     scratch_edges_;
  uint8_t map_gen_ = 0;

  // Current capacities (0 = not yet allocated).  The three *_cap_max_
  // fields grow monotonically — comment in ensure_scratch_buffers says
  // "never shrinks".  Wrapped so that promise is type-enforced.
  // ptr_mask_ is a derived view of map_cap_ kept raw for the inner-loop
  // load on line 608.
  crucible::fixy::wrap::Monotonic<uint32_t> map_cap_      {0};  // PtrMap capacity (power of 2)
  uint32_t                              ptr_mask_     = 0;  // map_cap_ - 1
  crucible::fixy::wrap::Monotonic<uint32_t> slot_cap_max_ {0};  // SlotInfo buffer capacity
  crucible::fixy::wrap::Monotonic<uint32_t> edge_cap_max_ {0};  // Edge buffer capacity

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
      // #876 WRAP-BgThread-5: allocate_zeroed replaces the manual
      // calloc + abort + free pair.  Previous buffer's dtor (move-
      // assigned-into) frees the prior alloc; new buffer is zeroed
      // (calloc-equivalent) for the gen-counter reset path below.
      map_cap_.advance(needed_map);
      ptr_mask_ = map_cap_.get() - 1;
      scratch_map_ = ::crucible::fixy::handle::AlignedBuffer<PtrSlot>::allocate_zeroed(map_cap_.get());
      map_gen_ = 0; // fresh buffer, reset gen
    }

    if (needed_slots > slot_cap_max_.get()) {
      slot_cap_max_.advance(needed_slots);
      scratch_slots_ = ::crucible::fixy::handle::AlignedBuffer<SlotInfo>::allocate_zeroed(slot_cap_max_.get());
    }

    if (needed_edges > edge_cap_max_.get()) {
      edge_cap_max_.advance(needed_edges);
      scratch_edges_ = ::crucible::fixy::handle::AlignedBuffer<Edge>::allocate_zeroed(edge_cap_max_.get());
    }
  }

  // ── PtrMap operations ─────────────────────────────────────────────

  // gnu::const: depends only on the pointer value, no memory access.
  [[nodiscard, gnu::const]] static uint32_t hash_ptr(void* ptr) noexcept {
    return static_cast<uint32_t>(
        std::bit_cast<uintptr_t>(ptr) * 0x9E3779B97F4A7C15ULL >> 32);
  }

  [[nodiscard]] static InsertResult ptr_map_insert(
      PtrSlot* map, uint8_t gen, uint32_t mask,
      void* key, OpIndex op_index, uint8_t port, SlotId slot_id) {
    uint32_t bucket_idx = hash_ptr(key) & mask;
    for (uint32_t probe = 0; probe <= mask; probe++) {
      auto& slot = map[(bucket_idx + probe) & mask];
      if (slot.gen != gen) {
        // Stale generation → empty slot. Claim it.
        // GAPS-097: tag the foreign data_ptr as External provenance
        // at the moment of storage; downstream code reading slot.key
        // sees a Tagged value and cannot accidentally route it into
        // internal-trust APIs.
        slot.key = PtrMapKey{key};
        slot.op_index = op_index;
        slot.port = port;
        slot.slot_id = slot_id;
        slot.gen = gen;
        return {.slot = &slot, .was_alias = false, .old_op = {}, .old_port = 0, .old_slot = {}};
      }
      if (slot.key.value() == key) {
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
      if (slot.gen == gen && slot.key.value() == key)
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
      ::crucible::effects::row_contains_v<  // ROW-CONTAINS-OK: concrete row literal (run_required_row), not a Ctx capability check
          run_required_row,
          ::crucible::effects::Effect::Bg>,
      "run_required_row MUST contain Effect::Bg — the bg thread "
      "by definition runs in the Bg context.");
  static_assert(
      ::crucible::effects::row_contains_v<  // ROW-CONTAINS-OK: concrete row literal (run_required_row), not a Ctx capability check
          run_required_row,
          ::crucible::effects::Effect::Alloc>,
      "run_required_row MUST contain Effect::Alloc — region "
      "construction and current_trace growth are allocations.");
  static_assert(
      ::crucible::effects::row_contains_v<  // ROW-CONTAINS-OK: concrete row literal (run_required_row), not a Ctx capability check
          run_required_row,
          ::crucible::effects::Effect::IO>,
      "run_required_row MUST contain Effect::IO — region_ready_cb "
      "fires with the freshly-built region (audit log, federated "
      "cache enqueue).");
  static_assert(
      ::crucible::effects::row_contains_v<  // ROW-CONTAINS-OK: concrete row literal (run_required_row), not a Ctx capability check
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

    // GAPS-099 Phase 1: 4-stage pipeline.  Channel flow is
    //   Start → DrainTraceRing → TraceBatch → DetectIteration →
    //          BuildWork → BuildTrace → GraphPublish → MakeRegion → Sink
    // Each adjacent pair is a typed PermissionedSpscChannel; each stage's
    // permission proof is minted by splitting a fresh root permission for
    // its tag.  Capacity 64 on every internal channel — backpressure
    // accumulates at the slowest stage rather than at any single fixed
    // bottleneck.
    TraceBatchChannel trace_batches;
    BuildWorkChannel build_work;
    GraphPublishChannel graph_publish;

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

    auto publish_whole = saf::mint_permission_root<
        spsc_tag::Whole<BgGraphPublishTag>>();
    auto [publish_prod_perm, publish_cons_perm] =
        saf::mint_permission_split<
            spsc_tag::Producer<BgGraphPublishTag>,
            spsc_tag::Consumer<BgGraphPublishTag>>(std::move(publish_whole));

    auto start_prod = start.producer(std::move(start_prod_perm));
    auto start_cons = start.consumer(std::move(start_cons_perm));
    auto trace_prod = trace_batches.producer(std::move(trace_prod_perm));
    auto trace_cons = trace_batches.consumer(std::move(trace_cons_perm));
    auto build_prod = build_work.producer(std::move(build_prod_perm));
    auto build_cons = build_work.consumer(std::move(build_cons_perm));
    auto publish_prod = graph_publish.producer(std::move(publish_prod_perm));
    auto publish_cons = graph_publish.consumer(std::move(publish_cons_perm));

    auto ctx = effects::BgDrainCtx{}.template in_row<run_required_row>();
    while (!start_prod.try_push(BgPipelineStart{this})) {
      CRUCIBLE_SPIN_PAUSE;
    }
    auto drain_stage = concurrent::mint_stage<&DrainTraceRingFn>(
        ctx, std::move(start_cons), std::move(trace_prod));
    auto detect_stage = concurrent::mint_stage<&DetectIterationFn>(
        ctx, std::move(trace_cons), std::move(build_prod));
    auto build_stage = concurrent::mint_stage<&BuildTraceFn>(
        ctx, std::move(build_cons), std::move(publish_prod));
    auto publish_stage = concurrent::mint_stage<&MakeRegionFn>(
        ctx, std::move(publish_cons), BgSinkProducerHandle{});

    auto pipeline = concurrent::mint_pipeline(
        ctx,
        std::move(drain_stage),
        std::move(detect_stage),
        std::move(build_stage),
        std::move(publish_stage));
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
        const uint32_t num_ops = graph->num_ops.get_assuming_set();
        const uint32_t num_slots = graph->num_slots.get_assuming_set();
        const uint32_t max_meta_end = graph->max_meta_end.get_assuming_set();

        // Use pre-computed content hash — no redundant second pass.
        auto* region = make_region(
            a, arena, graph->ops, num_ops, graph->content_hash);

        if (graph->slots && num_slots > 0) {
          region->plan = compute_memory_plan(
              a, graph->slots, num_slots);
        }

        // Advance MetaLog tail AFTER all reads are done (zero-copy safety).
        if (max_meta_end > 0)
          meta_log.get().value()->advance_tail(max_meta_end);

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
            crucible::fixy::wrap::Positive<size_t>{aux_bytes},
            crucible::fixy::wrap::PowerOfTwo<size_t>{alignof(int64_t)})) :
        nullptr;

    // ── PtrMap: bump generation (zero memset) ──

    map_gen_++;
    if (map_gen_ == 0) [[unlikely]] {
      // Wrap-around every 255 calls: full reset.
      std::fill_n(scratch_map_.data(), map_cap_.get(), PtrSlot{});
      map_gen_ = 1;
    }

    // ── SlotInfo: value-init the portion we'll use ──

    uint32_t slot_cap = std::min(slot_cap_max_.get(),
        std::max(uint32_t{256}, total_inputs + total_outputs));
    std::fill_n(scratch_slots_.data(), slot_cap, SlotInfo{});
    uint32_t next_slot_raw = 0;

    // ── Edge buffer: scratch, no init needed ──

    uint32_t num_edges = 0;

    // ── Hoist scratch buffer pointers into locals ──────────────────
    //
    // Same aliasing issue as the vector data pointers: the compiler
    // reloads scratch_map_/scratch_slots_/scratch_edges_/map_gen_
    // from this->member on every access because it can't prove arena
    // writes don't alias *this.  Local copies → register-resident.
    PtrSlot* const local_map = scratch_map_.data();
    SlotInfo* const local_slots = scratch_slots_.data();
    Edge* const local_edges = scratch_edges_.data();
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
      te.grad_enabled = (re.op_flags & op_flag::GRAD_ENABLED) != 0;
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

        // Aux pointers: cursor advance into bulk block. Each section
        // begins lifetime as a typed array via std::start_lifetime_as_array
        // (P2590R2) — the bulk arena memcpy elsewhere then writes into the
        // typed objects without UB.
        te.scalar_args = (n_scalars > 0) ?
            std::start_lifetime_as_array<int64_t>(aux_cursor, n_scalars) :
            nullptr;
        aux_cursor += n_scalars * sizeof(int64_t);
        te.input_trace_indices =
            std::start_lifetime_as_array<OpIndex>(aux_cursor, n_in);
        aux_cursor += n_in * sizeof(OpIndex);
        te.input_slot_ids =
            std::start_lifetime_as_array<SlotId>(aux_cursor, n_in);
        aux_cursor += n_in * sizeof(SlotId);
        te.output_slot_ids =
            std::start_lifetime_as_array<SlotId>(aux_cursor, n_out);
        aux_cursor += n_out * sizeof(SlotId);

        if (n_scalars > 0)
          // #1057 WRAP-TraceRing-5: re.scalar_values is FixedArray<int64_t, 5>;
          // .data() recovers the contiguous int64_t* for the bulk memcpy.
          std::memcpy(te.scalar_args, re.scalar_values.data(),
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
          const uint64_t dim_h_local =
              raw_dim_hash(detail::dim_hash_simd_det(meta));
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
            uint32_t bucket_idx =
                hash_ptr(raw_data_ptr(meta_base[next_off])) & local_mask;
            __builtin_prefetch(&local_map[bucket_idx], 0, 1);
          }
          if (next_re.num_outputs > 0) {
            uint32_t out_off = next_off + next_re.num_inputs;
            uint32_t bucket_idx =
                hash_ptr(raw_data_ptr(meta_base[out_off])) & local_mask;
            __builtin_prefetch(&local_map[bucket_idx], 1, 1);
          }
        }
      }

      // ── DFG edges + input slot tracking ──

      for (uint16_t j = 0; j < te.num_inputs; j++) {
        void* input_ptr = raw_data_ptr(te.input_metas[j]);
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
          // compute_storage_nbytes_det pins this projection as
          // DetSafe<Pure>; .peek().value() is the explicit boundary
          // where the memory planner accepts the saturated byte count.
          slot_info.nbytes =
              compute_storage_nbytes_det(
                  external_tensor_meta(te.input_metas[j])).peek().value();
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
        void* output_ptr = raw_data_ptr(te.output_metas[j]);
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
                compute_storage_nbytes_det(
                    external_tensor_meta(te.output_metas[j])).peek().value();
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
          slot_info.nbytes =
              compute_storage_nbytes_det(
                  external_tensor_meta(te.output_metas[j])).peek().value();
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
    auto* graph = alloc_trace_graph(a, arena);
    graph->ops = ops;
    graph->slots = slots;
    graph->num_slots.set(num_slots);
    graph->content_hash = ContentHash{detail::fmix64(content_h_local)};
    graph->max_meta_end.set(max_meta_end);
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
    // WRAP-BgThread-4 #875: extract via .value() — plan->device_capability
    // in MerkleDag.h is still raw uint64_t (separate WRAP-* task scope).
    plan->device_capability = device_capability.value();
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
    // num_ops = max_op + 1 ≥ 1 here by construction (max_op was set from
    // death_op + 1 of at least one internal slot above; num_internal > 0
    // is guaranteed by the early-return at line 1393).  num_ops + 1 cannot
    // wrap because num_ops < UINT32_MAX (death_op is uint32_t and we just
    // computed +1, so max_op ≤ UINT32_MAX - 1, hence num_ops ≤ UINT32_MAX).
    // The asserts inform the analyzer which can't see the chain.
    [[assume(num_ops > 0)]];
    [[assume(num_ops < UINT32_MAX)]];
    auto* birth_off = arena.alloc_array<uint32_t>(a, num_ops + 1);
    auto* death_off = arena.alloc_array<uint32_t>(a, num_ops + 1);
    [[assume(birth_off != nullptr)]];
    [[assume(death_off != nullptr)]];
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
