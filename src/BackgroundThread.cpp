// The bodies of the cold and background members of crucible/BackgroundThread.h:
// the start and the stop of the pipeline thread, the thread body, the
// channels and the four pipeline stages.  The trace graph build is in
// src/BackgroundThreadTraceBuild.cpp, and the memory plan is in
// src/BackgroundThreadMemoryPlan.cpp.
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
#include <fixy/concurrent/EventCount.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/concurrent/StageNet.h>
#include <fixy/os/Futex.h>
#include <fixy/os/OwnedThread.h>
#include <fixy/os/SpinBudget.h>
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
    return ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, StartTag>>();
}
[[nodiscard]] inline auto trace_batch_root() noexcept {
    return ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, TraceBatchTag>>();
}
[[nodiscard]] inline auto build_work_root() noexcept {
    return ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, BuildWorkTag>>();
}
[[nodiscard]] inline auto graph_publish_root() noexcept {
    return ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, GraphPublishTag>>();
}

// The idle wait of a stage.  A stage with no work polls for SPIN_BUDGET, and
// then it parks until work comes.  The budget is a time, so it is the same on
// each core: a PAUSE takes from about 10 cycles to about 140 cycles across
// x86_64 cores.  80 microseconds are about 2048 pauses of an AMD Zen 4 core.
// The end of the spin decides only when the stage parks, and each item comes
// out of its channel in the same order.  The foreground puts ops into the trace
// ring with no system call, so no thread wakes the drain stage.  It sleeps
// with a bound, and it polls the ring again after each sleep.  The detect
// stage and the build stage receive with receive_waiting of StagePolicy, so
// they park until the stage before them pushes or closes.  A caller fills the
// owner mailbox with no wake, so the publish stage parks with a bound.  A sleep
// or a timed park starts at MIN_SLEEP_NANOS, and each one with no work between
// doubles the bound up to the longest sleep of the stage.  A producer stage
// that finds its output channel full parks by StagePolicy until the next stage
// receives.
//
// Each channel of the pipeline is a parked channel (fixy/concurrent/
// ParkedChannel.h).  It owns the counts of its waits, and each door of its ends
// notifies the count that its change can end a wait of, so no stage can wait on
// a count that no notify reaches.  Each door takes the context of its stage,
// which the stage gives to its body (fixy/concurrent/StageShape.h).
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
//
// The wait policy of a stage: a spin of 80 microseconds, then a park.  The
// load context of each stage owns Block and Nondet, so it takes the policy.
using StagePolicy = ::fixy::concurrent::SpinThenPark<80000>;
static_assert(::fixy::concurrent::CtxFitsWaitPolicy<::fixy::BgLoadCtx, StagePolicy>,
              "a stage of the pipeline spins and then parks, so its context must own Block and Nondet");
inline constexpr ::fixy::time::SpinBudget SPIN_BUDGET = StagePolicy::budget;
inline constexpr uint64_t MIN_SLEEP_NANOS = 50000;
// The longest sleep of the drain stage.
inline constexpr uint64_t RING_MAX_SLEEP_NANOS = 200000;
// The longest timed park of the publish stage.  It bounds only the wait of a
// job in the owner mailbox.
inline constexpr uint64_t MAILBOX_MAX_SLEEP_NANOS = 1000000;

// The wait policy of a park after the spin of a stage (StageIdle).
using ParkAtOnce = ::fixy::concurrent::SpinThenPark<0>;

// The futex calls of the pipeline use words of the process that the pipeline
// owns, so the kernel refuses a call only in a broken process.
[[nodiscard]] inline ::fixy::futex::Deadline deadline_after(::fixy::BgLoadCtx const& ctx, uint64_t nanos) noexcept {
    const auto clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
    const ::foundation::effects::cap::Nondet nondet =
        ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx);
    return ::fixy::futex::Deadline::after(clock, nondet, nanos)
        .expect("the monotonic clock gives the deadline of a park");
}

// The idle state of one stage: the end of the spin since the last work, the
// bound of the next sleep, and the longest sleep.
class StageIdle {
public:
    explicit StageIdle(uint64_t max_sleep_nanos) noexcept : max_sleep_nanos_{max_sleep_nanos} {}

    // The stage found work.
    void reset() noexcept {
        is_spinning_ = false;
        sleep_nanos_ = MIN_SLEEP_NANOS;
    }

    // A poll found no work in the ring.  The stage spins, and then it sleeps
    // for the bound.
    void pause_or_sleep(::fixy::BgLoadCtx const& ctx) noexcept {
        if (should_spin_(ctx)) return;
        const ::fixy::futex::FutexWord sleep_word{0};
        (void)::fixy::futex::wait(ctx, sleep_word, 0, ::fixy::some(deadline_after(ctx, next_sleep_nanos_())))
            .expect("the kernel parks a stage of the pipeline on its own word");
    }

    // A poll found the channel of input empty.  The stage spins, and then it
    // parks until the channel holds an item or its end, or until the bound.
    // The park takes no item, so the stage can do other work before its next
    // poll.
    template <class Input>
    void pause_or_park(::fixy::BgLoadCtx const& ctx, Input& input) noexcept {
        if (should_spin_(ctx)) return;
        static_cast<void>(
            input.template wait_for_item_until<ParkAtOnce>(ctx, deadline_after(ctx, next_sleep_nanos_())));
    }

private:
    // The first empty poll after work starts the spin budget.  Each empty
    // poll after the end of the budget parks or sleeps, until the stage finds
    // work again.
    [[nodiscard]] bool should_spin_(::fixy::BgLoadCtx const& ctx) noexcept {
        const ::foundation::effects::cap::Nondet nondet =
            ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx);
        if (!is_spinning_) {
            spin_end_ = ::fixy::time::SpinDeadline::after(nondet, SPIN_BUDGET);
            is_spinning_ = true;
        }
        if (spin_end_.has_passed(nondet)) return false;
        CRUCIBLE_SPIN_PAUSE;
        return true;
    }

    // The double of a sleep clamps at the largest sleep, so a large bound of
    // the caller gives that bound and not a wrapped short sleep.
    [[nodiscard]] uint64_t next_sleep_nanos_() noexcept {
        const uint64_t nanos = sleep_nanos_;
        sleep_nanos_ = ::fixy::min(::fixy::saturating_mul(nanos, uint64_t{2}), max_sleep_nanos_);
        return nanos;
    }

    bool is_spinning_ = false;
    ::fixy::time::SpinDeadline spin_end_ = ::fixy::time::SpinDeadline::passed();
    uint64_t sleep_nanos_ = MIN_SLEEP_NANOS;
    uint64_t max_sleep_nanos_ = MIN_SLEEP_NANOS;
};

// Loop of the Vigil.  The foreground records each op into the trace ring,
// the four stages make the regions of the ops, and the publish stage counts
// the ops that it processed.  A flush of the foreground waits on that count.
// A job of the foreground waits for a free owner mailbox, and then for its
// done mark.  run_pipeline_ states this loop over the graph of the pipeline
// that it mints, and the door ensure_loop_live of fixy/concurrent/StageNet.h
// checks the loop at compile time (the loop).  The loop takes the ends of the
// four stages from that graph (pipeline_graph_t and StageEdges), so the
// channels between the stages are the channels that the gate of the pipeline
// reads.
//
// The publish stage serves the mailbox before each receive and after each
// park, and it goes on when the mailbox is empty.  So a job is a branch of the
// stage, and the door ensure_arms_live checks each arm and the arms together.
// In the flush arm, the stage serves no job.  In the job arm, the foreground
// records, posts a job, waits on its done mark and then flushes, and the
// stage serves the job before its receive.  A job that comes after the
// receive of the stage waits for the next serve, after the count of that
// period: the late job arm.  With that third arm, the foreground and the
// stage take one arm together in each period.  The door checks that the
// three arms agree on each channel: a process holds the same sides in each
// arm, and the ends of each channel state one capacity.  So a run that takes
// the arms in any sequence is live (the arms).
//
// Each channel of the loop states its capacity.  The ring holds
// TraceRing::CAPACITY ops, and a full ring refuses an op with no wait, so the
// record never waits on the drain stage.  The count holds each bump, so the
// publish stage never waits on it.  The mailbox holds one job, and the done
// mark of a job is one flag.  The handles of the three channels between the
// stages state the capacities of those channels.
//
// A proof of the base holds some ends at each call: RecordProof the publish
// of the head of the ring, and PublishStage the bump of the count, the serve
// of the mailbox and the region callback.  The drain end of the ring has no
// proof, so the loop states it and no call checks it.
//
// These cells wait on nothing, so the loop holds no end of them: the metadata
// log, which gives an op no records when it is full and releases its tail
// with no wait, the publication slot, which keeps the newest region, the
// regions that the foreground holds, which the marks call of the publish stage
// reads one time and takes none of while the foreground writes them
// (crucible/HeldRegions.h), and the stop signal, which the drain stage reads
// after each sleep.

// The names of the processes of the loop.
struct ForegroundName {
    static constexpr ::foundation::algebra::net::NetName process_name{"foreground"};
};
struct DrainName {
    static constexpr ::foundation::algebra::net::NetName process_name{"drain"};
};
struct DetectName {
    static constexpr ::foundation::algebra::net::NetName process_name{"detect"};
};
struct BuildName {
    static constexpr ::foundation::algebra::net::NetName process_name{"build"};
};
struct PublishName {
    static constexpr ::foundation::algebra::net::NetName process_name{"publish"};
};

// The cells of the loop around the stages, each with its capacity.  The
// count reads as a channel of the top capacity of a channel of a system.
struct RingChannel {
    static constexpr uint32_t channel_capacity = TraceRing::CAPACITY;
};
struct ProcessedCountChannel {
    static constexpr uint32_t channel_capacity = ~uint32_t{0};
};
struct OwnerMailboxChannel {
    static constexpr uint32_t channel_capacity = 1;
};
struct JobDoneChannel {
    static constexpr uint32_t channel_capacity = 1;
};

namespace {

// Pops at most BATCH_SIZE entries of the ring into the five arrays of a
// batch, through the consumer end of the ring, and gives the count.  Each
// array lends its run in place through its frame door, with the key of its
// brand, so the pop writes the arrays with no copy.
[[nodiscard]] uint32_t
pop_trace_batch(TraceRing::Consumer& ring, BackgroundThread::BgTraceBatch& batch,
                ::foundation::core::LoanKey<BackgroundThread::BatchEntriesBrand>& entries_key,
                ::foundation::core::LoanKey<BackgroundThread::BatchMetasBrand>& metas_key,
                ::foundation::core::LoanKey<BackgroundThread::BatchScopesBrand>& scopes_key,
                ::foundation::core::LoanKey<BackgroundThread::BatchCallsitesBrand>& calls_key,
                ::foundation::core::LoanKey<BackgroundThread::BatchDepthsBrand>& depths_key) noexcept {
    return batch.entries.with_mut(entries_key, [&](auto entries) noexcept {
        return batch.meta_starts.with_mut(metas_key, [&](auto meta_starts) noexcept {
            return batch.scope_hashes.with_mut(scopes_key, [&](auto scope_hashes) noexcept {
                return batch.callsite_hashes.with_mut(calls_key, [&](auto callsite_hashes) noexcept {
                    return batch.depths.with_mut(depths_key, [&](auto depths) noexcept {
                        return TraceRing::try_pop_batch(ring, std::move(entries), std::move(meta_starts),
                                                        std::move(scope_hashes), std::move(callsite_hashes),
                                                        std::move(depths));
                    });
                });
            });
        });
    });
}

// Pushes the Box into the channel of the producer.  While the channel is full,
// the call waits by StagePolicy until the next stage receives.  The next stage
// ends only at the end of its stream, and only this producer ends that stream.
// So a refused push means that the next stage is gone while the stream is open,
// and the process ends.
template <class Producer, class T>
void push_item(::fixy::BgLoadCtx const& ctx, Producer& producer, ::fixy::Box<T>&& item) noexcept {
    producer.template push_waiting<StagePolicy>(ctx, std::move(item))
        .match([](::fixy::Unit /*pushed*/) noexcept {},
               [](::fixy::concurrent::PushRefused<::fixy::Box<T>> /*refused*/) noexcept {
                   ::fixy::fatal("a stage of the pipeline closed its input before the end of the stream");
               });
}

// True when a receive found the end of the stream.
template <class T>
[[nodiscard]] bool is_end_of_stream(::fixy::Result<T, ::fixy::concurrent::NoItem> const& received) noexcept {
    return received.err().value_or(::fixy::concurrent::NoItem::Empty) == ::fixy::concurrent::NoItem::Closed;
}

// The parked consumer handle of a stage, together with the thread the stage
// serves.  Only run_pipeline_ builds one, from a handle and the thread object.
// Each door gives the handle the context of the caller, which is the context
// of the stage, so the input holds no context.  The input owes the consume of
// its handle, and the stage closes it.
template <class Consumer>
class[[= ::foundation::linear::linear{}]][[= ::foundation::linear::owes_consume{}]] StageInput {
    Consumer inner_;
    ::fixy::Ref<BackgroundThread> owner_;

    StageInput(Consumer&& inner, BackgroundThread& owner) noexcept
        : inner_{std::move(inner)}, owner_{::fixy::erase_brand(::fixy::to(owner))} {}
    friend struct ::crucible::BackgroundThread;

public:
    static constexpr std::size_t per_call_working_set = Consumer::per_call_working_set;
    // The pipeline joins this input to the output before it only when the two
    // name one channel and bind one channel instance, so the input names and
    // reports the channel of the consumer end that it holds.
    using channel_type = typename Consumer::channel_type;
    // The count of the messages that the channel holds: the channel_capacity
    // of the consumer end that the input holds, which the producer end of the
    // channel states too.
    static constexpr std::size_t channel_capacity = Consumer::channel_capacity;

    [[nodiscard]] ::foundation::ChannelIdentity<channel_type> channel_identity() const noexcept {
        return inner_.channel_identity();
    }

    StageInput(StageInput&&) noexcept = default;
    StageInput(const StageInput&) = delete("a stage input owns the linear consumer end of its channel");
    StageInput& operator=(const StageInput&) = delete("a stage input owns the linear consumer end of its channel");
    StageInput& operator=(StageInput&&) = delete("a stage input binds to one channel for life");

    // The oldest item, or no value while the channel is empty.
    template <class Ctx>
    [[nodiscard]] auto try_pop(Ctx const& ctx) noexcept {
        return inner_.try_pop(ctx);
    }

    // The oldest item, NoItem::Empty, or NoItem::Closed after the last item of
    // a stream that the stage before this one ended.
    template <class Ctx>
    [[nodiscard]] auto try_receive(Ctx const& ctx) noexcept {
        return inner_.try_receive(ctx);
    }

    // The oldest item, or NoItem::Closed after the last item of the stream.
    // While the channel is empty and open, the call waits by Policy.
    template <::fixy::concurrent::WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto receive_waiting(Ctx const& ctx) noexcept {
        return inner_.template receive_waiting<Policy>(ctx);
    }

    // Waits by Policy until the channel holds an item or its end, or until the
    // deadline.  The call takes no item.
    template <::fixy::concurrent::WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto wait_for_item_until(Ctx const& ctx, ::fixy::futex::Deadline const& deadline) noexcept {
        return inner_.template wait_for_item_until<Policy>(ctx, deadline);
    }

    // Ends the consumer handle.
    template <class Ctx>
    void close(Ctx const& ctx) && noexcept {
        std::move(inner_).close(ctx);
    }

    [[nodiscard]] BackgroundThread& owner() const noexcept { return *owner_; }
};

}  // namespace

}  // namespace detail::bg_pipeline

struct BackgroundThread::Pipeline {
    // True after the first start().  Only the thread that owns the
    // BackgroundThread reads and writes it.
    bool has_started = false;

    // Each channel is parked, so it owns the counts of its waits (the idle
    // wait of a stage).
    using StartChannel = ::fixy::concurrent::ParkedChannel<
        ::fixy::concurrent::channel_t<BgPipelineStart, 1, decltype(detail::bg_pipeline::start_root())>>;
    // A Box owns each message of the three channels after the start, and the
    // channel moves the Box from one stage to the next (the owners of
    // fixy/concurrent/PermissionedBoxChannel.h).
    using TraceBatchChannel = ::fixy::concurrent::ParkedChannel<
        ::fixy::concurrent::box_channel_t<BgTraceBatch, 64, decltype(detail::bg_pipeline::trace_batch_root())>>;
    using BuildWorkChannel = ::fixy::concurrent::ParkedChannel<
        ::fixy::concurrent::box_channel_t<BgBuildWork, 64, decltype(detail::bg_pipeline::build_work_root())>>;
    using GraphPublishChannel = ::fixy::concurrent::ParkedChannel<
        ::fixy::concurrent::box_channel_t<BgGraphPublish, 64, decltype(detail::bg_pipeline::graph_publish_root())>>;

    // The sink takes the end mark of the pipeline by value, and it takes
    // each mark at its first push.  The sink holds no message, because the
    // push itself takes the mark.  So it states no channel_capacity, and the
    // marked system of a stage graph reads its channel as a rendezvous (the
    // marked system of fixy/concurrent/StageNet.h).
    struct BgSinkProducerHandle {
        [[nodiscard]] bool try_push(BgPipelineDone const& /*done*/) noexcept { return true; }
    };

private:
    friend struct BackgroundThread;

    // The four stages of the pipeline, in the order of its channels.  Each
    // stage takes the input over the consumer handle of its channel, the
    // producer handle of the next channel and the context of the stage.  The
    // handles carry the scope brands of the frames of run_pipeline_, so each
    // stage is a template over the types of its two ends, and only
    // run_pipeline_ names them.  Each stage closes its output and then its
    // input at its end.  Each stage is private, so only run_pipeline_ starts
    // one.
    //
    // The drain stage pops batches from the trace ring until a signal on
    // stop_requested, then pushes the last batch and closes its channel.
    // Each later stage ends at the end of its input stream, and it closes
    // its own channel then.
    template <class Input, class Output>
    static void DrainTraceRingFn(Input&& in, Output&& out, ::fixy::BgLoadCtx const& ctx,
                                 ::foundation::core::LoanKey<BatchEntriesBrand>& entries_key,
                                 ::foundation::core::LoanKey<BatchMetasBrand>& metas_key,
                                 ::foundation::core::LoanKey<BatchScopesBrand>& scopes_key,
                                 ::foundation::core::LoanKey<BatchCallsitesBrand>& calls_key,
                                 ::foundation::core::LoanKey<BatchDepthsBrand>& depths_key);

    // The detect stage feeds each entry to the iteration detector, cuts a
    // work item at each boundary, and pushes a commit marker after each
    // batch.  It applies each divergence reset at the point of the
    // divergence in the op stream.
    template <class Input, class Output>
    static void DetectIterationFn(Input&& in, Output&& out, ::fixy::BgLoadCtx const& ctx);

    // The build stage builds a trace graph from each work item and
    // forwards commit markers unchanged.  A work item that met the overflow
    // of the metadata log sends nothing.
    template <class Input, class Output>
    static void BuildTraceFn(Input&& in, Output&& out, ::fixy::BgLoadCtx const& ctx);

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
    template <class Input>
    static void MakeRegionFn(Input&& in, BgSinkProducerHandle&& out, ::fixy::BgLoadCtx const& ctx);
};

// ignore_regions_ and no_region_marks_ come before region_ready_ and
// region_marks_ in the class, so each is built before the Ref of it.  The
// region store holds one empty slot for each region of the limits.  No
// consumer of the ring runs at the construction, so the tail of the ring holds
// still for the read of ring_base_.
BackgroundThread::BackgroundThread(BackgroundThreadKey const& /*key*/, ::foundation::effects::Alloc allocation,
                                   TraceRingSide& ring_side, MetaLogSide& meta_log_side, Setup const& setup)
    : ring_{::fixy::erase_brand(::fixy::to(ring_side))},
      meta_log_{::fixy::erase_brand(::fixy::to(meta_log_side))},
      ring_base_{ring_side.with_owner([](TraceRing const& ring) noexcept { return ring.total_consumed(); })},
      region_ready_{::fixy::Option<::fixy::Ref<RegionReadyCall const>>{setup.on_region_ready}.value_or(
          ::fixy::erase_brand(::fixy::to(ignore_regions_)))},
      region_marks_{::fixy::Option<::fixy::Ref<RegionMarksCall const>>{setup.region_marks}.value_or(
          ::fixy::erase_brand(::fixy::to(no_region_marks_)))},
      scratch_retained_limit_{setup.limits.scratch_retained},
      region_store_{::fixy::mint_heap_array_with<RegionMemory>(allocation, setup.limits.region_store.raw(),
                                                               [](size_t) noexcept { return RegionMemory{}; })},
      free_slots_{::fixy::mint_heap_bit_set<RegionStoreSlots>(allocation, setup.limits.region_store)},
      swept_slots_{::fixy::mint_heap_bit_set<RegionStoreSlots>(allocation, setup.limits.region_store)},
      settled_slots_{::fixy::mint_heap_bit_set<RegionStoreSlots>(allocation, setup.limits.region_store)},
      uncompiled_regions_{allocation, setup.limits.uncompiled_queue},
      pipeline_{::fixy::mint_box<Pipeline>(allocation)} {
    build_scratch_.arena.set_retained_limit(scratch_retained_limit_);
    publish_scratch_.arena.set_retained_limit(scratch_retained_limit_);
    for (uint32_t position = 0; position < free_slots_.extent().raw(); ++position)
        free_slots_.set(RegionSlot{position});
}

::fixy::Box<BgBuildWork> BackgroundThread::make_commit_work(::foundation::effects::Alloc alloc, uint32_t count) {
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
    meta_cursor_ = ::fixy::strict_add(::fixy::strict_add(meta_start.raw(), uint64_t{entry.num_inputs}),
                                      uint64_t{entry.num_outputs});
    return meta_start.raw();
}

// The ring grows to the next power of two, and the oldest held op moves to
// slot zero.  The detect stage drops each op that no future work can hold,
// so the count stays at most half of the history of the detector, K ops more
// and the op that the detect stage adds before it drops.
void PendingOps::grow_(::foundation::effects::Alloc alloc) {
    const uint64_t new_capacity =
        mask_ == 0 ? MIN_CAPACITY : ::fixy::strict_mul(::fixy::strict_add(mask_, uint64_t{1}), uint64_t{2});
    CRUCIBLE_FATAL_INVARIANT(new_capacity <= IterationDetector::HISTORY_MAX_CAPACITY);
    ::fixy::MaskedArray<TraceRing::Entry> entries = ::fixy::mint_masked_array<TraceRing::Entry>(alloc, new_capacity);
    ::fixy::MaskedArray<MetaIndex> meta_starts = ::fixy::mint_masked_array<MetaIndex>(alloc, new_capacity);
    ::fixy::MaskedArray<uint64_t> record_starts = ::fixy::mint_masked_array<uint64_t>(alloc, new_capacity);
    ::fixy::MaskedArray<ScopeHash> scope_hashes = ::fixy::mint_masked_array<ScopeHash>(alloc, new_capacity);
    ::fixy::MaskedArray<CallsiteHash> callsite_hashes = ::fixy::mint_masked_array<CallsiteHash>(alloc, new_capacity);
    ::fixy::MaskedArray<OpDepth> depths = ::fixy::mint_masked_array<OpDepth>(alloc, new_capacity);
    for (uint64_t index = 0; index < count_; ++index) {
        entries.with_masked(index, [held = entry(index)](TraceRing::Entry& slot) noexcept { slot = held; });
        meta_starts.with_masked(index, [held = meta_start(index)](MetaIndex& slot) noexcept { slot = held; });
        record_starts.with_masked(index, [held = record_start(index)](uint64_t& slot) noexcept { slot = held; });
        scope_hashes.with_masked(index, [held = scope_hash(index)](ScopeHash& slot) noexcept { slot = held; });
        callsite_hashes.with_masked(index, [held = callsite_hash(index)](CallsiteHash& slot) noexcept { slot = held; });
        depths.with_masked(index, [held = depth(index)](OpDepth& slot) noexcept { slot = held; });
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

void PendingOps::push(::foundation::effects::Alloc alloc, TraceRing::Entry const& entry, MetaIndex meta_start,
                      uint64_t record_start, ScopeHash scope_hash, CallsiteHash callsite_hash, OpDepth depth) {
    if (mask_ == 0 || count_ == mask_ + 1) [[unlikely]]
        grow_(alloc);
    const uint64_t slot = head_ + count_;
    entries_.with_masked(slot, [&entry](TraceRing::Entry& held) noexcept { held = entry; });
    meta_starts_.with_masked(slot, [meta_start](MetaIndex& held) noexcept { held = meta_start; });
    record_starts_.with_masked(slot, [record_start](uint64_t& held) noexcept { held = record_start; });
    scope_hashes_.with_masked(slot, [scope_hash](ScopeHash& held) noexcept { held = scope_hash; });
    callsite_hashes_.with_masked(slot, [callsite_hash](CallsiteHash& held) noexcept { held = callsite_hash; });
    depths_.with_masked(slot, [depth](OpDepth& held) noexcept { held = depth; });
    ++count_;
}

void BackgroundThread::drop_unclaimable_ops_() noexcept {
    const uint64_t first_held = ::fixy::strict_sub(detector_.pos_, pending_ops_.size());
    const uint64_t oldest_claimable = detector_.oldest_claimable();
    if (oldest_claimable > first_held) pending_ops_.drop_oldest(oldest_claimable - first_held);
}

::fixy::Option<::fixy::Box<BgBuildWork>>
BackgroundThread::prepare_iteration_build_work(::foundation::effects::Alloc alloc) {
    uint32_t total = static_cast<uint32_t>(pending_ops_.size());
    const uint32_t iter_len = detector_.last_completed_len;

    const uint32_t warmup = ::fixy::saturating_sub(::fixy::saturating_sub(total, IterationDetector::K), iter_len);
    pending_ops_.drop_oldest(warmup);
    total = ::fixy::saturating_sub(total, warmup);

    const uint32_t completed_len = ::fixy::saturating_sub(total, IterationDetector::K);
    (void)iterations_completed.bump();

    ::fixy::Option<::fixy::Box<BgBuildWork>> built{::fixy::none};
    if (completed_len > 0) {
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
        detector_.restart_after_divergence();
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
void BackgroundThread::release_meta_log(PublishStage const& stage, uint64_t meta_floor) {
    meta_log_->with_end(stage, [meta_floor](MetaLog::Consumer& log) noexcept {
        if (meta_floor <= MetaLog::released(log)) return;
        CRUCIBLE_FATAL_INVARIANT(meta_floor <= MetaLog::produced(log));
        MetaLog::advance_tail(log, meta_floor);
    });
}

// The node and the plan go into the arena of the parts of the region, and
// the arrays of the sweep into the scratch of the publish stage, whose epoch
// ends in this call.  The memory goes to a slot of the region store before
// the region becomes visible, so each region that a holder can name has an
// owner.  A store with no free slot gets one sweep with fresh marks.  When that
// sweep frees no slot, the publish discards the region, as a reset discards a
// stale region, and counts it in regions_dropped_full_.
//
// The arena moves out of the parts for the build of the node and the plan,
// and back in before the parts move to the store.  The Box holds the arena on
// the heap, so neither move moves a node.  The push into the uncompiled queue
// gives the QueuedRegion of the region.  Then one exchange of the store moves
// the region memory into the slot, and the region becomes visible after it.
// Only the publish stage reads the queue, so no reader of the queue finds the
// region between the push and the exchange.
//
// Complexity: O(S / 64) for S slots of the store, for the search of a free
// slot, and the sweep (sweep_regions_).
void BackgroundThread::publish_region(::foundation::effects::Bg const& bg, PublishStage const& stage, BuiltRegion parts,
                                      StreamPoint reset_point) {
    ::fixy::Option<RegionSlot> free_slot = find_free_slot_();
    if (free_slot.is_none()) [[unlikely]] {
        free_slot = free_a_slot_(bg.alloc, stage);
    }
    if (free_slot.is_none()) [[unlikely]] {
        ++regions_dropped_full_;
        return;
    }
    const RegionSlot slot = std::move(free_slot).expect("the test above found a free slot");

    // Each caller gives a region of a build, which holds its arena.  A sum type of the publish message will prove it.
    ::fixy::Box<::fixy::BumpArena<DagArena>> arena =
        std::move(parts.arena_).expect("a built region holds the arena of its runs");
    const DagRef<RegionNode> region = mint_dag_node<RegionNode>(bg.alloc, arena);
    init_region_node(*region, parts.runs_.ops, parts.content_hash(), parts.numerics());
    // The plan lives in the arena of the region, outside the epoch.  The body
    // of the epoch captures the device of the plan, the plan and the slots by
    // value, and not the owner, so it reaches no arena of an epoch.  A refused
    // plan leaves the region with no plan, so the region stays on eager.  A
    // region with no slot has no plan.
    if (parts.runs_.slots.size() > 0) {
        const DagRef<MemoryPlan> plan = arena->make<MemoryPlan>(bg.alloc);
        const bool is_planned =
            publish_scratch_.arena.with_epoch([target = plan_target_(), alloc = bg.alloc, plan,
                                               slots = parts.runs_.slots](PlanScratch::Epoch& epoch) mutable noexcept {
                return BackgroundThread::compute_memory_plan(alloc, target, plan, epoch, slots).is_ok();
            });
        if (is_planned) region->plan = make_plan_link(plan);
    }
    recompute_merkle(*region);
    parts.arena_ = ::fixy::Option<::fixy::Box<::fixy::BumpArena<DagArena>>>::some(std::move(arena));
    const ConstRegionLink const_published = ConstRegionLink::some(region);

    // The sweep reads the new region, and the region that its push took out of
    // the queue.
    QueuePush pushed = uncompiled_regions_.push(stage, region, slot);
    for (const RegionSlot pushed_out : pushed.pushed_out)
        swept_slots_.set(pushed_out);
    free_slots_.reset(slot);
    swept_slots_.set(slot);
    // The free slot holds no region, so the memory that the exchange gives
    // back holds nothing.
    static_cast<void>(region_store_.exchange(
        slot.raw(), RegionMemory{.parts = std::move(parts),
                                 .queued = std::move(pushed.queued),
                                 .published = PublishedRegion{.region = const_published, .reset_point = reset_point},
                                 .left_generation = RegionMemory::NOT_LEFT}));
    last_published_ = const_published;

    active_region_.store_release(RegionLink::some(region));
    // The callback gets a proof of its own, which dies with the call, and
    // the record in the slot of the store, whose address it can keep.
    (*region_ready_)(
        bg, PublishStage{},
        region_store_.view().get(slot.raw()).expect("an id of a slot lies below the count of the store")->published);
    sweep_regions_(bg.alloc, stage, (*region_marks_)(PublishStage{}));
}

void BackgroundThread::set_left_generation_(RegionSlot slot, uint64_t generation) noexcept {
    region_store_.with_element(
        slot.raw(), [generation](RegionMemory& memory) noexcept { memory.left_generation = generation; },
        [] noexcept { ::fixy::fatal("an id of a slot lies below the count of the store"); });
}

::fixy::Option<RegionSlot> BackgroundThread::find_free_slot_() const noexcept {
    return free_slots_.with_keys([](auto const& slots) noexcept -> ::fixy::Option<RegionSlot> {
        for (const RegionSlot slot : slots)
            return ::fixy::some(slot);
        return ::fixy::none;
    });
}

::fixy::Option<RegionSlot> BackgroundThread::find_slot_of_(ConstRegionLink region) const noexcept {
    return ::fixy::find_if(region_store_.view(),
                           [region](RegionMemory const& memory) noexcept {
                               return memory.queued.node().is_some()
                                   && ::fixy::same_target(memory.queued.node(), region);
                           })
        .map([](size_t position) noexcept {
            return RegionSlot{::fixy::narrow<uint32_t>(position).expect("the count of the store is a uint32_t")};
        });
}

// The last publish into the full store swept it with the marks of that time.
// The foreground can have written its holders after it, and the write lets
// regions go.
::fixy::Option<RegionSlot> BackgroundThread::free_a_slot_(::foundation::effects::Alloc alloc,
                                                          PublishStage const& stage) noexcept {
    sweep_regions_(alloc, stage, (*region_marks_)(PublishStage{}));
    return find_free_slot_();
}

// The sweep releases a region when no holder names it.  The last published
// region and the region in the slot stay.  With a foreground, each other
// region also waits for a write of the foreground holders after the sweep
// that found the region out of the view of the foreground.  That write names
// each region that the foreground can still read, so the foreground reads no
// region that the write does not name.  With no foreground, the sweep
// releases a region with no holder at once.  While the foreground writes its
// holders, the sweep marks no region left and releases none (RegionMarks).
//
// Slots of a sweep.  The sweep runs after each publish, and it reads the slots
// of swept_slots_ only.  The set holds the slot of each region that a sweep can
// change:
//   - each region that a sweep did not mark left: the last published region,
//     the handed region, and a region that the owner kept (keep_region)
//   - each region that left the uncompiled queue.
// Each other region is out of the view of the foreground and has its mark, and
// the queue holds it.  No sweep changes it before it leaves the queue, and the
// push that takes it out gives its slot.  A publish and keep_region put the
// slots of their regions into the set, and a sweep puts in the slot of a handed
// region that left the view before.  A slot leaves the set when the sweep
// settles its region in the queue or releases it.  The set decides only which
// slots the sweep reads, and the rule above decides each release.
//
// With no foreground, a region in the queue needs no mark.  If the marks of the
// owner name a foreground at a later time, the mark of such a region comes when
// it leaves the queue, which is later than a walk of each slot gives it, and
// never earlier.
//
// Complexity: O(S / 64 + W * M) for S slots of the store, W slots of the set and
// M marks, and O(S) more when the owner hands a region that is not the last
// published one.  W is at most the count of regions that left the queue, plus
// the last published region, the one before it, the handed region and the kept
// regions.
//
// The sweep reads each slot through the View of the const store, and then
// writes the step for the slot through the element door of the store.
//
// The walk of the keys of swept_slots_ lends the keys to a body, and a body
// that writes the store could keep a borrow in it.  So the body only appends
// each key to a local list of plain slot ids, and the sweep of each slot runs
// after the walk, in the same ascending order.  W is at most the count of the
// regions that left the queue, plus four: the last published region, the one
// before it, the handed region and the kept regions.  A sweep with W > 0 takes
// one segment of 256 bytes (64 ids) from alloc for the list, and the end of the
// list frees it.  This is on the publish stage, never on the hot path.
void BackgroundThread::sweep_regions_(::foundation::effects::Alloc alloc, PublishStage const& stage,
                                      RegionMarks const& marks) noexcept {
    if (marks.handed.is_some() && !::fixy::same_target(marks.handed, last_published_)) [[unlikely]] {
        for (const RegionSlot handed : find_slot_of_(marks.handed))
            swept_slots_.set(handed);
    }
    ::fixy::SegmentList<RegionSlot> sweep_order{};
    swept_slots_.with_keys([alloc, &sweep_order](auto const& slots) noexcept {
        for (const RegionSlot slot : slots)
            sweep_order.append(alloc, slot);
    });
    for (const RegionSlot slot : sweep_order) {
        if (!sweep_slot_(stage, marks, slot)) settled_slots_.set(slot);
    }
    swept_slots_.subtract(settled_slots_).expect("the two sets have the count of the store");
    settled_slots_.clear();
}

bool BackgroundThread::sweep_slot_(PublishStage const& stage, RegionMarks const& marks, RegionSlot slot) noexcept {
    const RegionMemory& memory =
        *region_store_.view().get(slot.raw()).expect("an id of a slot lies below the count of the store");
    const bool is_queued = uncompiled_regions_.holds(stage, memory.queued);
    switch (find_sweep_step_(stage, marks, memory)) {
        case SweepStep::Keep:
            set_left_generation_(slot, RegionMemory::NOT_LEFT);
            return true;
        case SweepStep::Wait:
            return true;
        case SweepStep::MarkLeft:
            set_left_generation_(slot, marks.foreground_generation);
            return !is_queued;
        case SweepStep::Settle:
            return false;
        case SweepStep::Release:
            region_store_.with_element(
                slot.raw(), [](RegionMemory& released) noexcept { released = RegionMemory{}; },
                [] noexcept { ::fixy::fatal("an id of a slot lies below the count of the store"); });
            free_slots_.set(slot);
            return false;
        default:
            ::fixy::unreachable();
    }
}

BackgroundThread::SweepStep BackgroundThread::find_sweep_step_(PublishStage const& stage, RegionMarks const& marks,
                                                               RegionMemory const& memory) const noexcept {
    using Foreground = RegionMarks::Foreground;
    const DagRef<RegionNode const> region =
        memory.queued.node().expect("the sweep reads only slots that hold a region");
    const auto is_region = [region](DagRef<RegionNode const> other) noexcept {
        return ::fixy::same_object(region, other);
    };
    if (last_published_.is_some_and(is_region) || marks.handed.is_some_and(is_region)) return SweepStep::Keep;
    if (marks.foreground == Foreground::Writing) return SweepStep::Wait;
    const bool has_read_foreground = marks.foreground == Foreground::Read;
    if (has_read_foreground && memory.left_generation == RegionMemory::NOT_LEFT) return SweepStep::MarkLeft;
    if (uncompiled_regions_.holds(stage, memory.queued)) return SweepStep::Settle;
    if (has_read_foreground && marks.foreground_generation <= memory.left_generation) return SweepStep::Wait;
    for (DagRef<RegionNode const> const held : marks.held) {
        if (is_region(held)) return SweepStep::Wait;
    }
    return SweepStep::Release;
}

void BackgroundThread::keep_region(PublishStage const&, ConstRegionLink region) noexcept {
    for (const RegionSlot kept : find_slot_of_(region)) {
        set_left_generation_(kept, RegionMemory::NOT_LEFT);
        swept_slots_.set(kept);
    }
}

uint32_t BackgroundThread::region_count(PublishStage const&) const noexcept {
    return ::fixy::narrow<uint32_t>(region_store_.size() - free_slots_.size())
        .expect("the count of the store is a uint32_t");
}

::fixy::Option<uint64_t> BackgroundThread::left_generation_of(PublishStage const&, RegionSlot slot) const noexcept {
    if (free_slots_.test(slot)) return ::fixy::none;
    return ::fixy::some(region_store_.view()
                            .get(slot.raw())
                            .expect("an id of a slot lies below the count of the store")
                            ->left_generation);
}

::fixy::Quantity<::fixy::unit::Byte, size_t> BackgroundThread::region_bytes(PublishStage const&) const noexcept {
    ::fixy::Quantity<::fixy::unit::Byte, size_t> bytes{size_t{0}};
    for (RegionMemory const& memory : region_store_.view()) {
        bytes = ::fixy::saturating_add(bytes, memory.parts.arena_bytes());
    }
    return bytes;
}

template <class Input, class Output>
void BackgroundThread::Pipeline::DrainTraceRingFn(Input&& in, Output&& out, ::fixy::BgLoadCtx const& ctx,
                                                  ::foundation::core::LoanKey<BatchEntriesBrand>& entries_key,
                                                  ::foundation::core::LoanKey<BatchMetasBrand>& metas_key,
                                                  ::foundation::core::LoanKey<BatchScopesBrand>& scopes_key,
                                                  ::foundation::core::LoanKey<BatchCallsitesBrand>& calls_key,
                                                  ::foundation::core::LoanKey<BatchDepthsBrand>& depths_key) {
    BackgroundThread& owner = in.owner();
    while (in.try_pop(ctx).is_none()) {
        CRUCIBLE_SPIN_PAUSE;
    }

    ::foundation::effects::Alloc const alloc = ctx.cap().alloc;
    ::fixy::Box<BgTraceBatch> batch = ::fixy::mint_box<BgTraceBatch>(alloc);
    detail::bg_pipeline::StageIdle idle{detail::bg_pipeline::RING_MAX_SLEEP_NANOS};

    // A stop request does not wake a sleep of this stage, so the stage reads
    // the request at the end of its sleep, at most RING_MAX_SLEEP_NANOS later.
    // The stage opens the consumer side of the ring for each pop with its
    // proof, which lives for the loop on the thread of the stage.
    TraceRingSide& ring_side = *owner.ring_;
    DrainStage const stage{};
    auto const pop = [&](BgTraceBatch& into) noexcept {
        return ring_side.with_end(stage, [&](TraceRing::Consumer& ring) noexcept {
            return detail::bg_pipeline::pop_trace_batch(ring, into, entries_key, metas_key, scopes_key, calls_key,
                                                        depths_key);
        });
    };

    while (true) {
        if (owner.stop_requested.peek()) break;

        batch->count = pop(*batch);

        if (batch->count == 0) {
            idle.pause_or_sleep(ctx);
            continue;
        }
        idle.reset();

        detail::bg_pipeline::push_item(ctx, out, std::move(batch));
        batch = ::fixy::mint_box<BgTraceBatch>(alloc);
    }

    batch->count = pop(*batch);
    if (batch->count > 0) {
        detail::bg_pipeline::push_item(ctx, out, std::move(batch));
    }

    std::move(out).close(ctx);
    std::move(in).close(ctx);
}

template <class Input, class Output>
void BackgroundThread::Pipeline::DetectIterationFn(Input&& in, Output&& out, ::fixy::BgLoadCtx const& ctx) {
    BackgroundThread& owner = in.owner();
    ::foundation::effects::Alloc const alloc = ctx.cap().alloc;
    while (true) {
        // The receive waits for a batch or for the end of the stream, and it
        // wakes the drain stage when that stage waits for room.
        ::fixy::Result<::fixy::Box<BgTraceBatch>, ::fixy::concurrent::NoItem> received =
            in.template receive_waiting<detail::bg_pipeline::StagePolicy>(ctx);
        if (detail::bg_pipeline::is_end_of_stream(received)) break;

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

            if (owner.detector_.check(alloc, entry.schema_hash, entry.shape_hash)) {
                ::fixy::Option<::fixy::Box<BgBuildWork>> work = owner.prepare_iteration_build_work(alloc);
                if (work.is_some()) {
                    detail::bg_pipeline::push_item(ctx, out, std::move(work).expect("the test above found work"));
                }
            }
            owner.drop_unclaimable_ops_();
        }
        // A reset at the end of the batch applies now, so the detector holds
        // no ops of the trace that diverged while it waits for the next op.
        owner.apply_due_reset_();

        // The snapshot goes out before the commit marker, so a flush that
        // reads the marker reads this snapshot or a later one.
        owner.detector_snapshot.store_release(DetectorSnapshot{
            .boundaries = owner.detector_.boundaries_detected.get(),
            .period = owner.detector_.confirmed ? DetectorPeriod::Confirmed : DetectorPeriod::Searching});

        detail::bg_pipeline::push_item(ctx, out, owner.make_commit_work(alloc, ops.count));
    }
    std::move(out).close(ctx);
    std::move(in).close(ctx);
}

template <class Input, class Output>
void BackgroundThread::Pipeline::BuildTraceFn(Input&& in, Output&& out, ::fixy::BgLoadCtx const& ctx) {
    BackgroundThread& owner = in.owner();

    while (true) {
        // The receive waits for a work or for the end of the stream, and it
        // wakes the detect stage when that stage waits for room.
        ::fixy::Result<::fixy::Box<BgBuildWork>, ::fixy::concurrent::NoItem> received =
            in.template receive_waiting<detail::bg_pipeline::StagePolicy>(ctx);
        if (detail::bg_pipeline::is_end_of_stream(received)) break;

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
            detail::bg_pipeline::push_item(ctx, out, std::move(publish));
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
        // epoch, in the publish message.  The region owns its buffers and
        // does not point into the scratch.  A work that the build refuses
        // (BuildRefusal) builds no region.  The local holds the Box of the
        // message, not the region, so the region and the Option of its arena
        // lie only in the heap object of the Box.
        //
        // The body captures the state of the build, the metadata log by a
        // const reference and the work, and not the owner, so it reaches no
        // arena of the owner.  The build reads the log through the view that
        // the consumer side of the log gives (owner_view of
        // fixy/concurrent/SpscEnds.h), and it reads only the records that the
        // meta starts of the drained ops of the work name.  The record path
        // published each such record before its op, and the publish stage
        // releases a record only after the build of its work.
        ::fixy::Option<::fixy::Box<BgGraphPublish>> publish{::fixy::none};
        TraceBuildState& build_state = owner.build_state_;
        ::fixy::Ref<MetaLog const, ::fixy::concurrent::SpscSplitBrand<MetaLog>> const meta_log_view =
            owner.meta_log_->owner_view();
        MetaLog const& meta_log = *meta_log_view;
        ::foundation::effects::Alloc const alloc = ctx.cap().alloc;
        owner.build_scratch_.arena.with_epoch([&build_state, &meta_log, alloc, &work,
                                               &publish](TraceScratch::Epoch& epoch) noexcept {
            BackgroundThread::build_trace_(build_state, meta_log, alloc, epoch, work->trace.view(),
                                           work->meta_starts.view(), work->scope_hashes.view(),
                                           work->callsite_hashes.view(), work->depths.view(), work->numerics)
                .match(
                    [&](BuiltTrace trace) noexcept {
                        publish = ::fixy::some(
                            ::fixy::mint_box<BgGraphPublish>(alloc, BgGraphPublish{.kind = BgGraphPublish::Kind::Region,
                                                                                   .region = std::move(trace.region),
                                                                                   .reset_point = work->reset_point}));
                    },
                    [](BuildRefusal /*refusal*/) noexcept {});
        });

        // A refused work sends no region.  The floor of a later commit marker
        // releases its records.
        if (publish.is_none()) {
            continue;
        }

        detail::bg_pipeline::push_item(ctx, out, std::move(publish).expect("the build made a region"));
    }
    std::move(out).close(ctx);
    std::move(in).close(ctx);
}

// Keeping the publish stage separate means the publish-side delegate
// never sees the build-side machinery, so a crash-stop during publish
// cannot taint the build stage's owner pointer.
template <class Input>
void BackgroundThread::Pipeline::MakeRegionFn(Input&& in, BgSinkProducerHandle&& out, ::fixy::BgLoadCtx const& ctx) {
    const ::foundation::effects::Bg bg = ctx.cap();
    BackgroundThread& owner = in.owner();
    const OwnerMailboxScope mailbox{owner, PublishStage{}};
    detail::bg_pipeline::StageIdle idle{detail::bg_pipeline::MAILBOX_MAX_SLEEP_NANOS};

    while (true) {
        owner.serve_owner_mailbox_(PublishStage{});
        // The receive wakes the build stage when that stage waits for room.
        ::fixy::Result<::fixy::Box<BgGraphPublish>, ::fixy::concurrent::NoItem> received = in.try_receive(ctx);
        if (received.is_err()) {
            if (detail::bg_pipeline::is_end_of_stream(received)) {
                // The sink takes each value at its first push.
                const bool is_taken = out.try_push(BgPipelineDone{});
                CRUCIBLE_FATAL_INVARIANT(is_taken);
                std::move(in).close(ctx);
                return;
            }
            // A job in the owner mailbox gives no wake, so the park is
            // timed, and the stage serves the mailbox after each park.
            idle.pause_or_park(ctx, in);
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
            (void)owner.total_processed.bump_by(PublishStage{}, publish->commit_count);
            // The count comes first, and then the notify wakes each flush
            // that parks.  With no flush that parks, the notify is one fence
            // and one load.
            owner.processed_events_.notify_all(ctx);
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

void BackgroundThread::set_numerics_(NumericsHash numerics) noexcept {
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(numerics));
    CRUCIBLE_PRE(!numerics.is_sentinel());
    numerics_ = numerics;
    stream_numerics_ = numerics;
    requested_numerics_.store_release(numerics);
}

// No destructor body runs before the members end, so thread_, the last
// member, stops and joins the pipeline thread while each other member is
// alive (the thread of an owner of fixy/os/OwnedThread.h).  The definition
// comes before start_(), so the door of the thread reads it as defaulted.
BackgroundThread::~BackgroundThread() = default;

void BackgroundThread::start_(::fixy::InitLoadCtx const& startup, PlanTarget const& target) {
    // The store comes before the thread starts, so a flush after this start
    // waits for the pipeline, and the thread stores stopped at its end.
    pipeline_run_.store_release(PipelineRun::running);
    global_schema_table().seal(startup);
    global_ckernel_table().value()->seal(startup);
    rank_ = target.rank;
    world_size_ = target.world_size;
    device_capability_ = target.device_capability;
    rearm_stop_();
    if (!pipeline_->has_started) {
        pipeline_->has_started = true;
        // The first op of the ring owns the first record that the log has
        // not released.  A later start keeps the cursor, because the ring
        // keeps the ops that the last pipeline did not read.
        meta_cursor_ = meta_log_->with_owner([](MetaLog const& log) noexcept { return log.total_released(); });
    }
    // The lambda is the entry of the pipeline thread, so it takes the
    // background context of that thread from the door.  It borrows this
    // object through the door, which keeps the thread in thread_.  The
    // process ends when the host has no resource for one more thread.
    ::fixy::thread::mint_owner_thread<&BackgroundThread::thread_, &BackgroundThread::request_stop_>(
        startup, ::fixy::thread::ThreadOptions{}, *this,
        [](BackgroundThread& owner) noexcept {
            const ::fixy::BgLoadCtx ctx{::foundation::effects::host::BackgroundOwner::mint_background_context()};
            owner.run_pipeline_(ctx);
            owner.end_pipeline_run_(ctx);
        })
        .expect("the thread door starts the pipeline thread");
}

void BackgroundThread::stop_() { thread_.stop_and_join(); }

BackgroundThread::Running<StartedBackgroundThread::HolderScope>
StartedBackgroundThread::start_(BackgroundThread& thread, ::fixy::InitLoadCtx const& startup, NumericsHash numerics,
                                BackgroundThread::PlanTarget const& target) {
    BackgroundThread::Stopped<HolderScope> stopped{BackgroundThreadKey{}, thread};
    stopped.set_numerics(numerics);
    return std::move(stopped).start(startup, target);
}

StartedBackgroundThread::StartedBackgroundThread(::fixy::InitLoadCtx const& startup,
                                                 ::foundation::effects::Alloc allocation,
                                                 BackgroundThread::TraceRingSide& ring_side,
                                                 BackgroundThread::MetaLogSide& meta_log_side,
                                                 BackgroundThread::Setup const& setup, NumericsHash numerics,
                                                 BackgroundThread::PlanTarget const& target)
    : thread_{BackgroundThreadKey{}, allocation, ring_side, meta_log_side, setup},
      running_{start_(thread_, startup, numerics, target)} {}

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

    // The stages, the pipeline and its run.  The body of the innermost frame
    // below calls it with the handle of each end of each channel.  Each
    // handle carries the scope brand of its frame, so the stages are
    // templates over the types of the handles.
    auto const run_stages = [this, &ctx](auto start_prod, auto start_end, auto trace_prod, auto trace_end,
                                         auto build_prod, auto build_end, auto publish_prod,
                                         auto publish_end) noexcept {
        using detail::bg_pipeline::StageInput;
        auto start_cons = StageInput<decltype(start_end)>{std::move(start_end), *this};
        auto trace_cons = StageInput<decltype(trace_end)>{std::move(trace_end), *this};
        auto build_cons = StageInput<decltype(build_end)>{std::move(build_end), *this};
        auto publish_cons = StageInput<decltype(publish_end)>{std::move(publish_end), *this};

        start_prod.try_push(ctx, BgPipelineStart{}).expect("the start channel holds the start signal of one run");
        std::move(start_prod).close(ctx);
        auto drain_stage =
            ::fixy::concurrent::mint_stage<&Pipeline::DrainTraceRingFn<decltype(start_cons), decltype(trace_prod)>>(
                ctx, std::move(start_cons), std::move(trace_prod));
        auto detect_stage =
            ::fixy::concurrent::mint_stage<&Pipeline::DetectIterationFn<decltype(trace_cons), decltype(build_prod)>>(
                ctx, std::move(trace_cons), std::move(build_prod));
        auto build_stage =
            ::fixy::concurrent::mint_stage<&Pipeline::BuildTraceFn<decltype(build_cons), decltype(publish_prod)>>(
                ctx, std::move(build_cons), std::move(publish_prod));
        auto publish_stage = ::fixy::concurrent::mint_stage<&Pipeline::MakeRegionFn<decltype(publish_cons)>>(
            ctx, std::move(publish_cons), Pipeline::BgSinkProducerHandle{});

        auto pipeline = ::fixy::concurrent::mint_pipeline(ctx, std::move(drain_stage), std::move(detect_stage),
                                                          std::move(build_stage), std::move(publish_stage));

        // The loop of the Vigil over the graph of the pipeline, in its three
        // arms (the loop of the Vigil).  The doors run at compile time.
        namespace cc = ::fixy::concurrent;
        namespace loop = detail::bg_pipeline;
        using graph = cc::pipeline_graph_t<decltype(pipeline)>;
        // The marked system of the graph is live.  Its input end is the start
        // signal, and its output end is the sink, which holds no message.
        static_assert([] consteval noexcept {
            constexpr auto marked = cc::marked_system_of_stage_graph<graph>();
            return ::foundation::algebra::net::check_liveness(marked).is_admitted()
                && marked.channels[3].capacity == Pipeline::StartChannel::channel_capacity
                && marked.channels[4].capacity == 0;
        }());
        using drain_process =
            cc::LoopProcess<loop::DrainName, cc::ReceivesOn<loop::RingChannel>, cc::StageEdges<graph, 0>>;
        using detect_process = cc::LoopProcess<loop::DetectName, cc::StageEdges<graph, 1>>;
        using build_process = cc::LoopProcess<loop::BuildName, cc::StageEdges<graph, 2>>;
        using job_foreground =
            cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::RingChannel>,
                            cc::SendsOn<loop::OwnerMailboxChannel>, cc::ReceivesOn<loop::JobDoneChannel>,
                            cc::ReceivesOn<loop::ProcessedCountChannel>>;
        using flush_arm = cc::StageLoop<
            cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::RingChannel>,
                            cc::ReceivesOn<loop::ProcessedCountChannel>>,
            drain_process, detect_process, build_process,
            cc::LoopProcess<loop::PublishName, cc::StageEdges<graph, 3>, cc::SendsOn<loop::ProcessedCountChannel>>>;
        using job_arm = cc::StageLoop<job_foreground, drain_process, detect_process, build_process,
                                      cc::LoopProcess<loop::PublishName, cc::ReceivesOn<loop::OwnerMailboxChannel>,
                                                      cc::SendsOn<loop::JobDoneChannel>, cc::StageEdges<graph, 3>,
                                                      cc::SendsOn<loop::ProcessedCountChannel>>>;
        using late_job_arm = cc::StageLoop<
            job_foreground, drain_process, detect_process, build_process,
            cc::LoopProcess<loop::PublishName, cc::StageEdges<graph, 3>, cc::SendsOn<loop::ProcessedCountChannel>,
                            cc::ReceivesOn<loop::OwnerMailboxChannel>, cc::SendsOn<loop::JobDoneChannel>>>;
        cc::ensure_arms_live<flush_arm, job_arm, late_job_arm>();
        // The channels of the flush arm are the ring, the count and the three
        // channels between the stages, and each takes the capacity of its
        // type.
        static_assert([] consteval noexcept {
            constexpr auto marked = cc::marked_system_of_loop<flush_arm>();
            return marked.channels[0].capacity == TraceRing::CAPACITY
                && marked.channels[1].capacity == loop::ProcessedCountChannel::channel_capacity
                && marked.channels[2].capacity == Pipeline::TraceBatchChannel::channel_capacity
                && marked.channels[3].capacity == Pipeline::BuildWorkChannel::channel_capacity
                && marked.channels[4].capacity == Pipeline::GraphPublishChannel::channel_capacity;
        }());

        std::move(pipeline).run(ctx);
    };

    // A frame door of each channel opens its two ends and gives them to its
    // body (the frame doors of fixy/concurrent/PermissionedChannel.h).  The
    // stages, the pipeline and its run live in the innermost body, so each
    // stage joins inside the frame of each channel, and each handle ends
    // before its channel.  Each body splits the root of the next channel,
    // and each inner body takes the handles of the outer frames by move.
    namespace cc = ::fixy::concurrent;
    auto [start_prod_perm, start_cons_perm] =
        perm::mint_permission_split<typename Pipeline::StartChannel::producer_tag,
                                    typename Pipeline::StartChannel::consumer_tag>(detail::bg_pipeline::start_root());
    cc::with_ends(
        start, std::move(start_prod_perm), std::move(start_cons_perm), [&](auto start_prod, auto start_end) noexcept {
            auto [trace_prod_perm, trace_cons_perm] =
                perm::mint_permission_split<typename Pipeline::TraceBatchChannel::producer_tag,
                                            typename Pipeline::TraceBatchChannel::consumer_tag>(
                    detail::bg_pipeline::trace_batch_root());
            cc::with_ends(
                trace_batches, std::move(trace_prod_perm), std::move(trace_cons_perm),
                [&, start_prod = std::move(start_prod),
                 start_end = std::move(start_end)](auto trace_prod, auto trace_end) mutable noexcept {
                    auto [build_prod_perm, build_cons_perm] =
                        perm::mint_permission_split<typename Pipeline::BuildWorkChannel::producer_tag,
                                                    typename Pipeline::BuildWorkChannel::consumer_tag>(
                            detail::bg_pipeline::build_work_root());
                    cc::with_ends(
                        build_work, std::move(build_prod_perm), std::move(build_cons_perm),
                        [&, start_prod = std::move(start_prod), start_end = std::move(start_end),
                         trace_prod = std::move(trace_prod),
                         trace_end = std::move(trace_end)](auto build_prod, auto build_end) mutable noexcept {
                            auto [publish_prod_perm, publish_cons_perm] =
                                perm::mint_permission_split<typename Pipeline::GraphPublishChannel::producer_tag,
                                                            typename Pipeline::GraphPublishChannel::consumer_tag>(
                                    detail::bg_pipeline::graph_publish_root());
                            cc::with_ends(graph_publish, std::move(publish_prod_perm), std::move(publish_cons_perm),
                                          [&, start_prod = std::move(start_prod), start_end = std::move(start_end),
                                           trace_prod = std::move(trace_prod), trace_end = std::move(trace_end),
                                           build_prod = std::move(build_prod), build_end = std::move(build_end)](
                                              auto publish_prod, auto publish_end) mutable noexcept {
                                              run_stages(std::move(start_prod), std::move(start_end),
                                                         std::move(trace_prod), std::move(trace_end),
                                                         std::move(build_prod), std::move(build_end),
                                                         std::move(publish_prod), std::move(publish_end));
                                          });
                        });
                });
        });
}

}  // namespace crucible
