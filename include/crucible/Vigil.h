#pragma once

// The single orchestration point, owning every runtime component: the SPSC
// ring, the tensor metadata log, the background thread that drains them, the
// per-iteration transaction log, and optionally the content-addressed store.
//
// Three operational modes:
//   RECORDING  the adapter dispatches ops normally and records each one
//   COMPILED   an active region is live and replay drives execution
//   DIVERGED   a transient replay status; the persistent mode falls back to
//              RECORDING once divergence is handled
//
// An observer reads the mode through mode().  The foreground dispatch path
// reads the status flag directly.
//
// Member declaration order is load-bearing for destruction safety.  bg_ is
// declared last, so it is destroyed first and joins the background thread
// before every member that thread touches is invalidated.
//
// All live Vigils of a process share one foreground thread.  The schema
// table and the kernel table are global, and each has one writer: the
// foreground thread.  A Vigil claims the thread that first asks it for a
// producer context.  A thread that claims a second Vigil while a different
// thread holds a live claim ends the process (ProducerClaim in
// foundation/effects/Ctx.h).  After every claim is gone, another thread can
// claim.

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>
#include <crucible/ForegroundCtx.h>
#include <crucible/HeldRegions.h>
#include <crucible/IterationDetector.h>
#include <crucible/LoadError.h>
#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/RegionCache.h>
#include <crucible/SchemaTable.h>
#include <crucible/TraceRing.h>
#include <crucible/Transaction.h>
#include <crucible/ValidatedOp.h>
#include <crucible/cipher/SessionPersistenceSurface.h>
#include <crucible/warden/Policy.h>
#include <crucible/warden/WatchdogVerdict.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/ScopedView.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/session/VigilModeCell.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Mix.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>  // std::move, std::forward

// A Box holds the telemetry and the watchdog, so this header only declares
// them.  src/Vigil.cpp includes their headers, and it defines each special
// member of Vigil that makes or ends a Box.
namespace crucible::perf {
class Senses;
}  // namespace crucible::perf

namespace crucible::warden {
class DeadlineWatchdog;
}  // namespace crucible::warden

namespace crucible {

// The two sides of the trace ring of a Vigil: the proof of the record path
// (RecordProof of crucible/ForegroundCtx.h), which only the producer claim of
// the Vigils makes, opens the producer side, and the proof of the drain stage
// of the background thread opens the consumer side.
struct VigilRingSides final {
    using producer_proof = RecordProof;
    using consumer_proof = BackgroundThread::DrainStage;
};

// The two sides of the metadata log of a Vigil: the proof of the record path
// opens the producer side, and the proof of the publish stage, which releases
// the records, opens the consumer side.
struct VigilLogSides final {
    using producer_proof = RecordProof;
    using consumer_proof = BackgroundThread::PublishStage;
};

class Vigil {
public:
    // The mode types live in a smaller header, so an observer names them
    // without this header's whole dependency closure.
    //
    // A monotonic wrapper would be wrong for the mode: the lifecycle is
    // deliberately cyclic, running RECORDING to COMPILED and back to
    // RECORDING after a divergence.  ModeCell keeps the atomic private and
    // exposes only the two transitions the runtime actually performs.
    //
    // The cell is private, and only the foreground publishes a mode, in the
    // call that activates or deactivates the replay context (ReplayState).
    // A session over the cell would drive those transitions, so Vigil hands
    // out no session handle.  An observer reads mode().
    //
    // The pending region uses a latest-wins publication slot rather than a
    // publish-once cell for the same reason: divergence recovery publishes
    // several regions over one process lifetime.
    using Mode = ::fixy::session::vigil_mode::Mode;
    using ModeCell = ::fixy::session::vigil_mode::ModeCell;
    using ModeWriter = ::fixy::session::vigil_mode::ModeWriter;

    struct Config {
        // The most characters of cipher_path.  It is the bound of the root of
        // the store, Cipher::MAX_ROOT_PATH_BYTES, and src/Vigil.cpp checks
        // that the two agree.
        static constexpr size_t CIPHER_PATH_MAX = 4096;

        int32_t rank = -1;
        int32_t world_size = 0;
        uint64_t device_capability = 0;
        // The root directory of the store.  An empty path gives no store.
        ::fixy::GrowText cipher_path = ::fixy::mint_grow_text(CIPHER_PATH_MAX);

        // Enabling this loads the scheduler-switch tracing subprogram and
        // observes the dispatcher's preempt count once per region
        // transition, publishing the verdict through the watchdog
        // accessors.  This is observation only: nothing here changes the
        // scheduler class, and a caller acts on the verdict itself.
        //
        // Off by default because it needs CAP_BPF and a kernel carrying the
        // sched_switch tracepoint's type information.  Attaching fails
        // gracefully, but then every observation returns InsufficientData,
        // so the cost buys no signal.
        bool enable_deadline_watchdog = false;

        // Only the deadline-miss budget and the window length are read here.
        warden::Policy watchdog_policy = warden::Policy::production();

        // The numerics policy of the run, fixed for the life of the Vigil,
        // and the eager word that the front end read at the attach.  The
        // hash of each region folds the two (compute_numerics_hash).
        NumericsPolicy numerics{};
        EagerNumerics eager{};

        // The limits of the memory of the background pipeline: the bytes that
        // each scratch arena keeps after an epoch, the regions of the region
        // store and the regions of the uncompiled queue.  PipelineLimits
        // states what each limit refuses.
        PipelineLimits limits{};
    };

    // Consecutive op matches required to confirm an iteration boundary
    // before the replay context activates.  It equals the iteration
    // detector's signature length.
    static constexpr uint32_t ALIGNMENT_K = 5;
    // The equality above was a comment until now, and a comment does not
    // hold. The detector reports a boundary after K matching ops, and
    // alignment then walks exactly that many entries of the published region
    // to find where the boundary fell. Raise one without the other and
    // alignment lands on the wrong op of every iteration.
    static_assert(ALIGNMENT_K == IterationDetector::K,
                  "Vigil::ALIGNMENT_K must equal IterationDetector::K.  Alignment replays the same "
                  "window the detector matched on, so the two lengths are one constant.");

    // Structural upper bound on the value alignment_pos_ can hold.
    // try_align_ advances it toward min(region->num_ops, ALIGNMENT_K).  The
    // increment that reaches that threshold also clears pending_activation_,
    // so no further increment happens, and the next consumed region resets
    // the position to zero.  No path stores more than ALIGNMENT_K.
    //
    // These two are public so the negative-compile fixtures can construct
    // out-of-range values in a constant expression and fire the contract.
    static constexpr uint8_t ALIGNMENT_POS_MAX = static_cast<uint8_t>(ALIGNMENT_K);
    static_assert(ALIGNMENT_K <= UINT8_MAX, "ALIGNMENT_POS_MAX must fit in uint8_t.  Widen AlignmentPos's "
                                            "underlying type if ALIGNMENT_K is raised past 255.");
    using AlignmentPos = ::fixy::Refined<::fixy::bounded_above<ALIGNMENT_POS_MAX>, uint8_t>;

    // The one door to an alignment position.  The bound is checked here, at
    // compile time for a constant and at run time otherwise.
    [[nodiscard]] static constexpr AlignmentPos alignment_pos_at(uint8_t pos) noexcept {
        return ::fixy::mint_refined<::fixy::bounded_above<ALIGNMENT_POS_MAX>>(pos);
    }

    // The most ops that an alignment against a region of region_ops ops reads
    // before it ends with no match.  A stream that holds the region repeats
    // it, so the head of the region starts within one period after any op,
    // and its K ops complete the match.  An alignment that reads one period
    // and K ops with no match is in a stream that does not hold the region.
    // try_align_ then ends the wait, and the ops go to the ring again.
    [[nodiscard]] static constexpr uint32_t alignment_bound(uint32_t region_ops) noexcept {
        return ::fixy::saturating_add(region_ops, ALIGNMENT_K);
    }

    // The constructors, the destructor and flush have no cold attribute.  A
    // call to a cold function on each path makes GCC compile the caller as
    // an unlikely function.  A hot dispatch loop in that caller then
    // compiles for size.

    // No persistence, no distributed context.
    Vigil() : Vigil(Config{}) {}

    // The constructor runs at process startup, so it opens the init door one
    // time and hands that one context to every startup step below.
    explicit Vigil(Config cfg)
        : Vigil(std::move(cfg), ::fixy::InitLoadCtx{::foundation::effects::host::InitOwner::mint_init_context()}) {}

private:
    // Makes the ring and the metadata log, opens the store when the
    // configuration names a path, loads the watchdog when the configuration
    // asks for it, and starts the background thread.  A store that does not
    // open gives a report of the reason and a Vigil with no store, and
    // has_store tells the caller.  The body is in
    // src/Vigil.cpp, so a translation unit that makes a Vigil compiles no
    // part of the background pipeline.
    //
    // The program load of the watchdog waits in the kernel for the verifier,
    // so the constructor takes the startup load context.  The transaction log
    // reads no clock and takes no context.
    Vigil(Config cfg, ::fixy::InitLoadCtx const& startup);

public:
    // src/Vigil.cpp defines it.  The destruction order of the members is
    // the reverse of their declaration order, and bg_ joins the background
    // thread first.
    ~Vigil();

    // The Refs of bg_ name the consumer sides of ring_ and meta_log_ of this
    // Vigil.  A Ref to a member of the same object is sound only while that
    // object stays at its address, so a Vigil has no copy and no move.
    Vigil(const Vigil&) = delete("the Refs of bg_ name the sides of ring_ and meta_log_ of this Vigil, so a Vigil "
                                 "has no copy");
    Vigil& operator=(const Vigil&) = delete("the Refs of bg_ name the sides of ring_ and meta_log_ of this Vigil, "
                                            "so a Vigil has no copy");
    Vigil(Vigil&&) = delete("the Refs of bg_ name the sides of ring_ and meta_log_ at the address of this Vigil, so "
                            "a Vigil has no move");
    Vigil& operator=(Vigil&&) = delete("the Refs of bg_ name the sides of ring_ and meta_log_ at the address of this "
                                       "Vigil, so a Vigil has no move");

    // Reserves a ring slot, appends the tensor metadata, and then commits an
    // op fingerprint to the slot (TraceRing::try_record).  Returns false when the ring
    // is full, in which case the op is dropped and the next iteration
    // re-records everything.  A full metadata log gives the op no records.
    //
    // The records of the op are the inputs and then the outputs of the
    // entry, and the build reads that many records for the op.
    // mint_validated_op is the one door to a ValidatedOp, and it refuses a
    // count of records that disagrees with the entry.
    //
    // This is a producer entry point in its own right, not a helper of
    // dispatch_op, so it carries the same thread gate.  Without one, a
    // caller that reaches the ring through here from a second thread gets
    // the torn append dispatch_op refuses, and the two entry points state
    // different ownership rules for one ring.
    //
    // A caller that names no depth records an op that no recorded op runs.
    // The record stack notes the op (OpRecordStack::note_given), as for
    // dispatch_op.
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool record_op(const ValidatedOp& op, ScopeHash scope_hash = {},
                                                           CallsiteHash callsite_hash = {}, OpDepth depth = {}) {
        const VigilFgCtx fg = assert_producer_thread_();
        op_record_stack_.note_given(depth);
        return record_proven_(fg, op, scope_hash, callsite_hash, depth);
    }

    // Called once per op by the adapter.  A RECORD action means the caller
    // executes eagerly and the op has been recorded; a COMPILED action means
    // the outputs are already allocated and reachable through output_ptr and
    // input_ptr.
    //
    // Only the hot paths live inline here.  Divergence recovery, consuming a
    // pending region and alignment all sit in noinline helpers, so the hot
    // path needs no callee-saved registers for them.
    //
    // The depth is the nesting depth of the op (OpDepth).  A replay reads no
    // depth: the cursor walks the ops in the order that they arrive, and a
    // recording gives that order.
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE DispatchResult dispatch_op(const ValidatedOp& op,
                                                                                     ScopeHash scope_hash = {},
                                                                                     CallsiteHash callsite_hash = {},
                                                                                     OpDepth depth = {}) {
        // Armed in every build mode, release included. The ring behind this
        // call is single-producer: two threads appending concurrently claim
        // the same slot, and the second overwrites the first with no
        // diagnostic. A backward pass under a foreign runtime dispatches
        // part of its operations on a worker thread of its own by default,
        // so this is reachable from a first run rather than from a rewrite.
        //
        // The cost is a relaxed load and a comparison on a path that already
        // writes a full cache line and issues two release stores.
        //
        // Commit 498ad4d0 gave the torch adapter's backward windows one
        // producer, recorded and replayed. The gate stays armed in release
        // after that commit, for two reasons:
        //
        //  - The first call is the claim. is_producer_thread() reads the
        //    claimed id, and the unboxed kernels and the boxed fallback ask
        //    it before they record. With the gate compiled out, no thread is
        //    ever claimed and that question answers true on every thread.
        //  - A producer path with no serialisation exists. The C ABI
        //    (crucible_dispatch_op and crucible_dispatch_op_ex in
        //    vessel/torch/vessel_api.cpp, called by crucible_mode.py) does
        //    not ask is_producer_thread(), so this gate is its only guard.
        //
        // The gate returns the context of this Vigil's claim, which the record
        // path takes.  It is the one check of the thread on each path of the
        // dispatch: the replay below takes the key of the context, which this
        // Vigil holds, and checks the thread no second time.
        const VigilFgCtx fg = assert_producer_thread_();
        const TraceRing::Entry& entry = op.entry();

        // The record stack notes each op that reaches the Vigil, on each
        // path, so a nest can tell if its own op reached it after the ops
        // inside it (OpNest::is_unrecorded).
        op_record_stack_.note_given(depth);

        // The scope of the context is open only while the context replays, so
        // the advance runs only in the compiled mode.  The body advances, binds
        // the reads of the external slots, and gives the step back.  The binds
        // of the data slots and the divergence run after it: they write this
        // Vigil, which holds the key of the context, and no body reaches the key.
        const SchemaHash schema_hash = entry.schema_hash;
        const ShapeHash shape_hash = entry.shape_hash;
        ::fixy::Option<ReplayStep> replayed = replay_.with_compiled(
            [schema_hash, shape_hash, &op, &bound = bound_scalars_] [[gnu::always_inline]] (
                CompiledScope & scope) noexcept { return replay_op_(scope, schema_hash, shape_hash, op, bound); });
        if (replayed.is_some()) [[likely]] {
            const ReplayStep step = std::move(replayed).expect("the test above found the step of the scope");
            if (step.status() == ReplayStatus::DIVERGED) [[unlikely]]
                return handle_divergence_(fg, op);
            return compiled_result_(step);
        }

        // The acquire load pairs with the release store on the background
        // thread, so every byte of the region it wrote before that store is
        // visible here.  A relaxed load could miss the store for one op and
        // record it instead of aligning, which costs an extra op before
        // alignment completes.
        if (pending_region_.load_acquire().is_some() || pending_activation_.is_some()) [[unlikely]]
            return dispatch_transition_(fg, op, scope_hash, callsite_hash, depth);

        (void)record_proven_(fg, op, scope_hash, callsite_hash, depth);
        return {
            .action = DispatchResult::Action::RECORD, .status = ReplayStatus::MATCH, .pad = {}, .op_index = OpIndex{}};
    }

    // Both leaves of dispatch_op are memory-only: a ring append on one
    // side, a guard check and an engine advance on the other.  Neither
    // allocates, blocks, performs I/O, or needs an init, test or background
    // context, so the effect row is empty.
    //
    // This entry asks for the context of this Vigil's producer claim, which
    // only mint_producer_context gives.  That context has the empty row and
    // the brand of the Vigil.  An init helper, a background helper or a test
    // fixture holds a different context, so its call does not compile.
    //
    // The context proves the claim at the time of the mint.  A reference to
    // it can still reach another thread, so dispatch_op checks the thread
    // again before the ring head can advance.
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE DispatchResult
    dispatch_op_pure(VigilFgCtx const&, const ValidatedOp& op, ScopeHash scope_hash = {},
                     CallsiteHash callsite_hash = {}, OpDepth depth = {}) {
        return dispatch_op(op, scope_hash, callsite_hash, depth);
    }

    // True when a call to record_op or dispatch_op from this thread is
    // permitted: either no thread holds the producer role yet, or this
    // thread holds it.
    //
    // An adapter asks this before it reaches the recording path from a
    // thread it does not control.  A foreign thread is told to execute the
    // op eagerly and leave the ring alone, which is what keeps the recorded
    // op stream single-threaded and therefore reproducible.  The gate below
    // stays as the last resort for a caller that does not ask.
    //
    // Two threads that both arrive before either has claimed both read true
    // here, and the loser of the claim below ends the process.  That case is
    // a program with no defined producer, and this query cannot repair it.
    [[nodiscard]] bool is_producer_thread() const noexcept { return producer_claim_.is_claimable_by_caller(); }

    // Gives up the producer claim of the calling thread (release_claim of
    // ProducerClaim).  The next op claims this Vigil again, from any thread.
    // With no claim, nothing changes.  A claim of another thread ends the
    // process.  The front end calls it where its session leaves the thread,
    // such as the exit of CrucibleMode of the vessel.
    void release_producer_claim() noexcept { producer_claim_.release_claim(); }

    // The context of the thread that holds this Vigil's producer claim.  The
    // first call claims the calling thread, and a call from any other thread
    // then ends the process, as dispatch_op does.  The output, input and
    // external-slot surfaces below ask for it.  Not constexpr: the claim
    // reads the thread id and an atomic.
    [[nodiscard]] CRUCIBLE_INLINE VigilFgCtx mint_producer_context() noexcept { return assert_producer_thread_(); }

    // The eager word of the front end that the regions of the Vigil record
    // under: the word of the Config, or the last word that
    // observe_eager_numerics gave.
    [[nodiscard]] EagerNumerics eager_numerics() const noexcept { return eager_; }

    // The front end reads its eager word again at each region entry, which a
    // dispatch reports (DispatchResult::enters_region), and gives it here.  A
    // word that did not change costs one compare.  A changed word changes the
    // numerics of the run (compute_numerics_hash): a region that replays
    // under the old word stops, as at a divergence, and the pipeline cuts
    // each region after this point under the new word.  The word changes
    // between two ops of the stream, so the point is the count of the ops in
    // the ring.
    CRUCIBLE_INLINE void observe_eager_numerics(VigilFgCtx const&, EagerNumerics live) {
        if (live == eager_) [[likely]]
            return;
        change_eager_numerics_(live);
    }

    // Registers the name of one schema from the thread that holds the
    // producer claim of this Vigil, as crucible::register_schema_name does.
    // The copy of the name allocates through the allocation capability of the
    // startup context, which this Vigil keeps.  So the foreground context
    // holds no atom, and only this cold path allocates through the kept one.
    // A collision of an internal name ends the process, so the registration
    // of an internal name gives nothing.
    [[gnu::cold]] void register_schema_name(InternalSchemaName const& name) {
        ::crucible::register_schema_name(assert_producer_thread_(), allocation_, name)
            .expect("an internal name lands in the global or the late schema table");
    }

    // The same for a name from outside the process, which gives the refusal
    // of a collision (SchemaTableOf::register_name).
    [[nodiscard, gnu::cold]] SchemaRegistration register_schema_name(SanitizedSchemaName const& name) {
        return ::crucible::register_schema_name(assert_producer_thread_(), allocation_, name);
    }

    // Only the foreground writes the mode, at each activation and
    // deactivation of the replay context.  The cell loads with acquire
    // order, so a reader on another thread that reads a mode also sees what
    // the foreground wrote before it published that mode.
    //
    // Not gnu::pure, despite reading nothing else: another thread can change
    // the value between two loads, so common-subexpression elimination would
    // be wrong.
    [[nodiscard]] Mode mode() const noexcept { return replay_.mode(); }

    // True while the foreground replays a region.  The mode follows the
    // context: each activation and deactivation of the context publishes
    // the matching mode in the same call (ReplayState), so this and
    // context().is_compiled() agree whenever the foreground reads them.
    [[nodiscard]] bool is_compiled() const noexcept { return mode() == Mode::COMPILED; }

    // True when the background thread published a region that the
    // foreground has not taken yet.  The foreground takes it on its next
    // dispatch and aligns to it before replay can start, so this is true
    // earlier than is_compiled.  The acquire load pairs with the release
    // publish, and flush() orders that publish before it returns.
    [[nodiscard]] bool has_pending_region() const noexcept { return pending_region_.load_acquire().is_some(); }

    // The newest region of the Vigil: the region in the publication slot when
    // its reset point is the requested point, or else the region that the
    // producer thread took last.  The producer takes the region of the slot
    // at a dispatch, and the region of a rollback or a load.  A region in the
    // slot is newer than the region that the producer took: a take empties
    // the slot, and the mark of a rollback or a load makes the region in the
    // slot stale.  So the result names no region that a reset made stale.
    //
    // A caller can read the region while the slot holds it or the producer
    // has it.  After that, the caller can read it until the producer thread
    // next gets a region, diverges, does a rollback or loads.  Each of those
    // writes the regions that the foreground holds, and the publish stage can
    // then release the region.  No region before the first publication.
    [[nodiscard]] ::fixy::Option<DagRef<const RegionNode>> active_region() const noexcept { return newest_region_(); }

    // Advanced only by the background thread, once per region transition.
    [[nodiscard]] uint64_t current_step() const noexcept { return step_.get(); }

    // The head of the store, or the none hash when the Vigil has no store.
    // The body is in src/Vigil.cpp, which includes the store.
    [[nodiscard]] ContentHash head_hash() const noexcept;

    // True when the Vigil has a store.  A Vigil has no store when the
    // configuration names no path, or when the store at the path does not
    // open.  Then persist and load give false.
    [[nodiscard]] bool has_store() const noexcept { return cipher_.is_some(); }

    // Waits until the background thread has fully processed every entry the
    // ring held when this was called.  Fully processed means drained, fed to
    // the iteration detector, and carried through the whole boundary
    // handler, region publication included.
    //
    // Waiting for the ring to empty instead would be wrong: the drain
    // empties the ring before processing starts, so the wait would return
    // while the background thread was still building the region.  Comparing
    // the produced and processed counters is what closes that window, and
    // the release-acquire pairing on the processed counter is what makes
    // every background side effect visible on return.
    //
    // The wait spins and then parks (BackgroundThread::flush), so the context
    // of the caller owns Block and Nondet: an init, test or background
    // context.  The foreground context of the hot path owns neither.  When
    // the background thread stops before it reaches the count, the result is
    // FlushRefusal::pipeline_stopped, and the call does not hang.
    template <class Ctx>
        requires ::fixy::concurrent::CtxFitsWaitPolicy<Ctx, BackgroundThread::FlushPolicy>
    [[nodiscard]] FlushResult flush(Ctx const& ctx) noexcept {
        return bg_.shared().flush(ctx);
    }

    [[nodiscard]] bool flush_complete() const {
        return bg_.shared().count_processed_ring_ops() >= ring_total_produced();
    }

    // Makes the previously superseded transaction active again, restoring
    // the replay state to match.
    //
    // The publish stage owns the transaction log, and on_region_ready writes
    // it for every region it publishes.  So the rollback runs there, as a job
    // between two publications, and never beside one.  The job copies out the
    // restored region, because a transaction pointer belongs to the owning
    // thread and a later publication can recycle its slot.  It also requests
    // a mark at the point of the rollback, so no region cut before the
    // rollback replaces the restored region.  The foreground part then acts
    // on that copy alone, on the producer thread (adopt_region_).  The body
    // is in src/Vigil.cpp.  The log stamps the two transactions with places of
    // its own count and reads no clock, so the rollback takes no tag.
    [[nodiscard, gnu::cold]] bool rollback();

    // Traverses the compiled DAG without the adapter.  eval_guard takes a
    // guard and returns the currently observed value; exec_region executes
    // one region and reports success.  Returns true when replay finished
    // with no guard mismatch.
    template <typename GuardEval, typename RegionExec>
    [[nodiscard]] bool replay(GuardEval&& eval_guard, RegionExec&& exec_region) {
        for (const DagRef<const RegionNode> region : newest_region_()) {
            return crucible::replay(*region, std::forward<GuardEval>(eval_guard),
                                    std::forward<RegionExec>(exec_region));
        }
        return false;
    }

    // Serializes the active region to the store and advances its head.
    // A no-op when no store path was configured.  The store writes and
    // flushes files, so the caller's context must fit the store.  The head
    // commit reads no clock.  False when the store or the commit failed.
    //
    // This header only declares the store.  A caller of persist or load
    // includes crucible/Cipher.h, as each caller of a member of the store
    // does (crucible/cipher/SessionPersistenceSurface.h).
    template <class Ctx>
        requires ::crucible::CtxFitsCipherPersistence<Ctx>
    [[nodiscard, gnu::cold]] bool persist(Ctx const& ctx) {
        // The body takes the step and the active region as values, and no
        // part of the Vigil.  The store is made only from open(), so holding
        // one proves that it is open, and one mint of the open view serves
        // the store of the region and the record of its event.
        uint64_t const step = current_step();
        ::fixy::Option<DagRef<const RegionNode>> const active = active_region();
        return cipher_.with_mut(
            [&ctx, active, step](auto cipher) noexcept {
                auto& store = **cipher;
                return active.match(
                    [&ctx, &store, step](DagRef<const RegionNode> region) noexcept {
                        auto open_view = store.mint_open_view(ctx);
                        const ContentHash hash = store.store(ctx, open_view, store.content_addressed(region));
                        if (!hash) return false;
                        return store.record_event(ctx, open_view, hash, step).is_ok();
                    },
                    [] noexcept { return false; });
            },
            [] noexcept { return false; });
    }

    // Loads the most recent stored region and makes it active, activating
    // replay too when that region carries a memory plan.  A region with no
    // plan leaves the context recording, also when it replayed a region
    // before the load.  A store that refuses the object of its head gives
    // false, and the Vigil reports the reason to standard error.  The
    // activation is foreground state, so this runs on the producer thread.
    // The row is the row that an open view of the store requires.
    //
    // Each load puts its region in an arena of its own.  Before the load, the
    // Vigil ends the arena of each earlier load whose region no holder of the
    // foreground names (release_unheld_loads_).  So repeated loads keep only
    // the regions that the foreground can still read.
    //
    // The frame of the store only reads the store into a new arena, and gives
    // out owned data: the arena moves into an Option of the caller, and the
    // position of its head region in it is the result of the frame.  The
    // Vigil takes the region from the arena after the frame, and writes its
    // own members there.  So no borrow of the frame reaches the Vigil.
    template <class Ctx>
        requires ::foundation::effects::CtxAdmits<Ctx, ::crucible::CipherSessionEventPersistenceRow>
    [[nodiscard, gnu::cold]] bool load(Ctx const& ctx, ::foundation::effects::Alloc a) {
        if (cipher_.is_none()) return false;
        const VigilFgCtx fg = assert_producer_thread_();
        release_unheld_loads_();
        ::fixy::Option<LoadArena> loaded_arena{::fixy::none};
        ::fixy::Option<HeadPosition> head_position = cipher_.with_mut(
            [&ctx, a, &loaded_arena](auto cipher) noexcept -> ::fixy::Option<HeadPosition> {
                LoadArena arena = ::fixy::mint_box<::fixy::BumpArena<DagArena>>(a, LOAD_BLOCK_BYTES);
                ::fixy::Option<DagRef<RegionNode>> head = load_head_(ctx, a, **cipher, *arena);
                if (head.is_none()) return ::fixy::none;
                const DagRef<RegionNode> region = std::move(head).expect("the test above found the head region");
                HeadPosition const position = arena->position_of(region).expect("the arena of the load made its head");
                loaded_arena = ::fixy::Option<LoadArena>::some(std::move(arena));
                return ::fixy::Option<HeadPosition>::some(position);
            },
            [] noexcept -> ::fixy::Option<HeadPosition> { return ::fixy::none; });
        if (head_position.is_none()) return false;
        LoadArena arena = std::move(loaded_arena).expect("the frame that gave a position gave its arena");
        const DagRef<RegionNode> region =
            arena->at(std::move(head_position).expect("the test above found the position of the head"))
                .expect("a position of the arena of the load names its head region");
        // Each region cut before the load holds ops that ran before it.
        bg_.shared().request_mark(ring_total_produced());
        adopt_region_(fg, *region);
        keep_load_(std::move(arena), ConstRegionLink::some(region));
        return true;
    }

    // The pre-allocated address of the slot of an output of the current op.
    // A slot holds one storage, so the address is the first byte of the
    // storage of the output, and a view at an offset starts storage_offset
    // elements later (strided_view_of).  Valid only after dispatch_op
    // returned COMPILED with a matching or complete status, until the next
    // dispatch or the next change of the mode.  A call while the context
    // records ends the process.  The address leaves the replay through the
    // keyed door of the context for the foreign ABI
    // (CrucibleContext::read_output_address).  The context proves that the
    // caller holds this Vigil's producer claim.  The pool gives the device of a
    // slot (PoolAllocator::slot_data).  input_ptr gives the same for an input.
    //
    // Each address and each reference that the members that follow return
    // points into a member of this Vigil.  A temporary Vigil releases its
    // members at the end of the statement.  A deleted twin refuses each call
    // on a temporary Vigil.
    [[nodiscard]] ::fixy::Option<::fixy::RunAddress> output_ptr(VigilFgCtx const& /*fg*/, OutputPosition position) & {
        const CrucibleContext::OutsideAddress read = replay_.read_output_address(position);
        return read.match([](::fixy::Option<::fixy::RunAddress> const& address) noexcept { return address; },
                          [] noexcept -> ::fixy::Option<::fixy::RunAddress> { refuse_call_outside_replay_(); });
    }

    ::fixy::Option<::fixy::RunAddress> output_ptr(VigilFgCtx const&, OutputPosition) && =
        delete("a temporary Vigil releases its pool at the end of the statement. Use a Vigil that has a name");

    [[nodiscard]] ::fixy::Option<::fixy::RunAddress> input_ptr(VigilFgCtx const& /*fg*/, InputPosition position) & {
        const CrucibleContext::OutsideAddress read = replay_.read_input_address(position);
        return read.match([](::fixy::Option<::fixy::RunAddress> const& address) noexcept { return address; },
                          [] noexcept -> ::fixy::Option<::fixy::RunAddress> { refuse_call_outside_replay_(); });
    }

    ::fixy::Option<::fixy::RunAddress> input_ptr(VigilFgCtx const&, InputPosition) && =
        delete("a temporary Vigil releases its pool at the end of the statement. Use a Vigil that has a name");

    // The address of an external slot of the active region and the device of
    // its owner.  A slot that the plan of the active region does not mark
    // external gives its ExternalRefusal (CompiledScope::register_external),
    // and the slot stays as it was.  A call while the context records ends the
    // process.
    [[nodiscard, gnu::cold]] ::fixy::Result<::fixy::Unit, ExternalRefusal>
    register_external(VigilFgCtx const& /*fg*/, SlotId sid, TensorData data) {
        using Registered = ::fixy::Result<::fixy::Unit, ExternalRefusal>;
        return replay_
            .with_compiled([sid, data](CompiledScope& scope) noexcept { return scope.register_external(sid, data); })
            .match([](Registered registered) noexcept { return registered; },
                   [] noexcept -> Registered { refuse_call_outside_replay_(); });
    }

    // One data slot of an op of a replayed region (ScalarRole of Types.h),
    // and the value that the replay bound to it last.
    struct BoundScalar {
        OpIndex op{};
        ScalarSlot slot{::fixy::index<0>()};
        int64_t value = 0;
    };

    // The data slots of the region of the last activation or switch, in the
    // order of the ops and of the slots, each with its value.  The activation
    // gives each slot the value of the recording, and the replay of each op
    // writes the value of each data slot of the live op (bind_scalars_).  So
    // after the replay of an op, its slots hold the values of this iteration,
    // and a kernel of the region reads its scalar arguments here.  Each slot
    // keeps its value while the Vigil records after a divergence.
    [[nodiscard]] ::fixy::View<const BoundScalar> bound_scalars(VigilFgCtx const&) const& noexcept {
        return bound_scalars_.slots.view();
    }

    ::fixy::View<const BoundScalar> bound_scalars(VigilFgCtx const&) const&& =
        delete("a temporary Vigil releases its slots at the end of the statement. Use a Vigil that has a name");

    // The count of loads whose arena the Vigil keeps: the last load, and each
    // earlier load whose region a holder of the foreground named at the last
    // load (release_unheld_loads_).  At most LOAD_SLOTS.  Only the producer
    // thread reads or writes the arenas, so the query takes its context.
    // Complexity: O(LOAD_SLOTS).
    [[nodiscard]] uint32_t kept_load_count(VigilFgCtx const&) const noexcept {
        uint32_t kept = 0;
        for (const ::fixy::Option<LoadedRegion>& load : loads_) {
            if (load.is_some()) ++kept;
        }
        return kept;
    }

    [[nodiscard]] uint64_t compiled_iterations() const { return replay_.context().compiled_iterations(); }
    [[nodiscard]] uint64_t diverged_count() const { return replay_.context().diverged_count(); }
    [[nodiscard]] const CrucibleContext& context() const& { return replay_.context(); }
    const CrucibleContext& context() const&& =
        delete("a temporary Vigil releases its context at the end of the statement. Use a Vigil that has a name");
    [[nodiscard]] const RegionCache& region_cache() const& { return region_cache_; }
    const RegionCache& region_cache() const&& =
        delete("a temporary Vigil releases its cache at the end of the statement. Use a Vigil that has a name");

    // The stack of the slots into which an adapter writes the records of an
    // op before it builds the op.  The thread that holds the producer claim
    // owns it, as it owns the producer surfaces of the ring and of the
    // metadata log.  The slots are a member, so the record of an op fills no
    // list on the stack of each call.  The adapter opens a frame of the stack
    // for each op (with_foreign_frame of ValidatedOp.h), so an op that runs
    // inside another op writes other slots.
    //
    // An op path calls this door, so the door does not read the thread
    // identity.  dispatch_op checks the thread again before the ring head
    // can advance.
    [[nodiscard]] OpRecordStack& op_record_stack(VigilFgCtx const&) & noexcept { return op_record_stack_; }
    OpRecordStack& op_record_stack(VigilFgCtx const&) && =
        delete("a temporary Vigil releases its slots at the end of the statement. Use a Vigil that has a name");

    // Gives the closing entry of a nest whose op did not reach this Vigil
    // after an op inside the nest did (OpNest::is_unrecorded): the op raised,
    // or the adapter refused its record.  The entry has the schema hash
    // kUnrecordedNestSchema, the depth of the nest and no record, so a reader
    // of the post-order stream gives the ops of the nest to it and not to the
    // next op.  The build refuses each region that holds it.  A replay
    // diverges at it, because no region holds it.  The result of the dispatch
    // tells the adapter nothing, so the call drops it.  VigilOpNest calls it
    // at its close, also in an unwind.  Cold: an op that raises is rare.
    [[gnu::cold]] CRUCIBLE_NOINLINE void give_unrecorded_nest(OpDepth depth) {
        TraceRing::Entry entry{};
        entry.schema_hash = kUnrecordedNestSchema;
        entry.shape_hash = ShapeHash{::foundation::reflect::combine_ids(kUnrecordedNestSchema.raw(), depth.raw())};
        // This code builds each field of the entry, so the entry is valid by
        // construction, as the comment of TraceRing::ValidatedEntryPtr states.
        const ValidatedOp op = mint_validated_op(mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(),
                                                 ::fixy::View<const TensorMeta>{})
                                   .expect("an entry with no tensor takes no record");
        (void)dispatch_op(op, ScopeHash{}, CallsiteHash{}, depth);
    }

    // Counters that any thread can read with no context.  Each value is a
    // snapshot, and it can be stale by the time the caller reads it.
    [[nodiscard]] ::fixy::Stale<uint32_t> ring_size() const noexcept {
        return ring_.with_owner([](TraceRing const& ring) noexcept { return ring.size(); });
    }
    [[nodiscard]] uint64_t ring_total_produced() const noexcept {
        return ring_.with_owner([](TraceRing const& ring) noexcept { return ring.total_produced(); });
    }
    [[nodiscard]] ::fixy::Stale<uint32_t> meta_log_size() const noexcept {
        return meta_log_.with_owner([](MetaLog const& log) noexcept { return log.size(); });
    }
    // The count of the records that the record path put into the metadata
    // log, from the first record of the log.
    [[nodiscard]] uint64_t meta_log_total_produced() const noexcept {
        return meta_log_.with_owner([](MetaLog const& log) noexcept { return log.total_produced(); });
    }

    [[nodiscard]] uint32_t bg_iterations_completed() const { return bg_.shared().iterations_completed.get(); }
    // The count of iteration boundaries that the detector reported, and true
    // while the detector holds an accepted period.  Each value comes from the
    // snapshot that the detect stage published after its last batch, so any
    // thread can read it while the pipeline runs.  After flush(), it covers
    // each op that the ring held at the call of flush.
    [[nodiscard]] uint32_t bg_detector_boundaries() const noexcept {
        return bg_.shared().detector_snapshot.load_acquire().boundaries;
    }
    [[nodiscard]] bool bg_detector_confirmed() const noexcept {
        return bg_.shared().detector_snapshot.load_acquire().period == BackgroundThread::DetectorPeriod::Confirmed;
    }

    // The count of regions in the region store of the pipeline, and the
    // count of published regions that a full store discarded.  Each call
    // runs on the publish stage, between two publications.
    [[nodiscard]] uint32_t region_store_count() {
        uint32_t count = 0;
        bg_.run_on_publish_stage(
            [&count](BackgroundThread::PublishStageView& bg, BackgroundThread::PublishStage const& stage) noexcept {
                count = bg.region_count(stage);
            });
        return count;
    }
    [[nodiscard]] uint64_t region_store_drops() {
        uint64_t drops = 0;
        bg_.run_on_publish_stage(
            [&drops](BackgroundThread::PublishStageView& bg, BackgroundThread::PublishStage const& stage) noexcept {
                drops = bg.regions_dropped_full(stage);
            });
        return drops;
    }

    // True only when the configuration asked for the watchdog and the
    // tracing subprogram actually attached.
    //
    // True does not imply a usable verdict yet.  The first window always
    // reports InsufficientData while the baseline is captured.
    [[nodiscard]] bool watchdog_enabled() const noexcept { return wd_.is_some(); }

    // The acquire load pairs with the release store on the background
    // thread.  Reads InsufficientData when no region transition has happened
    // yet, and when the watchdog is disabled.
    [[nodiscard]] ::crucible::warden::WatchdogVerdict last_watchdog_verdict() const noexcept {
        return wd_last_verdict_.load_acquire();
    }

    // While the watchdog is enabled, each region transition increments
    // exactly one of these three.  The acquire load pairs with the release
    // half of the increment on the background thread.
    [[nodiscard]] uint32_t watchdog_healthy_count() const noexcept { return wd_healthy_count_.load_acquire(); }
    [[nodiscard]] uint32_t watchdog_downgrade_count() const noexcept { return wd_downgrade_count_.load_acquire(); }
    [[nodiscard]] uint32_t watchdog_insufficient_count() const noexcept {
        return wd_insufficient_count_.load_acquire();
    }

    // The watchdog itself is deliberately not exposed by reference.  Its
    // window state is non-atomic and the background thread mutates it during
    // each observation, so a read through a leaked reference would be a data
    // race.  The verdict and the three counters above are the whole
    // diagnostic surface; anything more must be published as another atomic
    // from the region-ready callback.

private:
    // Ends the process for a call of output_ptr, input_ptr or register_external
    // while the context records.  A front end calls them only after dispatch_op
    // gave COMPILED, so such a call is a defect of the caller.  The body is in
    // src/Vigil.cpp, so a call of a compiled op costs the branch alone.
    [[noreturn]] CRUCIBLE_COLD static void refuse_call_outside_replay_() noexcept;

    // The first call claims the thread; every later call verifies the match.
    // A call from a second thread ends the process, in every build mode.
    // This is the one check between a second producer and a ring that tears
    // without a diagnostic: both threads claim the same slot and the later
    // write erases the earlier one.
    [[nodiscard]] CRUCIBLE_INLINE VigilFgCtx assert_producer_thread_() noexcept {
        return producer_claim_.mint_producer_context();
    }

    // Records one op with the proof of the record path, which the claim makes
    // from the context of the caller.  Each caller minted that context in the
    // same call (assert_producer_thread_), so the record reads the thread one
    // time.
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool record_proven_(VigilFgCtx const& fg, const ValidatedOp& op,
                                                                ScopeHash scope_hash, CallsiteHash callsite_hash,
                                                                OpDepth depth) noexcept {
        const RecordProof proof = producer_claim_.prove_sole_writer<RecordProducerRole>(fg);
        // One frame opens the two producer sides, and the body inlines into
        // the record, so it takes its captures in registers and puts no stack
        // canary on the hot path.
        return ::fixy::concurrent::with_held_ends(ring_.producer_side(), proof, meta_log_.producer_side(), proof,
                                                  [&op, scope_hash, callsite_hash, depth] [[gnu::always_inline]] (
                                                      TraceRing::Producer & ring, MetaLog::Producer & log) noexcept {
                                                      return TraceRing::try_record(ring, log, op.metas(), op.entry(),
                                                                                   scope_hash, callsite_hash, depth);
                                                  });
    }

    // The body of active_region.  The producer thread is the one writer of
    // the requested reset, so on that thread the comparison cannot race it.
    [[nodiscard]] ::fixy::Option<DagRef<const RegionNode>> newest_region_() const noexcept {
        const PendingRegion pending = pending_region_.load_acquire();
        for (const auto& published : pending) {
            // The foreground reads a region of the pipeline only as const.
            if (published->reset_point == bg_.shared().requested_reset())
                return ConstRegionLink::some(
                    ConstRegionLink{published->region}.expect("the publish stage gives a record of a region"));
        }
        return taken_region_.load_acquire();
    }

    // The store part of load.  The store is a template parameter, so each
    // member of the store resolves where a caller instantiates load, and
    // this header needs only the declaration of the store.
    template <class Ctx, class Store>
    [[nodiscard]] static ::fixy::Option<DagRef<RegionNode>>
    load_head_(Ctx const& ctx, ::foundation::effects::Alloc a, Store& store, ::fixy::BumpArena<DagArena>& arena) {
        if (store.empty()) return ::fixy::none;
        auto open_view = store.mint_open_view(ctx);
        // A refusal gives no region, and the report tells the person why the
        // store gave no region of its head.
        const auto loaded = store.load_content_addressed(ctx, open_view, a, store.head(), arena);
        return loaded.match(
            [](auto const& payload) -> ::fixy::Option<DagRef<RegionNode>> {
                return ::fixy::Option<DagRef<RegionNode>>::some(payload.get());
            },
            [](LoadError reason) -> ::fixy::Option<DagRef<RegionNode>> {
                ::fixy::report(::fixy::Sink::Err, "[crucible] the Cipher store gives no region of its head: {}\n",
                               load_error_text(reason));
                return ::fixy::none;
            });
    }

    // Opens the store and the watchdog of the configuration, and gives the
    // numerics of the start of the pipeline.  The initializer of bg_ calls it
    // before the start, while each other member is complete.  The body is in
    // src/Vigil.cpp.
    [[nodiscard, gnu::cold]] NumericsHash prepare_start_(::fixy::InitLoadCtx const& startup);

    // Runs on the background thread when a new region is ready.  It must
    // not touch the persistence store: that owns mutable resident-cache and
    // log state and belongs to the foreground.  The background thread hands
    // over its own context, so the observation acts under a context the
    // caller holds, not one built here.
    //
    // Commits and activates a transaction for the region, publishes the
    // region to the foreground, and observes the watchdog when it is
    // loaded.  The body is in src/Vigil.cpp.
    [[gnu::cold]] void on_region_ready(::foundation::effects::Bg const& bg, BackgroundThread::PublishStage const& stage,
                                       PublishedRegion const& published) noexcept;

    // Gives the regions that this Vigil holds to a sweep of the region store,
    // on the publish stage (BackgroundThread::RegionMarksCall).
    //
    // The words of the foreground that it reads are the publication slot, the
    // sequence of held_regions_ and its cells, and each one is an atomic.  The
    // transaction log and the record of the published region belong to the
    // publish stage.  It takes no lock and waits for nothing: when the
    // foreground writes held_regions_ at this time, the marks say so and name
    // no holder of it.  The body is in src/Vigil.cpp.
    [[gnu::cold, nodiscard]] BackgroundThread::RegionMarks
    read_region_marks_(BackgroundThread::PublishStage const& stage) noexcept;

    // Makes a region from outside the pipeline active: the region of a
    // rollback or of a load.  The caller first requests a mark at the
    // current point, so each region that the pipeline cut before it is
    // stale.  The region in the slot goes to the cache, the alignment walk
    // closes, and the context replays the region, or records when the region
    // has no memory plan.  The body is in src/Vigil.cpp.
    [[gnu::cold]] void adopt_region_(VigilFgCtx const& fg, RegionNode const& region);

    // Writes the regions that the foreground holds into held_regions_: the
    // region of the alignment walk, the region of the replay context, the
    // region that the producer took last and the regions of the cache.
    // After the call, the foreground reads no other region than these and
    // the last published region.  Runs on the producer thread at the end of
    // each cold path that can change one of these holders.
    [[gnu::cold]] void publish_held_regions_() noexcept {
        held_regions_.with_write_([this](auto store_cell) noexcept {
            store_cell(::fixy::index<0>(), pending_activation_);
            store_cell(::fixy::index<1>(), replay_.context().active_region());
            store_cell(::fixy::index<2>(), taken_region_.load_acquire());
            for (uint32_t age = 0; age < RegionCache::CAP; ++age) {
                store_cell(Holders::Position::check(3 + age).expect("the cells hold each age of the cache"),
                           region_cache_.region_at(age));
            }
        });
    }

    // Ends the arena of each load whose region no holder of the foreground
    // names.  Runs on the producer thread before each load.  Complexity:
    // O(LOAD_SLOTS * Holders::CAP).
    [[gnu::cold]] void release_unheld_loads_() noexcept {
        // The test reads only the cells of the holders, through a const
        // reference, and no other part of the Vigil.
        Holders const& held = held_regions_;
        for (const auto slot : loads_.indices()) {
            const bool is_unheld = loads_.at(slot).is_some_and(
                [&held](const LoadedRegion& kept) noexcept { return !held.holds(kept.region); });
            if (is_unheld) loads_.set(slot, ::fixy::none);
        }
    }

    // Keeps the arena of a load beside its region, in a free slot.  The
    // holders name at most Holders::CAP regions, and the release before
    // the load left a slot only for a region that a holder names.  So one
    // slot of LOAD_SLOTS is free for the new load.
    [[gnu::cold]] void keep_load_(::fixy::Box<::fixy::BumpArena<DagArena>>&& arena, ConstRegionLink region) noexcept {
        for (const auto slot : loads_.indices()) {
            if (loads_.at(slot).is_some()) continue;
            loads_.set(slot, ::fixy::some(LoadedRegion{.arena = std::move(arena), .region = region}));
            return;
        }
        ::fixy::fatal(
            "a load of a Vigil found no free slot for its arena, although the holders name at most {} regions",
            Holders::CAP);
    }

    // The data slots of the active region, in the order of the ops, and the
    // next slot of the iteration, at most the count of the slots.  Each
    // activation and each switch of a region writes them
    // (register_inputs_from_region_), and replay_op_ walks them.  Only the
    // producer thread reads or writes them.  The reads of the external slots
    // stand in the replay context, beside the active region.
    struct BoundScalars {
        ::fixy::HeapArray<BoundScalar> slots{};
        uint32_t next = 0;
    };

    // Binds the inputs of the op that the replay matched to the live op: each
    // external tensor to the data of the live op, and each data slot to the
    // value of the live op.  An external is a tensor that no op of the region
    // writes, such as a parameter or a batch of the data loader, and its
    // address can change from one iteration to the next.  A data scalar, such
    // as a step size, can change its value from one iteration to the next.
    // The activation lists each read of an external
    // (CompiledScope::list_external_reads) and each data slot (BoundScalars),
    // in the order of the ops, and two cursors walk the lists once in each
    // iteration.  An op that reads no external and has no data slot costs four
    // compares.  The cursors go back to the start at op 0 and when the
    // iteration completes.  O(1) for each op, and O(the reads and the slots) for
    // the iteration.
    //
    // The advance and the binds run in one compiled scope of the context, so
    // one test of the mode serves them.  The body of the scope reaches the data
    // slots of this Vigil and no other part of it, because this Vigil holds the
    // key of the context.  A DIVERGED step binds nothing.
    [[nodiscard]] CRUCIBLE_INLINE static ReplayStep replay_op_(CompiledScope& scope, SchemaHash schema_hash,
                                                               ShapeHash shape_hash, const ValidatedOp& op,
                                                               BoundScalars& bound) {
        const ReplayStep step = scope.advance(schema_hash, shape_hash);
        if (step.status() == ReplayStatus::DIVERGED) [[unlikely]]
            return step;
        const uint32_t matched = step.matched_op_index().value().raw();
        // A new iteration starts the cursors again.  The COMPLETE below does
        // that too, but the switch of a region advances the engine with no
        // bind, so a rewind can pass this function by.
        if (matched == 0) {
            scope.rewind_external_reads();
            bound.next = 0;
        }
        scope.bind_external_reads(OpIndex{matched}, op);
        // The cursor passes the slots of the earlier ops, writes the value of
        // each slot of the live op, and stops at the first slot of a later op.
        // The element door of each step lends the slot at the cursor in place,
        // and its none arm stops the walk at the end of the list.  The guard
        // matched the tags and the data slots of the live op, and an entry
        // stores each of its five slots, so each read is in bounds.
        enum class SlotStep : uint8_t {
            Stop,
            Next,
        };
        while (bound.slots.with_element(
                   bound.next,
                   [matched, &op](BoundScalar& slot) noexcept {
                       if (slot.op.raw() > matched) return SlotStep::Stop;
                       if (slot.op.raw() == matched) slot.value = op.entry().scalar_values.at(slot.slot.value());
                       return SlotStep::Next;
                   },
                   [] noexcept { return SlotStep::Stop; })
               == SlotStep::Next) {
            ++bound.next;
        }
        if (step.status() == ReplayStatus::COMPLETE) {
            scope.rewind_external_reads();
            bound.next = 0;
        }
        return step;
    }

    // The result of an op that the replay matched: the status of its guard
    // and the position of the op in the region, from the step of the advance
    // (ReplayStep).  A COMPLETE gives the last op of the region.  The two
    // COMPILED results of dispatch_op and of the switch after a divergence
    // come from this one door.
    [[nodiscard]] CRUCIBLE_INLINE static DispatchResult compiled_result_(ReplayStep step) noexcept {
        return DispatchResult::compiled(step.status(), step.matched_op_index().value());
    }

    // The cold dispatch paths below are noinline so the hot path needs no
    // callee-saved registers for them.

    // Looks the diverging shape up in the region cache, tries to switch to a
    // matching region, and falls back to recording.
    //
    // The divergent operation is deliberately not recorded, as the tail of
    // this function says.  A switch to a region that matches the op binds the
    // inputs of the op to the new region, as a match of dispatch_op does.  The
    // op comes by value, as in dispatch_transition_, so the op of dispatch_op
    // has no address outside its frame.
    [[nodiscard, gnu::cold]] CRUCIBLE_NOINLINE DispatchResult handle_divergence_(VigilFgCtx const& fg, ValidatedOp op) {
        const SchemaHash schema_hash = op.entry().schema_hash;
        const ShapeHash shape_hash = op.entry().shape_hash;
        const uint32_t div_pos = replay_.context().engine().ops_matched();

        // A region the background thread published while the context was
        // compiled is still in the slot, because the replay branch of
        // dispatch_op never reads it.  Leave it there and the first dispatch
        // after this reset consumes it and aligns against it.  That re-enters
        // replay on the trace this divergence just invalidated.  Worse,
        // alignment records nothing, so the background thread never receives
        // the new trace it needs to build a region that matches.
        //
        // Its ops are not worthless, so it goes to the region cache rather
        // than the floor.  find_alternate below can then pick it, but only
        // through try_switch_region_, which first verifies that every op
        // before the divergence position is identical.
        //
        // The pipeline can still publish a region of ops recorded before
        // the reset request below.  The reset point of that region is not
        // the requested point, so consume_pending_region_ caches it too.
        cache_pending_region_();

        // Alignment state is foreground-only, and it describes a partial walk
        // into a region this reset abandons.  No path compiles the context
        // while a walk is open: an alignment closes its walk when it
        // activates, and adopt_region_ closes it for a rollback or a load.
        // The reset here keeps that rule local to this function.
        pending_activation_ = ::fixy::none;
        alignment_pos_ = alignment_pos_at(0);
        alignment_ops_ = 0;

        // The context is compiled, so it replays a region.
        const ConstRegionLink diverged = replay_.context().active_region();
        region_cache_.insert(*ConstRegionLink{diverged}.expect("a compiled context replays a region"));

        // Look for a cached region that matches at the divergence position,
        // excluding the current one.
        const ConstRegionLink alt = region_cache_.find_alternate(div_pos, schema_hash, shape_hash, diverged);

        bool is_switched = false;
        for (const DagRef<const RegionNode>& region : alt)
            is_switched = try_switch_region_(fg, *region, div_pos);
        if (is_switched) {
            // The switch leaves the context compiled, so advance past the
            // divergent op.
            const ReplayStep step = replay_
                                        .with_compiled([schema_hash, shape_hash, &live = ::fixy::as_const(op),
                                                        &bound = bound_scalars_](CompiledScope& scope) noexcept {
                                            return replay_op_(scope, schema_hash, shape_hash, live, bound);
                                        })
                                        .expect("a switch of region leaves the context compiled");
            if (step.status() != ReplayStatus::DIVERGED) {
                publish_held_regions_();
                return compiled_result_(step);
            }
            // Diverging twice in a row falls through to the reset below.
        }

        replay_.deactivate();
        // The reset applies at the point of the divergence: each op in the
        // ring was recorded before it, and the next op comes after it.  A
        // restart refutes the period of the region that diverged.  The
        // closing entry of a nest whose op raised is no evidence against that
        // period: the op did not finish, and the next iteration can repeat the
        // period.  So that reset is a mark, which makes each region cut before
        // the point stale and keeps the periods of the detector.
        if (schema_hash == kUnrecordedNestSchema) {
            bg_.shared().request_mark(ring_total_produced());
        } else {
            bg_.shared().request_restart(ring_total_produced());
        }
        publish_held_regions_();
        // The divergent op is deliberately not recorded: it would poison the
        // background thread's iteration detector.
        return {.action = DispatchResult::Action::RECORD,
                .status = ReplayStatus::DIVERGED,
                .pad = {},
                .op_index = OpIndex{}};
    }

    // Takes the validated op rather than the Entry behind it.  An Entry
    // reference has to pass mint_validated_op again before record_op takes
    // it.  Carrying the op keeps the check of the caller.
    //
    // The op comes by value.  A reference to the op of dispatch_op gives
    // that op an address outside the frame, and then the stack protector
    // puts a canary on each path of dispatch_op.
    [[nodiscard, gnu::cold]] CRUCIBLE_NOINLINE DispatchResult dispatch_transition_(VigilFgCtx const& fg, ValidatedOp op,
                                                                                   ScopeHash scope_hash,
                                                                                   CallsiteHash callsite_hash,
                                                                                   OpDepth depth) {
        const TraceRing::Entry& entry = op.entry();
        // A newer region arriving mid-alignment replaces the pending one.  A
        // region with other ops restarts the alignment from zero, because
        // the old partial match means nothing for it.  A region with the same
        // ops keeps the walk (take_published_region_).
        if (pending_region_.load_acquire().is_some()) consume_pending_region_();

        // An alignment that completes activates the region, and the next op
        // is the first op that replays: the front end reads its eager word.
        bool enters_region = false;
        if (pending_activation_.is_some()) {
            // Nothing is recorded during alignment, because it would create
            // false iteration boundaries in the background detector.  The
            // bound of try_align_ ends an alignment that finds no match.
            try_align_(fg, entry.schema_hash, entry.shape_hash);
            enters_region = replay_.context().is_compiled();
        } else {
            (void)record_proven_(fg, op, scope_hash, callsite_hash, depth);
        }
        publish_held_regions_();

        return {.action = DispatchResult::Action::RECORD,
                .status = ReplayStatus::MATCH,
                .enters_region = enters_region,
                .pad = 0,
                .op_index = OpIndex{}};
    }

    // The body of observe_eager_numerics for a changed word.  The region in
    // the slot and the region of an open alignment were cut under the old
    // word, so they go to the cache, and a replayed region goes there too.
    // The cache keeps them for a return to the old word, and the doors of a
    // replay refuse them under the new word.  The pipeline drops each
    // region cut before the point, so none reaches the slot after it.
    [[gnu::cold]] CRUCIBLE_NOINLINE void change_eager_numerics_(EagerNumerics live) {
        eager_ = live;
        const NumericsHash numerics = compute_numerics_hash(cfg_.numerics, live);
        cache_pending_region_();
        for (const DagRef<const RegionNode>& walked : pending_activation_)
            region_cache_.insert(*walked);
        pending_activation_ = ::fixy::none;
        alignment_pos_ = alignment_pos_at(0);
        alignment_ops_ = 0;
        const ConstRegionLink replayed = replay_.context().active_region();
        if (replay_.change_numerics(numerics))
            region_cache_.insert(*ConstRegionLink{replayed}.expect("a replay that changes its numerics has a region"));
        bg_.shared().request_numerics(ring_total_produced(), numerics);
        publish_held_regions_();
    }

    // Moves the region in the slot to the cache, for a caller that knows that
    // each region the slot can hold is stale.
    [[gnu::cold]] void cache_pending_region_() {
        const PendingRegion stale = pending_region_.exchange_acq_rel(::fixy::none);
        for (const auto& published : stale)
            region_cache_.insert(
                *ConstRegionLink{published->region}.expect("the publish stage gives a record of a region"));
    }

    // Moves the published region into foreground-only alignment state.  It
    // does not activate replay; the alignment phase does that.
    //
    // A region whose reset point is not the requested point holds ops
    // recorded before the last divergence.  The publish stage can give it
    // out just before the request, so the check runs here too.  The region
    // goes to the cache, as in handle_divergence_, and an open alignment
    // keeps its state.  The foreground is the one writer of the request, so
    // this read cannot race it.  A region that is not stale is the region
    // that the producer took last (active_region).
    [[gnu::cold]] void consume_pending_region_() {
        const PendingRegion consumed = pending_region_.exchange_acq_rel(::fixy::none);
        for (const auto& published : consumed)
            take_published_region_(*published);
    }

    [[gnu::cold]] void take_published_region_(PublishedRegion const& published) {
        const RegionNode& region =
            *ConstRegionLink{published.region}.expect("the publish stage gives a record of a region");
        if (published.reset_point != bg_.shared().requested_reset()) {
            region_cache_.insert(region);
            return;
        }
        const ConstRegionLink taken = ConstRegionLink::some(::fixy::to(region));
        taken_region_.store_release(taken);
        if (region.num_ops() == 0) return;

        // A newer region that holds the ops of an open walk takes the walk
        // over, and the walk keeps its position.  The background publishes
        // a region at each iteration boundary of a backlog, and each one has
        // the same ops, so a restart at each one would lose the matched ops.
        const bool continues_walk = pending_activation_.is_some_and(
            [&region, position = alignment_pos_.value()](DagRef<const RegionNode> const& walked) noexcept {
                return holds_walk_(*walked, region, position);
            });
        pending_activation_ = taken;
        if (continues_walk) return;
        alignment_pos_ = alignment_pos_at(0);
        alignment_ops_ = 0;
    }

    // True when newer holds the ops that a walk of pos ops into walked
    // matched, so the walk can go on in newer: the two regions have the same
    // content and op count, and their first pos ops are equal.  O(K) for
    // K = ALIGNMENT_K.
    [[nodiscard, gnu::cold]] static bool holds_walk_(const RegionNode& walked, const RegionNode& newer,
                                                     uint8_t pos) noexcept {
        if (walked.content_hash != newer.content_hash || walked.num_ops() != newer.num_ops()) return false;
        for (uint32_t i = 0; i < pos; ++i) {
            if (walked.op_at(i).schema_hash != newer.op_at(i).schema_hash
                || walked.op_at(i).shape_hash != newer.op_at(i).shape_hash)
                return false;
        }
        return true;
    }

    // A sliding-window match against the region's first K ops.
    //
    // When the background thread publishes a region, the foreground's
    // position within the iteration is unknown.  K consecutive matches
    // locate the boundary, at which point replay activates and the engine
    // skips forward over the ops already matched.  One op could match by
    // coincidence; K in a row will not.
    [[gnu::cold]] void try_align_(VigilFgCtx const& fg, SchemaHash schema, ShapeHash shape) {
        // The single caller reaches this helper from inside a branch that
        // tested pending_activation_.  expect() checks it again in each
        // build, with one branch on this cold path.
        const RegionNode& region =
            *ConstRegionLink{pending_activation_}.expect("the caller tested the region of the alignment");
        ++alignment_ops_;

        const uint8_t pos = alignment_pos_.value();
        if (schema == region.op_at(pos).schema_hash && shape == region.op_at(pos).shape_hash) {
            // The increment cannot exceed the bound: once the previous call
            // reached the threshold it cleared pending_activation_, so this
            // function is not called again past that point.
            alignment_pos_ = alignment_pos_at(static_cast<uint8_t>(pos + uint8_t{1}));
        } else {
            alignment_pos_ = alignment_pos_at(restart_after_mismatch_(region, pos, schema, shape));
        }

        // A region shorter than K is matched in full instead.
        const uint32_t threshold = (region.num_ops() < ALIGNMENT_K) ? region.num_ops() : ALIGNMENT_K;

        if (uint32_t{alignment_pos_.value()} >= threshold) {
            if (!replay_.activate(region)) {
                // A region with no memory plan, or with other numerics than
                // the run, cannot replay.
                pending_activation_ = ::fixy::none;
                return;
            }

            register_inputs_from_region_(fg, region, uint32_t{alignment_pos_.value()});
            region_cache_.insert(region);

            // The matched ops already executed eagerly, so the engine has
            // to skip them for the next op to check against the right entry.
            const uint32_t aligned = alignment_pos_.value();
            static_cast<void>(
                replay_
                    .with_compiled([&region, aligned](CompiledScope& scope) noexcept {
                        for (uint32_t i = 0; i < aligned; i++) {
                            const ReplayStatus status =
                                scope.advance(region.op_at(i).schema_hash, region.op_at(i).shape_hash).status();
                            // The hashes fed in here are the region's own, and
                            // alignment already compared each one against the
                            // same entry, so a mismatch means that the cursor of
                            // the engine is out of step.  The data is not bad.
                            // The check stays in each build, and it runs once
                            // for each aligned op, at the activation of a region
                            // only.
                            CRUCIBLE_FATAL_INVARIANT(status == ReplayStatus::MATCH || status == ReplayStatus::COMPLETE);
                        }
                    })
                    .expect("an activation that gave true leaves the context compiled"));

            pending_activation_ = ::fixy::none;
            return;
        }

        // The stream does not hold the region (alignment_bound).  The region
        // goes to the cache, as a stale pending region does, and the next op
        // goes to the ring, so the background can publish a region of the
        // stream that the foreground sees.
        //
        // This branch sends no reset to the background.  The reset can refute
        // the accepted period as the period of a region that diverged, and
        // this region never ran.  The detector accepts a period only when two
        // periods of recorded ops are equal, and a region holds the last of
        // the two.  So the ops that the alignment did not record cannot put
        // into a region a sequence that the stream does not hold.
        if (alignment_ops_ >= alignment_bound(region.num_ops())) {
            region_cache_.insert(region);
            pending_activation_ = ::fixy::none;
            alignment_pos_ = alignment_pos_at(0);
        }
    }

    // The match length after the op (schema, shape) did not match region op
    // pos.  The last `border` ops seen are region ops pos - border to pos - 1.
    // They and this op continue a match when they equal the first border ops
    // and this op equals op border.  The longest such border wins, as in the
    // Knuth-Morris-Pratt search.  A restart at op 0 only misses the start A A A
    // A B in the stream A A A A A B, and then the alignment never completes:
    // nothing is recorded during alignment, so no other region replaces it.
    // O(K^2) for K = ALIGNMENT_K, on a cold path.
    [[nodiscard, gnu::cold]] static uint8_t restart_after_mismatch_(const RegionNode& region, uint8_t pos,
                                                                    SchemaHash schema, ShapeHash shape) noexcept {
        for (uint32_t border = pos; border-- > 0;) {
            bool is_border = true;
            for (uint32_t i = 0; i < border && is_border; ++i) {
                const auto& early = region.op_at(i);
                const auto& late = region.op_at(uint32_t{pos} - border + i);
                is_border = early.schema_hash == late.schema_hash && early.shape_hash == late.shape_hash;
            }
            if (is_border && schema == region.op_at(border).schema_hash && shape == region.op_at(border).shape_hash)
                return static_cast<uint8_t>(border + 1);
        }
        return 0;
    }

    // Walks the region's ops to recover the storage address of each external
    // slot from the recorded tensor metadata, and lists each read of an
    // external and each data slot for advance_and_bind_reads_ and
    // bind_scalars_.  The replay resumes at op
    // resume_op, so each cursor starts at the first item of that op or of a
    // later op.  A read of an earlier op of this iteration keeps the recorded
    // address, and a data slot of an earlier op keeps the recorded value,
    // until the next iteration binds it again.  Runs once per activation and
    // per switch of a region, on the producer thread, which the context of the
    // claim proves.  O(the inputs and the scalars of the region) and one
    // allocation of each list.
    [[gnu::cold]] void register_inputs_from_region_(VigilFgCtx const& /*fg*/, const RegionNode& region,
                                                    uint32_t resume_op) {
        list_bound_scalars_(region, resume_op);
        // Every call site activates the context on the region immediately
        // before it calls here, so the context is compiled, and the region
        // has a plan.
        const DagRef<MemoryPlan const> plan = PlanLink{region.plan}.expect("an active region has a memory plan");
        static_cast<void>(replay_
                              .with_compiled([&region, &plan, resume_op](CompiledScope& scope) noexcept {
                                  scope.list_external_reads(OpIndex{resume_op});
                                  register_recorded_externals_(scope, region, *plan);
                              })
                              .expect("each caller activates the context on the region first"));
    }

    // Registers the storage address of each external slot of the plan, from
    // the recorded metadata of the first input of the region that reads it.
    // The id of a slot is its position in the plan, which the inputs of the
    // ops name.  O(the external slots times the inputs of the region).
    [[gnu::cold]] static void register_recorded_externals_(CompiledScope& scope, const RegionNode& region,
                                                           const MemoryPlan& plan) {
        const ::fixy::View<const TensorSlot> plan_slots = plan.slots.view();
        for (uint32_t position = 0; position < plan_slots.size(); position++) {
            if (!plan_slots.get(position).expect("the loop bounds the position")->is_external) continue;
            const SlotId target{position};
            ::fixy::Option<TensorData> data{::fixy::none};

            for (uint32_t i = 0; i < region.num_ops() && data.is_none(); i++) {
                const auto& te = region.op_at(i);
                // An op with no slot ids or no metadata of its inputs has no
                // pair of the two, and try_zip gives an error.
                for (auto const& pairs : te.input_slot_ids().walk().try_zip(te.input_metas().walk()).ok()) {
                    for (auto [slot_id, meta] : pairs) {
                        if (*slot_id == target) {
                            data = storage_address(*meta);
                            break;
                        }
                    }
                }
            }

            for (const TensorData found : data)
                scope.register_external(target, found).expect("the plan of the active region marks the slot external");
        }
    }

    // Writes bound_scalars_: each data slot of an op of the region, in the
    // order of the ops and of the slots, with the value of the recording.  Two
    // passes, one to count and one to fill, so the list takes one allocation.
    // O(the ops of the region).
    [[gnu::cold]] void list_bound_scalars_(const RegionNode& region, uint32_t resume_op) {
        const auto each_slot = [&region](auto&& on_slot) {
            for (uint32_t i = 0; i < region.num_ops(); i++) {
                const TraceEntry& te = region.op_at(i);
                const DataSlots data = te.scalar_kinds.data_slots();
                if (!data.has_data()) continue;
                uint32_t position = 0;
                for (const int64_t value : te.scalar_args()) {
                    for (const ScalarSlot slot : scalar_slot_at(position)) {
                        if (data.role(slot) == ScalarRole::DATA) on_slot(BoundScalar{OpIndex{i}, slot, value});
                    }
                    ++position;
                }
            }
        };
        uint32_t count = 0;
        each_slot([&count](const BoundScalar&) noexcept { ++count; });
        ::fixy::HeapArray<BoundScalar> listed = ::fixy::mint_heap_array(allocation_, count, BoundScalar{});
        uint32_t filled = 0;
        bound_scalars_.next = count;
        // The body captures the counter that it writes, and not the Vigil, so
        // it writes no borrow of its frame into the Vigil.
        listed = ::fixy::with_mut(std::move(listed), [&each_slot, &filled, &next_bound = bound_scalars_.next, count,
                                                      resume_op](auto scalars) noexcept {
            each_slot([&](const BoundScalar& bound) noexcept {
                auto slot = scalars.get_mut(filled).expect("the first pass counted each slot");
                *slot = bound;
                if (next_bound == count && bound.op.raw() >= resume_op) next_bound = filled;
                ++filled;
            });
        });
        bound_scalars_.slots = std::move(listed);
    }

    // Verifies the prefix match, then delegates the pool detach, the slot
    // migration and the engine advance.  Returns true when the switch
    // succeeded and the engine sits at div_pos.
    [[nodiscard, gnu::cold]] bool try_switch_region_(VigilFgCtx const& fg, const RegionNode& alt, uint32_t div_pos) {
        if (alt.plan.is_none()) return false;

        // Every op before the divergence point must carry an identical
        // schema and shape in both regions.
        if (div_pos > 0) {
            // A divergence position above zero comes from a compiled
            // context, which holds an active region.  expect() checks it.
            const RegionNode& old_region =
                *replay_.context().active_region().expect("a compiled context holds an active region");

            if (div_pos > alt.num_ops()) return false;

            for (uint32_t i = 0; i < div_pos; i++) {
                if (old_region.op_at(i).schema_hash != alt.op_at(i).schema_hash
                    || old_region.op_at(i).shape_hash != alt.op_at(i).shape_hash)
                    return false;
            }
        }

        if (!replay_.switch_region(alt, div_pos)) return false;
        register_inputs_from_region_(fg, alt, div_pos);
        // This holds only on the success path; every failure returns above.
        // Pinning it here catches a refactor that skips the publication or
        // publishes the wrong region, which would otherwise keep dispatching
        // against the stale one.
        CRUCIBLE_POST(0, ::fixy::same_target(replay_.context().active_region(), ::fixy::some(::fixy::to(alt))));
        return true;
    }

    // Declaration order below is destruction order reversed, and that is
    // load-bearing.  bg_ must stay last so it is destroyed first and joins
    // the background thread before anything that thread touches goes away.

    Config cfg_;
    // The ring and the metadata log, each in a held split with its two ends
    // (fixy/concurrent/SpscEnds.h).  The record path opens the two producer
    // sides with the proof of the producer claim.  The background thread
    // holds a Ref to each consumer side from its construction, and it opens
    // the side of the ring with the proof of its drain stage and the side of
    // the log with the proof of its publish stage.  bg_ is the last member,
    // so it joins the pipeline thread before the two splits end.
    ::fixy::concurrent::SpscHeldSplit<TraceRing, VigilRingSides> ring_;
    ::fixy::concurrent::SpscHeldSplit<MetaLog, VigilLogSides> meta_log_;
    // Owned by the publish stage: each member takes that stage's proof, and
    // the foreground reaches the log only through run_on_publish_stage.
    TransactionLog<16, BackgroundThread::PublishStage> tx_log_;
    // The store, when the configuration names a path.  A Box holds it, so
    // this header only declares the class.  The member has no initializer
    // here: an initializer makes each unit that includes this header define
    // the constructor of the Option, which can end the Box.  The constructor
    // in src/Vigil.cpp makes the empty Option.
    ::fixy::Option<::fixy::Box<Cipher>> cipher_;
    // Every dispatch and every record must come from one and the same
    // thread.  Another thread entering breaks the ring's single-producer
    // protocol and can corrupt the head-to-tail relationship.  The claim
    // holds the first dispatching thread, and every later dispatch checks
    // the match in every build mode, release included.  It is also the one
    // production route to a foreground context.
    ::foundation::effects::host::ProducerClaim<Vigil> producer_claim_;
    ::fixy::AtomicMonotonic<uint64_t> step_ = ::fixy::mint_atomic_monotonic<uint64_t>(0);

    // The arena of one load, and the region that the load put in it.  The
    // arena holds each byte of the region.
    struct LoadedRegion {
        ::fixy::Box<::fixy::BumpArena<DagArena>> arena;
        ConstRegionLink region{};
    };
    // The arena of a load, and the position of its head region in it: the
    // owned data that the frame of the store gives out (load).
    using LoadArena = ::fixy::Box<::fixy::BumpArena<DagArena>>;
    using HeadPosition = ::fixy::ArenaPosition<RegionNode, DagArena>;
    // The bytes of the first block of the arena of a load.  A larger region
    // takes a block of its own size.
    static constexpr ::fixy::Quantity<::fixy::unit::Byte, size_t> LOAD_BLOCK_BYTES{size_t{1} << 16};
    // The regions that the foreground holds.  The holders name at most
    // Holders::CAP regions, so a load always finds a free slot.
    using Holders = HeldRegions<Vigil>;
    static constexpr uint32_t LOAD_SLOTS = Holders::CAP + 1;
    // The arenas of the loads whose region a holder of the foreground named
    // at the last load.  Only the producer thread reads or writes them.  The
    // member stands before the holders of the regions, so it ends after them.
    ::fixy::FixedArray<::fixy::Option<LoadedRegion>, LOAD_SLOTS> loads_{};

    // The publication slot: the region that the publish stage gave to the
    // foreground and that the foreground did not take yet.  The store is
    // release on the background side and the load is acquire on the
    // foreground side, so the region data written before the store is
    // visible after the load.  It is a reusable latest-wins slot rather than
    // a publish-once cell because divergence recovery publishes several
    // regions over one process lifetime.  The slot holds the record of the
    // region in the region store, which carries its reset point.  It has its
    // own cache line, because the publish stage writes it while the
    // foreground reads its neighbours.
    using PendingRegion = ::fixy::Option<::fixy::Ref<const PublishedRegion>>;
    alignas(64)::fixy::Atomic<PendingRegion> pending_region_{};
    // The region that the producer thread took last (active_region).  Only
    // the producer thread writes it, and a reader on another thread loads it.
    ::fixy::Atomic<ConstRegionLink> taken_region_{};
    // The five below are foreground-only.
    ConstRegionLink pending_activation_{};
    AlignmentPos alignment_pos_ = alignment_pos_at(0);
    // The ops that the alignment of pending_activation_ read (alignment_bound).
    uint32_t alignment_ops_ = 0;
    // The eager word of the front end that the regions record under.  The
    // constructor sets the word of the Config, and only the producer thread
    // changes it (observe_eager_numerics).
    EagerNumerics eager_{};

    // The replay context and the mode that an observer reads.  The producer
    // thread changes the context only through this class, and each change
    // of the context publishes the matching mode in the same call.  So the
    // mode and the context never disagree after a call.  A region with no
    // memory plan cannot replay, so its activation leaves the context
    // recording.
    //
    // The mode cell has its own cache line: an observer on another thread
    // reads it while the foreground writes the counters of the context.
    //
    // A region replays only under the numerics that its content hash folds
    // (RegionNode::numerics).  The kernels of a region come from its
    // numerics, so a region of other numerics computes other bits.  The two
    // doors of a replay, the activation and the switch, refuse such a region.
    // The check runs once for each activation or switch, never for each op.
    //
    // The key of the context (the key of a context of crucible/CrucibleContext.h)
    // is a private member beside the context, so a change of the mode costs no
    // instruction to reach it.  A key of the brand opens each context, and it
    // does not prove which one.  So the Vigil reaches a door that takes the key
    // only through this class, with its own key and its own context.
    class ReplayState {
    public:
        // The context allocates the pool of each region that it activates,
        // through the allocation capability of the startup context.  The
        // numerics are the numerics of the run (compute_numerics_hash).
        ReplayState(::foundation::effects::Alloc allocation, NumericsHash numerics) noexcept
            : ctx_{allocation}, numerics_{numerics} {}

        [[nodiscard]] const CrucibleContext& context() const noexcept { return ctx_; }
        [[nodiscard]] Mode mode() const noexcept { return mode_.load(); }

        // Replays region from its first op.  The context records when the
        // region has no memory plan, or when its numerics are not the
        // numerics of the run.  Returns true when the context replays.
        [[nodiscard]] bool activate(const RegionNode& region) {
            if (!(region.numerics == numerics_) || !ctx_.activate(key_, region)) {
                deactivate();
                return false;
            }
            mode_.publish_compiled(mode_writer_);
            return true;
        }

        // Records from the next op.
        void deactivate() {
            if (!ctx_.is_compiled()) return;
            ctx_.deactivate(key_);
            mode_.publish_recording_after_divergence(mode_writer_);
        }

        // Moves the replay to alt at div_pos.  The context stays compiled,
        // so the mode does not change.  A region of other numerics gives
        // false, and the context stays on its region.
        [[nodiscard]] bool switch_region(const RegionNode& alt, uint32_t div_pos) {
            if (!(alt.numerics == numerics_)) return false;
            return ctx_.switch_region(key_, alt, div_pos);
        }

        // The numerics of the run become numerics.  A region that replays
        // under the old numerics stops, the context counts a divergence, and
        // it records from the next op.  Returns true when the context
        // replayed.
        bool change_numerics(NumericsHash numerics) {
            numerics_ = numerics;
            if (!ctx_.is_compiled()) return false;
            ctx_.deactivate_on_divergence(key_);
            mode_.publish_recording_after_divergence(mode_writer_);
            return true;
        }

        // Lends the compiled scope of the context to body while the context
        // replays, with the key of the context, and gives no value while it
        // records (CrucibleContext::with_compiled).  The gate of the door
        // refuses a body that reaches the key, so a body that names this class
        // or the Vigil does not compile.
        template <class Body>
            requires CompiledBody<Body>
        [[nodiscard]] CRUCIBLE_INLINE CompiledAnswer<Body> with_compiled(Body&& body) noexcept {
            return ctx_.with_compiled(key_, static_cast<Body&&>(body));
        }

        // The addresses of the slots of the matched op for the vessel, with the
        // key of the context (CrucibleContext::read_output_address).
        [[nodiscard]] CrucibleContext::OutsideAddress read_output_address(OutputPosition position) noexcept {
            return ctx_.read_output_address(key_, position);
        }
        [[nodiscard]] CrucibleContext::OutsideAddress read_input_address(InputPosition position) noexcept {
            return ctx_.read_input_address(key_, position);
        }

    private:
        CrucibleContext ctx_;
        // The one key of ctx_ (the key of a context).  The key is empty, so it
        // takes no byte.
        [[no_unique_address]] ContextKey key_{
            ::foundation::core::mint_held_key<ContextKeyBrand>(ContextKeyBrand::key_proof{})};
        NumericsHash numerics_{};
        // The one writer of the mode cell.  The producer thread holds it, so
        // only the producer thread publishes a mode.  It stands before the
        // cell, which keeps its id.
        ModeWriter mode_writer_ = ::fixy::session::vigil_mode::mint_mode_writer();
        alignas(64) ModeCell mode_{mode_writer_};
    };
    ReplayState replay_;
    RegionCache region_cache_;

    // The stack of op_record_stack.  Only the producer thread reads or
    // writes it, and each slot holds a TensorMeta from the construction of
    // the Vigil on.
    OpRecordStack op_record_stack_{};

    // The data slots of the active region and the cursor of the iteration
    // (BoundScalars).
    BoundScalars bound_scalars_{};

    // The regions that the foreground holds, for the sweep of the region
    // store on the publish stage (crucible/HeldRegions.h).  The foreground
    // writes the cells at the end of each cold path that can change one of its
    // holders, and never on the replay path.
    Holders held_regions_;

    // The watchdog owns its telemetry source, the Senses, so the two end
    // together.  wd_ must precede bg_.  The background thread's region-ready
    // callback observes through wd_.  Destroying bg_ first joins that thread,
    // after which no observation can start, and only then does the
    // subprogram unload.
    //
    // The counters have no lifetime constraint of their own, but sit here
    // for locality: the background thread writes them in the same frame it
    // drives the watchdog.
    //
    // The constructor makes the watchdog only when the configuration asks
    // for it, and the Option is empty otherwise.  The member has no
    // initializer, for the reason that cipher_ gives.
    ::fixy::Option<::fixy::Box<::crucible::warden::DeadlineWatchdog>> wd_;
    ::fixy::Atomic<::crucible::warden::WatchdogVerdict> wd_last_verdict_{
        ::crucible::warden::WatchdogVerdict::InsufficientData};
    ::fixy::Atomic<uint32_t> wd_healthy_count_{};
    ::fixy::Atomic<uint32_t> wd_downgrade_count_{};
    ::fixy::Atomic<uint32_t> wd_insufficient_count_{};

    // The allocation capability of the startup context.  A registration of a
    // schema name on the producer thread allocates through it, so the
    // foreground context itself holds no atom.  The tag is empty, so it adds
    // no storage.  It has no initializer, because only the constructor of
    // the Vigil holds the startup context, and it sets the tag.
    [[no_unique_address]] ::foundation::effects::Alloc allocation_;

    // The region callback of bg_, on_region_ready of this Vigil, and its marks
    // call, read_region_marks_.  bg_ borrows the two, so they must precede
    // bg_.  Destroying bg_ first joins the pipeline thread, the one caller of
    // the two, and only then do these members and the rest of the Vigil end.
    // Each CallRef holds the address of this Vigil, so a Vigil has no copy and
    // no move.
    BackgroundThread::RegionReadyCall const region_ready_;
    BackgroundThread::RegionMarksCall const region_marks_;

    // The thread of the pipeline, which the constructor starts after each other
    // member is complete.
    StartedBackgroundThread bg_;  // Must stay last.
};

// The nest of the record of one op that an adapter gives to a Vigil.  It
// opens an OpNest on the record stack of the Vigil, and the adapter gives the
// op its depth.  At the close, when the op did not reach the Vigil after an
// op inside the nest did, the nest gives the closing entry at its depth
// (Vigil::give_unrecorded_nest).  The nest lives in the call of its op, so an
// unwind of a raise in that call closes it too.  An adapter opens it before
// the frame of the op on the record stack, so the frame closes first.  O(1) for
// each open and each close that gives no entry.
class [[nodiscard]] VigilOpNest {
public:
    VigilOpNest(Vigil& vigil, VigilFgCtx const& fg) noexcept
        : vigil_{::fixy::erase_brand(::fixy::to(vigil))}, nest_{vigil.op_record_stack(fg)} {}

    ~VigilOpNest() {
        if (nest_.is_unrecorded()) [[unlikely]]
            vigil_->give_unrecorded_nest(nest_.depth());
    }

    VigilOpNest(const VigilOpNest&) = delete("a nest closes at the end of the scope of its op");
    VigilOpNest& operator=(const VigilOpNest&) = delete("a nest closes at the end of the scope of its op");
    VigilOpNest(VigilOpNest&&) = delete("a nest closes at the end of the scope of its op");
    VigilOpNest& operator=(VigilOpNest&&) = delete("a nest closes at the end of the scope of its op");

    // The depth of the op of this nest.
    [[nodiscard]] constexpr OpDepth depth() const noexcept { return nest_.depth(); }

private:
    ::fixy::Ref<Vigil> vigil_;
    OpNest nest_;
};

}  // namespace crucible
