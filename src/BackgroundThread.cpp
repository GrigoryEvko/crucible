// The bodies of the cold and background members of crucible/BackgroundThread.h:
// the start and the stop of the pipeline thread, the thread body, the
// channels and the four pipeline stages, the trace graph build and the memory
// plan.
//
// Only this translation unit instantiates the pipeline: run_pipeline_ holds
// the body of run_in_row, over fixy::BgLoadCtx, the one context that admits
// the row of the pipeline.

#include <crucible/BackgroundThread.h>

#include <crucible/CKernel.h>
#include <crucible/SchemaTable.h>
#include <fixy/Refined.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Stage.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Lifetime.h>
#include <foundation/Saturate.h>
#include <foundation/permissions/Permission.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>

namespace crucible {

namespace detail::bg_pipeline {

// The user tags of the four channels of the background pipeline.
struct StartTag {};
struct TraceBatchTag {};
struct BuildWorkTag {};
struct GraphPublishTag {};

// One call site for the root of each channel.  Each run of the pipeline
// mints its four roots here, so the type of each channel names the brand
// of one site, and the stage functions can name the channel types.
[[nodiscard]] inline auto start_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::fixy::concurrent::spsc_tag::Whole<StartTag>>();
}
[[nodiscard]] inline auto trace_batch_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::fixy::concurrent::spsc_tag::Whole<TraceBatchTag>>();
}
[[nodiscard]] inline auto build_work_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::fixy::concurrent::spsc_tag::Whole<BuildWorkTag>>();
}
[[nodiscard]] inline auto graph_publish_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::fixy::concurrent::spsc_tag::Whole<GraphPublishTag>>();
}

}  // namespace detail::bg_pipeline

struct BackgroundThread::Pipeline {
    // The thread that runs run_in_row.  start() sets it, and stop() joins it.
    std::jthread thread;

    using StartChannel =
        ::fixy::concurrent::spsc_channel_t<BgPipelineStart, 1, decltype(detail::bg_pipeline::start_root())>;
    using TraceBatchChannel =
        ::fixy::concurrent::spsc_channel_t<BgTraceBatch*, 64, decltype(detail::bg_pipeline::trace_batch_root())>;
    using BuildWorkChannel =
        ::fixy::concurrent::spsc_channel_t<BgBuildWork*, 64, decltype(detail::bg_pipeline::build_work_root())>;
    using GraphPublishChannel =
        ::fixy::concurrent::spsc_channel_t<BgGraphPublish*, 64, decltype(detail::bg_pipeline::graph_publish_root())>;

    // The consumer end of a stage, together with the thread the stage
    // serves.  Only run_pipeline_ builds one, from a channel end and the
    // thread object.
    template <class Consumer>
    class StageInput {
        Consumer inner_;
        BackgroundThread* owner_;

        StageInput(Consumer&& inner, BackgroundThread& owner) noexcept : inner_{std::move(inner)}, owner_{&owner} {}
        friend struct BackgroundThread;

    public:
        static constexpr std::size_t per_call_working_set = Consumer::per_call_working_set;
        // The pipeline joins this input to the output before it only when
        // the two name one channel and bind one channel instance, so the
        // input names and reports the channel of the consumer end that it
        // holds.
        using channel_type = typename Consumer::channel_type;

        [[nodiscard]] ::foundation::ChannelIdentity<channel_type> channel_identity() const noexcept {
            return inner_.channel_identity();
        }

        StageInput(StageInput&&) noexcept = default;
        StageInput(const StageInput&) = delete("a stage input owns the linear consumer end of its channel");
        StageInput& operator=(const StageInput&) = delete("a stage input owns the linear consumer end of its channel");
        StageInput& operator=(StageInput&&) = delete("a stage input binds to one channel for life");

        [[nodiscard]] decltype(std::declval<Consumer&>().try_pop()) try_pop() noexcept { return inner_.try_pop(); }

        [[nodiscard]] BackgroundThread& owner() const noexcept { return *owner_; }
    };

    using StartInput = StageInput<typename StartChannel::ConsumerHandle>;
    using TraceBatchInput = StageInput<typename TraceBatchChannel::ConsumerHandle>;
    using BuildWorkInput = StageInput<typename BuildWorkChannel::ConsumerHandle>;
    using GraphPublishInput = StageInput<typename GraphPublishChannel::ConsumerHandle>;

    // Also carries the owner to the publish stage from its first
    // iteration, so the stage can serve the owner mailbox before any
    // region arrives.
    struct BgSinkProducerHandle {
        BackgroundThread* owner = nullptr;

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

    // The four stages of the pipeline, in the order of its channels.
    //
    // The drain stage pops batches from the trace ring until a signal on
    // stop_requested, then pushes the last batch and the stop sentinel.
    static void DrainTraceRingFn(StartInput&& in, typename TraceBatchChannel::ProducerHandle&& out);

    // The detect stage feeds each entry to the iteration detector, cuts a
    // work item at each boundary, and pushes a commit marker after each
    // batch.  It clears its trace and bumps the reset epoch on a divergence
    // reset.
    static void DetectIterationFn(TraceBatchInput&& in, typename BuildWorkChannel::ProducerHandle&& out);

    // The build stage builds a trace graph from each work item and
    // forwards commit markers unchanged.  It forwards the stop sentinel, a
    // null BgBuildWork*, as a null BgGraphPublish*.
    //
    // The pipeline runs this body as the entry of its own thread, so the
    // body takes the background context of that thread from the door.
    static void BuildTraceFn(BuildWorkInput&& in, typename GraphPublishChannel::ProducerHandle&& out);

    // The publish stage publishes each graph whose reset epoch is current,
    // releases each stale one, and advances total_processed at each commit
    // marker.
    //
    // The channel is SPSC FIFO, so a commit marker can only arrive here
    // after every region produced from its batch and from preceding batches
    // has gone through publish_trace_graph.  A completed flush therefore
    // implies every region is published.
    //
    // This stage owns the state the region callback writes.  It builds a
    // publish-stage proof for each call, serves the owner mailbox before
    // every pop, and closes the mailbox on its way out, so a job posted to
    // it runs here between two publications and never beside one.
    //
    // The pipeline runs this body as the entry of its own thread, so the
    // body takes the background context of that thread from the door.
    static void MakeRegionFn(GraphPublishInput&& in, BgSinkProducerHandle&& out);
};

BackgroundThread::BackgroundThread() = default;

BackgroundThread::BgBuildWork* BackgroundThread::make_commit_work(uint32_t count) {
    auto* work = new BgBuildWork{};
    work->commit_only = true;
    work->commit_count = count;
    return work;
}

BackgroundThread::BgBuildWork* BackgroundThread::prepare_iteration_build_work() {
    uint32_t total = static_cast<uint32_t>(current_trace.size());
    const uint32_t iter_len = detector.last_completed_len;

    const uint32_t warmup =
        ::foundation::sat::sub_sat(::foundation::sat::sub_sat(total, IterationDetector::K), iter_len);
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
        total = ::foundation::sat::sub_sat(total, warmup);
    }

    const uint32_t completed_len = ::foundation::sat::sub_sat(total, IterationDetector::K);
    last_iteration_length = completed_len;
    iterations_completed.bump();

    BgBuildWork* work = nullptr;
    if (meta_log && completed_len > 0) {
        work = new BgBuildWork{};
        work->completed_len = completed_len;
        work->epoch = reset_epoch.get();
        work->trace.assign(current_trace.begin(), current_trace.begin() + completed_len);
        work->meta_starts.assign(current_meta_starts.begin(), current_meta_starts.begin() + completed_len);
        work->scope_hashes.assign(current_scope_hashes.begin(), current_scope_hashes.begin() + completed_len);
        work->callsite_hashes.assign(current_callsite_hashes.begin(), current_callsite_hashes.begin() + completed_len);
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

void BackgroundThread::discard_trace_graph(TraceGraph* graph) {
    if (!graph) return;
    const uint32_t max_meta_end = graph->max_meta_end.get_assuming_set();
    if (max_meta_end > 0) {
        meta_log.get()->advance_tail(max_meta_end);
    }
}

void BackgroundThread::publish_trace_graph(::foundation::effects::Bg const& bg, PublishStage const& stage,
                                           TraceGraph* graph) {
    if (!graph) return;

    const uint32_t num_ops = graph->num_ops.get_assuming_set();
    const uint32_t num_slots = graph->num_slots.get_assuming_set();
    const uint32_t max_meta_end = graph->max_meta_end.get_assuming_set();

    // The gate covers the arena bump cursor and nothing else.  The
    // region-ready callback stays outside it: that callback commits a
    // transaction, flips the mode cell and reads sampled counters, and
    // inside the gate the build stage would sleep for all of it.
    //
    // Nothing after this scope touches the arena.  recompute_merkle
    // walks nodes that already exist, advance_tail moves a counter in
    // the metadata log, and the queue below is heap storage the publish
    // stage owns alone.
    RegionNode* region = nullptr;
    with_arena_alloc_gate_(::fixy::BgLoadCtx{bg}, [&]() noexcept {
        region = make_region(bg.alloc, arena, graph->ops, num_ops, graph->content_hash);
        if (graph->slots && num_slots > 0) {
            region->plan = compute_memory_plan(bg.alloc, graph->slots, num_slots);
        }
    });

    if (max_meta_end > 0) {
        meta_log.get()->advance_tail(max_meta_end);
    }

    recompute_merkle(region);
    uncompiled_regions.push(region);

    active_region.store(region, std::memory_order_release);
    if (region_ready_cb) {
        region_ready_cb(bg, stage, region);
    }
}

void BackgroundThread::Pipeline::DrainTraceRingFn(StartInput&& in, typename TraceBatchChannel::ProducerHandle&& out) {
    BackgroundThread* const owner = &in.owner();
    while (!in.try_pop()) {
        CRUCIBLE_SPIN_PAUSE;
    }

    auto batch = std::make_unique<BgTraceBatch>();

    while (true) {
        if (owner->stop_requested.peek()) break;

        batch->count = owner->ring.get()->try_pop_batch(batch->entries, batch->meta_starts, batch->scope_hashes,
                                                        batch->callsite_hashes, BATCH_SIZE);

        if (batch->count == 0) {
            CRUCIBLE_SPIN_PAUSE;
            continue;
        }

        push_pipeline(out, batch.get());
        batch.release();
        batch = std::make_unique<BgTraceBatch>();
    }

    batch->count = owner->ring.get()->try_pop_batch(batch->entries, batch->meta_starts, batch->scope_hashes,
                                                    batch->callsite_hashes, BATCH_SIZE);
    if (batch->count > 0) {
        push_pipeline(out, batch.get());
        batch.release();
    }

    BgTraceBatch* stop = nullptr;
    push_pipeline(out, stop);
}

void BackgroundThread::Pipeline::DetectIterationFn(TraceBatchInput&& in,
                                                   typename BuildWorkChannel::ProducerHandle&& out) {
    BackgroundThread* const owner = &in.owner();
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
    }
}

void BackgroundThread::Pipeline::BuildTraceFn(BuildWorkInput&& in, typename GraphPublishChannel::ProducerHandle&& out) {
    const ::fixy::BgLoadCtx ctx{::foundation::effects::host::BackgroundOwner::mint_background_context()};
    BackgroundThread* const owner = &in.owner();

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

        if (work->commit_only) {
            // Forward the marker downstream so total_processed advances
            // only after every preceding graph has been published.
            // Bumping here instead races the publish stage: the
            // foreground can see the counter catch up, dispatch, and find
            // no pending region, or an active region whose plan is still
            // being computed.
            auto publish = std::make_unique<BgGraphPublish>();
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
        owner->with_arena_alloc_gate_(ctx, [&]() noexcept {
            graph = owner->build_trace_from(ctx.cap().alloc, work->completed_len, work->trace.data(),
                                            work->meta_starts.data(), work->scope_hashes.data(),
                                            work->callsite_hashes.data());
        });

        // Forwarding only well-formed graphs keeps the downstream
        // contract at "a non-null graph is publishable".
        if (!graph) continue;

        auto publish = std::make_unique<BgGraphPublish>();
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
void BackgroundThread::Pipeline::MakeRegionFn(GraphPublishInput&& in, BgSinkProducerHandle&& out) {
    const ::foundation::effects::Bg bg = ::foundation::effects::host::BackgroundOwner::mint_background_context();
    BackgroundThread* const owner = &in.owner();
    CRUCIBLE_ASSERT(out.owner == owner);
    const OwnerMailboxScope mailbox{*owner, PublishStage{}};

    while (true) {
        owner->serve_owner_mailbox_(PublishStage{});
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

        owner->publish_trace_graph(bg, PublishStage{}, publish->graph);
    }
}

void BackgroundThread::start(TraceRing* ring_ptr, MetaLog* meta_log_ptr, int32_t rank_, int32_t world_size_,
                             uint64_t device_cap) {
    global_schema_table().seal();
    global_ckernel_table().value()->seal();
    ring.set(ring_ptr);
    meta_log.set(meta_log_ptr);
    rank = rank_;
    world_size = world_size_;
    device_capability = ::fixy::mint_tagged<::fixy::tags::source::Meridian>(device_cap);
    reserve_iteration_buffers_();
    stop_requested.reset_in_quiescent_context(::fixy::handle::OneShotFlag::QuiescenceProof{});
    // The lambda is the entry of the pipeline thread, so it takes the
    // background context of that thread from the door.
    if (!pipeline_) {
        pipeline_ = std::make_unique<Pipeline>();
    }
    pipeline_->thread = std::jthread([this](std::stop_token) noexcept {
        run_in_row(::fixy::BgLoadCtx{::foundation::effects::host::BackgroundOwner::mint_background_context()});
    });
}

void BackgroundThread::stop() {
    stop_requested.signal();
    if (pipeline_ && pipeline_->thread.joinable()) {
        pipeline_->thread.join();
    }
}

BackgroundThread::~BackgroundThread() { stop(); }

void BackgroundThread::run_pipeline_(::fixy::BgLoadCtx const& ctx) noexcept {
    namespace perm = ::foundation::permissions;

    // Channel flow is
    //   Start → DrainTraceRing → TraceBatch → DetectIteration →
    //          BuildWork → BuildTrace → GraphPublish → MakeRegion → Sink
    // Each adjacent pair is one typed channel, and each stage's
    // permission proof comes from splitting a fresh root permission for
    // its channel.  The root carries the brand that the channel names.
    Pipeline::TraceBatchChannel trace_batches;
    Pipeline::BuildWorkChannel build_work;
    Pipeline::GraphPublishChannel graph_publish;

    Pipeline::StartChannel start;
    auto [start_prod_perm, start_cons_perm] =
        perm::mint_permission_split<typename Pipeline::StartChannel::producer_tag,
                                    typename Pipeline::StartChannel::consumer_tag>(detail::bg_pipeline::start_root());

    auto [trace_prod_perm, trace_cons_perm] =
        perm::mint_permission_split<typename Pipeline::TraceBatchChannel::producer_tag,
                                    typename Pipeline::TraceBatchChannel::consumer_tag>(
            detail::bg_pipeline::trace_batch_root());

    auto [build_prod_perm, build_cons_perm] =
        perm::mint_permission_split<typename Pipeline::BuildWorkChannel::producer_tag,
                                    typename Pipeline::BuildWorkChannel::consumer_tag>(
            detail::bg_pipeline::build_work_root());

    auto [publish_prod_perm, publish_cons_perm] =
        perm::mint_permission_split<typename Pipeline::GraphPublishChannel::producer_tag,
                                    typename Pipeline::GraphPublishChannel::consumer_tag>(
            detail::bg_pipeline::graph_publish_root());

    auto start_prod = start.producer(std::move(start_prod_perm));
    auto start_cons = Pipeline::StartInput{start.consumer(std::move(start_cons_perm)), *this};
    auto trace_prod = trace_batches.producer(std::move(trace_prod_perm));
    auto trace_cons = Pipeline::TraceBatchInput{trace_batches.consumer(std::move(trace_cons_perm)), *this};
    auto build_prod = build_work.producer(std::move(build_prod_perm));
    auto build_cons = Pipeline::BuildWorkInput{build_work.consumer(std::move(build_cons_perm)), *this};
    auto publish_prod = graph_publish.producer(std::move(publish_prod_perm));
    auto publish_cons = Pipeline::GraphPublishInput{graph_publish.consumer(std::move(publish_cons_perm)), *this};

    while (!start_prod.try_push(BgPipelineStart{})) {
        CRUCIBLE_SPIN_PAUSE;
    }
    auto drain_stage =
        ::fixy::concurrent::mint_stage<&Pipeline::DrainTraceRingFn>(ctx, std::move(start_cons), std::move(trace_prod));
    auto detect_stage =
        ::fixy::concurrent::mint_stage<&Pipeline::DetectIterationFn>(ctx, std::move(trace_cons), std::move(build_prod));
    auto build_stage =
        ::fixy::concurrent::mint_stage<&Pipeline::BuildTraceFn>(ctx, std::move(build_cons), std::move(publish_prod));
    auto publish_stage = ::fixy::concurrent::mint_stage<&Pipeline::MakeRegionFn>(ctx, std::move(publish_cons),
                                                                                 Pipeline::BgSinkProducerHandle{this});

    auto pipeline = ::fixy::concurrent::mint_pipeline(ctx, std::move(drain_stage), std::move(detect_stage),
                                                      std::move(build_stage), std::move(publish_stage));
    std::move(pipeline).run(ctx);
}

void BackgroundThread::ensure_scratch_buffers(uint32_t total_inputs, uint32_t total_outputs) {
    // Power-of-two capacity at a load factor below one half.  The unique
    // pointer count is the output count plus a small external fraction.
    uint32_t raw_map_size =
        std::max(MIN_PTR_MAP_CAP,
                 ::foundation::sat::mul_sat(::foundation::sat::add_sat(total_outputs, uint32_t{256}), uint32_t{2}));
    uint32_t needed_map = (raw_map_size >= MAX_PTR_MAP_CAP) ? MAX_PTR_MAP_CAP : std::bit_ceil(raw_map_size);

    // Unique storages are bounded by the outputs plus external headroom.
    uint32_t needed_slots = std::max(MIN_SCRATCH_SLOT_CAP, ::foundation::sat::add_sat(total_outputs, uint32_t{1024}));

    // Data-flow edges are bounded by the inputs, alias edges by the
    // outputs.
    uint32_t needed_edges = std::max(MIN_SCRATCH_EDGE_CAP, ::foundation::sat::add_sat(total_inputs, total_outputs));

    if (needed_map > map_cap_.get()) {
        map_cap_.advance(needed_map);
        ptr_mask_ = map_cap_.get() - 1;
        scratch_map_ = ::foundation::AlignedBuffer<PtrSlot>::allocate_value_initialized(map_cap_.get());
        // The fresh buffer is zeroed, so generation zero is unused.
        map_gen_ = 0;
    }

    if (needed_slots > slot_cap_max_.get()) {
        slot_cap_max_.advance(needed_slots);
        scratch_slots_ = ::foundation::AlignedBuffer<SlotInfo>::allocate_value_initialized(slot_cap_max_.get());
    }

    if (needed_edges > edge_cap_max_.get()) {
        edge_cap_max_.advance(needed_edges);
        scratch_edges_ = ::foundation::AlignedBuffer<Edge>::allocate_value_initialized(edge_cap_max_.get());
    }
}

TraceGraph* BackgroundThread::build_trace_from(::foundation::effects::Alloc a, uint32_t count,
                                               const TraceRing::Entry* trace_data, const MetaIndex* meta_data,
                                               const ScopeHash* scope_data, const CallsiteHash* callsite_data) {
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
            if (max_meta_end > 0) meta_log.get()->advance_tail(max_meta_end);
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
        meta_base = meta_log.get()->try_contiguous(first_meta, total_metas);
        if (!meta_base) [[unlikely]] {
            meta_base = arena.alloc_array<TensorMeta>(a, total_metas);
            for (uint32_t meta_idx = 0; meta_idx < total_metas; meta_idx++)
                meta_base[meta_idx] = meta_log.get()->at(first_meta + meta_idx);
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
            ? static_cast<char*>(arena.alloc(a, ::fixy::mint_refined<::fixy::positive>(aux_bytes),
                                             ::fixy::mint_refined<::fixy::power_of_two>(size_t{alignof(int64_t)})))
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
            te.scalar_args = (n_scalars > 0)
                               ? ::foundation::lifetime::start_as_array<int64_t>(aux_cursor, n_scalars).data()
                               : nullptr;
            aux_cursor += n_scalars * sizeof(int64_t);
            te.input_trace_indices = ::foundation::lifetime::start_as_array<OpIndex>(aux_cursor, n_in).data();
            aux_cursor += n_in * sizeof(OpIndex);
            te.input_slot_ids = ::foundation::lifetime::start_as_array<SlotId>(aux_cursor, n_in).data();
            aux_cursor += n_in * sizeof(SlotId);
            te.output_slot_ids = ::foundation::lifetime::start_as_array<SlotId>(aux_cursor, n_out).data();
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
                slot_info.nbytes = compute_storage_nbytes_det(external_tensor_meta(te.input_metas[j])).peek().value();
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
                slot_info.nbytes = compute_storage_nbytes_det(external_tensor_meta(te.output_metas[j])).peek().value();
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

MemoryPlan* BackgroundThread::compute_memory_plan(::foundation::effects::Alloc a, TensorSlot* slots,
                                                  uint32_t num_slots) {
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
                uint64_t born_aligned_size = (slots[born_slot_id].nbytes + ALIGNMENT - 1) & ~uint64_t(ALIGNMENT - 1);

                uint32_t best_dying_match_idx = UINT32_MAX;
                uint64_t best_waste_bytes = UINT64_MAX;
                for (uint32_t dying_idx = 0; dying_idx < dying_count_capped; dying_idx++) {
                    if (!dying_consumed[dying_idx] && dying_slot_info[dying_idx].aligned_size >= born_aligned_size) {
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
                        free_block(dying_slot_info[best_dying_match_idx].offset + born_aligned_size, best_waste_bytes);
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

}  // namespace crucible
