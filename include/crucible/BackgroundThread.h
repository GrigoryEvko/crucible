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

#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/_Saturate.h>
#include <crucible/SchemaTable.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/_Pipeline.h>
#include <crucible/concurrent/SpinLock.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/effects/_FxAliases.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/fixy/Handle.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/TraceGraph.h>

namespace crucible {

// Drains the ring buffer, detects iteration boundaries and builds a trace
// graph per iteration.
//
// Scratch buffers (PtrMap, SlotInfo, Edge) are allocated once and reused, so
// the drain path performs no per-call allocation.
struct BackgroundThread {
    using RingPtr = crucible::fixy::wrap::NonNull<TraceRing*>;
    using MetaLogPtr = crucible::fixy::wrap::NonNull<MetaLog*>;
    crucible::fixy::wrap::WriteOnce<RingPtr> ring;
    crucible::fixy::wrap::WriteOnce<MetaLogPtr> meta_log;

    int32_t rank = -1;
    int32_t world_size = 0;
    // A vendor-encoded hardware identity measured by the startup calibration
    // pass, not synthesized or defaulted.  Zero marks the pre-init state.
    using DeviceCapability = ::crucible::fixy::wrap::Tagged<uint64_t, ::crucible::fixy::tags::source::Meridian>;
    DeviceCapability device_capability{0};

    // Written by the background thread, read by the foreground.  The
    // store(release) publishes the region data written before it, and the
    // foreground's load(acquire) must see that data.  Relaxed would let the
    // foreground dereference a pointer to a region with garbage fields.
    //
    // Own cache line, because the foreground reads it while the background
    // writes the fields that follow.
    alignas(64) std::atomic<RegionNode*> active_region{nullptr};

    struct RegionReadyCallback {
        // The noexcept is load-bearing.  This fires mid-publish on the
        // background thread, so an escaping exception would unwind through
        // persistence and commit and leave the runtime half-applied.  Under
        // -fno-exceptions that cannot happen, and the noexcept pins the
        // invariant so a throwing callable reddens at the assignment site.
        using Fn = void (*)(void*, RegionNode*) noexcept;

        void* ctx = nullptr;
        Fn fn = nullptr;

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return fn != nullptr; }

        void operator()(RegionNode* region) const noexcept { fn(ctx, region); }
    };

    // Own cache line: background-only state.  Sharing a line with
    // active_region would drag the callback object into the foreground's
    // cache on every acquire load.
    alignas(64) RegionReadyCallback region_ready_cb;

    IterationDetector detector;

    // The four vectors run in parallel, one element per recorded op.
    // current_meta_starts holds MetaIndex::none() for an op with no tensor
    // arguments.
    std::vector<TraceRing::Entry> current_trace;
    std::vector<MetaIndex> current_meta_starts;
    std::vector<ScopeHash> current_scope_hashes;
    std::vector<CallsiteHash> current_callsite_hashes;

    // Own cache line: the background writes it, the foreground reads it for
    // diagnostics.  Sharing a line with the vectors above would invalidate
    // the foreground's copy on every push_back.
    alignas(64) crucible::fixy::wrap::Monotonic<uint32_t> iterations_completed{0};
    uint32_t last_iteration_length = 0;

    // The build stage and the publish stage are separate pipeline threads.
    // Arena storage is append-only, so pointers already handed out stay
    // valid, but the bump cursor is shared mutable state.  This gate
    // serializes the two allocation windows against each other without
    // weakening the permissioned SPSC stage topology.
    //
    // The gate spins on a pause budget and then yields, so a waiter can
    // reach the kernel.  That is why the windows it covers are kept to the
    // arena bumps themselves: every extra statement inside the gate is
    // another reason for the other stage to reach the yield.
    alignas(64) concurrent::SpinLock arena_alloc_gate_;
    Arena arena{1 << 20};

    // Regions that have been published but not compiled.  This is the
    // hand-off point a compiler backend attaches to, and until one exists
    // nothing reads it.
    //
    // It is a bounded ring rather than a growing list for that reason: an
    // unread list gains one pointer per published region and is never
    // reclaimed, so it grows for the whole life of the process.  Bounding it
    // keeps the hand-off point while making the retention constant.
    //
    // A backend that keeps pace with the publish stage never has more than
    // one graph-publish channel's worth of regions outstanding, so the bound
    // is that channel's depth.  A backend further behind than that is not
    // catching up, and the regions it missed describe shapes the model has
    // already moved past.
    //
    // The publish stage is the sole writer.  total() is safe to read from
    // any thread; at() is not, and needs the pipeline quiescent.
    class UncompiledRegionQueue {
    public:
        static constexpr uint32_t CAP = 64;

        void push(RegionNode* region) noexcept {
            slots_[static_cast<uint32_t>(total_.peek_relaxed() % CAP)] = region;
            (void)total_.bump();
        }

        // Every region ever published, including the ones the bound
        // dropped.  total() - size() is how many were dropped.
        [[nodiscard]] uint64_t total() const noexcept { return total_.get(); }

        [[nodiscard]] uint32_t size() const noexcept {
            const uint64_t seen = total_.get();
            return (seen < CAP) ? static_cast<uint32_t>(seen) : CAP;
        }

        // age 0 is the most recently published region.  The bound is size()
        // rather than CAP so that a fresh queue cannot hand back the null a
        // never-written slot still holds.  The clause is the in-body macro
        // because its predicate reaches a member through `this`, which a
        // plain pre() skips when the compiler folds this body.
        [[nodiscard]] RegionNode* at(uint32_t age) const noexcept {
            CRUCIBLE_PRE(age < size());
            const uint64_t seen = total_.get();
            return slots_[static_cast<uint32_t>((seen + CAP - 1u - age) % CAP)];
        }

    private:
        RegionNode* slots_[CAP]{};
        crucible::fixy::wrap::AtomicMonotonic<uint64_t> total_{0};
    };

    UncompiledRegionQueue uncompiled_regions;

    // A one-way signal for a single run() invocation.  start() re-arms it
    // under quiescence before launching the next pipeline.
    alignas(64) crucible::fixy::handle::OneShotFlag stop_requested;
    std::jthread pipeline_thread;

    // Total entries fully processed.  The write surface is friend-gated to
    // PublishStageAuth so only the publishing stage can advance it: bumping
    // from an earlier stage races the publish callback, and the gate turns
    // that into a "private member" diagnostic.
    //
    // The foreground must see every prior background write when its acquire
    // load sees the count.  bump_by's acq_rel supplies the release half.
    struct PublishStageAuth;
    struct PublishStageTag {};
    using TotalProcessedCell = fixy::handle::PublishCommitCell<PublishStageTag, PublishStageAuth>;
    TotalProcessedCell total_processed;

    // The foreground raises this when compiled replay diverges.  The
    // background clears its accumulated trace and detector on observing it,
    // so leftover signature ops from the pre-divergence iteration cannot
    // poison the next region.  The detector keeps one thing: the periods
    // that broke, so a layer square that diverged is not picked again.
    //
    // Own cache line: the foreground writes it while the background reads it
    // each drain cycle, and the neighbouring fields are background-private.
    alignas(64) crucible::fixy::handle::OneShotFlag reset_requested;

    // Which side of the last reset a piece of pipeline work belongs to.
    //
    // Clearing the detect stage's accumulated trace is not enough on its
    // own.  A region whose entries were all recorded before the divergence
    // may already be queued for the build or the publish stage, and it
    // finishes and publishes after the reset.  The foreground has just
    // deactivated replay and gone back to recording, and that publish hands
    // it the shape that diverged: it aligns against it, activates, and the
    // region cache then offers it as an alternate for the next divergence.
    //
    // So each work item carries the epoch it was cut under, and the two
    // stages downstream drop anything older than the current one.  The
    // detect stage is the sole writer, and it bumps inside the reset itself,
    // which puts every item already in flight one epoch behind.
    //
    // Commit markers are exempt.  They carry no region, only the count of
    // entries a batch consumed, and total_processed has to advance past a
    // dropped region for flush() to return.
    alignas(64) crucible::fixy::wrap::AtomicMonotonic<uint32_t> reset_epoch{0};

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
        // The reset epoch this work was cut under.  Meaningless on a
        // commit-only marker, which is never dropped.
        uint32_t epoch = 0;
        std::vector<TraceRing::Entry> trace;
        std::vector<MetaIndex> meta_starts;
        std::vector<ScopeHash> scope_hashes;
        std::vector<CallsiteHash> callsite_hashes;
    };

    struct BgPipelineDone {};
    struct BgPipelineStart {
        BackgroundThread* owner = nullptr;
    };

    // Lifetime: the producing stage owns it until it is pushed, after which
    // the consuming stage owns it and deletes it once the graph is
    // published.  The graph itself is arena-allocated and outlives this
    // wrapper.
    //
    // Two shapes flow through the channel:
    //   graph non-null, commit_only false — an iteration region to publish.
    //   graph null, commit_only true — a marker carrying the entry count.
    //
    // The marker has to travel through the publish stage so that
    // total_processed advances only after every region ahead of it has been
    // published.  Bumping the counter at the build stage instead races the
    // publish stage: a flush can return while a region is still queued, and
    // the caller then observes a compiled mode with no pending region, or an
    // active region whose plan is not finished.
    struct BgGraphPublish {
        BackgroundThread* owner = nullptr;
        TraceGraph* graph = nullptr;
        bool commit_only = false;
        uint32_t commit_count = 0;
        // Carried through from the BgBuildWork this came from, so a reset
        // that lands while the build stage is mid-graph is still caught
        // here, one stage later.
        uint32_t epoch = 0;
    };

    struct BgPipelineStartTag {};
    struct BgTraceBatchTag {};
    struct BgBuildWorkTag {};
    struct BgGraphPublishTag {};

    using StartChannel = concurrent::PermissionedSpscChannel<BgPipelineStart, 1, BgPipelineStartTag>;
    using TraceBatchChannel = concurrent::PermissionedSpscChannel<BgTraceBatch*, 64, BgTraceBatchTag>;
    using BuildWorkChannel = concurrent::PermissionedSpscChannel<BgBuildWork*, 64, BgBuildWorkTag>;
    using GraphPublishChannel = concurrent::PermissionedSpscChannel<BgGraphPublish*, 64, BgGraphPublishTag>;

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

        const uint32_t warmup = crucible::sat::sub_sat(crucible::sat::sub_sat(total, IterationDetector::K), iter_len);
        if (warmup > 0) [[unlikely]] {
            auto shift = [warmup](auto& vec) {
                const auto n = vec.size();
                if (warmup < n) {
                    std::memmove(vec.data(), vec.data() + warmup, (n - warmup) * sizeof(vec[0]));
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

        const uint32_t completed_len = crucible::sat::sub_sat(total, IterationDetector::K);
        last_iteration_length = completed_len;
        iterations_completed.bump();

        BgBuildWork* work = nullptr;
        if (meta_log && completed_len > 0) {
            work = new BgBuildWork{};
            work->owner = this;
            work->completed_len = completed_len;
            work->epoch = reset_epoch.get();
            work->trace.assign(current_trace.begin(), current_trace.begin() + completed_len);
            work->meta_starts.assign(current_meta_starts.begin(), current_meta_starts.begin() + completed_len);
            work->scope_hashes.assign(current_scope_hashes.begin(), current_scope_hashes.begin() + completed_len);
            work->callsite_hashes.assign(current_callsite_hashes.begin(),
                                         current_callsite_hashes.begin() + completed_len);
        }

        auto retain_tail = [](auto& vec) {
            constexpr uint32_t K = IterationDetector::K;
            const auto n = vec.size();
            if (n > K) {
                std::memmove(vec.data(), vec.data() + n - K, K * sizeof(vec[0]));
                vec.resize(K);
            }
        };
        retain_tail(current_trace);
        retain_tail(current_meta_starts);
        retain_tail(current_scope_hashes);
        retain_tail(current_callsite_hashes);

        return work;
    }

    // Releases a graph the publish stage will not publish.  The arena holds
    // the graph itself and never gives storage back, but the metadata log is
    // a ring the foreground keeps writing into, so its tail has to advance
    // past the entries this graph read or the log fills for good.
    void discard_trace_graph(TraceGraph* graph) CRUCIBLE_NO_THREAD_SAFETY {
        if (!graph) return;
        const uint32_t max_meta_end = graph->max_meta_end.get_assuming_set();
        if (max_meta_end > 0) {
            meta_log.get().value()->advance_tail(max_meta_end);
        }
    }

    void publish_trace_graph(effects::Alloc a, TraceGraph* graph) CRUCIBLE_NO_THREAD_SAFETY {
        if (!graph) return;

        const uint32_t num_ops = graph->num_ops.get_assuming_set();
        const uint32_t num_slots = graph->num_slots.get_assuming_set();
        const uint32_t max_meta_end = graph->max_meta_end.get_assuming_set();

        // The gate covers the arena bump cursor and nothing else.  It used
        // to wrap this whole function, which meant the region-ready callback
        // ran inside it: that callback commits a transaction, flips the mode
        // cell and reads sampled counters, and the build stage was locked
        // out of the arena for all of it.  The gate spins on a budget and
        // then yields, so the build stage reached the kernel every time.
        //
        // Nothing after this scope touches the arena.  recompute_merkle
        // walks nodes that already exist, advance_tail moves a counter in
        // the metadata log, and the queue below is heap storage the publish
        // stage owns alone.
        RegionNode* region = nullptr;
        {
            concurrent::SpinGuard guard{arena_alloc_gate_};
            region = make_region(a, arena, graph->ops, num_ops, graph->content_hash);
            if (graph->slots && num_slots > 0) {
                region->plan = compute_memory_plan(a, graph->slots, num_slots);
            }
        }

        if (max_meta_end > 0) {
            meta_log.get().value()->advance_tail(max_meta_end);
        }

        recompute_merkle(region);
        uncompiled_regions.push(region);

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
                batch->entries, batch->meta_starts, batch->scope_hashes, batch->callsite_hashes, BATCH_SIZE);

            if (batch->count == 0) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }

            push_pipeline(out, batch.get());
            batch.release();
            batch = std::make_unique<BgTraceBatch>();
            batch->owner = owner;
        }

        batch->count = owner->ring.get().value()->try_pop_batch(batch->entries, batch->meta_starts, batch->scope_hashes,
                                                                batch->callsite_hashes, BATCH_SIZE);
        if (batch->count > 0) {
            push_pipeline(out, batch.get());
            batch.release();
        }

        BgTraceBatch* stop = nullptr;
        push_pipeline(out, stop);
    }

    static void DetectIterationFn(typename TraceBatchChannel::ConsumerHandle&& in,
                                  typename BuildWorkChannel::ProducerHandle&& out) {
        [[maybe_unused]] auto bg = effects::mint_bg_context(effects::detail::ctx_mint::bg_key{});

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
                owner->detector.restart_after_divergence();
                owner->current_trace.clear();
                owner->current_meta_starts.clear();
                owner->current_scope_hashes.clear();
                owner->current_callsite_hashes.clear();
                // Everything already handed downstream was cut from
                // pre-divergence entries.  The bump is what the build and
                // publish stages compare against to drop it.  It happens
                // last so the cleared state above is visible to whoever
                // observes the new epoch.
                (void)owner->reset_epoch.bump();
            };

            (void)owner->reset_requested.check_and_run(do_reset);

            for (uint32_t i = 0; i < batch->count; ++i) {
                (void)owner->reset_requested.check_and_run(do_reset);

                owner->current_trace.push_back(batch->entries[i]);
                owner->current_meta_starts.push_back(batch->meta_starts[i]);
                owner->current_scope_hashes.push_back(batch->scope_hashes[i]);
                owner->current_callsite_hashes.push_back(batch->callsite_hashes[i]);

                if (owner->detector.check(batch->entries[i].schema_hash, batch->entries[i].shape_hash)) {
                    if (auto work = std::unique_ptr<BgBuildWork>(owner->prepare_iteration_build_work())) {
                        push_pipeline(out, work.get());
                        work.release();
                    }
                }
            }

            auto commit = std::unique_ptr<BgBuildWork>(owner->make_commit_work(batch->count));
            push_pipeline(out, commit.get());
            commit.release();
            (void)bg;
        }
    }

    // The stop sentinel, a null BgBuildWork*, is forwarded as a null
    // BgGraphPublish*.
    static void BuildTraceFn(typename BuildWorkChannel::ConsumerHandle&& in,
                             typename GraphPublishChannel::ProducerHandle&& out) {
        [[maybe_unused]] auto bg = effects::mint_bg_context(effects::detail::ctx_mint::bg_key{});

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
                // Forward the marker downstream so total_processed advances
                // only after every preceding graph has been published.
                // Bumping here instead races the publish stage: the
                // foreground can see the counter catch up, dispatch, and find
                // no pending region, or an active region whose plan is still
                // being computed.
                auto publish = std::make_unique<BgGraphPublish>();
                publish->owner = owner;
                publish->graph = nullptr;
                publish->commit_only = true;
                publish->commit_count = work->commit_count;
                publish->epoch = work->epoch;
                push_pipeline(out, publish.get());
                publish.release();
                continue;
            }

            // A stale work item is deliberately still built here rather
            // than dropped on the spot.  Dropping it would save the arena
            // bump, but the metadata entries it covers still have to be
            // released or the log fills for good, and the only thing that
            // knows that range is the scan inside build_trace_from.  Doing
            // it here instead would put a second copy of "where does this
            // work's metadata end" in the tree, which is the kind of
            // duplicate that drifts.
            //
            // So the stale check sits one stage on, at the publish, where
            // the graph carries its own max_meta_end.  The cost is one
            // wasted graph per in-flight region per divergence, on a path
            // that only runs when replay has already failed.

            TraceGraph* graph = nullptr;
            {
                concurrent::SpinGuard guard{owner->arena_alloc_gate_};
                graph =
                    owner->build_trace_from(bg.alloc, work->completed_len, work->trace.data(), work->meta_starts.data(),
                                            work->scope_hashes.data(), work->callsite_hashes.data());
            }

            // Forwarding only well-formed graphs keeps the downstream
            // contract at "a non-null graph is publishable".
            if (!graph) continue;

            auto publish = std::make_unique<BgGraphPublish>();
            publish->owner = owner;
            publish->graph = graph;
            publish->commit_only = false;
            publish->commit_count = 0;
            publish->epoch = work->epoch;
            push_pipeline(out, publish.get());
            publish.release();
        }
    }

    // Keeping the publish stage separate means the publish-side delegate
    // never sees the build-side machinery, so a crash-stop during publish
    // cannot taint the build stage's owner pointer.
    //
    // The channel is SPSC FIFO, so a commit marker can only arrive here
    // after every region produced from its batch and from preceding batches
    // has gone through publish_trace_graph.  A completed flush therefore
    // implies every region is published.
    static void MakeRegionFn(typename GraphPublishChannel::ConsumerHandle&& in, BgSinkProducerHandle&& out) {
        [[maybe_unused]] auto bg = effects::mint_bg_context(effects::detail::ctx_mint::bg_key{});

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
                // The marker sits strictly after every publish message from
                // the same and earlier batches, so this bump happens-after
                // every publish above.  Its release pairs with the
                // foreground's acquire load.
                (void)PublishStageAuth::commit(owner->total_processed, publish->commit_count);
                continue;
            }

            // A divergence reset landed after this region's entries were
            // cut, so the region describes the shape that just failed to
            // replay.  Publishing it would hand that shape back to a
            // foreground that has already fallen back to recording.
            //
            // This is the only stale check, and it sits here rather than at
            // the build stage so that the graph's own max_meta_end is the
            // one thing that decides which metadata range to release.  It
            // also catches a reset that landed while the build stage was
            // mid-graph.
            //
            // The graph itself is arena storage and stays where it is.  Only
            // the metadata log needs the hand-off: the entries this graph
            // read are dead either way, and the foreground has to be able to
            // reuse that space.
            if (publish->epoch != owner->reset_epoch.get()) {
                owner->discard_trace_graph(publish->graph);
                continue;
            }

            owner->publish_trace_graph(bg.alloc, publish->graph);
        }
    }

    // The only legal caller of bump_by on total_processed.  Any other call
    // site fails to compile because bump_by is private to this friend.
    //
    // Defined late in the class body so the friend declaration on the cell
    // above sees the forward-declared name.
    struct PublishStageAuth {
        [[nodiscard]] static uint64_t commit(TotalProcessedCell& cell, uint64_t delta) noexcept {
            return cell.bump_by(delta);
        }
    };

    // Sealing the global registration tables here turns "all registrations
    // complete before the background thread starts" into a load-bearing
    // rule: a late registration is rejected by the table contract instead of
    // racing the lookups this thread performs.
    void start(TraceRing* ring_ptr, MetaLog* meta_log_ptr, int32_t rank_ = -1, int32_t world_size_ = 0,
               uint64_t device_cap = 0) CRUCIBLE_NO_THREAD_SAFETY {
        global_schema_table().seal();
        global_ckernel_table().value()->seal();
        ring.set(RingPtr{ring_ptr});
        meta_log.set(MetaLogPtr{meta_log_ptr});
        rank = rank_;
        world_size = world_size_;
        device_capability = DeviceCapability{device_cap};
        reserve_iteration_buffers_();
        stop_requested.reset_in_quiescent_context(::crucible::fixy::handle::OneShotFlag::QuiescenceProof{});
        pipeline_thread = std::jthread([this](std::stop_token) noexcept { run_in_row<run_required_row>(); });
    }

    void stop() CRUCIBLE_NO_THREAD_SAFETY {
        stop_requested.signal();
        if (pipeline_thread.joinable()) {
            pipeline_thread.join();
        }
    }

    ~BackgroundThread() CRUCIBLE_NO_THREAD_SAFETY { stop(); }

    BackgroundThread() = default;
    BackgroundThread(const BackgroundThread&) = delete("BackgroundThread owns a pipeline jthread");
    BackgroundThread& operator=(const BackgroundThread&) = delete("BackgroundThread owns a pipeline jthread");
    BackgroundThread(BackgroundThread&&) = delete("BackgroundThread owns a pipeline jthread with captured this");
    BackgroundThread&
    operator=(BackgroundThread&&) = delete("BackgroundThread owns a pipeline jthread with captured this");

    void set_region_ready_callback(void* ctx, RegionReadyCallback::Fn fn) noexcept {
        region_ready_cb = RegionReadyCallback{.ctx = ctx, .fn = fn};
    }

#ifdef CRUCIBLE_BENCH
public:  // The bench harness times sub-phases against the scratch buffers.
#else
private:
#endif
    static constexpr uint32_t MIN_PTR_MAP_CAP = 4096;
    static constexpr uint32_t MIN_SCRATCH_SLOT_CAP = 8192;
    static constexpr uint32_t MIN_SCRATCH_EDGE_CAP = 16384;
    // std::bit_ceil is undefined at 2^31, so the cap stops one power below.
    static constexpr uint32_t MAX_PTR_MAP_CAP = uint32_t{1} << 30;

    // Open-addressing map from data_ptr to (op_index, output_port, slot_id).
    // A slot counts as empty when slot.gen differs from map_gen_, so bumping
    // map_gen_ clears the whole table in constant time and only the
    // wrap-around every 255 calls needs a real memset.
    //
    // The key is an address observed from a foreign allocator, so the
    // background thread treats it as an opaque cookie: hash and equality
    // only, never an offset into an internal pool.
    using PtrMapKey = ::crucible::fixy::wrap::Tagged<void*, ::crucible::fixy::tags::source::External>;

    struct PtrSlot {
        PtrMapKey key{nullptr};
        OpIndex op_index;
        SlotId slot_id;
        uint8_t port = 0;
        uint8_t gen = 0;
        uint8_t pad[6]{};
    };

    static_assert(sizeof(PtrSlot) == 24, "PtrSlot should be 24 bytes");
    static_assert(sizeof(PtrMapKey) == sizeof(void*), "Tagged<void*,External> must collapse to sizeof(void*) "
                                                      "(regime-1 EBO) so PtrSlot stays at 24 bytes");

    // The field order matches the bulk-copyable region of TensorSlot at
    // offsets 8 through 31, so the slot build below copies the whole block
    // with one 24-byte memcpy instead of ten field assignments.
    struct SlotInfo {
        uint64_t nbytes = 0;
        OpIndex birth_op;
        OpIndex death_op;
        ScalarType dtype = ScalarType::Undefined;
        DeviceType device_type = DeviceType::CPU;
        int8_t device_idx = -1;
        Layout layout = Layout::Strided;
        bool is_external = false;
        uint8_t pad[3]{};
    };

    static_assert(sizeof(SlotInfo) == 24, "SlotInfo: matches TensorSlot bulk region");

    // old_op, old_port and old_slot carry the displaced entry and are
    // meaningful only when was_alias is true.
    struct InsertResult {
        PtrSlot* slot = nullptr;
        bool was_alias = false;
        OpIndex old_op;
        uint8_t old_port = 0;
        SlotId old_slot;
    };

    ::crucible::fixy::handle::AlignedBuffer<PtrSlot> scratch_map_;
    ::crucible::fixy::handle::AlignedBuffer<SlotInfo> scratch_slots_;
    ::crucible::fixy::handle::AlignedBuffer<Edge> scratch_edges_;
    uint8_t map_gen_ = 0;

    // Zero means not yet allocated.  ptr_mask_ is a derived view of map_cap_
    // kept raw because the inner probe loop loads it every iteration.
    crucible::fixy::wrap::Monotonic<uint32_t> map_cap_{0};
    uint32_t ptr_mask_ = 0;
    crucible::fixy::wrap::Monotonic<uint32_t> slot_cap_max_{0};
    crucible::fixy::wrap::Monotonic<uint32_t> edge_cap_max_{0};

    // Called after the scan above, which supplies exact counts.  Buffers
    // grow and never shrink, so the cost amortizes across iterations.
    void ensure_scratch_buffers(uint32_t total_inputs, uint32_t total_outputs) {
        // Power-of-two capacity at a load factor below one half.  The unique
        // pointer count is the output count plus a small external fraction.
        uint32_t raw_map_size = std::max(
            MIN_PTR_MAP_CAP, crucible::sat::mul_sat(crucible::sat::add_sat(total_outputs, uint32_t{256}), uint32_t{2}));
        uint32_t needed_map = (raw_map_size >= MAX_PTR_MAP_CAP) ? MAX_PTR_MAP_CAP : std::bit_ceil(raw_map_size);

        // Unique storages are bounded by the outputs plus external headroom.
        uint32_t needed_slots = std::max(MIN_SCRATCH_SLOT_CAP, crucible::sat::add_sat(total_outputs, uint32_t{1024}));

        // Data-flow edges are bounded by the inputs, alias edges by the
        // outputs.
        uint32_t needed_edges = std::max(MIN_SCRATCH_EDGE_CAP, crucible::sat::add_sat(total_inputs, total_outputs));

        if (needed_map > map_cap_.get()) {
            map_cap_.advance(needed_map);
            ptr_mask_ = map_cap_.get() - 1;
            scratch_map_ = ::crucible::fixy::handle::AlignedBuffer<PtrSlot>::allocate_zeroed(map_cap_.get());
            // The fresh buffer is zeroed, so generation zero is unused.
            map_gen_ = 0;
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

    [[nodiscard, gnu::const]] static uint32_t hash_ptr(void* ptr) noexcept {
        return static_cast<uint32_t>(std::bit_cast<uintptr_t>(ptr) * 0x9E3779B97F4A7C15ULL >> 32);
    }

    [[nodiscard]] static InsertResult ptr_map_insert(PtrSlot* map, uint8_t gen, uint32_t mask, void* key,
                                                     OpIndex op_index, uint8_t port, SlotId slot_id) {
        uint32_t bucket_idx = hash_ptr(key) & mask;
        for (uint32_t probe = 0; probe <= mask; probe++) {
            auto& slot = map[(bucket_idx + probe) & mask];
            if (slot.gen != gen) {
                // A stale generation means the slot is empty.  Claim it.
                slot.key = PtrMapKey{key};
                slot.op_index = op_index;
                slot.port = port;
                slot.slot_id = slot_id;
                slot.gen = gen;
                return {.slot = &slot, .was_alias = false, .old_op = {}, .old_port = 0, .old_slot = {}};
            }
            if (slot.key.value() == key) {
                InsertResult result{.slot = &slot,
                                    .was_alias = (slot.op_index != op_index),
                                    .old_op = slot.op_index,
                                    .old_port = slot.port,
                                    .old_slot = slot.slot_id};
                slot.op_index = op_index;
                slot.port = port;
                // slot_id is deliberately left alone: an alias shares the
                // storage, so it shares the slot.
                return result;
            }
        }
        return {.slot = nullptr, .was_alias = false, .old_op = {}, .old_port = 0, .old_slot = {}};  // table full
    }

    struct PtrLookup {
        OpIndex op_index;
        SlotId slot_id;
        uint8_t port = 0;
    };

    [[nodiscard]] static PtrLookup ptr_map_lookup(const PtrSlot* map, uint8_t gen, uint32_t mask, void* key) {
        if (!key) return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};
        uint32_t bucket_idx = hash_ptr(key) & mask;
        for (uint32_t probe = 0; probe <= mask; probe++) {
            auto& slot = map[(bucket_idx + probe) & mask];
            if (slot.gen == gen && slot.key.value() == key)
                return {.op_index = slot.op_index, .slot_id = slot.slot_id, .port = slot.port};
            if (slot.gen != gen) return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};  // empty → miss
        }
        return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};
    }

public:
    // The drain loop runs in the background context, arena-allocates regions
    // and grows the trace vectors, fires the region callback as its only
    // externally observable side effect, and spin-pauses on an empty ring.
    // Those are the four atoms below.  A caller must declare a superset;
    // declaring fewer is a compile error.
    using run_required_row =
        ::crucible::effects::Row<::crucible::effects::Effect::Bg, ::crucible::effects::Effect::Alloc,
                                 ::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;

    // The asserts below pin the exact row contents, so a refactor that drops
    // or adds an atom fails here rather than silently moving the fence.
    static_assert(
        std::is_same_v<run_required_row,
                       ::crucible::effects::Row<::crucible::effects::Effect::Bg, ::crucible::effects::Effect::Alloc,
                                                ::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>>,
        "BackgroundThread::run_required_row must be exactly "
        "Row<Bg, Alloc, IO, Block>.  Adding or removing an atom changes the "
        "row every site that spawns this thread must declare.");
    static_assert(::crucible::effects::row_size_v<run_required_row> == 4u,
                  "run_required_row must be exactly 4 atoms (Bg + Alloc + IO + Block).");
    static_assert(
        ::crucible::effects::
            row_contains_v<  // ROW-CONTAINS-OK: concrete row literal (run_required_row), not a Ctx capability check
                run_required_row, ::crucible::effects::Effect::Bg>,
        "run_required_row must contain Effect::Bg.  This thread runs in the "
        "background context by definition.");
    static_assert(
        ::crucible::effects::
            row_contains_v<  // ROW-CONTAINS-OK: concrete row literal (run_required_row), not a Ctx capability check
                run_required_row, ::crucible::effects::Effect::Alloc>,
        "run_required_row must contain Effect::Alloc.  Region construction "
        "and trace-vector growth allocate.");
    static_assert(
        ::crucible::effects::
            row_contains_v<  // ROW-CONTAINS-OK: concrete row literal (run_required_row), not a Ctx capability check
                run_required_row, ::crucible::effects::Effect::IO>,
        "run_required_row must contain Effect::IO.  The region callback "
        "fires with the freshly built region.");
    static_assert(
        ::crucible::effects::
            row_contains_v<  // ROW-CONTAINS-OK: concrete row literal (run_required_row), not a Ctx capability check
                run_required_row, ::crucible::effects::Effect::Block>,
        "run_required_row must contain Effect::Block.  The drain spin-pauses "
        "on an empty ring and the scheduler parks the thread.");

    template <typename CallerRow>
        requires ::crucible::effects::Subrow<run_required_row, CallerRow>
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

        // Channel flow is
        //   Start → DrainTraceRing → TraceBatch → DetectIteration →
        //          BuildWork → BuildTrace → GraphPublish → MakeRegion → Sink
        // Each adjacent pair is one typed channel, and each stage's
        // permission proof comes from splitting a fresh root permission for
        // its tag.
        TraceBatchChannel trace_batches;
        BuildWorkChannel build_work;
        GraphPublishChannel graph_publish;

        StartChannel start;
        auto start_whole = saf::mint_permission_root<spsc_tag::Whole<BgPipelineStartTag>>();
        auto [start_prod_perm, start_cons_perm] =
            saf::mint_permission_split<spsc_tag::Producer<BgPipelineStartTag>, spsc_tag::Consumer<BgPipelineStartTag>>(
                std::move(start_whole));

        auto trace_whole = saf::mint_permission_root<spsc_tag::Whole<BgTraceBatchTag>>();
        auto [trace_prod_perm, trace_cons_perm] =
            saf::mint_permission_split<spsc_tag::Producer<BgTraceBatchTag>, spsc_tag::Consumer<BgTraceBatchTag>>(
                std::move(trace_whole));

        auto build_whole = saf::mint_permission_root<spsc_tag::Whole<BgBuildWorkTag>>();
        auto [build_prod_perm, build_cons_perm] =
            saf::mint_permission_split<spsc_tag::Producer<BgBuildWorkTag>, spsc_tag::Consumer<BgBuildWorkTag>>(
                std::move(build_whole));

        auto publish_whole = saf::mint_permission_root<spsc_tag::Whole<BgGraphPublishTag>>();
        auto [publish_prod_perm, publish_cons_perm] =
            saf::mint_permission_split<spsc_tag::Producer<BgGraphPublishTag>, spsc_tag::Consumer<BgGraphPublishTag>>(
                std::move(publish_whole));

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
        auto drain_stage = concurrent::mint_stage<&DrainTraceRingFn>(ctx, std::move(start_cons), std::move(trace_prod));
        auto detect_stage =
            concurrent::mint_stage<&DetectIterationFn>(ctx, std::move(trace_cons), std::move(build_prod));
        auto build_stage = concurrent::mint_stage<&BuildTraceFn>(ctx, std::move(build_cons), std::move(publish_prod));
        auto publish_stage =
            concurrent::mint_stage<&MakeRegionFn>(ctx, std::move(publish_cons), BgSinkProducerHandle{});

        auto pipeline = concurrent::mint_pipeline(ctx, std::move(drain_stage), std::move(detect_stage),
                                                  std::move(build_stage), std::move(publish_stage));
        std::move(pipeline).run();
    }

public:
    static constexpr uint32_t MAX_SLOTS = 65536;

    // Turns ring entries plus tensor metadata into a CSR property graph.
    // Returns nullptr on metadata-log overflow.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] TraceGraph* build_trace(effects::Alloc a, uint32_t count)
        CRUCIBLE_NO_THREAD_SAFETY {
        return build_trace_from(a, count, current_trace.data(), current_meta_starts.data(), current_scope_hashes.data(),
                                current_callsite_hashes.data());
    }

    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] TraceGraph*
    build_trace_from(effects::Alloc a, uint32_t count, const TraceRing::Entry* trace_data, const MetaIndex* meta_data,
                     const ScopeHash* scope_data, const CallsiteHash* callsite_data) CRUCIBLE_NO_THREAD_SAFETY {
        // Scan for totals and for metadata-log overflow.
        uint32_t max_meta_end = 0;
        uint32_t first_meta = UINT32_MAX;
        uint32_t total_inputs = 0;
        uint32_t total_outputs = 0;
        uint32_t total_scalars = 0;
        for (uint32_t i = 0; i < count; i++) {
            MetaIndex ms = meta_data[i];
            const auto& re = trace_data[i];
            if (!ms.is_valid() && (re.num_inputs + re.num_outputs) > 0) {
                // Overflow on an op that did have tensors.
                if (max_meta_end > 0) meta_log.get().value()->advance_tail(max_meta_end);
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

        ensure_scratch_buffers(total_inputs, total_outputs);

        auto* ops = arena.alloc_array<TraceEntry>(a, count);

        // Point straight into the metadata log's circular buffer instead of
        // copying.  These pointers stay valid until advance_tail runs, which
        // is deferred until every read below is done.  A buffer wrap makes
        // the contiguous view unavailable and falls back to an arena copy.
        TensorMeta* meta_base = nullptr;
        if (first_meta != UINT32_MAX) {
            uint32_t total_metas = max_meta_end - first_meta;
            meta_base = meta_log.get().value()->try_contiguous(first_meta, total_metas);
            if (!meta_base) [[unlikely]] {
                meta_base = arena.alloc_array<TensorMeta>(a, total_metas);
                for (uint32_t meta_idx = 0; meta_idx < total_metas; meta_idx++)
                    meta_base[meta_idx] = meta_log.get().value()->at(first_meta + meta_idx);
            }
        }

        // One auxiliary block holds scalars, trace indices and slot ids.
        // The section order keeps every section naturally aligned:
        //   [int64_t scalars]  align 8
        //   [OpIndex indices]  align 4
        //   [SlotId in_slots]  align 4
        //   [SlotId out_slots] align 4
        size_t aux_bytes =
            static_cast<size_t>(total_scalars) * sizeof(int64_t) + static_cast<size_t>(total_inputs) * sizeof(OpIndex)
            + static_cast<size_t>(total_inputs) * sizeof(SlotId) + static_cast<size_t>(total_outputs) * sizeof(SlotId);
        char* aux_cursor =
            (aux_bytes > 0)
                ? static_cast<char*>(arena.alloc(a, crucible::fixy::wrap::Positive<size_t>{aux_bytes},
                                                 crucible::fixy::wrap::PowerOfTwo<size_t>{alignof(int64_t)}))
                : nullptr;

        map_gen_++;
        if (map_gen_ == 0) [[unlikely]] {
            // Generation wrapped, so stale slots would read as current.
            std::fill_n(scratch_map_.data(), map_cap_.get(), PtrSlot{});
            map_gen_ = 1;
        }

        // Value-initialize only the portion this call uses.
        uint32_t slot_cap = std::min(slot_cap_max_.get(), std::max(uint32_t{256}, total_inputs + total_outputs));
        std::fill_n(scratch_slots_.data(), slot_cap, SlotInfo{});
        uint32_t next_slot_raw = 0;

        // The edge buffer needs no initialization: num_edges bounds every
        // read.
        uint32_t num_edges = 0;

        // Hoisted into locals because the compiler cannot prove the arena
        // writes below do not alias *this, and would otherwise reload each
        // member on every access.
        PtrSlot* const local_map = scratch_map_.data();
        SlotInfo* const local_slots = scratch_slots_.data();
        Edge* const local_edges = scratch_edges_.data();
        const uint8_t local_gen = map_gen_;
        const uint32_t local_mask = ptr_mask_;

        // The fold object owns the seed, the per-op mix and the finalizer,
        // so this streaming producer cannot drift from the span producer in
        // MerkleDag.h.  Spelling the seed here instead is what used to make
        // a change to one of them silent.
        //
        // NoRecipe, and it is a statement about this build rather than about
        // these ops.  A region's content hash is the compiler's cache key,
        // and the key is only sound if it separates regions whose numerics
        // differ.  Nothing in the runtime selects numerics: RecipePool and
        // RecipeRegistry exist and are tested, and no production translation
        // unit constructs either one, so there is exactly one unnamed
        // numerical regime and every region belongs to it.  Under one regime
        // a recipe-blind key separates nothing it needs to separate, and
        // folding a placeholder in its place would move every persisted hash
        // to distinguish a set of size one from itself.
        //
        // What must not happen is a SECOND regime arriving while this line
        // still says NoRecipe: two regions with identical ops under
        // different recipes would then share a key, and a lookup made under
        // one would be served a kernel compiled under the other.  A wrong
        // kernel is worse than no kernel, so the recipe has to reach this
        // fold in the same change that first selects one.  It would reach it
        // from the recording boundary, which is the only place that knows
        // which numerics the caller asked for: Vigil::dispatch_op fills a
        // TraceRing::Entry, that entry is drained into the TraceEntry below,
        // and neither carries a recipe field today.  Adding one is a change
        // to TraceRing.h and Vigil.h, and from there the recipe arrives here
        // and this construction takes it.
        ContentHashFold content_fold{ContentHashFold::NoRecipe{}};

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
            // The assert fires before the first memcpy can inherit garbage,
            // and the clamp on the next line stands in where contracts
            // compile out.
            contract_assert(re.num_scalar_args <= 5);
            uint16_t n_scalars = std::min(re.num_scalar_args, uint16_t(5));
            te.num_scalar_args = n_scalars;

            if (ms.is_valid()) {
                uint16_t n_in = re.num_inputs;
                uint16_t n_out = re.num_outputs;

                uint32_t meta_offset = ms.raw() - first_meta;
                te.input_metas = meta_base + meta_offset;
                te.output_metas = meta_base + meta_offset + n_in;

                // Each section of the auxiliary block begins its lifetime as
                // a typed array here, so the later bulk memcpy writes into
                // live objects rather than raw storage.
                te.scalar_args =
                    (n_scalars > 0) ? std::start_lifetime_as_array<int64_t>(aux_cursor, n_scalars) : nullptr;
                aux_cursor += n_scalars * sizeof(int64_t);
                te.input_trace_indices = std::start_lifetime_as_array<OpIndex>(aux_cursor, n_in);
                aux_cursor += n_in * sizeof(OpIndex);
                te.input_slot_ids = std::start_lifetime_as_array<SlotId>(aux_cursor, n_in);
                aux_cursor += n_in * sizeof(SlotId);
                te.output_slot_ids = std::start_lifetime_as_array<SlotId>(aux_cursor, n_out);
                aux_cursor += n_out * sizeof(SlotId);

                if (n_scalars > 0) std::memcpy(te.scalar_args, re.scalar_values.data(), n_scalars * sizeof(int64_t));

            } else {
                te.input_metas = nullptr;
                te.output_metas = nullptr;
                te.scalar_args = nullptr;
                te.input_trace_indices = nullptr;
                te.input_slot_ids = nullptr;
                te.output_slot_ids = nullptr;
                te.num_inputs = 0;
                te.num_outputs = 0;
            }

            // Both branches above fully populate `te`, so one fold covers
            // the tensor and no-tensor cases alike.  This is the same object
            // the canonical whole-region hash drives, so the streaming
            // result is bit-identical to that pass by construction and needs
            // no second walk.
            content_fold.fold(te);

            // Prefetch the map slots the next op will probe.  Processing the
            // current op gives the lines time to arrive.
            if (i + 1 < count) {
                MetaIndex next_ms = meta_data[i + 1];
                if (next_ms.is_valid()) {
                    uint32_t next_off = next_ms.raw() - first_meta;
                    const auto& next_re = trace_data[i + 1];
                    if (next_re.num_inputs > 0) {
                        uint32_t bucket_idx = hash_ptr(raw_data_ptr(meta_base[next_off])) & local_mask;
                        __builtin_prefetch(&local_map[bucket_idx], 0, 1);
                    }
                    if (next_re.num_outputs > 0) {
                        uint32_t out_off = next_off + next_re.num_inputs;
                        uint32_t bucket_idx = hash_ptr(raw_data_ptr(meta_base[out_off])) & local_mask;
                        __builtin_prefetch(&local_map[bucket_idx], 1, 1);
                    }
                }
            }

            for (uint16_t j = 0; j < te.num_inputs; j++) {
                void* input_ptr = raw_data_ptr(te.input_metas[j]);
                auto lookup = ptr_map_lookup(local_map, local_gen, local_mask, input_ptr);
                te.input_trace_indices[j] = lookup.op_index;
                if (lookup.op_index.is_valid()) {
                    te.input_slot_ids[j] = lookup.slot_id;
                    // Armed in a release build too. The very next statement
                    // writes local_edges[num_edges], so a cap that the edge
                    // count has already reached runs off an arena array and
                    // corrupts whatever the arena handed out next.
                    //
                    // Not a contract clause because a precondition cannot
                    // name a counter this loop is advancing, and because
                    // this form does not depend on the contract evaluation
                    // semantic, which is a per-target build option.
                    CRUCIBLE_FATAL_INVARIANT(num_edges < edge_cap_max_.get());
                    local_edges[num_edges++] = {.src = OpIndex{lookup.op_index.raw()},
                                                .dst = OpIndex{i},
                                                .src_port = lookup.port,
                                                .dst_port = static_cast<uint8_t>(j),
                                                .kind = EdgeKind::DATA_FLOW,
                                                .pad = 0};
                    if (lookup.slot_id.raw() < slot_cap) {
                        auto& slot_info = local_slots[lookup.slot_id.raw()];
                        slot_info.death_op = std::max(slot_info.death_op, OpIndex{i});
                    }
                } else if (input_ptr != nullptr && next_slot_raw < slot_cap) {
                    // First sight of an external tensor, such as a parameter
                    // or a data-loader output.
                    SlotId new_slot{next_slot_raw++};
                    te.input_slot_ids[j] = new_slot;
                    auto& slot_info = local_slots[new_slot.raw()];
                    slot_info.birth_op = OpIndex{0};
                    slot_info.death_op = OpIndex{i};
                    slot_info.is_external = true;
                    slot_info.nbytes =
                        compute_storage_nbytes_det(external_tensor_meta(te.input_metas[j])).peek().value();
                    slot_info.dtype = te.input_metas[j].dtype;
                    slot_info.device_type = te.input_metas[j].device_type;
                    slot_info.device_idx = te.input_metas[j].device_idx;
                    slot_info.layout = te.input_metas[j].layout;
                    (void)ptr_map_insert(local_map, local_gen, local_mask, input_ptr, OpIndex{}, 0, new_slot);
                } else {
                    te.input_slot_ids[j] = SlotId{};
                }
            }

            for (uint16_t j = 0; j < te.num_outputs; j++) {
                void* output_ptr = raw_data_ptr(te.output_metas[j]);
                if (!output_ptr) {
                    te.output_slot_ids[j] = SlotId{};
                    continue;
                }

                auto result = ptr_map_insert(local_map, local_gen, local_mask, output_ptr, OpIndex{i},
                                             static_cast<uint8_t>(j), SlotId{0});

                if (result.was_alias) {
                    te.output_slot_ids[j] = result.old_slot;
                    if (result.old_slot.raw() < slot_cap) {
                        auto& slot_info = local_slots[result.old_slot.raw()];
                        slot_info.death_op = std::max(slot_info.death_op, OpIndex{i});
                        const uint64_t output_nbytes =
                            compute_storage_nbytes_det(external_tensor_meta(te.output_metas[j])).peek().value();
                        slot_info.nbytes = std::max(slot_info.nbytes, output_nbytes);
                    }
                    if (result.old_op.is_valid()) {
                        // Same bound as the data-flow edge above, and armed
                        // for the same reason: the next statement writes
                        // local_edges[num_edges].
                        CRUCIBLE_FATAL_INVARIANT(num_edges < edge_cap_max_.get());
                        local_edges[num_edges++] = {.src = OpIndex{result.old_op.raw()},
                                                    .dst = OpIndex{i},
                                                    .src_port = result.old_port,
                                                    .dst_port = static_cast<uint8_t>(j),
                                                    .kind = EdgeKind::ALIAS,
                                                    .pad = 0};
                    }
                } else if (next_slot_raw < slot_cap) {
                    SlotId new_slot{next_slot_raw++};
                    auto& slot_info = local_slots[new_slot.raw()];
                    slot_info.birth_op = OpIndex{i};
                    slot_info.death_op = OpIndex{i};
                    slot_info.is_external = false;
                    slot_info.nbytes =
                        compute_storage_nbytes_det(external_tensor_meta(te.output_metas[j])).peek().value();
                    slot_info.dtype = te.output_metas[j].dtype;
                    slot_info.device_type = te.output_metas[j].device_type;
                    slot_info.device_idx = te.output_metas[j].device_idx;
                    slot_info.layout = te.output_metas[j].layout;
                    te.output_slot_ids[j] = new_slot;
                    if (result.slot) result.slot->slot_id = new_slot;
                } else {
                    te.output_slot_ids[j] = SlotId{};
                }
            }
        }

        uint32_t num_slots = std::min(next_slot_raw, slot_cap);
        TensorSlot* slots = nullptr;
        if (num_slots > 0) {
            slots = arena.alloc_array<TensorSlot>(a, num_slots);
            static_assert(sizeof(SlotInfo) == 24);
            static_assert(offsetof(TensorSlot, nbytes) == 8);
            static_assert(offsetof(SlotInfo, nbytes) == 0);
            for (uint32_t slot_idx = 0; slot_idx < num_slots; slot_idx++) {
                // The offset is assigned later by compute_memory_plan.
                slots[slot_idx].offset_bytes = 0;
                std::memcpy(&slots[slot_idx].nbytes, &local_slots[slot_idx], sizeof(SlotInfo));
                slots[slot_idx].slot_id = SlotId{slot_idx};
                std::memset(slots[slot_idx].pad2, 0, sizeof(slots[slot_idx].pad2));
            }
        }

        auto* graph = alloc_trace_graph(a, arena);
        graph->ops = ops;
        graph->slots = slots;
        graph->num_slots.set(num_slots);
        graph->content_hash = content_fold.finish();
        graph->max_meta_end.set(max_meta_end);
        build_csr(a, arena, graph, local_edges, num_edges, count);

        return graph;
    }

public:
    // Orders birth and death events with an O(n + k) counting sort, then
    // assigns offsets with a sweep line.  The alignment is what the GPU
    // needs for coalesced access.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] MemoryPlan* compute_memory_plan(effects::Alloc a, TensorSlot* slots,
                                                                               uint32_t num_slots)
        CRUCIBLE_NO_THREAD_SAFETY {
        static constexpr uint32_t ALIGNMENT = 256;

        auto* plan = arena.alloc_obj<MemoryPlan>(a);
        plan->slots = slots;
        plan->num_slots = num_slots;
        plan->num_external = 0;
        plan->pool_bytes = 0;

        plan->rank = rank;
        plan->world_size = world_size;
        plan->device_capability = device_capability.value();
        plan->device_type = DeviceType::CPU;
        plan->device_idx = -1;
        std::memset(plan->pad0, 0, sizeof(plan->pad0));

        if (num_slots == 0) return plan;

        uint32_t max_op = 0;
        for (uint32_t s = 0; s < num_slots; s++) {
            if (slots[s].is_external) {
                plan->num_external++;
            } else {
                if (plan->device_type == DeviceType::CPU && slots[s].device_type != DeviceType::CPU) {
                    plan->device_type = slots[s].device_type;
                    plan->device_idx = slots[s].device_idx;
                }
                if (slots[s].death_op.raw() + 1 > max_op) max_op = slots[s].death_op.raw() + 1;
            }
        }

        uint32_t num_internal = num_slots - plan->num_external;
        if (num_internal == 0) return plan;

        // Bucket the slots by birth_op and by death_op + 1.  The sweep
        // range is [0, max_op].
        uint32_t num_ops = max_op + 1;
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

        // num_ops is at least one by construction: the early return above
        // guarantees at least one internal slot, and max_op was set from that
        // slot's death_op plus one.  num_ops + 1 cannot wrap either, because
        // death_op is a uint32_t and the increment already happened, so
        // max_op is at most UINT32_MAX - 1.  The analyzer cannot see that
        // chain, hence the assumptions.
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

        auto* born_slots = arena.alloc_array<uint32_t>(a, num_internal);
        auto* dead_slots = arena.alloc_array<uint32_t>(a, num_internal);
        // Cursors start as copies of the offsets and advance during scatter.
        auto* birth_cur = arena.alloc_array<uint32_t>(a, num_ops);
        auto* death_cur = arena.alloc_array<uint32_t>(a, num_ops);
        std::memcpy(birth_cur, birth_off, num_ops * sizeof(uint32_t));
        std::memcpy(death_cur, death_off, num_ops * sizeof(uint32_t));

        for (uint32_t s = 0; s < num_slots; s++) {
            if (slots[s].is_external) continue;
            born_slots[birth_cur[slots[s].birth_op.raw()]++] = s;
            uint32_t free_at = slots[s].death_op.raw() + 1;
            if (free_at < num_ops) dead_slots[death_cur[free_at]++] = s;
        }

        // The free list keeps sizes and offsets in separate arrays so the
        // first-fit scan touches only sizes.  Freeing appends, allocation
        // scans then swap-removes.  A sorted list with coalescing is the
        // alternative and it loses: it costs an insertion and a memmove per
        // operation, while the direct reuse matching below already captures
        // most same-size matches and keeps the list short.
        //
        // Neither array is initialized.  free_list_count bounds every read.
        constexpr uint32_t MAX_FREE = 4096;
        uint64_t free_list_sizes[MAX_FREE];
        uint64_t free_list_offsets[MAX_FREE];
        uint32_t free_list_count = 0;
        uint64_t pool_end = 0;

        auto free_block = [&](uint64_t offset, uint64_t size) {
            if (free_list_count < MAX_FREE) [[likely]] {
                free_list_offsets[free_list_count] = offset;
                free_list_sizes[free_list_count] = size;
                ++free_list_count;
            }
        };

        auto alloc_slot = [&](uint32_t s, uint64_t aligned_size) {
            for (uint32_t f = 0; f < free_list_count; f++) {
                if (free_list_sizes[f] >= aligned_size) {
                    slots[s].offset_bytes = free_list_offsets[f];
                    if (free_list_sizes[f] == aligned_size) {
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

        struct DyingInfo {
            uint64_t aligned_size = 0;
            uint64_t offset = 0;
        };
        constexpr uint32_t MAX_PER_OP = 64;

        for (uint32_t op = 0; op < num_ops; op++) {
            uint32_t dying_begin = death_off[op], dying_end = death_off[op + 1];
            uint32_t born_begin = birth_off[op], born_end = birth_off[op + 1];
            uint32_t num_dying = dying_end - dying_begin;
            uint32_t num_born = born_end - born_begin;

            // Match a dying slot to a born slot directly, bypassing the
            // free list entirely.
            uint32_t dying_count_capped = num_dying < MAX_PER_OP ? num_dying : MAX_PER_OP;
            uint32_t born_count_capped = num_born < MAX_PER_OP ? num_born : MAX_PER_OP;
            bool dying_consumed[MAX_PER_OP]{};
            bool born_assigned[MAX_PER_OP]{};

            if (dying_count_capped > 0 && born_count_capped > 0) {
                DyingInfo dying_slot_info[MAX_PER_OP];
                for (uint32_t i = 0; i < dying_count_capped; i++) {
                    uint32_t dying_slot_id = dead_slots[dying_begin + i];
                    dying_slot_info[i] = {.aligned_size =
                                              (slots[dying_slot_id].nbytes + ALIGNMENT - 1) & ~uint64_t(ALIGNMENT - 1),
                                          .offset = slots[dying_slot_id].offset_bytes};
                }

                for (uint32_t born_idx = 0; born_idx < born_count_capped; born_idx++) {
                    uint32_t born_slot_id = born_slots[born_begin + born_idx];
                    uint64_t born_aligned_size =
                        (slots[born_slot_id].nbytes + ALIGNMENT - 1) & ~uint64_t(ALIGNMENT - 1);

                    uint32_t best_dying_match_idx = UINT32_MAX;
                    uint64_t best_waste_bytes = UINT64_MAX;
                    for (uint32_t dying_idx = 0; dying_idx < dying_count_capped; dying_idx++) {
                        if (!dying_consumed[dying_idx]
                            && dying_slot_info[dying_idx].aligned_size >= born_aligned_size) {
                            uint64_t waste_bytes = dying_slot_info[dying_idx].aligned_size - born_aligned_size;
                            if (waste_bytes < best_waste_bytes) {
                                best_dying_match_idx = dying_idx;
                                best_waste_bytes = waste_bytes;
                            }
                        }
                    }
                    if (best_dying_match_idx != UINT32_MAX) {
                        slots[born_slot_id].offset_bytes = dying_slot_info[best_dying_match_idx].offset;
                        dying_consumed[best_dying_match_idx] = true;
                        born_assigned[born_idx] = true;
                        if (best_waste_bytes > 0)
                            free_block(dying_slot_info[best_dying_match_idx].offset + born_aligned_size,
                                       best_waste_bytes);
                    }
                }
            }

            for (uint32_t i = 0; i < num_dying; i++) {
                if (i < dying_count_capped && dying_consumed[i]) continue;
                uint32_t dying_slot_id = dead_slots[dying_begin + i];
                free_block(slots[dying_slot_id].offset_bytes,
                           (slots[dying_slot_id].nbytes + ALIGNMENT - 1) & ~uint64_t(ALIGNMENT - 1));
            }

            for (uint32_t i = 0; i < num_born; i++) {
                if (i < born_count_capped && born_assigned[i]) continue;
                uint32_t born_slot_id = born_slots[born_begin + i];
                alloc_slot(born_slot_id, (slots[born_slot_id].nbytes + ALIGNMENT - 1) & ~uint64_t(ALIGNMENT - 1));
            }
        }

        plan->pool_bytes = pool_end;
        return plan;
    }
};

}  // namespace crucible
