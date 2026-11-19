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
#include <cstdint>
#include <memory>
#include <type_traits>

#include <crucible/IterationDetector.h>
#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/TraceGraph.h>
#include <crucible/TraceRing.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/handle/OneShotFlag.h>
#include <fixy/handle/PublishCommit.h>
#include <fixy/os/OwnedThread.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Mix.h>

namespace crucible {

// The limits of the memory of a background pipeline.  The owner gives them at
// construction, and they stay for the life of the pipeline.  Vigil::Config
// holds one, with these defaults.
//
//   scratch_retained  the bytes of blocks that each of the two scratch arenas
//                     keeps when an epoch ends.  An arena that holds more
//                     gives back each block but the first
//                     (BumpArena::set_retained_limit)
//   region_store      the count of regions that the region store holds.  A
//                     publish into a full store discards its region and
//                     counts it.  A store of zero regions discards each region
//   uncompiled_queue  the count of published regions that the uncompiled
//                     queue keeps.  A push into a full queue drops the oldest
//                     region.  A queue of zero regions keeps none and only
//                     counts
//
// With the default limits, the holders of the regions name at most 64 + 16 +
// 11 + 2 regions: the uncompiled queue, the transaction log of a Vigil, the
// holders of its foreground, and the last published region with the handed
// region.  The store of 128 regions keeps a margin above that count.
struct PipelineLimits {
    static constexpr uint32_t DEFAULT_REGION_STORE = 128;
    static constexpr uint32_t DEFAULT_UNCOMPILED_QUEUE = 64;

    ::fixy::Quantity<::fixy::unit::Byte, size_t> scratch_retained{::fixy::BumpArena::default_retained_bytes_limit};
    ::fixy::Quantity<::fixy::unit::Element, uint32_t> region_store{DEFAULT_REGION_STORE};
    ::fixy::Quantity<::fixy::unit::Element, uint32_t> uncompiled_queue{DEFAULT_UNCOMPILED_QUEUE};
};

// Drains the ring buffer, detects iteration boundaries and builds a trace
// graph per iteration.
//
// Scratch buffers (PtrMap, SlotInfo, Edge) are allocated once and reused, so
// the drain path performs no per-call allocation.
//
// The foreground thread and the stages of the pipeline share one object of
// this class, each stage through a Ref of its input (foundation/Transfer.h).
// Each field that two threads use is an atomic, a ring of one producer and
// one consumer, or a field of one stage alone, as the comment of the field
// tells, so the class states that two threads use it at the same time.
struct[[= ::foundation::transfer::shareable{}]] BackgroundThread {
    // The first start() sets each pointer, and never to null.  Each later
    // start() names the same ring and the same log, so no start sets a
    // pointer again.
    ::fixy::WriteOnceNonNull<TraceRing*> ring = ::fixy::mint_write_once_non_null<TraceRing*>();
    ::fixy::WriteOnceNonNull<MetaLog*> meta_log = ::fixy::mint_write_once_non_null<MetaLog*>();

    int32_t rank = -1;
    int32_t world_size = 0;
    // A vendor-encoded hardware identity measured by the startup calibration
    // pass, not synthesized or defaulted.  Zero marks the pre-init state.
    using DeviceCapability = ::fixy::Tagged<uint64_t, ::fixy::tags::source::Meridian>;
    DeviceCapability device_capability{};

    // The last published region.  The publish stage writes it, and a reader
    // on another thread loads it.  The store(release) publishes the region
    // data written before it, and the reader's load(acquire) must see that
    // data.  Relaxed would let the reader dereference a pointer to a region
    // with garbage fields.  The owner of the pipeline gives its foreground
    // the region through its own slot, which carries the reset point.
    //
    // Own cache line, because a reader reads it while the background writes
    // the fields that follow.  The reference is empty until the first
    // publication.
    alignas(64)::fixy::Atomic<::fixy::WeakRef<RegionNode>> active_region{};

    // A point of the recorded op stream, and the kind of the reset at it.
    // The point is the count of ops that the producer put into the trace ring
    // before it.  A reset applies at its point, so each op belongs to one
    // side of it.
    //
    // A restart follows a divergence: at the point, the detect stage drops
    // the ops that it holds and restarts the iteration detector.  A mark
    // follows a rollback or a load: each region cut before the point is
    // stale, and the detector keeps its ops and its history.
    //
    // One word holds the two: twice the count, plus one for a restart.  So
    // the points order by the count, and at one count a restart comes after
    // a mark.  The count stays below 2^63, which a stream of one op in each
    // nanosecond reaches after 292 years.
    class StreamPoint {
    public:
        constexpr StreamPoint() noexcept = default;

        [[nodiscard]] static constexpr StreamPoint restart_at(uint64_t ops_before) noexcept {
            return StreamPoint{(ops_before << 1) | uint64_t{1}};
        }
        [[nodiscard]] static constexpr StreamPoint mark_at(uint64_t ops_before) noexcept {
            return StreamPoint{ops_before << 1};
        }

        [[nodiscard]] constexpr uint64_t ops_before() const noexcept { return word_ >> 1; }
        [[nodiscard]] constexpr bool restarts_detector() const noexcept { return (word_ & uint64_t{1}) != 0; }

        auto operator<=>(const StreamPoint&) const = default;

    private:
        explicit constexpr StreamPoint(uint64_t word) noexcept : word_{word} {}

        uint64_t word_ = 0;
    };

    // A region that the publish stage gives to its owner, and the point of
    // the last reset that the detect stage applied before the first op of
    // the region.  The region store holds the record with its region, so
    // the address of the record lives as long as the region.
    struct PublishedRegion {
        ::fixy::WeakRef<RegionNode> region{};
        StreamPoint reset_point{};
    };

    // Proof that the caller owns the state that the region callback writes.
    //
    // Only this class builds one, and it builds a fresh one for each call
    // that runs on the owning thread: the region callback, and each job
    // that run_on_publish_stage posts.  The callee takes a reference to a
    // temporary that its call builds, so the proof dies when the call
    // returns.  A callee that keeps the address of its proof then keeps an
    // invalid pointer, and no later use of it is legal C++.  While no
    // publish stage runs, run_on_publish_stage builds one for its caller,
    // who then owns that state.  The class cannot be
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

    // The call of the callback of each published region.
    //
    // The noexcept is load-bearing.  The callback runs on the publish stage
    // in the middle of a publication, so an exception out of it would leave
    // the transaction commit and the mode state half done.  Nothing in the
    // tree throws, and the rule artifact-symbols refuses an artifact that
    // references __cxa_throw.  The noexcept also puts the rule in the type:
    // a callable that is not noexcept does not fit a CallRef of a noexcept
    // signature.
    //
    // The callback receives the background context of the thread that runs
    // it.  A callee that acts under a background context takes that one, and
    // never builds its own.  It also receives the publish-stage proof, which
    // is what lets it write state that the publish stage owns.  The record of
    // the region lives in the region store.
    using RegionReadyCall =
        ::fixy::CallRef<void(::foundation::effects::Bg const&, PublishStage const&, PublishedRegion const&) noexcept>;

    // The callback of a BackgroundThread that its holder gives no callback.
    static void ignore_region_(::foundation::effects::Bg const&, PublishStage const&, PublishedRegion const&) noexcept {
    }

    // The call of ignore_region_.  It has no object, so it cannot dangle.
    // region_ready_ refers to it when the holder gives no callback.
    RegionReadyCall const ignore_regions_ = RegionReadyCall::of<&ignore_region_>();

    // The callback, borrowed from the holder of this BackgroundThread.
    //
    // The holder keeps the CallRef and its callable alive while the pipeline
    // thread runs.  stop() and the destructor join the pipeline thread, and
    // only that thread calls the callback, so no call comes after the join.
    // Vigil declares region_ready_ before bg_, and bg_ is its last member.
    // So bg_ ends first, and its destructor joins the pipeline thread before
    // region_ready_ and the rest of the Vigil end.  A test declares its
    // CallRef and the object of the CallRef before its BackgroundThread, for
    // the same reason.
    //
    // Own cache line: background-only state.  Sharing a line with
    // active_region would drag the callback into the foreground's cache on
    // every acquire load.
    alignas(64)::fixy::Ref<RegionReadyCall const> region_ready_;

    // The call of a job: the job takes the publish-stage proof of the call.
    using OwnerJobCall = ::fixy::CallRef<void(PublishStage const&) noexcept>;

    // The call of the two sentinels of the mailbox.  No path runs a
    // sentinel, so a run of one ends the process.
    static void run_sentinel_job_(PublishStage const&) noexcept {
        ::fixy::fatal("a sentinel of the owner mailbox ran as a job");
    }

    // A job that a caller hands to the owner of the publish-stage state.
    // It lives on the caller's stack, and the caller waits on done, so the
    // record outlives every read the publish stage makes of it.  The call
    // borrows the job of the caller, which lives in the same frame.
    struct OwnerJob {
        OwnerJobCall call = OwnerJobCall::of<&run_sentinel_job_>();
        ::fixy::Atomic<bool> done{};
    };

    // Runs job on the thread that owns the publish-stage state, and returns
    // when it has run.  While the publish stage runs, that thread is the
    // publish stage, which runs the job between two publications.  While
    // no publish stage runs, the state has no other writer, and the caller
    // runs the job itself.  Either way, the job and the region callback
    // never overlap.
    //
    // The job takes this BackgroundThread and the PublishStage token as its
    // parameters.  The token is the proof of the fields of the publish stage,
    // and the door hands the borrow of this object on the thread that owns
    // those fields.  So a job captures no `this` and no BackgroundThread.
    // The door checks no closure of the job yet.  The job of a rollback gives
    // a region of the pipeline to the foreground, and a region has no
    // transfer fact until a published form of a region holds one.
    //
    // Cold path.  The caller spins while the publish stage finishes the
    // publication in hand, which is bounded by one region build.  A thread
    // that holds the stage already, in a region callback or in a job, must
    // not call this: it would wait on itself, so the contract stops it.
    template <typename Job>
        requires std::is_nothrow_invocable_v<Job&, BackgroundThread&, PublishStage const&>
    void run_on_publish_stage(Job&& job) noexcept {
        CRUCIBLE_PRE(stage_held_by_this_thread_ != ::fixy::WeakRef<const BackgroundThread>{*this});
        auto with_owner = [&job, this](PublishStage const& stage) noexcept { job(*this, stage); };
        OwnerJob request{.call = OwnerJobCall{with_owner}};
        const ::fixy::WeakRef<OwnerJob> posted{request};
        while (true) {
            const ::fixy::WeakRef<OwnerJob> seen = owner_mailbox_.load_acquire();
            if (!seen.has_value()) {
                if (owner_mailbox_.cas_acq_rel(seen, posted).is_ok()) {
                    while (!request.done.load_acquire()) {
                        CRUCIBLE_SPIN_PAUSE;
                    }
                    return;
                }
                continue;
            }
            if (seen == closed_mark_()) {
                if (owner_mailbox_.cas_acq_rel(seen, inline_mark_()).is_ok()) {
                    const ::fixy::WeakRef<const BackgroundThread> held_before = stage_held_by_this_thread_;
                    stage_held_by_this_thread_ = ::fixy::WeakRef<const BackgroundThread>{*this};
                    request.call(PublishStage{});
                    stage_held_by_this_thread_ = held_before;
                    owner_mailbox_.store_release(closed_mark_());
                    return;
                }
                continue;
            }
            // Another job is in flight, on the publish stage or inline.
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    IterationDetector detector;

    // The ops that the detect stage holds, from the oldest op that a future
    // iteration can hold to the newest op.  It is a ring of six parallel
    // arrays that grows by doubling.  The detect stage drops each op that
    // IterationDetector::oldest_claimable() passes, so the ring holds at
    // most half of the history of the detector and K ops more, and it never
    // grows past IterationDetector::HISTORY_MAX_CAPACITY ops.  At that
    // capacity it takes 49 MiB.
    class PendingOps {
    public:
        // Adds the newest op.  record_start is the position of the op in the
        // stream of records of the metadata log.  The ring grows with the
        // allocation capability of the detect stage.  Complexity: O(1), and
        // O(n) when the ring grows.
        void push(::foundation::effects::Alloc alloc, TraceRing::Entry const& entry, MetaIndex meta_start,
                  uint64_t record_start, ScopeHash scope_hash, CallsiteHash callsite_hash, OpDepth depth);

        // Drops the count oldest ops.  A count past size() drops each op.
        void drop_oldest(uint64_t count) noexcept {
            const uint64_t dropped = count < count_ ? count : count_;
            head_ += dropped;
            count_ -= dropped;
        }

        void clear() noexcept { drop_oldest(count_); }

        [[nodiscard]] uint64_t size() const noexcept { return count_; }

        // The op at index, counted from the oldest held op.  The capacity
        // of each array is a power of two, so its mask wraps the slot of
        // the ring with no check.  The ring holds count_ ops, so each index
        // below size() names a held op.
        [[nodiscard]] TraceRing::Entry const& entry(uint64_t index) const noexcept {
            return entries_.at_masked(head_ + index);
        }
        [[nodiscard]] MetaIndex meta_start(uint64_t index) const noexcept {
            return meta_starts_.at_masked(head_ + index);
        }
        [[nodiscard]] uint64_t record_start(uint64_t index) const noexcept {
            return record_starts_.at_masked(head_ + index);
        }
        [[nodiscard]] ScopeHash scope_hash(uint64_t index) const noexcept {
            return scope_hashes_.at_masked(head_ + index);
        }
        [[nodiscard]] CallsiteHash callsite_hash(uint64_t index) const noexcept {
            return callsite_hashes_.at_masked(head_ + index);
        }
        [[nodiscard]] OpDepth depth(uint64_t index) const noexcept { return depths_.at_masked(head_ + index); }

    private:
        static constexpr uint64_t MIN_CAPACITY = uint64_t{1} << 12;

        void grow_(::foundation::effects::Alloc alloc);

        ::fixy::MaskedArray<TraceRing::Entry> entries_;
        ::fixy::MaskedArray<MetaIndex> meta_starts_;
        ::fixy::MaskedArray<uint64_t> record_starts_;
        ::fixy::MaskedArray<ScopeHash> scope_hashes_;
        ::fixy::MaskedArray<CallsiteHash> callsite_hashes_;
        ::fixy::MaskedArray<OpDepth> depths_;
        // The capacity minus one, zero before the first push.
        uint64_t mask_ = 0;
        // The slot of the oldest held op, before the mask.
        uint64_t head_ = 0;
        uint64_t count_ = 0;
    };

private:
    // The ops that the detect stage holds.  The detect stage owns it.
    PendingOps pending_ops_;

    // The position in the stream of records after the records of the last
    // op that the detect stage read with a metadata index.  The detect stage
    // owns it.  The first start() sets it to the tail of the metadata log.
    uint64_t meta_cursor_ = 0;

    // The numerics hash of the run at its start: the hash of the default
    // policy until the owner sets one before start.
    NumericsHash numerics_ = compute_numerics_hash(NumericsPolicy{}, EagerNumerics{});

    // The numerics of the ops that the detect stage reads: the hash of the
    // start, until the detect stage applies a reset after a change of the
    // numerics (request_numerics).  Each work item carries the hash of its
    // cut, and the build folds it into the region.  The detect stage owns it.
    NumericsHash stream_numerics_ = numerics_;

    // The position of the op that the detect stage reads, from its metadata
    // index: the index when it is valid, else the cursor.  A valid index
    // moves the cursor past the records of the op.  A valid index before the
    // cursor ends the process, because a later release would then pass the
    // records of that op before its build copies them.
    [[nodiscard]] uint64_t take_record_start_(TraceRing::Entry const& entry, MetaIndex meta_start) noexcept;

    // The oldest record that a later work can hold: the position of the
    // oldest held op, or the cursor when the detect stage holds no op.
    [[nodiscard]] uint64_t meta_floor_() const noexcept {
        return pending_ops_.size() == 0 ? meta_cursor_ : pending_ops_.record_start(0);
    }

public:
    // Read the count only after a flush, when the detect stage waits for
    // ops.
    [[nodiscard]] uint64_t pending_op_count() const noexcept { return pending_ops_.size(); }

    // Own cache line: the background writes it, the foreground reads it for
    // diagnostics.  Sharing a line with the held ops above would invalidate
    // the foreground's copy on each push.  The foreground reads it while the
    // detect stage writes it, so the counter is atomic.
    alignas(64)::fixy::AtomicMonotonic<uint32_t> iterations_completed = ::fixy::mint_atomic_monotonic<uint32_t>(0);

    // True while the detector holds an accepted period.
    enum class DetectorPeriod : uint32_t {
        Searching = 0,
        Confirmed = 1,
    };

    // The count of iteration boundaries that the detector reported, and the
    // state of its period.  The count goes back to zero at a restart of the
    // detector.
    struct DetectorSnapshot {
        uint32_t boundaries = 0;
        DetectorPeriod period = DetectorPeriod::Searching;
    };

    // The snapshot of the detector after the last batch of the detect stage.
    // The detect stage writes the detector with no order, so no other thread
    // reads the detector while the pipeline runs.  It is the one writer of
    // this cell, and it stores the snapshot after the last reset of each
    // batch and before the commit marker of the batch.  So after a flush, the
    // snapshot covers each op that the ring held at the call of the flush.
    alignas(64)::fixy::Atomic<DetectorSnapshot> detector_snapshot{};

    // The count of work items that the build stage built.  It skips each
    // work cut before the requested reset.  The foreground reads the count
    // for diagnostics.
    [[nodiscard]] uint64_t works_built() const noexcept { return works_built_.get(); }

private:
    // The build stage is the one writer.
    ::fixy::AtomicMonotonic<uint64_t> works_built_ = ::fixy::mint_atomic_monotonic<uint64_t>(0);

public:
    // The parts of one built region, each in a buffer of the exact count.
    // The build stage makes them, a message moves them to the publish stage,
    // and the publish stage gives them to the region store, or frees them
    // when it discards the region.  The payload walk of the pipeline channels
    // reads each member (fixy/concurrent/PayloadRow.h).
    //
    // A region moves between threads (foundation/Transfer.h).  Each op points
    // into the buffers of its own region, which move with it, and no object
    // outside the region points into them, so the region states the property
    // that the pointers of its ops hide.
    struct[[= ::foundation::transfer::sendable{}]] BuiltRegion {
        ::fixy::HeapArray<TraceEntry> ops;
        // The tensor metadata of the ops.  An op points into this buffer.
        ::fixy::HeapArray<TensorMeta> metas;
        // The scalars, the producer of each input, and the slot ids of the
        // inputs and the outputs of the ops.  An op points into these
        // buffers.
        ::fixy::HeapArray<int64_t> scalars;
        ::fixy::HeapArray<OpIndex> producers;
        ::fixy::HeapArray<SlotId> slot_ids;
        // The slots of the memory plan.
        ::fixy::HeapArray<TensorSlot> slots;
        ContentHash content_hash;
        // The numerics hash that the content hash folds.
        NumericsHash numerics;
    };

    // A build: the trace graph, in the scratch arena that the caller gave,
    // and the parts of its region.  The graph dies with the epoch of the
    // scratch arena.  The region does not depend on the graph.
    struct BuiltTrace {
        ::fixy::WeakRef<TraceGraph> graph;
        BuiltRegion region;
    };

    // The regions that the owner of the pipeline holds, and the generation
    // of the holders of its foreground.  The region callback gives them to
    // set_region_marks on the publish stage, and the sweep of the region
    // store after the callback reads them.  An owner that gives none has no
    // foreground, and the store then releases each region that no holder of
    // the pipeline names.
    struct RegionMarks {
        static constexpr uint32_t CAP = 32;
        ::fixy::FixedArray<::fixy::WeakRef<const RegionNode>, CAP> held{};
        uint32_t count = 0;
        // The region that the owner can still give to its foreground, such
        // as the region in a publication slot.  The store keeps it as it
        // keeps the last published region.
        ::fixy::WeakRef<const RegionNode> handed{};
        // A foreground writes the regions that it holds, and each write
        // advances the generation.  The store releases a region that left
        // the view of the foreground only after a later write that does not
        // name it.
        bool has_foreground = false;
        uint64_t foreground_generation = 0;

        // Adds a region to the held regions.  An empty reference adds
        // nothing.
        void hold(::fixy::WeakRef<const RegionNode> region) noexcept {
            if (!region.has_value()) return;
            held.at(::fixy::Index<CAP>::check(count).expect(
                "the region marks of the pipeline are full: raise BackgroundThread::RegionMarks::CAP")) = region;
            ++count;
        }
    };

private:
    // The left generation of a region that is the last published region or
    // the handed region.
    static constexpr uint64_t NOT_LEFT = UINT64_MAX;

    // The memory of one published region: its parts, its RegionNode and its
    // MemoryPlan.  The publish stage makes the node, and the plan when the
    // region has slots.  A slot of the store with no node is free.
    struct RegionMemory {
        BuiltRegion parts;
        // One RegionNode, and one MemoryPlan when the region has slots.  An
        // empty node buffer marks a free slot of the store.
        ::fixy::HeapArray<RegionNode> node;
        ::fixy::HeapArray<MemoryPlan> plan;
        // The record that the region callback gets, and that the owner can
        // publish to its foreground.
        PublishedRegion published{};
        // The foreground generation at the first sweep that found the region
        // neither the last published region nor the handed region.
        uint64_t left_generation = NOT_LEFT;
    };

    // The arena of the trace graph and its CSR arrays.  Only the build stage
    // uses it, and an epoch covers one work item: the build stage ends it
    // after it moves the parts of the region into the message.  The
    // constructor sets its retained limit (PipelineLimits::scratch_retained).
    ::fixy::BumpArena build_scratch_{size_t{1} << 20};

    // The arena of the sweep of each memory plan.  Only the publish stage
    // uses it, and an epoch covers one publish.  The constructor sets its
    // retained limit.
    ::fixy::BumpArena publish_scratch_{size_t{1} << 20};

    // The memory of each live region, in the slots of a table of
    // PipelineLimits::region_store slots, which the constructor allocates.
    // Only the publish stage reads and writes it.  The sweep after each
    // publish releases each region that no holder names.  A foreground that
    // never gets a region writes no holders, and its regions stay.  A publish
    // into a full table then discards its region and counts it.
    ::fixy::HeapArray<RegionMemory> region_store_;
    ::fixy::WeakRef<const RegionNode> last_published_{};
    RegionMarks owner_marks_{};
    uint64_t regions_dropped_full_ = 0;

    // Releases each region of the store that no holder names, on the
    // publish stage after each publish.  The body tells the rule.
    void sweep_regions_(PublishStage const& stage) noexcept;

    // True when the uncompiled queue or the marks of the owner name region.
    [[nodiscard]] bool is_held_(::fixy::WeakRef<const RegionNode> region) const noexcept;

public:
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
    // one graph-publish channel's worth of regions outstanding, so the
    // default bound is that channel's depth (PipelineLimits).  A backend
    // further behind than that is not catching up, and the regions it missed
    // describe shapes the model has already moved past.
    //
    // The publish stage is the sole writer.  total() is safe to read from
    // any thread; at() is not, and needs the pipeline quiescent.  The region
    // store keeps each region that the queue holds, so the queue keeps at
    // most capacity() regions alive.
    class UncompiledRegionQueue {
    public:
        // A queue that keeps the capacity newest regions, in slots that the
        // allocation capability allocates.  A capacity of zero keeps no
        // region, and push then only counts.
        UncompiledRegionQueue(::foundation::effects::Alloc allocation,
                              ::fixy::Quantity<::fixy::unit::Element, uint32_t> capacity) noexcept
            : slots_{::fixy::mint_heap_array(allocation, capacity.raw(), ::fixy::WeakRef<RegionNode>{})} {}

        void push(RegionNode& region) noexcept {
            const uint64_t capacity = slots_.size();
            if (capacity != 0) {
                *slots_.get(total_.peek_relaxed() % capacity).expect("a count modulo the capacity is below it") =
                    ::fixy::WeakRef<RegionNode>{region};
            }
            (void)total_.bump();
        }

        // Every region ever published, including the ones the bound
        // dropped.  total() - size() is how many were dropped.
        [[nodiscard]] uint64_t total() const noexcept { return total_.get(); }

        // The count of regions that the queue keeps at most.
        [[nodiscard]] uint32_t capacity() const noexcept {
            return ::fixy::narrow<uint32_t>(slots_.size()).expect("the capacity came from a uint32_t");
        }

        [[nodiscard]] uint32_t size() const noexcept {
            const uint64_t seen = total_.get();
            return (seen < capacity()) ? static_cast<uint32_t>(seen) : capacity();
        }

        // age 0 is the most recently published region.  The bound is size()
        // rather than the capacity so that a fresh queue cannot hand back the
        // empty reference a never-written slot still holds.  The clause is the
        // in-body macro because its predicate reaches a member through
        // `this`, which a plain pre() skips when the compiler folds this body.
        [[nodiscard]] ::fixy::WeakRef<RegionNode> at(uint32_t age) const noexcept {
            CRUCIBLE_PRE(age < size());
            const uint64_t seen = total_.get();
            const uint64_t capacity = slots_.size();
            return *slots_.get((seen + capacity - 1u - age) % capacity)
                        .expect("a count modulo the capacity is below it");
        }

    private:
        ::fixy::HeapArray<::fixy::WeakRef<RegionNode>> slots_;
        ::fixy::AtomicMonotonic<uint64_t, ::fixy::CounterWriters::sole> total_ =
            ::fixy::mint_atomic_monotonic<uint64_t, ::fixy::CounterWriters::sole>(0);
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
    // thread that they serve.  The constructor makes the one object that
    // holds the thread, start() starts the thread, and the destructor of this
    // class joins it.
    struct Pipeline;
    ::fixy::Box<Pipeline> pipeline_;

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

    // The reset.  When compiled replay diverges, the producer requests a
    // restart at the point of the divergence, before it puts the next op
    // into the ring.  The detect stage applies the reset when its count of
    // ops reaches that point, and not when it first reads the request.  The
    // detect stage can still hold ops recorded before the divergence, and
    // two periods of such ops make a region of the shape that diverged.  At
    // the point, the detect stage clears its held ops and restarts the
    // detector, which keeps the periods that broke.  A rollback or a load
    // requests a mark, which the detect stage applies at its point too.
    //
    // Each work item carries the point of the last reset that the detect
    // stage applied before its cut, and each region carries the point of
    // its work.  A region whose point is not the requested point holds ops
    // recorded before the last reset.  The publish stage discards it.  A
    // region that the publish stage gave out just before a request is stale
    // too, so the producer also compares the point of each region that it
    // takes.  Commit markers carry no region, and no reset drops them.
    //
    // The producer requests each reset, on its own thread or in a job on the
    // publish stage that it waits for.  It requests with acquire and release
    // order before it puts the op at the point, so the detect stage reads
    // the request before it reads that op, and it never passes an unapplied
    // point.  The points come from a monotonic counter, so a request never
    // moves the point back.  A newer request replaces an older one that the
    // detect stage did not reach.  Two divergences with no op between them
    // request one point.
    void request_restart(uint64_t ops_before) noexcept {
        (void)requested_reset_.try_advance(StreamPoint::restart_at(ops_before));
    }

    // A mark that replaced an unapplied restart would lose the restart, so
    // the request is then a restart at the point of the mark.  The restart
    // also clears the ops between the two points.  A restart that the detect
    // stage applies after this read only makes the second restart one that a
    // mark would have done.
    void request_mark(uint64_t ops_before) noexcept {
        const StreamPoint last = requested_reset_.get();
        const bool has_unapplied_restart = last.restarts_detector() && applied_reset_.get() != last;
        (void)requested_reset_.try_advance(has_unapplied_restart ? StreamPoint::restart_at(ops_before)
                                                                 : StreamPoint::mark_at(ops_before));
    }

    // A change of the numerics of the run at a point, such as a change of
    // the eager word of the front end.  The op stream does not change with
    // the numerics, so the request is a mark: each region cut before the
    // point is stale, and the detector keeps its ops and its periods.  A
    // restart would refute the period of the region that replayed, and the
    // same loop would then get no region under the new numerics.  Each work
    // that the detect stage cuts after the point carries the new hash.
    //
    // The hash goes out with release order before the point, and the detect
    // stage reads it after it reads the point, so it reads the hash of this
    // request or of a newer one.  A newer request has a point that is not
    // before this one, so each region cut between the two points is stale,
    // and no region gives out an old point with a newer hash.
    void request_numerics(uint64_t ops_before, NumericsHash numerics) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(numerics));
        CRUCIBLE_PRE(!numerics.is_sentinel());
        requested_numerics_.store_release(numerics);
        request_mark(ops_before);
    }

    [[nodiscard]] StreamPoint requested_reset() const noexcept { return requested_reset_.get(); }

    // The point of the last reset that the detect stage applied.
    [[nodiscard]] StreamPoint applied_reset() const noexcept { return applied_reset_.get(); }

private:
    ::fixy::AtomicMonotonic<StreamPoint> requested_reset_ = ::fixy::mint_atomic_monotonic<StreamPoint>(StreamPoint{});
    // The numerics of the last request_numerics, or of set_numerics.  The
    // producer writes it, and the detect stage reads it at each reset that it
    // applies.
    ::fixy::Atomic<NumericsHash> requested_numerics_{compute_numerics_hash(NumericsPolicy{}, EagerNumerics{})};
    // The detect stage is the one writer of the two below.
    ::fixy::AtomicMonotonic<StreamPoint, ::fixy::CounterWriters::sole> applied_reset_ =
        ::fixy::mint_atomic_monotonic<StreamPoint, ::fixy::CounterWriters::sole>(StreamPoint{});
    // The count of ops that the detect stage read.
    uint64_t stream_ops_ = 0;

    // Applies the requested reset when the count of ops that the detect stage
    // read is at its point.  The detect stage calls it before each op and
    // after each batch.
    void apply_due_reset_() noexcept;

public:
    // The mailbox of the publish-stage state.  It holds one of four values:
    //   mailbox_closed_   no publish stage runs, so a caller may own the state
    //   mailbox_inline_   a caller runs a job while no publish stage runs
    //   empty             the publish stage runs and no job waits
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

    // The references of the two sentinels.
    [[nodiscard]] static ::fixy::WeakRef<OwnerJob> closed_mark_() noexcept {
        return ::fixy::WeakRef<OwnerJob>{mailbox_closed_};
    }
    [[nodiscard]] static ::fixy::WeakRef<OwnerJob> inline_mark_() noexcept {
        return ::fixy::WeakRef<OwnerJob>{mailbox_inline_};
    }

    alignas(64)::fixy::Atomic<::fixy::WeakRef<OwnerJob>> owner_mailbox_{closed_mark_()};

    // One acquire load when no job waits, which is the common case.  While
    // the stage runs, the mailbox holds no sentinel: the stage opened it,
    // and only the stage closes it.
    // The parameter proves that the caller is the stage.  The job gets a
    // proof of its own.
    void serve_owner_mailbox_(PublishStage const&) noexcept {
        const ::fixy::WeakRef<OwnerJob> job = owner_mailbox_.load_acquire();
        if (!job.has_value()) return;
        CRUCIBLE_FATAL_INVARIANT(job != closed_mark_() && job != inline_mark_());
        job->call(PublishStage{});
        owner_mailbox_.store_release(::fixy::WeakRef<OwnerJob>{});
        // The caller returns once it reads done, and its record dies with
        // its stack frame, so nothing reads the record after this store.
        job->done.store_release(true);
    }

    void close_owner_mailbox_(PublishStage const&) noexcept {
        const ::fixy::WeakRef<OwnerJob> last = owner_mailbox_.exchange_acq_rel(closed_mark_());
        if (!last.has_value()) return;
        last->call(PublishStage{});
        last->done.store_release(true);
    }

    void open_owner_mailbox_() noexcept {
        while (true) {
            auto opened = owner_mailbox_.cas_acq_rel(closed_mark_(), ::fixy::WeakRef<OwnerJob>{});
            if (opened.is_ok()) return;
            // The stage waits while a caller runs a job inline.  Any value
            // that is not a sentinel means a second stage runs, and the
            // state would then have two owners.
            const ::fixy::WeakRef<OwnerJob> seen =
                std::move(opened).err().expect("a refused swap gives the value that it read").observed;
            CRUCIBLE_FATAL_INVARIANT(seen == inline_mark_() || seen == closed_mark_());
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // The background thread whose stage this thread holds, or an empty
    // reference.  Read only by the contract in run_on_publish_stage, on the
    // cold path.  It is one object for the process, so every shared library
    // reads the same copy on a thread.
    CRUCIBLE_PROCESS_WIDE static inline thread_local ::fixy::WeakRef<const BackgroundThread>
        stage_held_by_this_thread_{};

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
            stage_held_by_this_thread_ = ::fixy::WeakRef<const BackgroundThread>{owner_};
        }
        ~OwnerMailboxScope() {
            stage_held_by_this_thread_ = held_before_;
            owner_.close_owner_mailbox_(PublishStage{});
        }
        OwnerMailboxScope(const OwnerMailboxScope&) = delete("the scope closes the mailbox once");
        OwnerMailboxScope& operator=(const OwnerMailboxScope&) = delete("the scope closes the mailbox once");

    private:
        BackgroundThread& owner_;
        ::fixy::WeakRef<const BackgroundThread> held_before_;
    };

    static constexpr uint32_t BATCH_SIZE = 4096;
    using BatchSlot = ::fixy::Index<BATCH_SIZE>;

    // The payloads below are plain data.  A stage reaches the thread it
    // serves through its input end (StageInput), so no payload points back
    // at the owner, and the effect row of each payload is read off data
    // alone.
    struct BgTraceBatch {
        uint32_t count = 0;
        ::fixy::FixedArray<TraceRing::Entry, BATCH_SIZE> entries{};
        ::fixy::FixedArray<MetaIndex, BATCH_SIZE> meta_starts{};
        ::fixy::FixedArray<ScopeHash, BATCH_SIZE> scope_hashes{};
        ::fixy::FixedArray<CallsiteHash, BATCH_SIZE> callsite_hashes{};
        ::fixy::FixedArray<OpDepth, BATCH_SIZE> depths{};
    };

    struct BgBuildWork {
        bool commit_only = false;
        uint32_t commit_count = 0;
        // The point of the last reset before the cut of this work.
        // Meaningless on a commit-only marker, which is never dropped.
        StreamPoint reset_point{};
        // The numerics of the stream at the cut, which the build folds into
        // the region.  Meaningless on a commit-only marker.
        NumericsHash numerics{};
        // On a commit-only marker, the oldest record that a later work can
        // hold (meta_floor_() when the detect stage made the marker).
        uint64_t meta_floor = 0;
        // The ops of the iteration, one element of each buffer for each op,
        // each built from the held op.  An iteration has at most
        // IterationDetector::MAX_PERIOD ops, so the five buffers take at most
        // 90 bytes for each op, 22.5 MiB together.  A commit-only marker
        // holds none.
        ::fixy::HeapArray<TraceRing::Entry> trace;
        ::fixy::HeapArray<MetaIndex> meta_starts;
        ::fixy::HeapArray<ScopeHash> scope_hashes;
        ::fixy::HeapArray<CallsiteHash> callsite_hashes;
        ::fixy::HeapArray<OpDepth> depths;
    };

    struct BgPipelineDone {};
    // The go signal of the drain stage.  run() pushes it once, before the
    // stages start.
    struct BgPipelineStart {};

    // Lifetime: a Box owns each message.  The producing stage owns the Box
    // until its push, the channel holds the object after it, and the Box of
    // the pop of the consuming stage ends the object once it is handled.  A
    // region message owns the parts of its region, so the region memory
    // moves with it.
    //
    // Two shapes flow through the channel, and kind names each one:
    //   Region   region holds a value: an iteration region to publish.
    //   Commit   region is empty: a marker that carries the entry count and
    //            the release floor of the metadata log.
    //
    // The marker has to travel through the publish stage so that
    // total_processed advances only after every region ahead of it has been
    // published.  Bumping the counter at the build stage instead races the
    // publish stage: a flush can return while a region is still queued, and
    // the caller then observes a compiled mode with no pending region, or an
    // active region whose plan is not finished.
    //
    // The floor travels with the marker for the same reason.  The publish
    // stage is the one writer of the tail of the metadata log.  Each work
    // that holds a record before the floor went into the channels before the
    // marker, and the build stage copied its records before it sent the
    // marker on, because each channel is FIFO.
    struct BgGraphPublish {
        enum class Kind : uint8_t {
            Region,
            Commit
        };
        Kind kind = Kind::Commit;
        // The parts of the region.  Only a region message holds them.
        BuiltRegion region{};
        uint32_t commit_count = 0;
        // The release floor of the metadata log.  Only a commit marker sets
        // it.
        uint64_t meta_floor = 0;
        // Carried through from the BgBuildWork this came from, so a reset
        // that lands while the build stage is mid-graph is still caught
        // here, one stage later.
        StreamPoint reset_point{};
    };

    // Makes a commit marker: a work item that carries only the count of
    // entries that one batch consumed and the release floor after them.  The
    // Box takes the allocation capability of the detect stage.
    [[nodiscard]] ::fixy::Box<BgBuildWork> make_commit_work(::foundation::effects::Alloc alloc, uint32_t count);

    // The detect stage calls this at each iteration boundary that the
    // detector reports.  It drops the held ops before the iteration, copies
    // the completed iteration into a work item, and keeps the last K ops,
    // which begin the next iteration.  The work item and its buffers take the
    // allocation capability of the detect stage.  It gives no work item when
    // the iteration is empty or no metadata log is set.
    [[nodiscard]] ::fixy::Option<::fixy::Box<BgBuildWork>>
    prepare_iteration_build_work(::foundation::effects::Alloc alloc);

    // The detect stage calls this after each op.  It drops the held ops
    // before IterationDetector::oldest_claimable(), because no future work
    // can hold them.
    void drop_unclaimable_ops_() noexcept;

    // The one release of the metadata log.  The publish stage calls it at
    // each commit marker with the floor of the marker.  It moves the tail to
    // the floor when the floor is past the tail, which gives the records
    // before the floor back to the foreground.  A floor at or before the tail
    // changes nothing, and a floor past the head ends the process.  The proof
    // shows that the caller is the publish stage, the one writer of the tail.
    void release_meta_log(PublishStage const& stage, uint64_t meta_floor) CRUCIBLE_NO_THREAD_SAFETY;

    // Makes the RegionNode and the memory plan of the region, publishes the
    // region and gives its memory to the region store.  The background
    // context of the publish stage allocates the node and the plan, and is
    // handed to the region callback, which runs on the same thread.  After
    // the callback, the sweep releases each region that no holder names.
    // The record of the region carries reset_point, the point of its work.
    void publish_region(::foundation::effects::Bg const& bg, PublishStage const& stage, BuiltRegion parts,
                        StreamPoint reset_point) CRUCIBLE_NO_THREAD_SAFETY;

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
    // The ring and the metadata log are references, so no caller can give
    // a null pointer.  The init context is the authority to start the
    // thread.  A start while a pipeline thread runs breaks the contract:
    // call stop() first.
    //
    // A start after stop() continues the stream of the last pipeline.  The
    // ring keeps the ops that the last pipeline did not read, and the detect
    // stage keeps its held ops, its detector and its cursor of the log.  So
    // a later start names the ring and the log of the first start, and a
    // start that names a different ring or log breaks the contract.
    void start(::fixy::InitLoadCtx const& startup, TraceRing& trace_ring, MetaLog& trace_meta_log, int32_t rank_ = -1,
               int32_t world_size_ = 0, uint64_t device_cap = 0) CRUCIBLE_NO_THREAD_SAFETY;

    // Sets the numerics hash of the start of the run, which the build folds
    // into each region until a request_numerics.  The owner calls it before
    // start, and the start of the pipeline thread orders the write before
    // each read of the pipeline.  A call while the pipeline runs breaks the
    // contract.
    void set_numerics(NumericsHash numerics) noexcept CRUCIBLE_NO_THREAD_SAFETY;

    [[nodiscard]] NumericsHash numerics() const noexcept { return numerics_; }

    // Signals the drain stage to stop and joins the pipeline thread, through
    // request_stop_ and the join of thread_.  The end of this object does the
    // same when the thread still runs.  The pipeline joins its stage threads
    // before the thread ends.  The signal
    // does not wake a sleeping drain stage, so the stage reads it at the end
    // of its sleep, within RING_MAX_SLEEP_NANOS (0.2 ms).  That bound comes
    // from the margin of the trace ring, which a sleep must not fill.
    void stop() CRUCIBLE_NO_THREAD_SAFETY;

    ~BackgroundThread() CRUCIBLE_NO_THREAD_SAFETY;

    // src/BackgroundThread.cpp defines the two, where the pipeline is a
    // complete type.  The caller gives the allocation capability of its
    // context, and the pipeline takes its storage through it.  The first
    // has no region callback.  The second borrows the callback of the
    // caller, which outlives this BackgroundThread (region_ready_).  The
    // limits size the region store and the uncompiled queue, and set the
    // retained limit of the two scratch arenas.
    explicit BackgroundThread(::foundation::effects::Alloc allocation, PipelineLimits const& limits = PipelineLimits{});
    BackgroundThread(::foundation::effects::Alloc allocation, RegionReadyCall const& on_region_ready,
                     PipelineLimits const& limits = PipelineLimits{});
    BackgroundThread(const BackgroundThread&) = delete("BackgroundThread owns the pipeline thread");
    BackgroundThread& operator=(const BackgroundThread&) = delete("BackgroundThread owns the pipeline thread");
    BackgroundThread(BackgroundThread&&) = delete("BackgroundThread owns the pipeline thread with captured this");
    BackgroundThread&
    operator=(BackgroundThread&&) = delete("BackgroundThread owns the pipeline thread with captured this");

    // The bytes of blocks that the two scratch arenas hold.  The build stage
    // and the publish stage write the arenas, so read the value only when no
    // pipeline thread runs: before start() or after stop().
    [[nodiscard]] size_t scratch_retained_bytes() const noexcept {
        return ::fixy::saturating_add(build_scratch_.retained_bytes(), publish_scratch_.retained_bytes());
    }

    // The retained limit of each of the two scratch arenas
    // (PipelineLimits::scratch_retained).
    [[nodiscard]] ::fixy::Quantity<::fixy::unit::Byte, size_t> scratch_retained_limit() const noexcept {
        return build_scratch_.retained_limit();
    }

    // Gives the regions that the owner holds to the sweep of this publish.
    // The region callback calls it, on the publish stage.
    void set_region_marks(PublishStage const&, RegionMarks const& marks) noexcept { owner_marks_ = marks; }

    // Marks a region that the owner gives to its foreground outside the
    // publication slot, such as the region of a rollback.  The store then
    // releases the region only after a later write of the foreground
    // holders that does not name it.  A region that the store does not hold
    // changes nothing.
    void keep_region(PublishStage const& stage, ::fixy::WeakRef<const RegionNode> region) noexcept;

    // The count of regions in the store, and the count of published regions
    // that a full store discarded.  The publish stage owns the store.
    [[nodiscard]] uint32_t region_count(PublishStage const& stage) const noexcept;
    [[nodiscard]] uint64_t regions_dropped_full(PublishStage const&) const noexcept { return regions_dropped_full_; }

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

    // Open-addressing map from the storage of a tensor to the last op that
    // wrote it, the output port of that op, the slot of the storage and the
    // last op that read it.  A slot counts as empty when slot.gen differs
    // from map_gen_, so bumping map_gen_ clears the whole table in constant
    // time and only the wrap-around every 255 calls needs a real memset.
    //
    // The key is the storage_key of a tensor: the address of the first byte
    // of its storage, which a foreign allocator gave, and the generation of
    // its allocation.  The views of one storage at each offset give one key.
    // The map reads the key only for a hash and an equality, never as an
    // offset into an internal pool.
    using PtrMapKey = StorageKey;

    struct PtrSlot {
        PtrMapKey key{};
        OpIndex op_index;
        SlotId slot_id;
        // The last op that read the storage as an input.
        OpIndex reader;
        uint8_t port = 0;
        uint8_t gen = 0;
        ::fixy::FixedArray<uint8_t, 2> pad{};
    };

    // Two slots on each cache line of 64 bytes.
    static_assert(sizeof(PtrSlot) == 32, "PtrSlot holds the key of 16 bytes and 16 bytes of the build");
    static_assert(sizeof(PtrMapKey) == 16, "a StorageKey holds the address word and the generation");

    // The fields are the fields of TensorSlot from nbytes to is_external, in
    // the same order.  The slot build of build_trace_from copies each field
    // into its TensorSlot.
    struct SlotInfo {
        uint64_t nbytes = 0;
        OpIndex birth_op;
        OpIndex death_op;
        ScalarType dtype = ScalarType::Undefined;
        DeviceType device_type = DeviceType::CPU;
        int8_t device_idx = -1;
        Layout layout = Layout::Strided;
        bool is_external = false;
        ::fixy::FixedArray<uint8_t, 3> pad{};
    };

    static_assert(sizeof(SlotInfo) == 24, "SlotInfo: matches TensorSlot bulk region");

    // old_op, old_port and old_slot carry the displaced entry and are
    // meaningful only when was_alias is true.
    struct InsertResult {
        ::fixy::WeakRef<PtrSlot> slot{};
        bool was_alias = false;
        OpIndex old_op;
        uint8_t old_port = 0;
        SlotId old_slot;
    };

    // The map has a power-of-two capacity, so its mask wraps each probe.
    ::fixy::MaskedArray<PtrSlot> scratch_map_;
    ::fixy::HeapArray<SlotInfo> scratch_slots_;
    ::fixy::HeapArray<Edge> scratch_edges_;
    uint8_t map_gen_ = 0;

    // Zero means not yet allocated.
    ::fixy::Monotonic<uint32_t> map_cap_ = ::fixy::mint_monotonic<uint32_t>(0);
    ::fixy::Monotonic<uint32_t> slot_cap_max_ = ::fixy::mint_monotonic<uint32_t>(0);
    ::fixy::Monotonic<uint32_t> edge_cap_max_ = ::fixy::mint_monotonic<uint32_t>(0);

    // build_trace_from calls this after its scan of the trace, which
    // supplies exact counts.  Buffers grow and never shrink, so the cost
    // amortizes across iterations.
    void ensure_scratch_buffers(::foundation::effects::Alloc a, uint32_t total_inputs, uint32_t total_outputs);

    // The bytes of the storage of a slot that the tensor reaches: from the
    // first byte of the storage to the end of the extent of the tensor.  A
    // slot holds the largest span of the tensors in it, so the view of each
    // one fits the run of the slot.  The sum saturates.
    [[nodiscard]] static uint64_t span_bytes_of_(const TensorMeta& meta) noexcept {
        return ::fixy::saturating_add(storage_offset_bytes(meta),
                                      compute_storage_nbytes_det(external_tensor_meta(meta)).peek().value());
    }

    // The bucket of a key is the low half of the fold of the hash of its
    // address key and its generation.  The address changes from run to run,
    // so the map gives no order.
    [[nodiscard, gnu::const]] static uint32_t hash_ptr(PtrMapKey key) noexcept {
        return static_cast<uint32_t>(::foundation::reflect::combine_ids(key.address.hash(), key.generation.raw()));
    }

    // The entry of key in the map, or no entry when the map does not hold
    // the key.  The probes stop at the first empty slot.
    [[nodiscard]] static ::fixy::WeakRef<PtrSlot> ptr_map_find(::fixy::MaskedArray<PtrSlot>& map, uint8_t gen,
                                                               PtrMapKey key) noexcept {
        const uint64_t bucket_idx = hash_ptr(key);
        for (uint64_t probe = 0; probe < map.size(); probe++) {
            PtrSlot& slot = map.at_masked(bucket_idx + probe);
            if (slot.gen != gen) return {};
            if (slot.key == key) return ::fixy::WeakRef<PtrSlot>{slot};
        }
        return {};
    }

    // The probes of the map start at the hash of the key, and the mask of
    // the map wraps each probe.  A full round of probes visits each slot.
    [[nodiscard]] static InsertResult ptr_map_insert(::fixy::MaskedArray<PtrSlot>& map, uint8_t gen, PtrMapKey key,
                                                     OpIndex op_index, uint8_t port, SlotId slot_id) {
        const uint64_t bucket_idx = hash_ptr(key);
        for (uint64_t probe = 0; probe < map.size(); probe++) {
            auto& slot = map.at_masked(bucket_idx + probe);
            if (slot.gen != gen) {
                // A stale generation means the slot is empty.  Claim it.
                slot.key = key;
                slot.op_index = op_index;
                slot.port = port;
                slot.slot_id = slot_id;
                slot.reader = OpIndex{};
                slot.gen = gen;
                return {.slot = ::fixy::WeakRef<PtrSlot>{slot},
                        .was_alias = false,
                        .old_op = {},
                        .old_port = 0,
                        .old_slot = {}};
            }
            if (slot.key == key) {
                InsertResult result{.slot = ::fixy::WeakRef<PtrSlot>{slot},
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
        return {.slot = {}, .was_alias = false, .old_op = {}, .old_port = 0, .old_slot = {}};  // table full
    }

    struct PtrLookup {
        OpIndex op_index;
        SlotId slot_id;
        uint8_t port = 0;
    };

    [[nodiscard]] static PtrLookup ptr_map_lookup(::fixy::MaskedArray<PtrSlot> const& map, uint8_t gen, PtrMapKey key) {
        if (key.address == ::fixy::mint_address_key(static_cast<void const*>(nullptr)))
            return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};
        const uint64_t bucket_idx = hash_ptr(key);
        for (uint64_t probe = 0; probe < map.size(); probe++) {
            auto& slot = map.at_masked(bucket_idx + probe);
            if (slot.gen == gen && slot.key == key)
                return {.op_index = slot.op_index, .slot_id = slot.slot_id, .port = slot.port};
            if (slot.gen != gen) return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};  // empty → miss
        }
        return {.op_index = OpIndex{}, .slot_id = SlotId{}, .port = 0};
    }

public:
    // The drain loop runs in the background context, allocates region
    // memory and grows the trace buffers, and fires the region callback as
    // its only externally observable side effect.  The callback can do I/O
    // and can block, so the row keeps those two atoms.  The loop runs until
    // the stop, with no bound, so the row keeps Div.  The caller hands a
    // context whose row admits each atom, and a context that admits fewer
    // is a compile error.
    using run_required_row =
        ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc,
                                   ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block,
                                   ::foundation::effects::Effect::Nondet, ::foundation::effects::Effect::Div>;

    // The row is exactly the background load context's row, so the entry of
    // the pipeline thread builds that context and no wider one exists.
    static_assert(std::is_same_v<run_required_row, typename ::fixy::BgLoadCtx::row_type>,
                  "BackgroundThread::run_required_row must be exactly Row<Bg, Alloc, IO, Block, Nondet, Div>, the "
                  "row of fixy::BgLoadCtx.  Adding or removing an atom changes the context every site that runs "
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

    // The reason that a build makes no region.  The build releases nothing,
    // and the floor of a later commit marker passes the records of the work.
    enum class BuildRefusal : uint8_t {
        // An op has tensors and no metadata index.  The foreground records
        // such an op when the metadata log is full.
        MetaLogOverflow,
        // An op is the closing entry of a nest whose op did not reach the
        // Vigil (kUnrecordedNestSchema of TraceRing.h).  That op did not
        // finish its computation in the trace, so the region stays eager.
        UnrecordedNest,
    };

    // A graph and the parts of its region, or the reason that the build
    // made none.
    using TraceBuild = ::fixy::Result<BuiltTrace, BuildRefusal>;

    // Turns the ring entries of trace and their tensor metadata into a CSR
    // property graph in scratch, and the parts of its region.  The caller
    // owns the five arrays, which hold one element for each op:
    // meta_starts, scope_hashes, callsite_hashes and depths are parallel to
    // trace.  meta_starts holds MetaIndex::none() for an op with no tensor
    // arguments.  The build stage calls it on each work item.  It reads the
    // metadata log and never writes its tail.  The graph and its CSR arrays
    // go into scratch.  The ops, their tensor metadata, their side arrays
    // and their slots go into the parts of the region, which own their
    // memory.
    // The content hash of the region folds numerics, the numerics of the
    // stream at the cut of the ops (BgBuildWork::numerics).
    [[nodiscard]] TraceBuild
    build_trace_from(::foundation::effects::Alloc a, ::fixy::BumpArena& scratch,
                     ::fixy::View<const TraceRing::Entry> trace, ::fixy::View<const MetaIndex> meta_starts,
                     ::fixy::View<const ScopeHash> scope_hashes, ::fixy::View<const CallsiteHash> callsite_hashes,
                     ::fixy::View<const OpDepth> depths, NumericsHash numerics) noexcept CRUCIBLE_NO_THREAD_SAFETY;

private:
    // The body of build_trace_from.  Each column is a fixy::BrandedView of
    // one branded scope over the ops, so one index of an op reads each
    // column with no check.  Only src/BackgroundThread.cpp instantiates it.
    // The noexcept lambda of the branded scope calls it.  The body is
    // noexcept too, so a Release inline of it in that lambda needs no
    // terminate pad for its contract checks and its arrays.
    template <class TraceColumn, class MetaColumn, class ScopeColumn, class CallsiteColumn, class DepthColumn>
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] TraceBuild
    build_trace_columns_(::foundation::effects::Alloc a, ::fixy::BumpArena& scratch, TraceColumn trace_data,
                         MetaColumn meta_data, ScopeColumn scope_data, CallsiteColumn callsite_data,
                         DepthColumn depth_data, NumericsHash numerics) noexcept CRUCIBLE_NO_THREAD_SAFETY;

public:
    // Why a memory plan refuses the slots of a region.  The region then has
    // no plan, and a context replays no region with no plan, so the region
    // stays on eager.
    enum class PlanRefusal : uint8_t {
        // The span of an internal slot, rounded up to the alignment of the
        // pool, does not fit in 64 bits.  A span that saturated is such a
        // span.
        SpanTooLarge,
        // The end of the pool does not fit in 64 bits.
        PoolEndOverflow,
    };

    // Orders birth and death events with an O(n + k) counting sort, then
    // assigns offsets with a sweep line.  The alignment is what the GPU
    // needs for coalesced access.  The birth_op of each internal slot is at
    // most its death_op, and the death_op is at most UINT32_MAX - 3.  The
    // function stops the process when a slot breaks this rule, because the
    // sweep would index past its arrays.  The plan goes into plan_storage,
    // the arrays of the sweep go into scratch, and the result is a Ref of
    // plan_storage, or the refusal of a span or of the end of the pool that
    // does not fit in 64 bits.  The plan borrows the slots.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] ::fixy::Result<::fixy::Ref<MemoryPlan>, PlanRefusal>
    compute_memory_plan(::foundation::effects::Alloc a, MemoryPlan& plan_storage, ::fixy::BumpArena& scratch,
                        ::fixy::View<TensorSlot> slots) noexcept CRUCIBLE_NO_THREAD_SAFETY;

private:
    // The stop hook of the pipeline thread (THE THREAD OF AN OWNER of
    // fixy/os/OwnedThread.h).  It signals the drain stage to stop, and it does
    // not join.  The stage reads the signal at the end of its sleep.
    void request_stop_() noexcept { stop_requested.signal(); }

    // The thread that runs run_in_row, while one runs.  start() gives it a
    // thread, and stop() and the end of this object call request_stop_ and
    // join it.  It is the last data member, so it ends first, and each other
    // member is alive while the thread ends.
    ::fixy::thread::OwnerThread<BackgroundThread> thread_{};
};

// The two sentinels of the owner mailbox.  Only their addresses matter, so
// each is one object for the process.  A job run from one shared library
// then compares against the address that the stage in another one stored.
CRUCIBLE_PROCESS_WIDE inline constinit BackgroundThread::OwnerJob BackgroundThread::mailbox_closed_{};
CRUCIBLE_PROCESS_WIDE inline constinit BackgroundThread::OwnerJob BackgroundThread::mailbox_inline_{};

}  // namespace crucible
