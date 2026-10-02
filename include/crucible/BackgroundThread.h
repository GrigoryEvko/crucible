#pragma once

// The background thread of the Vigil: it drains the trace ring, finds the
// iteration boundaries, builds a trace graph for each iteration and
// publishes a region with its memory plan.
//
// The class, its payload types and the owner mailbox are here.  The thread,
// the channels, the four pipeline stages, the trace graph build and the
// memory plan are cold or background code, so they are in
// src/BackgroundThread.cpp.  Each translation unit that includes this header
// then compiles no part of the pipeline and no channel type.

#include <atomic>
#include <bit>
#include <cstdint>
#include <expected>
#include <memory>
#include <type_traits>
#include <vector>

#include <crucible/IterationDetector.h>
#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/TraceGraph.h>
#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/handle/OneShotFlag.h>
#include <fixy/handle/PublishCommit.h>
#include <fixy/os/SpinLock.h>
#include <foundation/AlignedBuffer.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

namespace crucible {

// Drains the ring buffer, detects iteration boundaries and builds a trace
// graph per iteration.
//
// Scratch buffers (PtrMap, SlotInfo, Edge) are allocated once and reused, so
// the drain path performs no per-call allocation.
struct BackgroundThread {
    // start() sets each pointer one time, and never to null.
    ::fixy::WriteOnceNonNull<TraceRing*> ring = ::fixy::mint_write_once_non_null<TraceRing*>();
    ::fixy::WriteOnceNonNull<MetaLog*> meta_log = ::fixy::mint_write_once_non_null<MetaLog*>();

    int32_t rank = -1;
    int32_t world_size = 0;
    // A vendor-encoded hardware identity measured by the startup calibration
    // pass, not synthesized or defaulted.  Zero marks the pre-init state.
    using DeviceCapability = ::fixy::Tagged<uint64_t, ::fixy::tags::source::Meridian>;
    DeviceCapability device_capability{};

    // Written by the background thread, read by the foreground.  The
    // store(release) publishes the region data written before it, and the
    // foreground's load(acquire) must see that data.  Relaxed would let the
    // foreground dereference a pointer to a region with garbage fields.
    //
    // Own cache line, because the foreground reads it while the background
    // writes the fields that follow.
    alignas(64) std::atomic<RegionNode*> active_region{nullptr};

    // Proof that the caller owns the state that the region callback writes.
    //
    // Only this class builds one, and it builds a fresh one for each call
    // that runs on the owning thread: the region callback, and each job
    // that run_on_publish_stage posts.  The call takes the proof by value,
    // as a prvalue that guaranteed elision builds in the parameter, so the
    // proof dies when the call returns.  A job that keeps the address of
    // its proof then keeps an invalid pointer, and no later use of it is
    // legal C++.  While no publish stage runs, run_on_publish_stage builds
    // one for its caller, who then owns that state.  The class cannot be
    // copied or moved, so a thread cannot pass it to another thread by
    // value, and it is not trivially copyable and not an implicit-lifetime
    // type, so no bit_cast and no lifetime start over bytes can make one.
    //
    // This is the ownership half of concurrent separation logic (O'Hearn,
    // Resources, Concurrency and Local Reasoning, 2007).  One thread owns
    // the resource, the frame rule keeps every other thread out of it, and
    // ownership moves by a message to the owner, never by sharing.
    class PublishStage {
        PublishStage() noexcept {}
        friend struct BackgroundThread;

    public:
        PublishStage(const PublishStage&) = delete("the publish-stage proof stays on the thread that owns the state");
        PublishStage(PublishStage&&) = delete("the publish-stage proof stays on the thread that owns the state");
        PublishStage&
        operator=(const PublishStage&) = delete("the publish-stage proof stays on the thread that owns the state");
        PublishStage&
        operator=(PublishStage&&) = delete("the publish-stage proof stays on the thread that owns the state");
        // User-provided, because GCC counts a class whose copy and move are
        // all deleted as trivially copyable.  A destructor that is not
        // trivial takes the class out of trivially copyable and out of
        // implicit-lifetime, and costs nothing: the body is empty.
        ~PublishStage() {}
    };

    static_assert(std::is_empty_v<PublishStage>);
    static_assert(!std::is_default_constructible_v<PublishStage>);
    static_assert(!std::is_copy_constructible_v<PublishStage> && !std::is_move_constructible_v<PublishStage>);
    static_assert(!std::is_trivially_copyable_v<PublishStage>, "a trivially copyable proof is forgeable by bit_cast");
    static_assert(!std::is_implicit_lifetime_v<PublishStage>,
                  "an implicit-lifetime proof is forgeable by a lifetime start over bytes");

    struct RegionReadyCallback {
        // The noexcept is load-bearing.  The callback runs on the publish
        // stage in the middle of a publication, so an exception out of it
        // would leave the transaction commit and the mode state half done.
        // Nothing in the tree throws, and utils/scripts/check-no-throw-no-rtti.sh
        // refuses an artifact that references __cxa_throw.  The noexcept
        // also puts the rule in the type: a function that is not noexcept
        // does not convert to Fn.
        //
        // The callback receives the background context of the thread that
        // runs it.  A callee that acts under a background context takes that
        // one, and never builds its own.  It also receives the publish-stage
        // proof, which is what lets it write state that the publish stage
        // owns.
        using Fn = void (*)(void*, ::foundation::effects::Bg const&, PublishStage, RegionNode*) noexcept;

        void* ctx = nullptr;
        Fn fn = nullptr;

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return fn != nullptr; }

        // The caller proves that it runs on the stage, and the callback
        // gets a proof of its own that dies with the call.
        void operator()(::foundation::effects::Bg const& bg, PublishStage const&, RegionNode* region) const noexcept {
            fn(ctx, bg, PublishStage{}, region);
        }
    };

    // Own cache line: background-only state.  Sharing a line with
    // active_region would drag the callback object into the foreground's
    // cache on every acquire load.
    alignas(64) RegionReadyCallback region_ready_cb;

    // A job that a caller hands to the owner of the publish-stage state.
    // It lives on the caller's stack, and the caller waits on done, so the
    // record outlives every read the publish stage makes of it.
    struct OwnerJob {
        void* ctx = nullptr;
        void (*fn)(void*, PublishStage) noexcept = nullptr;
        std::atomic<bool> done{false};
    };

    // Runs job on the thread that owns the publish-stage state, and returns
    // when it has run.  While the publish stage runs, that thread is the
    // publish stage, which runs the job between two publications.  While
    // no publish stage runs, the state has no other writer, and the caller
    // runs the job itself.  Either way, the job and the region callback
    // never overlap.
    //
    // Cold path.  The caller spins while the publish stage finishes the
    // publication in hand, which is bounded by one region build.  A thread
    // that holds the stage already, in a region callback or in a job, must
    // not call this: it would wait on itself, so the contract stops it.
    template <typename Job>
        requires std::is_nothrow_invocable_v<Job&, PublishStage const&>
    void run_on_publish_stage(Job&& job) noexcept {
        CRUCIBLE_ASSERT(stage_held_by_this_thread_ != this);
        using JobType = std::remove_reference_t<Job>;
        OwnerJob request{};
        request.ctx = static_cast<void*>(std::addressof(job));
        request.fn = [](void* ctx, PublishStage stage) noexcept { (*static_cast<JobType*>(ctx))(stage); };
        while (true) {
            OwnerJob* seen = owner_mailbox_.load(std::memory_order_acquire);
            if (seen == nullptr) {
                if (owner_mailbox_.compare_exchange_weak(seen, &request, std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
                    while (!request.done.load(std::memory_order_acquire)) {
                        CRUCIBLE_SPIN_PAUSE;
                    }
                    return;
                }
                continue;
            }
            if (seen == &mailbox_closed_) {
                if (owner_mailbox_.compare_exchange_weak(seen, &mailbox_inline_, std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
                    const BackgroundThread* const held_before = stage_held_by_this_thread_;
                    stage_held_by_this_thread_ = this;
                    request.fn(request.ctx, PublishStage{});
                    stage_held_by_this_thread_ = held_before;
                    owner_mailbox_.store(&mailbox_closed_, std::memory_order_release);
                    return;
                }
                continue;
            }
            // Another job is in flight, on the publish stage or inline.
            CRUCIBLE_SPIN_PAUSE;
        }
    }

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
    alignas(64)::fixy::Monotonic<uint32_t> iterations_completed = ::fixy::mint_monotonic<uint32_t>(0);
    uint32_t last_iteration_length = 0;

private:
    // Each acquisition mints a token of this tag, so the token witnesses
    // that the acquisition comes from inside this class.  The tag and the
    // gate below are both private.  Code outside the class cannot name the
    // tag, and it cannot reach the gate to deduce the tag from its type.
    struct ArenaAllocGateTag {
        using permission_row = ::foundation::effects::Row<>;
    };

    // The build stage and the publish stage are separate pipeline threads.
    // Arena storage is append-only, so pointers already handed out stay
    // valid, but the bump cursor is shared mutable state.  This gate
    // serializes the two allocation windows against each other without
    // weakening the permissioned SPSC stage topology.
    //
    // Both stages run under a background context, so the gate is the
    // blocking one: a waiter sleeps in the kernel until the holder releases.
    // The windows it covers are kept to the arena bumps themselves, because
    // every extra statement inside the gate is more time the other stage
    // sleeps.
    alignas(64)::fixy::spin::BlockingLock<ArenaAllocGateTag> arena_alloc_gate_;

public:
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
        ::fixy::AtomicMonotonic<uint64_t> total_ = ::fixy::mint_atomic_monotonic<uint64_t>(0);
    };

    UncompiledRegionQueue uncompiled_regions;

    // A one-way signal for a single run() invocation.  start() re-arms it
    // under quiescence before launching the next pipeline.
    alignas(64)::fixy::handle::OneShotFlag stop_requested;

private:
    // The pipeline: its thread, the types of its four channels, the input
    // end of each stage and the four stage functions.  src/BackgroundThread.cpp
    // defines it, so no other translation unit compiles a channel type.  The
    // class is nested, so its stage functions reach the private state of the
    // thread that they serve.  start() makes the one object that holds the
    // thread, and the destructor of this class joins it.
    struct Pipeline;
    std::unique_ptr<Pipeline> pipeline_;

public:
    // Total entries fully processed.  The write surface is friend-gated to
    // PublishStageAuth so only the publishing stage can advance it: bumping
    // from an earlier stage races the publish callback, and the gate turns
    // that into a "private member" diagnostic.
    //
    // The foreground must see every prior background write when its acquire
    // load sees the count.  bump_by's acq_rel supplies the release half.
    struct PublishStageAuth;
    struct PublishStageTag {};
    using TotalProcessedCell = ::fixy::handle::PublishCommitCell<PublishStageTag, PublishStageAuth>;
    TotalProcessedCell total_processed;

    // The foreground raises this when compiled replay diverges.  The
    // background clears its accumulated trace and detector on observing it,
    // so leftover signature ops from the pre-divergence iteration cannot
    // poison the next region.  The detector keeps one thing: the periods
    // that broke, so a layer square that diverged is not picked again.
    //
    // Own cache line: the foreground writes it while the background reads it
    // each drain cycle, and the neighbouring fields are background-private.
    alignas(64)::fixy::handle::OneShotFlag reset_requested;

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
    alignas(64)::fixy::AtomicMonotonic<uint32_t> reset_epoch = ::fixy::mint_atomic_monotonic<uint32_t>(0);

    // The mailbox of the publish-stage state.  It holds one of four values:
    //   &mailbox_closed_  no publish stage runs, so a caller may own the state
    //   &mailbox_inline_  a caller runs a job while no publish stage runs
    //   nullptr           the publish stage runs and no job waits
    //   a job             the publish stage runs, and this job waits for it
    // The publish stage opens it as its first act and closes it as its last,
    // so a job posted while the stage runs runs on the stage, and any other
    // job runs on its caller.  The stage waits to open while a caller runs a
    // job inline.  No job is claimed twice, because each side claims it by
    // one compare-and-swap on this word.
    // Defined after the class: a job's member initializers are not usable
    // until the enclosing class is complete.
    static OwnerJob mailbox_closed_;
    static OwnerJob mailbox_inline_;
    alignas(64) std::atomic<OwnerJob*> owner_mailbox_{&mailbox_closed_};

    // One acquire load when no job waits, which is the common case.  While
    // the stage runs, the mailbox holds no sentinel: the stage opened it,
    // and only the stage closes it.
    // The parameter proves that the caller is the stage.  The job gets a
    // proof of its own.
    void serve_owner_mailbox_(PublishStage const&) noexcept {
        OwnerJob* const job = owner_mailbox_.load(std::memory_order_acquire);
        if (job == nullptr) return;
        CRUCIBLE_ASSERT(job != &mailbox_closed_ && job != &mailbox_inline_);
        job->fn(job->ctx, PublishStage{});
        owner_mailbox_.store(nullptr, std::memory_order_release);
        // The caller returns once it reads done, and its record dies with
        // its stack frame, so nothing reads the record after this store.
        job->done.store(true, std::memory_order_release);
    }

    void close_owner_mailbox_(PublishStage const&) noexcept {
        OwnerJob* const last = owner_mailbox_.exchange(&mailbox_closed_, std::memory_order_acq_rel);
        if (last == nullptr) return;
        last->fn(last->ctx, PublishStage{});
        last->done.store(true, std::memory_order_release);
    }

    void open_owner_mailbox_() noexcept {
        while (true) {
            OwnerJob* seen = &mailbox_closed_;
            if (owner_mailbox_.compare_exchange_weak(seen, nullptr, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
                return;
            }
            // The stage waits while a caller runs a job inline.  Any value
            // that is not a sentinel means a second stage runs, and the
            // state would then have two owners.
            CRUCIBLE_ASSERT(seen == &mailbox_inline_ || seen == &mailbox_closed_);
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // The background thread whose stage this thread holds, or null.  Read
    // only by the contract in run_on_publish_stage, on the cold path.  It is
    // one object for the process, so every shared library reads the same
    // copy on a thread.
    CRUCIBLE_PROCESS_WIDE static inline thread_local const BackgroundThread* stage_held_by_this_thread_ = nullptr;

    // The publish stage holds the mailbox open for as long as this scope
    // lives, so every way out of the stage closes it.  A mailbox left open
    // would make each later job wait for a stage that no longer runs.  The
    // proof that the constructor takes shows that the stage opens the
    // scope, and the scope keeps no reference to it.
    class OwnerMailboxScope {
    public:
        OwnerMailboxScope(BackgroundThread& owner, PublishStage const&) noexcept
            : owner_{owner}, held_before_{stage_held_by_this_thread_} {
            owner_.open_owner_mailbox_();
            stage_held_by_this_thread_ = &owner_;
        }
        ~OwnerMailboxScope() {
            stage_held_by_this_thread_ = held_before_;
            owner_.close_owner_mailbox_(PublishStage{});
        }
        OwnerMailboxScope(const OwnerMailboxScope&) = delete("the scope closes the mailbox once");
        OwnerMailboxScope& operator=(const OwnerMailboxScope&) = delete("the scope closes the mailbox once");

    private:
        BackgroundThread& owner_;
        const BackgroundThread* held_before_;
    };

    static constexpr uint32_t BATCH_SIZE = 4096;

    // The payloads below are plain data.  A stage reaches the thread it
    // serves through its input end (StageInput), so no payload points back
    // at the owner, and the effect row of each payload is read off data
    // alone.
    struct BgTraceBatch {
        uint32_t count = 0;
        TraceRing::Entry entries[BATCH_SIZE]{};
        MetaIndex meta_starts[BATCH_SIZE]{};
        ScopeHash scope_hashes[BATCH_SIZE]{};
        CallsiteHash callsite_hashes[BATCH_SIZE]{};
    };

    struct BgBuildWork {
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
    // The go signal of the drain stage.  run() pushes it once, before the
    // stages start.
    struct BgPipelineStart {};

    // Lifetime: the producing stage owns it until it is pushed, after which
    // the consuming stage owns it and deletes it once it is handled.  The
    // graph itself is arena-allocated and outlives this wrapper.
    //
    // Three shapes flow through the channel, and kind names each one:
    //   Region   graph is non-null: an iteration region to publish.
    //   Commit   graph is null: a marker that carries the entry count.
    //   Release  graph is null: the end of the metadata that a build read
    //            before it met an op with tensors and no metadata index.
    //
    // The marker has to travel through the publish stage so that
    // total_processed advances only after every region ahead of it has been
    // published.  Bumping the counter at the build stage instead races the
    // publish stage: a flush can return while a region is still queued, and
    // the caller then observes a compiled mode with no pending region, or an
    // active region whose plan is not finished.
    //
    // The release travels through the publish stage too.  That stage is the
    // one writer of the tail of the metadata log, and the works reach it in
    // the order of the recording, so each release is past the one before.  A
    // release from the build stage can pass the end of a region that is still
    // on its way to the publish stage.  The publish of that region then moves
    // the tail back, and the contract of the tail stops the process.
    struct BgGraphPublish {
        enum class Kind : uint8_t {
            Region,
            Commit,
            Release
        };
        Kind kind = Kind::Commit;
        TraceGraph* graph = nullptr;
        uint32_t commit_count = 0;
        // The new tail of the metadata log.  Only a release sets it.
        uint32_t meta_end = 0;
        // Carried through from the BgBuildWork this came from, so a reset
        // that lands while the build stage is mid-graph is still caught
        // here, one stage later.
        uint32_t epoch = 0;
    };

    // Gives each trace vector room for two batches.  start() calls it
    // before the pipeline thread starts.  The body stays here, where the
    // reserve ban of utils/scripts/check-banned-calls.py reads it.
    void reserve_iteration_buffers_() {
        constexpr uint32_t kInitialTraceCapacity = BATCH_SIZE * 2;
        current_trace.reserve(kInitialTraceCapacity);
        current_meta_starts.reserve(kInitialTraceCapacity);
        current_scope_hashes.reserve(kInitialTraceCapacity);
        current_callsite_hashes.reserve(kInitialTraceCapacity);
    }

    // Makes a commit marker: a work item that carries only the count of
    // entries that one batch consumed.
    [[nodiscard]] BgBuildWork* make_commit_work(uint32_t count);

    // The detect stage calls this at each iteration boundary that the
    // detector reports.  It removes the warmup ops before the iteration,
    // copies the completed iteration into a work item, and keeps the last K
    // ops for the next signature.  It returns null when the iteration is
    // empty or no metadata log is set.
    [[nodiscard]] BgBuildWork* prepare_iteration_build_work();

    // Releases a graph the publish stage will not publish.  The arena holds
    // the graph itself and never gives storage back, but the metadata log is
    // a ring the foreground keeps writing into, so its tail has to advance
    // past the entries this graph read or the log fills for good.
    void discard_trace_graph(PublishStage const& stage, TraceGraph* graph) CRUCIBLE_NO_THREAD_SAFETY;

    // Moves the tail of the metadata log to meta_end, which gives the entries
    // before it back to the foreground.  Zero releases nothing.  The publish
    // stage is the one writer of the tail, and the proof shows that the
    // caller is that stage.  A meta_end that does not move the tail forward
    // fails the contract of the tail.
    void release_meta_log(PublishStage const& stage, uint32_t meta_end) CRUCIBLE_NO_THREAD_SAFETY;

private:
    // Takes the gate under the context of the calling stage.
    //
    // A root mint at each acquisition is sound here.  The tag and the gate
    // are private, so only this class can mint a token of the tag.  The
    // token is a compile-time witness: the gate reads its type and no
    // state, and the token dies when the guard releases the gate.  The
    // mutual exclusion comes from the gate, not from the token.
    template <class Ctx, class Body>
        requires ::fixy::spin::CtxMayBlock<Ctx> && std::is_nothrow_invocable_v<Body&>
    void with_arena_alloc_gate_(Ctx const& ctx, Body&& body) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<ArenaAllocGateTag>();
        ::fixy::spin::GateGuard guard{ctx, arena_alloc_gate_, proof};
        body();
    }

public:
    // Takes the arena gate once, without a wait, and releases it at once.
    // True when no stage held the gate.  A test uses it to show that the
    // region callback runs outside the gate.
    template <class Ctx>
        requires ::fixy::spin::CtxMayBlock<Ctx>
    [[nodiscard]] bool try_probe_arena_alloc_gate(Ctx const& ctx) noexcept {
        auto proof = ::foundation::permissions::mint_permission_root<ArenaAllocGateTag>();
        ::fixy::spin::GateGuard guard{std::try_to_lock, ctx, arena_alloc_gate_, proof};
        return guard.was_acquired();
    }

    // The background context of the publish stage allocates the region and
    // is handed to the region callback, which runs on the same thread.
    void publish_trace_graph(::foundation::effects::Bg const& bg, PublishStage const& stage, TraceGraph* graph)
        CRUCIBLE_NO_THREAD_SAFETY;

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

    // Seals the global schema and kernel tables, sets the ring and the
    // metadata log, and starts the pipeline thread.  The thread runs
    // run_in_row under the background context, which it takes from the
    // door.
    //
    // Sealing the global registration tables here turns "all registrations
    // complete before the background thread starts" into a load-bearing
    // rule: a late registration is rejected by the table contract instead of
    // racing the lookups this thread performs.
    void start(TraceRing* ring_ptr, MetaLog* meta_log_ptr, int32_t rank_ = -1, int32_t world_size_ = 0,
               uint64_t device_cap = 0) CRUCIBLE_NO_THREAD_SAFETY;

    // Signals the drain stage to stop and joins the pipeline thread.  The
    // pipeline joins its stage threads before the thread ends.
    void stop() CRUCIBLE_NO_THREAD_SAFETY;

    ~BackgroundThread() CRUCIBLE_NO_THREAD_SAFETY;

    // src/BackgroundThread.cpp defines it, where the pipeline is a complete
    // type.
    BackgroundThread();
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
    using PtrMapKey = ::fixy::Tagged<void*, ::fixy::tags::source::External>;

    struct PtrSlot {
        PtrMapKey key{};
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
    // offsets 8 through 31, so the slot build of build_trace_from copies the
    // whole block with one 24-byte memcpy instead of ten field assignments.
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

    ::foundation::AlignedBuffer<PtrSlot> scratch_map_;
    ::foundation::AlignedBuffer<SlotInfo> scratch_slots_;
    ::foundation::AlignedBuffer<Edge> scratch_edges_;
    uint8_t map_gen_ = 0;

    // Zero means not yet allocated.  ptr_mask_ is a derived view of map_cap_
    // kept raw because the inner probe loop loads it every iteration.
    ::fixy::Monotonic<uint32_t> map_cap_ = ::fixy::mint_monotonic<uint32_t>(0);
    uint32_t ptr_mask_ = 0;
    ::fixy::Monotonic<uint32_t> slot_cap_max_ = ::fixy::mint_monotonic<uint32_t>(0);
    ::fixy::Monotonic<uint32_t> edge_cap_max_ = ::fixy::mint_monotonic<uint32_t>(0);

    // build_trace_from calls this after its scan of the trace, which
    // supplies exact counts.  Buffers grow and never shrink, so the cost
    // amortizes across iterations.
    void ensure_scratch_buffers(uint32_t total_inputs, uint32_t total_outputs);

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
                slot.key = ::fixy::mint_tagged<::fixy::tags::source::External>(key);
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
    // externally observable side effect, and sleeps on the arena gate.
    // Those are the four atoms below.  The caller hands a context whose row
    // admits all four, and a context that admits fewer is a compile error.
    using run_required_row =
        ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc,
                                   ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

    // The row is exactly the background load context's row, so the entry of
    // the pipeline thread builds that context and no wider one exists.
    static_assert(std::is_same_v<run_required_row, typename ::fixy::BgLoadCtx::row_type>,
                  "BackgroundThread::run_required_row must be exactly Row<Bg, Alloc, IO, Block>, the row of "
                  "fixy::BgLoadCtx.  Adding or removing an atom changes the context every site that runs "
                  "this thread must hand in.");

    // The context is the evidence for the row.  It comes from the door of
    // the background context, or from the test witness, and nothing else
    // builds one.
    //
    // Runs the four pipeline stages, and returns when the publish stage
    // gets the stop sentinel.  The drain stage sends that sentinel after a
    // signal on stop_requested.
    //
    // CtxAdmits holds only for a context over the background capability
    // whose row names all four atoms.  fixy::BgLoadCtx is that context, so
    // the body hands it to run_pipeline_, which takes that one type.
    // Another spelling of the same row passes the constraint and does not
    // convert.
    template <class Ctx>
        requires ::foundation::effects::CtxAdmits<Ctx, run_required_row>
    void run_in_row(Ctx const& ctx) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        run_pipeline_(ctx);
    }

private:
    // The body of run_in_row, in src/BackgroundThread.cpp.  Only that
    // translation unit instantiates the stages, the channels and the
    // pipeline.
    void run_pipeline_(::fixy::BgLoadCtx const& ctx) noexcept CRUCIBLE_NO_THREAD_SAFETY;

public:
    static constexpr uint32_t MAX_SLOTS = 65536;

    // The error of a build that meets an op with tensors and no metadata
    // index.  The foreground records such an op when the metadata log is
    // full, and the build then makes no graph.  meta_end is the end of the
    // metadata that the build read before that op, or zero when it read none.
    // The build does not release that metadata, because only the publish
    // stage writes the tail of the log (release_meta_log).
    struct MetaLogOverflow {
        uint32_t meta_end = 0;
    };

    // A non-null graph, or the overflow that stopped the build.
    using TraceBuild = std::expected<TraceGraph*, MetaLogOverflow>;

    // Turns ring entries plus tensor metadata into a CSR property graph.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] TraceBuild build_trace(::foundation::effects::Alloc a, uint32_t count)
        CRUCIBLE_NO_THREAD_SAFETY {
        return build_trace_from(a, count, current_trace.data(), current_meta_starts.data(), current_scope_hashes.data(),
                                current_callsite_hashes.data());
    }

    // build_trace over caller-owned arrays: count entries of trace_data,
    // with meta_data, scope_data and callsite_data in parallel.  The build
    // stage calls it on each work item.  It reads the metadata log and never
    // writes its tail.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] TraceBuild
    build_trace_from(::foundation::effects::Alloc a, uint32_t count, const TraceRing::Entry* trace_data,
                     const MetaIndex* meta_data, const ScopeHash* scope_data, const CallsiteHash* callsite_data)
        CRUCIBLE_NO_THREAD_SAFETY;

    // Orders birth and death events with an O(n + k) counting sort, then
    // assigns offsets with a sweep line.  The alignment is what the GPU
    // needs for coalesced access.  The birth_op of each internal slot is at
    // most its death_op, and the death_op is at most UINT32_MAX - 3.  The
    // function stops the process when a slot breaks this rule, because the
    // sweep would index past its arrays.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] MemoryPlan* compute_memory_plan(::foundation::effects::Alloc a,
                                                                               TensorSlot* slots, uint32_t num_slots)
        CRUCIBLE_NO_THREAD_SAFETY;
};

// The two sentinels of the owner mailbox.  Only their addresses matter, so
// each is one object for the process.  A job run from one shared library
// then compares against the address that the stage in another one stored.
CRUCIBLE_PROCESS_WIDE inline constinit BackgroundThread::OwnerJob BackgroundThread::mailbox_closed_{};
CRUCIBLE_PROCESS_WIDE inline constinit BackgroundThread::OwnerJob BackgroundThread::mailbox_inline_{};

}  // namespace crucible
