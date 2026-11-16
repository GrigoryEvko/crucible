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
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/PermissionedSpscBoxChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/os/Futex.h>
#include <fixy/os/OwnedThread.h>
#include <fixy/os/Time.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/permissions/Permission.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
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

// The idle wait of a stage.  A stage with no work polls SPIN_ROUNDS times,
// and then it parks until work comes.  The foreground puts ops into the trace
// ring with no system call, so no thread wakes the drain stage.  It sleeps
// with a bound, and it polls the ring again after each sleep.  Each later
// stage parks on the wait word of its input channel, and the stage before it
// wakes it after each push.  A caller fills the owner mailbox with no wake,
// so the park of the publish stage has a bound too.  A sleep starts at
// MIN_SLEEP_NANOS, and each sleep with no work between doubles the bound up
// to the longest sleep of the stage.
//
// The drain stage sleeps only after a spin that found no op, so a steady
// stream of records never meets a sleep.  A burst that starts in a sleep
// fills the ring until the drain stage runs again.  A foreground that records
// one op in each 10 ns puts 20000 ops into the ring in RING_MAX_SLEEP_NANOS,
// less than a third of the CAPACITY of the ring.  But the scheduler can run
// the drain stage later than its deadline when each core has work.  A burst
// of more than CAPACITY ops then meets a full ring, and the ring refuses the
// ops past it.  A refused op is the degrade path of the record.  The op leaves
// no mark in the ring, so the stream that the detect stage reads has a gap.
// The pipeline builds no region that holds the gap, and the whole iterations
// after it give regions again (test_background_thread_refused_ops).
inline constexpr uint32_t SPIN_ROUNDS = 2048;
inline constexpr uint64_t MIN_SLEEP_NANOS = 50000;
// The longest sleep of the drain stage, and of a producer that waits for a
// free cell of a channel.
inline constexpr uint64_t RING_MAX_SLEEP_NANOS = 200000;
// The longest timed park of the publish stage.  It bounds only the wait of a
// job in the owner mailbox.
inline constexpr uint64_t MAILBOX_MAX_SLEEP_NANOS = 1000000;

// The futex calls of the pipeline use words of the process that the pipeline
// owns, so the kernel refuses a call only in a broken process.
[[nodiscard]] inline ::fixy::Option<::fixy::futex::Deadline> deadline_after(::fixy::BgLoadCtx const& ctx,
                                                                            uint64_t nanos) noexcept {
    const auto clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
    const ::foundation::effects::cap::Nondet nondet =
        ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx);
    return ::fixy::some(::fixy::futex::Deadline::after(clock, nondet, nanos)
                            .expect("the monotonic clock gives the deadline of a park"));
}

// The wait word of one channel of the pipeline.  The consumer parks on it
// when the channel is empty, and the producer wakes it after each push.  The
// two sides change the word with a read-modify-write, so their changes have
// one order.  A push whose exchange comes after the exchange of the consumer
// reads PARKED and wakes the consumer.  The item of a push whose exchange
// comes before is visible to the test of the consumer after its exchange.
// So no wake is lost.  The producer stage closes its channel before its last
// wake (close_and_wake), and the test of the consumer reads the closed state
// too, so the end of the stream ends a park in the same way.  Each word has
// its own cache line.
class alignas(64) ChannelPark {
public:
    // The producer calls it after each push.  It makes a system call only
    // when the consumer parks.
    void signal(::fixy::BgLoadCtx const& ctx) noexcept {
        if (word_.exchange_acq_rel(IDLE) == PARKED) [[unlikely]] {
            (void)::fixy::futex::wake_all(ctx, word_).expect("the kernel wakes a parked stage of the pipeline");
        }
    }

    // The consumer calls it when the channel of input is empty.  It tests the
    // channel again after the mark.  The park ends at a wake, at the
    // deadline, or at once when the word changed after the mark.
    template <class Input>
    void park(::fixy::BgLoadCtx const& ctx, Input const& input,
              ::fixy::Option<::fixy::futex::Deadline> deadline) noexcept {
        (void)word_.exchange_acq_rel(PARKED);
        if (!input.has_item_or_end()) {
            (void)::fixy::futex::wait(ctx, word_, PARKED, std::move(deadline))
                .expect("the kernel parks a stage of the pipeline on its own word");
        }
        word_.store_release(IDLE);
    }

private:
    static constexpr uint32_t IDLE = 0;
    static constexpr uint32_t PARKED = 1;

    ::fixy::futex::FutexWord word_{IDLE};
};

// The wait words of the three channels that a stage parks on.  The run of
// the pipeline holds them beside its channels.
struct ChannelParks {
    ChannelPark trace_batches{};
    ChannelPark build_work{};
    ChannelPark graph_publish{};
};

// The idle state of one stage: the polls since the last work, the bound of
// the next sleep, and the longest sleep.
class StageIdle {
public:
    explicit StageIdle(uint64_t max_sleep_nanos) noexcept : max_sleep_nanos_{max_sleep_nanos} {}

    // The stage found work.
    void reset() noexcept {
        rounds_ = 0;
        sleep_nanos_ = MIN_SLEEP_NANOS;
    }

    // A poll found no work in the ring or no free cell in a channel.  The
    // stage spins, and then it sleeps for the bound.
    void pause_or_sleep(::fixy::BgLoadCtx const& ctx) noexcept {
        if (should_spin_()) return;
        const ::fixy::futex::FutexWord sleep_word{0};
        (void)::fixy::futex::wait(ctx, sleep_word, 0, deadline_after(ctx, next_sleep_nanos_()))
            .expect("the kernel parks a stage of the pipeline on its own word");
    }

    // A poll found the channel of input empty.  The stage spins, and then it
    // parks on the word of the channel.  A timed park ends at the bound.
    template <class Input>
    void pause_or_park(::fixy::BgLoadCtx const& ctx, ChannelPark& park, Input const& input, bool is_timed) noexcept {
        if (should_spin_()) return;
        park.park(ctx, input, is_timed ? deadline_after(ctx, next_sleep_nanos_()) : ::fixy::none);
    }

private:
    [[nodiscard]] bool should_spin_() noexcept {
        if (rounds_ == SPIN_ROUNDS) return false;
        ++rounds_;
        CRUCIBLE_SPIN_PAUSE;
        return true;
    }

    [[nodiscard]] uint64_t next_sleep_nanos_() noexcept {
        const uint64_t nanos = sleep_nanos_;
        sleep_nanos_ = ::fixy::min(nanos * 2, max_sleep_nanos_);
        return nanos;
    }

    uint32_t rounds_ = 0;
    uint64_t sleep_nanos_ = MIN_SLEEP_NANOS;
    uint64_t max_sleep_nanos_ = MIN_SLEEP_NANOS;
};

// True when no start set the slot, or when the slot holds object.  The first
// start sets the ring and the metadata log of the pipeline.  A start after
// stop() continues the stream of the last pipeline, so it must name the same
// ring and the same log: the ring holds the ops that the last pipeline did
// not read, and the cursor of the detect stage is a position in that log.
template <class T>
[[nodiscard]] bool is_unset_or_holds(::fixy::WriteOnceNonNull<T*> const& slot, T const& object) noexcept {
    return !slot.has_value() || ::fixy::same_object(::fixy::to(*slot), ::fixy::to(object));
}

}  // namespace detail::bg_pipeline

struct BackgroundThread::Pipeline {
    // True after the first start().  Only the thread that owns the
    // BackgroundThread reads and writes it.
    bool has_started = false;
    // The thread that runs run_in_row, while one runs.  start() sets it, and
    // stop() takes it and joins it.  It is the last data member, so it
    // joins first.
    ::fixy::Option<::fixy::thread::OwnedThread> thread{};

    using StartChannel =
        ::fixy::concurrent::spsc_channel_t<BgPipelineStart, 1, decltype(detail::bg_pipeline::start_root())>;
    // A Box owns each message of the three channels after the start, and the
    // channel moves the Box from one stage to the next (THE OWNERS of
    // fixy/concurrent/PermissionedSpscBoxChannel.h).
    using TraceBatchChannel =
        ::fixy::concurrent::spsc_box_channel_t<BgTraceBatch, 64, decltype(detail::bg_pipeline::trace_batch_root())>;
    using BuildWorkChannel =
        ::fixy::concurrent::spsc_box_channel_t<BgBuildWork, 64, decltype(detail::bg_pipeline::build_work_root())>;
    using GraphPublishChannel =
        ::fixy::concurrent::spsc_box_channel_t<BgGraphPublish, 64, decltype(detail::bg_pipeline::graph_publish_root())>;

    // The consumer end of a stage, together with the thread the stage
    // serves, the context of the run and the wait words of the channels.
    // Only run_pipeline_ builds one, from a channel end, the thread object,
    // its context and its wait words.  A stage parks and wakes through the
    // context, so the drain stage and the detect stage take no context from
    // the door.
    template <class Consumer>
    class StageInput {
        Consumer inner_;
        ::fixy::Ref<BackgroundThread> owner_;
        ::fixy::BgLoadCtx ctx_;
        ::fixy::Ref<detail::bg_pipeline::ChannelParks> parks_;

        StageInput(Consumer&& inner, BackgroundThread& owner, ::fixy::BgLoadCtx const& ctx,
                   detail::bg_pipeline::ChannelParks& parks) noexcept
            : inner_{std::move(inner)}, owner_{::fixy::to(owner)}, ctx_{ctx}, parks_{::fixy::to(parks)} {}
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

        // The oldest item, NoItem::Empty, or NoItem::Closed after the last
        // item of a stream that the stage before this one ended.
        [[nodiscard]] decltype(std::declval<Consumer&>().try_receive()) try_receive() noexcept {
            return inner_.try_receive();
        }

        // True when the channel holds an item, or when its stream ended.  A
        // test after the mark of a park sees each item of a push and the end
        // of each close whose wake the mark could miss.  The snapshot takes
        // the tag of Nondet from the load context.
        [[nodiscard]] bool has_item_or_end() const noexcept {
            return !inner_.empty_approx(
                       ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx_))
                || inner_.is_closed();
        }

        [[nodiscard]] BackgroundThread& owner() const noexcept { return *owner_; }
        [[nodiscard]] ::fixy::BgLoadCtx const& ctx() const noexcept { return ctx_; }
        [[nodiscard]] detail::bg_pipeline::ChannelParks& parks() const noexcept { return *parks_; }
    };

    using StartInput = StageInput<typename StartChannel::ConsumerHandle>;
    using TraceBatchInput = StageInput<typename TraceBatchChannel::ConsumerHandle>;
    using BuildWorkInput = StageInput<typename BuildWorkChannel::ConsumerHandle>;
    using GraphPublishInput = StageInput<typename GraphPublishChannel::ConsumerHandle>;

    // Also carries the owner to the publish stage from its first
    // iteration, so the stage can serve the owner mailbox before any
    // region arrives.
    // The sink takes the end mark of the pipeline by value, and it takes
    // each mark at its first push.
    struct BgSinkProducerHandle {
        ::fixy::WeakRef<BackgroundThread> owner{};

        [[nodiscard]] bool try_push(BgPipelineDone const& /*done*/) noexcept { return true; }
    };

    // Pushes the Box into the channel of producer, and wakes the consumer when
    // it parks on park.  A full channel means that its consumer works, and
    // its consumer gives no wake for a free cell.  A full channel gives the
    // Box back, so the producer spins, and then it sleeps with a growing
    // bound, until the push goes in.
    template <class Producer, class T>
    static void push_pipeline(::fixy::BgLoadCtx const& ctx, detail::bg_pipeline::ChannelPark& park, Producer& producer,
                              ::fixy::Box<T>&& value) {
        detail::bg_pipeline::StageIdle full{detail::bg_pipeline::RING_MAX_SLEEP_NANOS};
        ::fixy::Box<T> pending = std::move(value);
        for (;;) {
            ::fixy::Option<::fixy::Box<T>> kept = producer.try_push(std::move(pending));
            if (kept.is_none()) break;
            pending = std::move(kept).expect("a full channel gives the Box back");
            full.pause_or_sleep(ctx);
        }
        park.signal(ctx);
    }

    // Ends the producer handle of a stage, which closes its channel (THE END
    // OF THE STREAM of fixy/concurrent/ChannelEnd.h), and then wakes the
    // consumer.  The wake comes after the close, so a consumer that parks
    // after its last receive reads the end.
    template <class Producer>
    static void close_and_wake(::fixy::BgLoadCtx const& ctx, detail::bg_pipeline::ChannelPark& park,
                               Producer&& out) noexcept {
        ::foundation::linear::drop(std::move(out));
        park.signal(ctx);
    }

    // True when a receive found the end of the stream.
    template <class T>
    [[nodiscard]] static bool is_end_of_stream(::fixy::Result<T, ::fixy::concurrent::NoItem> const& received) noexcept {
        return received.err().value_or(::fixy::concurrent::NoItem::Empty) == ::fixy::concurrent::NoItem::Closed;
    }

    // One idle step of a stage whose input channel is empty: a poll of the
    // spin, or a park on the word of the channel.  A timed park ends at the
    // bound of idle.
    template <class Input>
    static void wait_for_item(Input const& in, detail::bg_pipeline::ChannelPark& park,
                              detail::bg_pipeline::StageIdle& idle, bool is_timed) noexcept {
        idle.pause_or_park(in.ctx(), park, in, is_timed);
    }

    // The four stages of the pipeline, in the order of its channels.
    //
    // The drain stage pops batches from the trace ring until a signal on
    // stop_requested, then pushes the last batch and closes its channel.
    // Each later stage ends at the end of its input stream, and it closes
    // its own channel then.
    static void DrainTraceRingFn(StartInput&& in, typename TraceBatchChannel::ProducerHandle&& out);

    // The detect stage feeds each entry to the iteration detector, cuts a
    // work item at each boundary, and pushes a commit marker after each
    // batch.  It applies each divergence reset at the point of the
    // divergence in the op stream.
    static void DetectIterationFn(TraceBatchInput&& in, typename BuildWorkChannel::ProducerHandle&& out);

    // The build stage builds a trace graph from each work item and
    // forwards commit markers unchanged.  A work item that met the overflow
    // of the metadata log sends nothing.
    //
    // The pipeline runs this body as the entry of its own thread, so the
    // body takes the background context of that thread from the door.
    static void BuildTraceFn(BuildWorkInput&& in, typename GraphPublishChannel::ProducerHandle&& out);

    // The publish stage publishes each region whose reset point is the
    // requested point, discards each stale one, and at each commit marker
    // releases the metadata log to the floor of the marker and advances
    // total_processed.  It is the one writer of the tail of the metadata log.
    //
    // The channel is SPSC FIFO, so a commit marker can only arrive here
    // after every region produced from its batch and from preceding batches
    // has gone through publish_region.  A completed flush therefore
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

// ignore_regions_ comes before region_ready_ in the class, so it is built
// before the Ref of it.
BackgroundThread::BackgroundThread(::foundation::effects::Alloc allocation)
    : region_ready_{::fixy::to(ignore_regions_)}, pipeline_{::fixy::mint_box<Pipeline>(allocation)} {}

BackgroundThread::BackgroundThread(::foundation::effects::Alloc allocation, RegionReadyCall const& on_region_ready)
    : region_ready_{::fixy::to(on_region_ready)}, pipeline_{::fixy::mint_box<Pipeline>(allocation)} {}

::fixy::Box<BackgroundThread::BgBuildWork> BackgroundThread::make_commit_work(::foundation::effects::Alloc alloc,
                                                                              uint32_t count) {
    ::fixy::Box<BgBuildWork> work = ::fixy::mint_box<BgBuildWork>(alloc);
    work->commit_only = true;
    work->commit_count = count;
    work->meta_floor = meta_floor_();
    return work;
}

// The foreground appends the records of an op before its ring entry, so a
// ring that refuses the entry leaves records that no op names.  A later
// valid index is then past the cursor, and the records between the two are
// dead.  The floor passes them with the next op.
uint64_t BackgroundThread::take_record_start_(TraceRing::Entry const& entry, MetaIndex meta_start) noexcept {
    if (!meta_start.is_valid()) return meta_cursor_;
    CRUCIBLE_FATAL_INVARIANT(meta_start.raw() >= meta_cursor_);
    meta_cursor_ = meta_start.raw() + entry.num_inputs + entry.num_outputs;
    return meta_start.raw();
}

// The ring grows to the next power of two, and the oldest held op moves to
// slot zero.  The detect stage drops each op that no future work can hold,
// so the count stays at most half of the history of the detector, K ops more
// and the op that the detect stage adds before it drops.
void BackgroundThread::PendingOps::grow_(::foundation::effects::Alloc alloc) {
    const uint64_t new_capacity = mask_ == 0 ? MIN_CAPACITY : (mask_ + 1) * 2;
    CRUCIBLE_FATAL_INVARIANT(new_capacity <= IterationDetector::HISTORY_MAX_CAPACITY);
    ::fixy::MaskedArray<TraceRing::Entry> entries = ::fixy::mint_masked_array<TraceRing::Entry>(alloc, new_capacity);
    ::fixy::MaskedArray<MetaIndex> meta_starts = ::fixy::mint_masked_array<MetaIndex>(alloc, new_capacity);
    ::fixy::MaskedArray<uint64_t> record_starts = ::fixy::mint_masked_array<uint64_t>(alloc, new_capacity);
    ::fixy::MaskedArray<ScopeHash> scope_hashes = ::fixy::mint_masked_array<ScopeHash>(alloc, new_capacity);
    ::fixy::MaskedArray<CallsiteHash> callsite_hashes = ::fixy::mint_masked_array<CallsiteHash>(alloc, new_capacity);
    ::fixy::MaskedArray<OpDepth> depths = ::fixy::mint_masked_array<OpDepth>(alloc, new_capacity);
    for (uint64_t index = 0; index < count_; ++index) {
        entries.at_masked(index) = entry(index);
        meta_starts.at_masked(index) = meta_start(index);
        record_starts.at_masked(index) = record_start(index);
        scope_hashes.at_masked(index) = scope_hash(index);
        callsite_hashes.at_masked(index) = callsite_hash(index);
        depths.at_masked(index) = depth(index);
    }
    entries_ = std::move(entries);
    meta_starts_ = std::move(meta_starts);
    record_starts_ = std::move(record_starts);
    scope_hashes_ = std::move(scope_hashes);
    callsite_hashes_ = std::move(callsite_hashes);
    depths_ = std::move(depths);
    mask_ = new_capacity - 1;
    head_ = 0;
}

void BackgroundThread::PendingOps::push(::foundation::effects::Alloc alloc, TraceRing::Entry const& entry,
                                        MetaIndex meta_start, uint64_t record_start, ScopeHash scope_hash,
                                        CallsiteHash callsite_hash, OpDepth depth) {
    if (mask_ == 0 || count_ == mask_ + 1) [[unlikely]]
        grow_(alloc);
    const uint64_t slot = head_ + count_;
    entries_.at_masked(slot) = entry;
    meta_starts_.at_masked(slot) = meta_start;
    record_starts_.at_masked(slot) = record_start;
    scope_hashes_.at_masked(slot) = scope_hash;
    callsite_hashes_.at_masked(slot) = callsite_hash;
    depths_.at_masked(slot) = depth;
    ++count_;
}

void BackgroundThread::drop_unclaimable_ops_() noexcept {
    const uint64_t first_held = detector.pos_ - pending_ops_.size();
    const uint64_t oldest_claimable = detector.oldest_claimable();
    if (oldest_claimable > first_held) pending_ops_.drop_oldest(oldest_claimable - first_held);
}

::fixy::Option<::fixy::Box<BackgroundThread::BgBuildWork>>
BackgroundThread::prepare_iteration_build_work(::foundation::effects::Alloc alloc) {
    uint32_t total = static_cast<uint32_t>(pending_ops_.size());
    const uint32_t iter_len = detector.last_completed_len;

    const uint32_t warmup = ::fixy::saturating_sub(::fixy::saturating_sub(total, IterationDetector::K), iter_len);
    pending_ops_.drop_oldest(warmup);
    total = ::fixy::saturating_sub(total, warmup);

    const uint32_t completed_len = ::fixy::saturating_sub(total, IterationDetector::K);
    (void)iterations_completed.bump();

    ::fixy::Option<::fixy::Box<BgBuildWork>> built{::fixy::none};
    if (meta_log && completed_len > 0) {
        ::fixy::Box<BgBuildWork> work = ::fixy::mint_box<BgBuildWork>(alloc);
        work->reset_point = applied_reset_.peek_relaxed();
        work->numerics = stream_numerics_;
        // Each buffer takes the oldest completed_len held ops, in order.
        PendingOps const& held = pending_ops_;
        work->trace = ::fixy::mint_heap_array_with<TraceRing::Entry>(
            alloc, completed_len, [&held](size_t index) noexcept { return held.entry(index); });
        work->meta_starts = ::fixy::mint_heap_array_with<MetaIndex>(
            alloc, completed_len, [&held](size_t index) noexcept { return held.meta_start(index); });
        work->scope_hashes = ::fixy::mint_heap_array_with<ScopeHash>(
            alloc, completed_len, [&held](size_t index) noexcept { return held.scope_hash(index); });
        work->callsite_hashes = ::fixy::mint_heap_array_with<CallsiteHash>(
            alloc, completed_len, [&held](size_t index) noexcept { return held.callsite_hash(index); });
        work->depths = ::fixy::mint_heap_array_with<OpDepth>(
            alloc, completed_len, [&held](size_t index) noexcept { return held.depth(index); });
        built = ::fixy::some(std::move(work));
    }

    // The last K ops begin the next iteration, and the ring keeps them.
    pending_ops_.drop_oldest(completed_len);
    return built;
}

// The detect stage reads the request before the op at its point, because the
// producer requests before it puts that op into the ring (request_restart and
// request_mark).  So an unapplied point is never behind the count of ops read.
// A newer request replaces an older one that the detect stage did not reach,
// and a restart at the newer point also clears the ops between the two
// points.  A mark keeps the held ops: they belong to the same stream, and
// only the regions cut before the mark are stale.  Each applied reset reads
// the numerics of the newest request (request_numerics), so each work cut
// after it carries them.
void BackgroundThread::apply_due_reset_() noexcept {
    const StreamPoint requested = requested_reset_.get();
    if (requested == applied_reset_.peek_relaxed()) return;
    CRUCIBLE_FATAL_INVARIANT(requested.ops_before() >= stream_ops_);
    if (requested.ops_before() != stream_ops_) return;
    if (requested.restarts_detector()) {
        detector.restart_after_divergence();
        pending_ops_.clear();
    }
    stream_numerics_ = requested_numerics_.load_acquire();
    applied_reset_.advance(requested);
}

// The floor is the position of a held op or the cursor of the detect stage,
// and the detect stage read each record before the cursor after the producer
// published it.  So a floor past the head means that the positions disagree
// with the log, and a tail past the head would give the producer slots that
// it did not write.
void BackgroundThread::release_meta_log(PublishStage const&, uint64_t meta_floor) {
    MetaLog& log = *meta_log.get();
    if (meta_floor <= log.tail.get()) return;
    CRUCIBLE_FATAL_INVARIANT(meta_floor <= log.head.get());
    log.advance_tail(meta_floor);
}

// The node and the plan go into buffers of the region, and the arrays of the
// sweep into the scratch of the publish stage, whose epoch ends in this call.
// The memory goes to a slot of the region store before the region becomes
// visible, so each region that a holder can name has an owner.  A store with
// no free slot discards the region, as a reset discards a stale region.
void BackgroundThread::publish_region(::foundation::effects::Bg const& bg, PublishStage const& stage, BuiltRegion parts,
                                      StreamPoint reset_point) {
    ::fixy::Option<RegionStoreSlot> free_slot{};
    for (const RegionStoreSlot slot : region_store_.indices()) {
        if (region_store_.at(slot).node.size() == 0) {
            free_slot = ::fixy::Option<RegionStoreSlot>::some(slot);
            break;
        }
    }
    if (free_slot.is_none()) [[unlikely]] {
        ++regions_dropped_full_;
        return;
    }
    RegionMemory& memory = region_store_.at(std::move(free_slot).expect("the test above found a free slot"));
    memory = RegionMemory{
        .parts = std::move(parts),
        .node = ::fixy::mint_heap_array_with<RegionNode>(bg.alloc, 1, [](size_t) noexcept { return RegionNode{}; }),
        .plan = {},
        .published = {},
        .left_generation = NOT_LEFT};
    RegionNode& region = *memory.node.get(0).expect("the region memory holds one node");
    init_region_node(region, memory.parts.ops.view(), memory.parts.content_hash, memory.parts.numerics);
    const uint32_t num_slots = static_cast<uint32_t>(memory.parts.slots.size());
    if (num_slots > 0) {
        memory.plan =
            ::fixy::mint_heap_array_with<MemoryPlan>(bg.alloc, 1, [](size_t) noexcept { return MemoryPlan{}; });
        MemoryPlan& plan_storage = *memory.plan.get(0).expect("the region memory holds one plan");
        // The plan lives in the memory of the region, outside the epoch, so
        // the body writes its link and the result of the epoch holds none.
        // The sweep gives a Ref of the storage of the plan.
        publish_scratch_.with_epoch([&]() noexcept {
            region.plan = PlanLink::some(
                compute_memory_plan(bg.alloc, plan_storage, publish_scratch_, memory.parts.slots.view()));
        });
    }

    recompute_merkle(region);
    const ::fixy::WeakRef<RegionNode> published{region};
    last_published_ = ::fixy::WeakRef<const RegionNode>{region};

    memory.published = PublishedRegion{.region = published, .reset_point = reset_point};

    uncompiled_regions.push(published.get());
    active_region.store_release(published);
    owner_marks_ = RegionMarks{};
    // The callback gets a proof of its own, which dies with the call.
    (*region_ready_)(bg, PublishStage{}, memory.published);
    sweep_regions_(stage);
}

// The sweep releases a region when no holder names it.  The last published
// region and the region in the slot stay.  With a foreground, each other
// region also waits for a write of the foreground holders after the sweep
// that found the region out of the view of the foreground.  That write names
// each region that the foreground can still read, so the foreground reads no
// region that the write does not name.  With no foreground, the sweep
// releases a region with no holder at once.  Complexity: O(S * H) for S slots
// and H held regions.
void BackgroundThread::sweep_regions_(PublishStage const&) noexcept {
    for (RegionMemory& memory : region_store_) {
        if (memory.node.size() == 0) continue;
        const ::fixy::WeakRef<const RegionNode> region{*memory.node.get(0).expect("a used slot holds its node")};
        if (region == last_published_ || region == owner_marks_.handed) {
            memory.left_generation = NOT_LEFT;
            continue;
        }
        if (owner_marks_.has_foreground && memory.left_generation == NOT_LEFT) {
            memory.left_generation = owner_marks_.foreground_generation;
            continue;
        }
        if (is_held_(region)) continue;
        if (owner_marks_.has_foreground && owner_marks_.foreground_generation <= memory.left_generation) continue;
        memory = RegionMemory{};
    }
}

bool BackgroundThread::is_held_(::fixy::WeakRef<const RegionNode> region) const noexcept {
    for (const ::fixy::WeakRef<const RegionNode> held :
         owner_marks_.held.view().window(0, owner_marks_.count).expect("the marks hold at most CAP regions")) {
        if (held == region) return true;
    }
    const uint32_t queued = uncompiled_regions.size();
    for (uint32_t age = 0; age < queued; ++age) {
        if (::fixy::WeakRef<const RegionNode>{uncompiled_regions.at(age).get()} == region) return true;
    }
    return false;
}

void BackgroundThread::keep_region(PublishStage const&, ::fixy::WeakRef<const RegionNode> region) noexcept {
    for (RegionMemory& memory : region_store_) {
        if (memory.node.size() == 0) continue;
        if (::fixy::WeakRef<const RegionNode>{*memory.node.get(0).expect("a used slot holds its node")} == region) {
            memory.left_generation = NOT_LEFT;
            return;
        }
    }
}

uint32_t BackgroundThread::region_count(PublishStage const&) const noexcept {
    uint32_t count = 0;
    for (const RegionMemory& memory : region_store_) {
        if (memory.node.size() != 0) ++count;
    }
    return count;
}

void BackgroundThread::Pipeline::DrainTraceRingFn(StartInput&& in, typename TraceBatchChannel::ProducerHandle&& out) {
    BackgroundThread& owner = in.owner();
    detail::bg_pipeline::ChannelPark& next = in.parks().trace_batches;
    while (in.try_pop().is_none()) {
        CRUCIBLE_SPIN_PAUSE;
    }

    ::foundation::effects::Alloc const alloc = in.ctx().cap().alloc;
    ::fixy::Box<BgTraceBatch> batch = ::fixy::mint_box<BgTraceBatch>(alloc);
    detail::bg_pipeline::StageIdle idle{detail::bg_pipeline::RING_MAX_SLEEP_NANOS};

    // A stop request does not wake a sleep of this stage, so the stage reads
    // the request at the end of its sleep, at most RING_MAX_SLEEP_NANOS later.
    while (true) {
        if (owner.stop_requested.peek()) break;

        batch->count = owner.ring.get()->try_pop_batch(batch->entries.view(), batch->meta_starts.view(),
                                                       batch->scope_hashes.view(), batch->callsite_hashes.view(),
                                                       batch->depths.view());

        if (batch->count == 0) {
            idle.pause_or_sleep(in.ctx());
            continue;
        }
        idle.reset();

        push_pipeline(in.ctx(), next, out, std::move(batch));
        batch = ::fixy::mint_box<BgTraceBatch>(alloc);
    }

    batch->count =
        owner.ring.get()->try_pop_batch(batch->entries.view(), batch->meta_starts.view(), batch->scope_hashes.view(),
                                        batch->callsite_hashes.view(), batch->depths.view());
    if (batch->count > 0) {
        push_pipeline(in.ctx(), next, out, std::move(batch));
    }

    close_and_wake(in.ctx(), next, std::move(out));
}

void BackgroundThread::Pipeline::DetectIterationFn(TraceBatchInput&& in,
                                                   typename BuildWorkChannel::ProducerHandle&& out) {
    BackgroundThread& owner = in.owner();
    detail::bg_pipeline::ChannelParks& parks = in.parks();
    // The park of this stage has no deadline, so the stage reads no bound.
    detail::bg_pipeline::StageIdle idle{detail::bg_pipeline::RING_MAX_SLEEP_NANOS};
    ::foundation::effects::Alloc const alloc = in.ctx().cap().alloc;
    while (true) {
        ::fixy::Result<::fixy::Box<BgTraceBatch>, ::fixy::concurrent::NoItem> received = in.try_receive();
        if (received.is_err()) {
            if (is_end_of_stream(received)) break;
            wait_for_item(in, parks.trace_batches, idle, false);
            continue;
        }
        idle.reset();

        // The channel gives the batch in a Box, which ends it after the
        // loop.  The stage reads the ops through this reference.
        ::fixy::Box<BgTraceBatch> const batch = std::move(received).expect("the test above found an item");
        BgTraceBatch const& ops = *batch;

        // The drain stage fills at most BATCH_SIZE ops.  The one test of the
        // count proves the slot of each op, so the loop checks no slot.
        const uint32_t op_count = ops.count;
        if (op_count > BATCH_SIZE) [[unlikely]] {
            ::fixy::fatal("a trace batch holds {} ops, more than BATCH_SIZE", op_count);
        }
        for (uint32_t i = 0; i < op_count; ++i) {
            const BatchSlot slot = BatchSlot::check(i).expect("the test of the count bounds each slot");
            const TraceRing::Entry& entry = ops.entries.at(slot);
            owner.apply_due_reset_();
            ++owner.stream_ops_;

            owner.pending_ops_.push(alloc, entry, ops.meta_starts.at(slot),
                                    owner.take_record_start_(entry, ops.meta_starts.at(slot)),
                                    ops.scope_hashes.at(slot), ops.callsite_hashes.at(slot), ops.depths.at(slot));

            if (owner.detector.check(alloc, entry.schema_hash, entry.shape_hash)) {
                ::fixy::Option<::fixy::Box<BgBuildWork>> work = owner.prepare_iteration_build_work(alloc);
                if (work.is_some()) {
                    push_pipeline(in.ctx(), parks.build_work, out, std::move(work).expect("the test above found work"));
                }
            }
            owner.drop_unclaimable_ops_();
        }
        // A reset at the end of the batch applies now, so the detector holds
        // no ops of the trace that diverged while it waits for the next op.
        owner.apply_due_reset_();

        push_pipeline(in.ctx(), parks.build_work, out, owner.make_commit_work(alloc, ops.count));
    }
    close_and_wake(in.ctx(), parks.build_work, std::move(out));
}

void BackgroundThread::Pipeline::BuildTraceFn(BuildWorkInput&& in, typename GraphPublishChannel::ProducerHandle&& out) {
    const ::fixy::BgLoadCtx ctx{::foundation::effects::host::BackgroundOwner::mint_background_context()};
    BackgroundThread& owner = in.owner();
    detail::bg_pipeline::ChannelParks& parks = in.parks();
    // The park of this stage has no deadline, so the stage reads no bound.
    detail::bg_pipeline::StageIdle idle{detail::bg_pipeline::RING_MAX_SLEEP_NANOS};

    while (true) {
        ::fixy::Result<::fixy::Box<BgBuildWork>, ::fixy::concurrent::NoItem> received = in.try_receive();
        if (received.is_err()) {
            if (is_end_of_stream(received)) break;
            wait_for_item(in, parks.build_work, idle, false);
            continue;
        }
        idle.reset();

        ::fixy::Box<BgBuildWork> const work = std::move(received).expect("the test above found an item");

        if (work->commit_only) {
            // Forward the marker downstream so total_processed advances
            // only after every preceding graph has been published.
            // Bumping here instead races the publish stage: the
            // foreground can see the counter catch up, dispatch, and find
            // no pending region, or an active region whose plan is still
            // being computed.
            ::fixy::Box<BgGraphPublish> publish = ::fixy::mint_box<BgGraphPublish>(ctx.cap().alloc);
            publish->kind = BgGraphPublish::Kind::Commit;
            publish->commit_count = work->commit_count;
            publish->meta_floor = work->meta_floor;
            publish->reset_point = work->reset_point;
            push_pipeline(ctx, parks.graph_publish, out, std::move(publish));
            continue;
        }

        // A work cut before the requested reset holds ops recorded before
        // it, so its region would be stale, and the build stage skips it.
        // The publish stage checks the region again, because a request can
        // come while the build stage is mid-graph.  The commit markers carry
        // the floor of the metadata log, so a skipped work releases nothing
        // of its own.
        if (work->reset_point != owner.requested_reset()) continue;
        (void)owner.works_built_.bump();

        // One epoch of the build scratch covers this work item.  The graph
        // and its CSR arrays die at its end, so only the region leaves the
        // epoch.  The region owns its buffers and does not point into the
        // scratch.  A work that the build refuses (BuildRefusal) builds no
        // region.
        ::fixy::Option<BuiltRegion> built{::fixy::none};
        owner.build_scratch_.with_epoch([&]() noexcept {
            owner
                .build_trace_from(ctx.cap().alloc, owner.build_scratch_, work->trace.view(), work->meta_starts.view(),
                                  work->scope_hashes.view(), work->callsite_hashes.view(), work->depths.view(),
                                  work->numerics)
                .match([&](BuiltTrace trace) noexcept { built = ::fixy::some(std::move(trace.region)); },
                       [](BuildRefusal /*refusal*/) noexcept {});
        });

        // A refused work sends no region.  The floor of a later commit marker
        // releases its records.
        if (built.is_none()) {
            continue;
        }

        BuiltRegion region = std::move(built).expect("the build made a region");

        push_pipeline(
            ctx, parks.graph_publish, out,
            ::fixy::mint_box<BgGraphPublish>(ctx.cap().alloc, BgGraphPublish{.kind = BgGraphPublish::Kind::Region,
                                                                             .region = std::move(region),
                                                                             .reset_point = work->reset_point}));
    }
    close_and_wake(ctx, parks.graph_publish, std::move(out));
}

// Keeping the publish stage separate means the publish-side delegate
// never sees the build-side machinery, so a crash-stop during publish
// cannot taint the build stage's owner pointer.
void BackgroundThread::Pipeline::MakeRegionFn(GraphPublishInput&& in, BgSinkProducerHandle&& out) {
    const ::foundation::effects::Bg bg = ::foundation::effects::host::BackgroundOwner::mint_background_context();
    BackgroundThread& owner = in.owner();
    CRUCIBLE_FATAL_INVARIANT(out.owner.try_get() == &owner);
    const OwnerMailboxScope mailbox{owner, PublishStage{}};
    detail::bg_pipeline::StageIdle idle{detail::bg_pipeline::MAILBOX_MAX_SLEEP_NANOS};

    while (true) {
        owner.serve_owner_mailbox_(PublishStage{});
        ::fixy::Result<::fixy::Box<BgGraphPublish>, ::fixy::concurrent::NoItem> received = in.try_receive();
        if (received.is_err()) {
            if (is_end_of_stream(received)) {
                // The sink takes each value at its first push.
                const bool is_taken = out.try_push(BgPipelineDone{});
                CRUCIBLE_FATAL_INVARIANT(is_taken);
                return;
            }
            // A job in the owner mailbox gives no wake, so the park is
            // timed, and the stage serves the mailbox after each park.
            wait_for_item(in, in.parks().graph_publish, idle, true);
            continue;
        }
        idle.reset();

        ::fixy::Box<BgGraphPublish> publish = std::move(received).expect("the test above found an item");

        if (publish->kind == BgGraphPublish::Kind::Commit) {
            // The marker sits strictly after every publish message from
            // the same and earlier batches, so the release and the bump
            // happen after every publish above.  A reset does not drop a
            // marker, so its floor releases records on each side of a
            // reset.  The release comes first, so a caller that sees the
            // count after a flush sees the tail too.  The bump's release
            // pairs with the foreground's acquire load.
            owner.release_meta_log(PublishStage{}, publish->meta_floor);
            (void)PublishStageAuth::commit(owner.total_processed, publish->commit_count);
            continue;
        }

        // The producer requested a reset at a point after the ops of this
        // region, so the region describes the shape that failed to replay.
        // Publishing it would hand that shape back to a foreground that has
        // already fallen back to recording.
        //
        // The stale check sits here rather than at the build stage, so it
        // also catches a reset requested while the build stage was
        // mid-graph.  A request can come between this check and the
        // publication, so the foreground does the same check when it takes
        // a region.  The region memory frees when its parts end here.
        BuiltRegion region = std::move(publish->region);
        const StreamPoint reset_point = publish->reset_point;
        if (reset_point != owner.requested_reset()) {
            continue;
        }

        owner.publish_region(bg, PublishStage{}, std::move(region), reset_point);
    }
}

void BackgroundThread::set_numerics(NumericsHash numerics) noexcept {
    CRUCIBLE_PRE(!pipeline_->thread.is_some());
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(numerics));
    CRUCIBLE_PRE(!numerics.is_sentinel());
    numerics_ = numerics;
    stream_numerics_ = numerics;
    requested_numerics_.store_release(numerics);
}

void BackgroundThread::start(::fixy::InitLoadCtx const& startup, TraceRing& trace_ring, MetaLog& trace_meta_log,
                             int32_t rank_, int32_t world_size_, uint64_t device_cap) {
    CRUCIBLE_PRE_MSG(!pipeline_->thread.is_some(), "a start while the pipeline thread runs: stop the pipeline first");
    CRUCIBLE_PRE_MSG(detail::bg_pipeline::is_unset_or_holds(ring, trace_ring),
                     "a start after stop names the trace ring of the first start");
    CRUCIBLE_PRE_MSG(detail::bg_pipeline::is_unset_or_holds(meta_log, trace_meta_log),
                     "a start after stop names the metadata log of the first start");
    global_schema_table().seal();
    global_ckernel_table().value()->seal();
    if (!ring.has_value()) ring.set(&trace_ring);
    if (!meta_log.has_value()) meta_log.set(&trace_meta_log);
    rank = rank_;
    world_size = world_size_;
    device_capability = ::fixy::mint_tagged<::fixy::tags::source::Meridian>(device_cap);
    stop_requested.reset_in_quiescent_context(::fixy::handle::OneShotFlag::QuiescenceProof{});
    if (!pipeline_->has_started) {
        pipeline_->has_started = true;
        // The first op of the ring owns the first record that the log has
        // not released.  A later start keeps the cursor, because the ring
        // keeps the ops that the last pipeline did not read.
        meta_cursor_ = meta_log.get()->tail.get();
    }
    // The lambda is the entry of the pipeline thread, so it takes the
    // background context of that thread from the door.  The process ends
    // when the host has no resource for one more thread.
    pipeline_->thread = ::fixy::Option<::fixy::thread::OwnedThread>::some(
        ::fixy::thread::mint_owned_thread(startup, ::fixy::thread::ThreadOptions{}, [this]() noexcept {
            run_in_row(::fixy::BgLoadCtx{::foundation::effects::host::BackgroundOwner::mint_background_context()});
        }).expect("the thread door starts the pipeline thread"));
}

void BackgroundThread::stop() {
    stop_requested.signal();
    std::move(pipeline_->thread)
        .match([](::fixy::thread::OwnedThread thread) noexcept { std::move(thread).join(); }, []() noexcept {});
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
    // The wait words of the three channels.  They live as long as the
    // channels, and each stage ends before this frame does.
    detail::bg_pipeline::ChannelParks parks{};

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
    auto start_cons = Pipeline::StartInput{start.consumer(std::move(start_cons_perm)), *this, ctx, parks};
    auto trace_prod = trace_batches.producer(std::move(trace_prod_perm));
    auto trace_cons = Pipeline::TraceBatchInput{trace_batches.consumer(std::move(trace_cons_perm)), *this, ctx, parks};
    auto build_prod = build_work.producer(std::move(build_prod_perm));
    auto build_cons = Pipeline::BuildWorkInput{build_work.consumer(std::move(build_cons_perm)), *this, ctx, parks};
    auto publish_prod = graph_publish.producer(std::move(publish_prod_perm));
    auto publish_cons =
        Pipeline::GraphPublishInput{graph_publish.consumer(std::move(publish_cons_perm)), *this, ctx, parks};

    while (!start_prod.try_push(BgPipelineStart{})) {
        CRUCIBLE_SPIN_PAUSE;
    }
    auto drain_stage =
        ::fixy::concurrent::mint_stage<&Pipeline::DrainTraceRingFn>(ctx, std::move(start_cons), std::move(trace_prod));
    auto detect_stage =
        ::fixy::concurrent::mint_stage<&Pipeline::DetectIterationFn>(ctx, std::move(trace_cons), std::move(build_prod));
    auto build_stage =
        ::fixy::concurrent::mint_stage<&Pipeline::BuildTraceFn>(ctx, std::move(build_cons), std::move(publish_prod));
    auto publish_stage = ::fixy::concurrent::mint_stage<&Pipeline::MakeRegionFn>(
        ctx, std::move(publish_cons), Pipeline::BgSinkProducerHandle{::fixy::WeakRef<BackgroundThread>{*this}});

    auto pipeline = ::fixy::concurrent::mint_pipeline(ctx, std::move(drain_stage), std::move(detect_stage),
                                                      std::move(build_stage), std::move(publish_stage));
    std::move(pipeline).run(ctx);
}

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
        total_scalars += ::fixy::min(re.num_scalar_args, uint16_t{5});
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
        meta_log.get()->copy_run(first_meta.raw(), meta_buffer.view());
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
        CRUCIBLE_FATAL_INVARIANT(re.num_scalar_args <= 5);
        const uint16_t n_scalars = re.num_scalar_args;

        // The scalars of each op go into its entry, also for an op with no
        // tensors, so they fold into the content hash.
        const ::fixy::View<int64_t> scalars =
            scalar_run.window(scalar_cursor, n_scalars).expect("the scan counted the scalars of each op");
        scalar_cursor += n_scalars;
        ::fixy::copy(scalars, re.scalar_values.view().window(0, n_scalars).expect("an entry stores five scalars"))
            .expect("the two runs hold the scalars of the op");
        te.set_scalar_args(scalars);
        // The tags of the copied slots.  The check above keeps the count at
        // most five, so the shift stays below the 16 bits of the tags.
        te.scalar_types =
            static_cast<uint16_t>(re.packed_scalar_types() & ((uint32_t{1} << (kScalarTypeBits * n_scalars)) - 1));

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

            // The key of a storage is the address of its first byte, and the
            // allocator can give that address to a new storage after a free.
            // So a key in the map names the same storage only when the op
            // shows that the storage lives across it: the op reads the
            // storage, which keeps it alive during the op (an in-place op,
            // an out= op, a view or an alias of an input); the output is a
            // view, which never owns a new storage; or an op in the nest of
            // the op, or the op itself, wrote it, as addmm writes the result
            // that linear returns.  Each other output at a known key is a new
            // storage at the address of a dead one, and takes a slot of its
            // own.  The mutable flag of the op tells nothing more: an in-place
            // or out= op reads the tensor that it writes, and an op such as
            // batch norm writes its running statistics in place and returns
            // new tensors.  An input at the address of a dead storage still
            // reads the slot of the dead storage, because a read shows no
            // allocation.
            const bool is_written_in_the_nest =
                entry && entry->op_index.is_valid() && entry->op_index.raw() >= nest_start;
            const bool is_same_storage = entry && entry->slot_id.is_valid()
                                      && (entry->reader == OpIndex{i} || (output_meta.flags & meta_flags::IS_VIEW) != 0
                                          || is_written_in_the_nest);
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

::fixy::Ref<MemoryPlan> BackgroundThread::compute_memory_plan(::foundation::effects::Alloc a, MemoryPlan& plan_storage,
                                                              ::fixy::BumpArena& scratch,
                                                              ::fixy::View<TensorSlot> slots) noexcept {
    static constexpr uint32_t ALIGNMENT = 256;

    MemoryPlan& plan = plan_storage;
    plan.slots = slots;
    const uint32_t num_slots = plan.num_slots();
    plan.num_external = 0;
    plan.pool_bytes = 0;

    plan.rank = rank;
    plan.world_size = world_size;
    plan.device_capability = device_capability.value();
    plan.device_type = DeviceType::CPU;
    plan.device_idx = -1;
    ::fixy::fill(plan.pad0.view(), uint8_t{0});

    if (num_slots == 0) return ::fixy::to(plan);

    // The slot at an index of the sweep.  Each index of the sweep is a
    // position in the slots, so it is below their count.
    const auto slot_at = [slots](uint32_t index) noexcept -> TensorSlot& {
        return *slots.get(index).expect("an index of the sweep is below the count of the slots");
    };

    // The caller gives the slots, so the lifetime of each internal slot is
    // checked here, where the slots enter the sweep.  The sweep indexes its
    // arrays by birth_op and by death_op + 1, and it sizes them from the
    // largest death_op + 2.  A birth after the death indexes past the
    // arrays.  A death above MAX_DEATH_OP, also the none() value of
    // OpIndex, makes the sizes wrap.  An external slot keeps its own
    // allocation, and the sweep does not read its lifetime.
    static constexpr uint32_t MAX_DEATH_OP = UINT32_MAX - 3;
    uint32_t max_op = 0;
    for (const TensorSlot& slot : slots) {
        if (slot.is_external) {
            plan.num_external++;
        } else {
            CRUCIBLE_FATAL_INVARIANT(slot.birth_op.raw() <= slot.death_op.raw());
            CRUCIBLE_FATAL_INVARIANT(slot.death_op.raw() <= MAX_DEATH_OP);
            if (plan.device_type == DeviceType::CPU && slot.device_type != DeviceType::CPU) {
                plan.device_type = slot.device_type;
                plan.device_idx = slot.device_idx;
            }
            if (slot.death_op.raw() + 1 > max_op) max_op = slot.death_op.raw() + 1;
        }
    }

    uint32_t num_internal = num_slots - plan.num_external;
    if (num_internal == 0) return ::fixy::to(plan);

    // Bucket the slots by birth_op and by death_op + 1.  The sweep range
    // is [0, max_op].  The checks above keep max_op in [1, UINT32_MAX - 2],
    // so num_ops and num_ops + 1 are positive and do not wrap.
    uint32_t num_ops = max_op + 1;
    const ::fixy::View<uint32_t> birth_count = scratch.array<uint32_t>(a, num_ops, 0);
    const ::fixy::View<uint32_t> death_count = scratch.array<uint32_t>(a, num_ops, 0);

    for (const TensorSlot& slot : slots) {
        if (slot.is_external) continue;
        ++detail::csr_element_(birth_count, slot.birth_op.raw());
        uint32_t free_at = slot.death_op.raw() + 1;
        if (free_at < num_ops) ++detail::csr_element_(death_count, free_at);
    }

    // The first offset of each array is the zero of the fill.
    const ::fixy::View<uint32_t> birth_off = scratch.array<uint32_t>(a, num_ops + 1, 0);
    const ::fixy::View<uint32_t> death_off = scratch.array<uint32_t>(a, num_ops + 1, 0);
    for (uint32_t o = 0; o < num_ops; o++) {
        detail::csr_element_(birth_off, o + 1) =
            detail::csr_element_(birth_off, o) + detail::csr_element_(birth_count, o);
        detail::csr_element_(death_off, o + 1) =
            detail::csr_element_(death_off, o) + detail::csr_element_(death_count, o);
    }

    const ::fixy::View<uint32_t> born_slots = scratch.array<uint32_t>(a, num_internal, 0);
    const ::fixy::View<uint32_t> dead_slots = scratch.array<uint32_t>(a, num_internal, 0);
    // Cursors start as copies of the offsets and advance during scatter.
    const ::fixy::View<uint32_t> birth_cur = scratch.array<uint32_t>(a, num_ops, 0);
    const ::fixy::View<uint32_t> death_cur = scratch.array<uint32_t>(a, num_ops, 0);
    ::fixy::copy(birth_cur, birth_off.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");
    ::fixy::copy(death_cur, death_off.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");

    for (uint32_t s = 0; s < num_slots; s++) {
        const TensorSlot& slot = slot_at(s);
        if (slot.is_external) continue;
        detail::csr_element_(born_slots, detail::csr_element_(birth_cur, slot.birth_op.raw())++) = s;
        uint32_t free_at = slot.death_op.raw() + 1;
        if (free_at < num_ops) detail::csr_element_(dead_slots, detail::csr_element_(death_cur, free_at)++) = s;
    }

    // The free list keeps sizes and offsets in separate arrays so the
    // first-fit scan touches only sizes.  Freeing appends, allocation
    // scans then swap-removes.  A sorted list with coalescing is the
    // alternative and it loses: it costs an insertion and a memmove per
    // operation, while the direct reuse matching below already captures
    // most same-size matches and keeps the list short.
    //
    // free_list_count bounds every read.  A free list of MAX_FREE blocks
    // drops each further block.
    constexpr uint32_t MAX_FREE = 4096;
    using FreeSlot = ::fixy::Index<MAX_FREE>;
    ::fixy::FixedArray<uint64_t, MAX_FREE> free_list_sizes{};
    ::fixy::FixedArray<uint64_t, MAX_FREE> free_list_offsets{};
    uint32_t free_list_count = 0;
    uint64_t pool_end = 0;

    auto free_block = [&](uint64_t offset, uint64_t size) {
        for (const FreeSlot next : FreeSlot::check(free_list_count)) {
            free_list_offsets.at(next) = offset;
            free_list_sizes.at(next) = size;
            ++free_list_count;
        }
    };

    // The first fit of the scan of the sizes, then a swap with the last
    // block or a cut of the fit.  Complexity: O(free_list_count).
    auto alloc_slot = [&](uint32_t s, uint64_t aligned_size) {
        const ::fixy::Option<std::size_t> fit = ::fixy::find_if(
            free_list_sizes.view().window(0, free_list_count).expect("the free list holds at most MAX_FREE blocks"),
            [aligned_size](uint64_t size) noexcept { return size >= aligned_size; });
        for (const std::size_t position : fit) {
            const FreeSlot block = FreeSlot::check(position).expect("a position of the free list is below MAX_FREE");
            slot_at(s).offset_bytes = free_list_offsets.at(block);
            if (free_list_sizes.at(block) == aligned_size) {
                --free_list_count;
                const FreeSlot last = FreeSlot::check(free_list_count).expect("the last block is below MAX_FREE");
                free_list_sizes.at(block) = free_list_sizes.at(last);
                free_list_offsets.at(block) = free_list_offsets.at(last);
            } else {
                free_list_offsets.at(block) += aligned_size;
                free_list_sizes.at(block) -= aligned_size;
            }
            return;
        }
        slot_at(s).offset_bytes = pool_end;
        pool_end += aligned_size;
    };

    struct DyingInfo {
        uint64_t aligned_size = 0;
        uint64_t offset = 0;
    };
    constexpr uint32_t MAX_PER_OP = 64;
    using PerOpSlot = ::fixy::Index<MAX_PER_OP>;
    // The slot of a position below a capped count.  The cap keeps it below
    // MAX_PER_OP, so the optimizer removes the check.
    auto prove_per_op = [](uint32_t position) noexcept {
        return PerOpSlot::check(position).expect("a capped count is at most MAX_PER_OP");
    };
    // The bytes of a slot rounded up to the alignment of the pool.  The span
    // of a slot saturates, so its rounding saturates too, at the largest
    // multiple of the alignment, and no span wraps to a small slot.
    auto aligned_bytes_of = [](uint64_t nbytes) noexcept {
        return ::fixy::align_up(nbytes, uint64_t{ALIGNMENT}).value_or(~uint64_t{ALIGNMENT - 1});
    };

    for (uint32_t op = 0; op < num_ops; op++) {
        uint32_t dying_begin = detail::csr_element_(death_off, op);
        uint32_t dying_end = detail::csr_element_(death_off, op + 1);
        uint32_t born_begin = detail::csr_element_(birth_off, op);
        uint32_t born_end = detail::csr_element_(birth_off, op + 1);
        uint32_t num_dying = dying_end - dying_begin;
        uint32_t num_born = born_end - born_begin;

        // Match a dying slot to a born slot directly, bypassing the
        // free list entirely.
        uint32_t dying_count_capped = num_dying < MAX_PER_OP ? num_dying : MAX_PER_OP;
        uint32_t born_count_capped = num_born < MAX_PER_OP ? num_born : MAX_PER_OP;
        ::fixy::FixedArray<bool, MAX_PER_OP> dying_consumed{};
        ::fixy::FixedArray<bool, MAX_PER_OP> born_assigned{};

        if (dying_count_capped > 0 && born_count_capped > 0) {
            ::fixy::FixedArray<DyingInfo, MAX_PER_OP> dying_slot_info{};
            for (uint32_t i = 0; i < dying_count_capped; i++) {
                const TensorSlot& dying_slot = slot_at(detail::csr_element_(dead_slots, dying_begin + i));
                dying_slot_info.at(prove_per_op(i)) = {.aligned_size = aligned_bytes_of(dying_slot.nbytes),
                                                       .offset = dying_slot.offset_bytes};
            }

            for (uint32_t born_idx = 0; born_idx < born_count_capped; born_idx++) {
                TensorSlot& born_slot = slot_at(detail::csr_element_(born_slots, born_begin + born_idx));
                uint64_t born_aligned_size = aligned_bytes_of(born_slot.nbytes);

                uint32_t best_dying_match_idx = UINT32_MAX;
                uint64_t best_waste_bytes = UINT64_MAX;
                for (uint32_t dying_idx = 0; dying_idx < dying_count_capped; dying_idx++) {
                    const DyingInfo& dying_info = dying_slot_info.at(prove_per_op(dying_idx));
                    if (!dying_consumed.at(prove_per_op(dying_idx)) && dying_info.aligned_size >= born_aligned_size) {
                        uint64_t waste_bytes = dying_info.aligned_size - born_aligned_size;
                        if (waste_bytes < best_waste_bytes) {
                            best_dying_match_idx = dying_idx;
                            best_waste_bytes = waste_bytes;
                        }
                    }
                }
                if (best_dying_match_idx != UINT32_MAX) {
                    const PerOpSlot best_match = prove_per_op(best_dying_match_idx);
                    born_slot.offset_bytes = dying_slot_info.at(best_match).offset;
                    dying_consumed.at(best_match) = true;
                    born_assigned.at(prove_per_op(born_idx)) = true;
                    if (best_waste_bytes > 0)
                        free_block(dying_slot_info.at(best_match).offset + born_aligned_size, best_waste_bytes);
                }
            }
        }

        for (uint32_t i = 0; i < num_dying; i++) {
            if (i < dying_count_capped && dying_consumed.at(prove_per_op(i))) continue;
            const TensorSlot& dying_slot = slot_at(detail::csr_element_(dead_slots, dying_begin + i));
            free_block(dying_slot.offset_bytes, aligned_bytes_of(dying_slot.nbytes));
        }

        for (uint32_t i = 0; i < num_born; i++) {
            if (i < born_count_capped && born_assigned.at(prove_per_op(i))) continue;
            uint32_t born_slot_id = detail::csr_element_(born_slots, born_begin + i);
            alloc_slot(born_slot_id, aligned_bytes_of(slot_at(born_slot_id).nbytes));
        }
    }

    plan.pool_bytes = pool_end;
    return ::fixy::to(plan);
}

}  // namespace crucible
