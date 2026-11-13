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
#include <crucible/IterationDetector.h>
#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/RegionCache.h>
#include <crucible/TraceRing.h>
#include <crucible/Transaction.h>
#include <crucible/cipher/SessionPersistenceSurface.h>
#include <crucible/warden/Policy.h>
#include <crucible/warden/WatchdogVerdict.h>
#include <fixy/Borrowed.h>
#include <fixy/Checked.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/FixedArray.h>
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

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>  // std::move, std::forward

// The telemetry and the watchdog are held through pointers, so this header
// only declares them.  src/Vigil.cpp includes their headers.
namespace crucible::perf {
class Senses;
}  // namespace crucible::perf

namespace crucible::warden {
class DeadlineWatchdog;
}  // namespace crucible::warden

namespace crucible {

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

    struct Config {
        int32_t rank = -1;
        int32_t world_size = 0;
        uint64_t device_capability = 0;
        std::string cipher_path;  // empty = no persistence

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
    // asks for it, and starts the background thread.  The body is in
    // src/Vigil.cpp, so a translation unit that makes a Vigil compiles no
    // part of the background pipeline.
    //
    // The transaction log reads the monotonic clock, and the program load
    // of the watchdog waits in the kernel for the verifier, so both take the
    // startup load context.
    Vigil(Config cfg, ::fixy::InitLoadCtx const& startup);

public:
    // src/Vigil.cpp defines it.  The destruction order of the members is
    // the reverse of their declaration order, and bg_ joins the background
    // thread first.
    ~Vigil();

    Vigil(const Vigil&) = delete("Vigil owns the runtime organism; not copyable");
    Vigil& operator=(const Vigil&) = delete("Vigil owns the runtime organism; not copyable");
    Vigil(Vigil&&) = delete("interior pointers from CrucibleContext and ring would dangle");
    Vigil& operator=(Vigil&&) = delete("interior pointers from CrucibleContext and ring would dangle");

    // Appends the tensor metadata, then pushes an op fingerprint to the
    // ring.  Returns false when either is full, in which case the op is
    // dropped and the next iteration re-records everything.
    //
    // The records are the inputs and then the outputs of the entry, so
    // n_metas must be their sum.  The build reads that many records for the
    // op, and a count that disagrees makes it read the records of another
    // op.  The check ends the process before the log or the ring changes.
    //
    // This is a producer entry point in its own right, not a helper of
    // dispatch_op, so it carries the same thread gate.  Without one, a
    // caller that reaches the ring through here from a second thread gets
    // the torn append dispatch_op refuses, and the two entry points state
    // different ownership rules for one ring.
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool record_op(TraceRing::ValidatedEntryPtr ve, const TensorMeta* metas,
                                                           uint32_t n_metas, ScopeHash scope_hash = {},
                                                           CallsiteHash callsite_hash = {}) {
        CRUCIBLE_PRE(ve.value() != nullptr);
        CRUCIBLE_PRE(n_metas == uint32_t{ve.value()->num_inputs} + ve.value()->num_outputs);
        (void)assert_producer_thread_();
        MetaIndex meta_start;  // default = none()
        if (metas && n_metas > 0) {
            meta_start = meta_log_->try_append(metas, n_metas);
        }
        return ring_->try_append(*ve.value(), meta_start, scope_hash, callsite_hash);
    }

    // Called once per op by the adapter.  A RECORD action means the caller
    // executes eagerly and the op has been recorded; a COMPILED action means
    // the outputs are already allocated and reachable through output_ptr and
    // input_ptr.
    //
    // Only the hot paths live inline here.  Divergence recovery, consuming a
    // pending region and alignment all sit in noinline helpers, so the hot
    // path needs no callee-saved registers for them.
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE DispatchResult dispatch_op(TraceRing::ValidatedEntryPtr ve,
                                                                                     const TensorMeta* metas,
                                                                                     uint32_t n_metas,
                                                                                     ScopeHash scope_hash = {},
                                                                                     CallsiteHash callsite_hash = {}) {
        CRUCIBLE_PRE(ve.value() != nullptr);
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
        // The gate returns the context of this Vigil's claim, and every view
        // of the replay chain below is minted from it.
        const VigilFgCtx fg = assert_producer_thread_();
        const TraceRing::Entry& entry = *ve.value();

        // The branch itself proves the context is compiled.  Minting the
        // view once here is what makes the engine transition below reachable
        // only from inside this branch.
        if (replay_.context().is_compiled()) [[likely]] {
            auto compiled_view = replay_.context().mint_compiled_view(fg);
            auto status = replay_.advance(entry.schema_hash, entry.shape_hash, compiled_view);
            if (status == ReplayStatus::DIVERGED) [[unlikely]]
                return handle_divergence_(fg, entry.schema_hash, entry.shape_hash);
            return {.action = DispatchResult::Action::COMPILED, .status = status, .pad = {}, .op_index = OpIndex{}};
        }

        // The acquire load pairs with the release store on the background
        // thread, so every byte of the region it wrote before that store is
        // visible here.  A relaxed load could miss the store for one op and
        // record it instead of aligning, which costs an extra op before
        // alignment completes.
        if (pending_region_.load_acquire().is_some() || pending_activation_) [[unlikely]]
            return dispatch_transition_(fg, ve, metas, n_metas, scope_hash, callsite_hash);

        (void)record_op(ve, metas, n_metas, scope_hash, callsite_hash);
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
    dispatch_op_pure(VigilFgCtx const&, TraceRing::ValidatedEntryPtr ve, const TensorMeta* metas, uint32_t n_metas,
                     ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {}) {
        CRUCIBLE_PRE(ve.value() != nullptr);
        return dispatch_op(ve, metas, n_metas, scope_hash, callsite_hash);
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

    // The context of the thread that holds this Vigil's producer claim.  The
    // first call claims the calling thread, and a call from any other thread
    // then ends the process, as dispatch_op does.  The output, input and
    // external-slot surfaces below ask for it.  Not constexpr: the claim
    // reads the thread id and an atomic.
    [[nodiscard]] CRUCIBLE_INLINE VigilFgCtx mint_producer_context() noexcept { return assert_producer_thread_(); }

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
    // then release the region.
    [[nodiscard]] const RegionNode* active_region() const noexcept { return newest_region_(); }

    // replay needs a writable view, because the caller's execution lambda
    // mutates the region as it writes outputs.  The const accessor above
    // serves introspection, and its rule for a read of the region applies
    // here too.
    [[nodiscard]] RegionNode* active_region_mut() noexcept { return newest_region_(); }

    // Advanced only by the background thread, once per region transition.
    [[nodiscard]] uint64_t current_step() const noexcept { return step_.get(); }

    // The head of the store, or the none hash when no store path was
    // configured.  The body is in src/Vigil.cpp, which includes the store.
    [[nodiscard]] ContentHash head_hash() const noexcept;

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
    void flush() {
        const uint64_t target_produced = ring_->total_produced();
        while (bg_.total_processed.load_acquire() < target_produced) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    [[nodiscard]] bool flush_complete() const { return bg_.total_processed.load_acquire() >= ring_->total_produced(); }

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
    // is in src/Vigil.cpp.
    [[nodiscard, gnu::cold]] bool rollback();

    // Traverses the compiled DAG without the adapter.  eval_guard takes a
    // guard and returns the currently observed value; exec_region executes
    // one region and reports success.  Returns true when replay finished
    // with no guard mismatch.
    template <typename GuardEval, typename RegionExec>
    [[nodiscard]] bool replay(GuardEval&& eval_guard, RegionExec&& exec_region) {
        RegionNode* region = active_region_mut();
        if (!region) return false;
        return crucible::replay(region, std::forward<GuardEval>(eval_guard), std::forward<RegionExec>(exec_region));
    }

    // Serializes the active region to the store and advances its head.
    // A no-op when no store path was configured.  The store writes and
    // flushes files, and the head commit reads the monotonic clock, so the
    // caller's context must fit a Cipher commit.  False when the store or
    // the clock read of the commit failed.
    //
    // This header only declares the store.  A caller of persist or load
    // includes crucible/Cipher.h, as each caller of a member of the store
    // does (crucible/cipher/SessionPersistenceSurface.h).
    template <class Ctx>
        requires ::crucible::cipher::CtxFitsCipherCommit<Ctx>
    [[nodiscard, gnu::cold]] bool persist(Ctx const& ctx) {
        if (!cipher_) return false;
        const RegionNode* region = active_region();
        if (!region) return false;
        return persist_into_(ctx, *cipher_, region);
    }

    // Loads the most recent stored region and makes it active, activating
    // replay too when that region carries a memory plan.  A region with no
    // plan leaves the context recording, also when it replayed a region
    // before the load.  The activation is foreground state, so this runs on
    // the producer thread.  The row is the row that an open view of the
    // store requires.
    template <class Ctx>
        requires ::foundation::effects::CtxAdmits<Ctx, ::crucible::CipherSessionEventPersistenceRow>
    [[nodiscard, gnu::cold]] bool load(Ctx const& ctx, ::foundation::effects::Alloc a) {
        if (!cipher_) return false;
        const VigilFgCtx fg = assert_producer_thread_();
        RegionNode* region = load_head_(ctx, a, *cipher_);
        if (!region) return false;
        // Each region cut before the load holds ops that ran before it.
        bg_.request_mark(ring_total_produced());
        adopt_region_(fg, *region);
        return true;
    }

    // Pre-allocated pointer for output j of the current op.  Valid only
    // after dispatch_op returned COMPILED with a matching or complete
    // status, which is what the minted view's precondition checks.  The
    // context proves that the caller holds this Vigil's producer claim.
    //
    // Each pointer and each reference that the members that follow return
    // points into a member of this Vigil.  A temporary Vigil releases its
    // members at the end of the statement.  A deleted twin refuses each call
    // on a temporary Vigil.
    [[nodiscard]] void* output_ptr(VigilFgCtx const& fg, uint16_t j) const& {
        auto compiled_view = replay_.context().mint_compiled_view(fg);
        return replay_.context().output_ptr(j, compiled_view);
    }

    void* output_ptr(VigilFgCtx const&, uint16_t) const&& =
        delete("a temporary Vigil releases its pool at the end of the statement. Use a Vigil that has a name");

    [[nodiscard]] void* input_ptr(VigilFgCtx const& fg, uint16_t j) const& {
        auto compiled_view = replay_.context().mint_compiled_view(fg);
        return replay_.context().input_ptr(j, compiled_view);
    }

    void* input_ptr(VigilFgCtx const&, uint16_t) const&& =
        delete("a temporary Vigil releases its pool at the end of the statement. Use a Vigil that has a name");

    void register_external(VigilFgCtx const& fg, SlotId sid, ::fixy::NonNull<void*> ptr) {
        auto compiled_view = replay_.context().mint_compiled_view(fg);
        replay_.register_external(sid, ptr, compiled_view);
    }

    [[nodiscard]] uint32_t compiled_iterations() const { return replay_.context().compiled_iterations(); }
    [[nodiscard]] uint32_t diverged_count() const { return replay_.context().diverged_count(); }
    [[nodiscard]] const CrucibleContext& context() const& { return replay_.context(); }
    const CrucibleContext& context() const&& =
        delete("a temporary Vigil releases its context at the end of the statement. Use a Vigil that has a name");
    [[nodiscard]] const RegionCache& region_cache() const& { return region_cache_; }
    const RegionCache& region_cache() const&& =
        delete("a temporary Vigil releases its cache at the end of the statement. Use a Vigil that has a name");

    // These two hand out the producer surface of the ring and of the
    // metadata log.  A caller that appends through one of them is the
    // producer, so each asks for the context of this Vigil's producer claim,
    // as record_op and dispatch_op do.  No op path calls them, so each also
    // checks the calling thread against the claim at run time.
    [[nodiscard]] TraceRing& ring(VigilFgCtx const&) & {
        CRUCIBLE_FATAL_INVARIANT(producer_claim_.is_claimable_by_caller());
        return *ring_;
    }
    TraceRing& ring(VigilFgCtx const&) && =
        delete("a temporary Vigil releases its ring at the end of the statement. Use a Vigil that has a name");
    [[nodiscard]] MetaLog& meta_log(VigilFgCtx const&) & {
        CRUCIBLE_FATAL_INVARIANT(producer_claim_.is_claimable_by_caller());
        return *meta_log_;
    }
    MetaLog& meta_log(VigilFgCtx const&) && =
        delete("a temporary Vigil releases its log at the end of the statement. Use a Vigil that has a name");

    // Counters that any thread can read with no context.  Each value is a
    // snapshot, and it can be stale by the time the caller reads it.
    [[nodiscard]] ::fixy::Stale<uint32_t> ring_size() const noexcept { return ring_->size(); }
    [[nodiscard]] uint64_t ring_total_produced() const noexcept { return ring_->total_produced(); }
    [[nodiscard]] ::fixy::Stale<uint32_t> meta_log_size() const noexcept { return meta_log_->size(); }

    [[nodiscard]] uint32_t bg_iterations_completed() const { return bg_.iterations_completed.get(); }
    [[nodiscard]] uint32_t bg_detector_boundaries() const { return bg_.detector.boundaries_detected.get(); }
    [[nodiscard]] bool bg_detector_confirmed() const { return bg_.detector.confirmed; }

    // The count of regions in the region store of the pipeline, and the
    // count of published regions that a full store discarded.  Each call
    // runs on the publish stage, between two publications.
    [[nodiscard]] uint32_t region_store_count() {
        uint32_t count = 0;
        bg_.run_on_publish_stage(
            [&count, this](BackgroundThread::PublishStage const& stage) noexcept { count = bg_.region_count(stage); });
        return count;
    }
    [[nodiscard]] uint64_t region_store_drops() {
        uint64_t drops = 0;
        bg_.run_on_publish_stage([&drops, this](BackgroundThread::PublishStage const& stage) noexcept {
            drops = bg_.regions_dropped_full(stage);
        });
        return drops;
    }

    // True only when the configuration asked for the watchdog and the
    // tracing subprogram actually attached.
    //
    // True does not imply a usable verdict yet.  The first window always
    // reports InsufficientData while the baseline is captured.
    [[nodiscard]] bool watchdog_enabled() const noexcept { return wd_ != nullptr; }

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
    // The first call claims the thread; every later call verifies the match.
    // A call from a second thread ends the process, in every build mode.
    // This is the one check between a second producer and a ring that tears
    // without a diagnostic: both threads claim the same slot and the later
    // write erases the earlier one.
    [[nodiscard]] CRUCIBLE_INLINE VigilFgCtx assert_producer_thread_() noexcept {
        return producer_claim_.mint_producer_context();
    }

    // The body of active_region.  The producer thread is the one writer of
    // the requested reset, so on that thread the comparison cannot race it.
    [[nodiscard]] RegionNode* newest_region_() const noexcept {
        const PendingRegion pending = pending_region_.load_acquire();
        for (const auto& published : pending) {
            if (published->reset_point == bg_.requested_reset()) return published->region.try_get();
        }
        return taken_region_.load_acquire().try_get();
    }

    // The store parts of persist and load.  The store is a template
    // parameter, so each member of the store resolves where a caller
    // instantiates persist or load, and this header needs only the
    // declaration of the store.
    //
    // The store is only ever made from open(), so holding one already
    // proves it is open.  One mint of the open view serves both calls.
    template <class Ctx, class Store>
    [[nodiscard]] bool persist_into_(Ctx const& ctx, Store& store, const RegionNode* region) {
        auto open_view = store.mint_open_view(ctx);
        const ContentHash hash = store.store(open_view, Store::content_addressed(region));
        if (!hash) return false;
        return store.record_event(ctx, open_view, hash, step_.get()).is_ok();
    }

    template <class Ctx, class Store>
    [[nodiscard]] RegionNode* load_head_(Ctx const& ctx, ::foundation::effects::Alloc a, Store& store) {
        if (store.empty()) return nullptr;
        auto open_view = store.mint_open_view(ctx);
        return store.load_content_addressed(open_view, a, store.head(), load_arena_).get();
    }

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
                                       BackgroundThread::PublishedRegion const& published);

    // Makes a region from outside the pipeline active: the region of a
    // rollback or of a load.  The caller first requests a mark at the
    // current point, so each region that the pipeline cut before it is
    // stale.  The region in the slot goes to the cache, the alignment walk
    // closes, and the context replays the region, or records when the region
    // has no memory plan.  The body is in src/Vigil.cpp.
    [[gnu::cold]] void adopt_region_(VigilFgCtx const& fg, RegionNode& region);

    // Writes the regions that the foreground holds into held_regions_: the
    // region of the alignment walk, the region of the replay context, the
    // region that the producer took last and the regions of the cache.
    // After the call, the foreground reads no other region than these and
    // the last published region.  Runs on the producer thread at the end of
    // each cold path that can change one of these holders.
    [[gnu::cold]] void publish_held_regions_() noexcept {
        const uint64_t before = held_regions_.sequence.load_acquire();
        held_regions_.sequence.store_release(before + 1);
        held_regions_.cells[0].store_release(pending_activation_);
        held_regions_.cells[1].store_release(
            ::fixy::WeakRef<const RegionNode>::from_raw(replay_.context().active_region()));
        held_regions_.cells[2].store_release(
            ::fixy::WeakRef<const RegionNode>::from_raw(taken_region_.load_acquire().try_get()));
        for (uint32_t age = 0; age < RegionCache::CAP; ++age) {
            held_regions_.cells[3 + age].store_release(region_cache_.region_at(age));
        }
        held_regions_.sequence.store_release(before + 2);
    }

    // Adds the regions of held_regions_ to marks, with the generation of the
    // write that holds them.  Runs on the publish stage, and waits while the
    // foreground writes the cells.
    void mark_held_regions_(BackgroundThread::RegionMarks& marks) const noexcept {
        while (true) {
            const uint64_t before = held_regions_.sequence.load_acquire();
            if ((before & 1u) == 0) {
                BackgroundThread::RegionMarks read = marks;
                for (uint32_t index = 0; index < HeldRegions::CAP; ++index) {
                    read.hold(held_regions_.cells[index].load_acquire());
                }
                if (held_regions_.sequence.load_acquire() == before) {
                    read.has_foreground = true;
                    read.foreground_generation = before / 2;
                    marks = read;
                    return;
                }
            }
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // The cold dispatch paths below are noinline so the hot path needs no
    // callee-saved registers for them.

    // Looks the diverging shape up in the region cache, tries to switch to a
    // matching region, and falls back to recording.
    //
    // Takes the two guard values and nothing else. It used to take the whole
    // Entry plus the metadata, the scope hash and the callsite hash, and the
    // last four were all `[[maybe_unused]]`: the divergent operation is
    // deliberately not recorded, as the tail of this function says, so no
    // metadata ever reaches a reader from here.
    [[nodiscard, gnu::cold]] CRUCIBLE_NOINLINE DispatchResult handle_divergence_(VigilFgCtx const& fg,
                                                                                 SchemaHash schema_hash,
                                                                                 ShapeHash shape_hash) {
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
        pending_activation_.reset();
        alignment_pos_ = alignment_pos_at(0);
        alignment_ops_ = 0;

        // The context is compiled, so it replays a region.
        const auto diverged = ::fixy::WeakRef<const RegionNode>::from_raw(replay_.context().active_region());
        region_cache_.insert(diverged.get());

        // Look for a cached region that matches at the divergence position,
        // excluding the current one.
        const ::fixy::WeakRef<const RegionNode> alt =
            region_cache_.find_alternate(div_pos, schema_hash, shape_hash, diverged);

        if (alt.has_value() && try_switch_region_(fg, alt.get(), div_pos)) {
            // The switch leaves the context compiled, so advance past the
            // divergent op.
            auto compiled_view = replay_.context().mint_compiled_view(fg);
            auto status = replay_.advance(schema_hash, shape_hash, compiled_view);
            if (status != ReplayStatus::DIVERGED) {
                publish_held_regions_();
                return {.action = DispatchResult::Action::COMPILED,
                        .status = status,
                        .pad = {},
                        .op_index = OpIndex{replay_.context().engine().ops_matched()}};
            }
            // Diverging twice in a row falls through to the reset below.
        }

        replay_.deactivate();
        // The reset applies at the point of the divergence: each op in the
        // ring was recorded before it, and the next op comes after it.
        bg_.request_restart(ring_total_produced());
        publish_held_regions_();
        // The divergent op is deliberately not recorded: it would poison the
        // background thread's iteration detector.
        return {.action = DispatchResult::Action::RECORD,
                .status = ReplayStatus::DIVERGED,
                .pad = {},
                .op_index = OpIndex{}};
    }

    // Takes the certified pointer rather than the Entry behind it. The two
    // spellings differ where it matters: an Entry reference has to be
    // certified again before record_op will take it, and certifying one that
    // is already certified means minting the second tag with no check behind
    // it. Carrying the pointer keeps the caller's certification.
    [[nodiscard, gnu::cold]] CRUCIBLE_NOINLINE DispatchResult
    dispatch_transition_(VigilFgCtx const& fg, TraceRing::ValidatedEntryPtr ve, const TensorMeta* metas,
                         uint32_t n_metas, ScopeHash scope_hash, CallsiteHash callsite_hash) {
        CRUCIBLE_PRE(ve.value() != nullptr);
        const TraceRing::Entry& entry = *ve.value();
        // A newer region arriving mid-alignment replaces the pending one.  A
        // region with other ops restarts the alignment from zero, because
        // the old partial match means nothing for it.  A region with the same
        // ops keeps the walk (take_published_region_).
        if (pending_region_.load_acquire().is_some()) consume_pending_region_();

        if (pending_activation_) {
            // Nothing is recorded during alignment, because it would create
            // false iteration boundaries in the background detector.  The
            // bound of try_align_ ends an alignment that finds no match.
            try_align_(fg, entry.schema_hash, entry.shape_hash);
        } else {
            (void)record_op(ve, metas, n_metas, scope_hash, callsite_hash);
        }
        publish_held_regions_();

        return {
            .action = DispatchResult::Action::RECORD, .status = ReplayStatus::MATCH, .pad = {}, .op_index = OpIndex{}};
    }

    // Moves the region in the slot to the cache, for a caller that knows that
    // each region the slot can hold is stale.
    [[gnu::cold]] void cache_pending_region_() {
        const PendingRegion stale = pending_region_.exchange_acq_rel(::fixy::none);
        for (const auto& published : stale)
            region_cache_.insert(*published->region);
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

    [[gnu::cold]] void take_published_region_(BackgroundThread::PublishedRegion const& published) {
        if (published.reset_point != bg_.requested_reset()) {
            region_cache_.insert(*published.region);
            return;
        }
        taken_region_.store_release(published.region);
        if (published.region->num_ops == 0) return;

        // A newer region that holds the ops of an open walk takes the walk
        // over, and the walk keeps its position.  The background publishes
        // a region at each iteration boundary of a backlog, and each one has
        // the same ops, so a restart at each one would lose the matched ops.
        const bool continues_walk = pending_activation_.has_value()
                                 && holds_walk_(*pending_activation_, *published.region, alignment_pos_.value());
        pending_activation_ = ::fixy::WeakRef<const RegionNode>{*published.region};
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
        if (walked.content_hash != newer.content_hash || walked.num_ops != newer.num_ops) return false;
        for (uint32_t i = 0; i < pos; ++i) {
            if (walked.ops[i].schema_hash != newer.ops[i].schema_hash
                || walked.ops[i].shape_hash != newer.ops[i].shape_hash)
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
        // tested pending_activation_.  get() checks it again in each build,
        // with one branch on this cold path.
        const RegionNode& region = pending_activation_.get();
        ++alignment_ops_;

        const uint8_t pos = alignment_pos_.value();
        if (schema == region.ops[pos].schema_hash && shape == region.ops[pos].shape_hash) {
            // The increment cannot exceed the bound: once the previous call
            // reached the threshold it cleared pending_activation_, so this
            // function is not called again past that point.
            alignment_pos_ = alignment_pos_at(static_cast<uint8_t>(pos + uint8_t{1}));
        } else {
            alignment_pos_ = alignment_pos_at(restart_after_mismatch_(region, pos, schema, shape));
        }

        // A region shorter than K is matched in full instead.
        const uint32_t threshold = (region.num_ops < ALIGNMENT_K) ? region.num_ops : ALIGNMENT_K;

        if (uint32_t{alignment_pos_.value()} >= threshold) {
            if (!replay_.activate(region)) {
                // No memory plan means the region cannot be compiled.
                pending_activation_.reset();
                return;
            }

            register_externals_from_region_(fg, region);
            region_cache_.insert(region);

            // The matched ops already executed eagerly, so the engine has
            // to skip them for the next op to check against the right entry.
            for (uint32_t i = 0; i < uint32_t{alignment_pos_.value()}; i++) {
                auto status = replay_.advance(region.ops[i].schema_hash, region.ops[i].shape_hash);
                // Debug-only: the hashes fed in here are the region's own,
                // and alignment already compared each one against the same
                // entry, so a mismatch means the engine's cursor is out of
                // step rather than that the data is bad. A release build
                // carries no check because the next advance diverges and the
                // recovery path already handles that.
                CRUCIBLE_DEBUG_ASSERT(status == ReplayStatus::MATCH || status == ReplayStatus::COMPLETE);
                (void)status;
            }

            pending_activation_.reset();
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
        if (alignment_ops_ >= alignment_bound(region.num_ops)) {
            region_cache_.insert(region);
            pending_activation_.reset();
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
                const auto& early = region.ops[i];
                const auto& late = region.ops[uint32_t{pos} - border + i];
                is_border = early.schema_hash == late.schema_hash && early.shape_hash == late.shape_hash;
            }
            if (is_border && schema == region.ops[border].schema_hash && shape == region.ops[border].shape_hash)
                return static_cast<uint8_t>(border + 1);
        }
        return 0;
    }

    // Walks the region's ops to recover each external slot's data pointer
    // from the recorded tensor metadata.  Runs once per activation.
    [[gnu::cold]] void register_externals_from_region_(VigilFgCtx const& fg, const RegionNode& region) {
        if (!region.plan) return;

        for (uint32_t slot_idx = 0; slot_idx < region.plan->num_slots; slot_idx++) {
            if (!region.plan->slots[slot_idx].is_external) continue;

            SlotId target = region.plan->slots[slot_idx].slot_id;
            void* ptr = nullptr;

            for (uint32_t i = 0; i < region.num_ops && !ptr; i++) {
                const auto& te = region.ops[i];
                if (!te.input_slot_ids) continue;
                for (uint16_t j = 0; j < te.num_inputs; j++) {
                    if (te.input_slot_ids[j] == target) {
                        ptr = raw_data_ptr(te.input_metas[j]);
                        break;
                    }
                }
            }

            if (ptr != nullptr) {
                // Every call site activates the context immediately before
                // calling here, so it is compiled.
                auto compiled_view = replay_.context().mint_compiled_view(fg);
                replay_.register_external(target, ::fixy::mint_refined<::fixy::non_null>(ptr), compiled_view);
            }
        }
    }

    // Verifies the prefix match, then delegates the pool detach, the slot
    // migration and the engine advance.  Returns true when the switch
    // succeeded and the engine sits at div_pos.
    [[nodiscard, gnu::cold]] bool try_switch_region_(VigilFgCtx const& fg, const RegionNode& alt, uint32_t div_pos) {
        if (!alt.plan) return false;

        // Every op before the divergence point must carry an identical
        // schema and shape in both regions.
        if (div_pos > 0) {
            // A divergence position above zero comes from a compiled
            // context, which holds an active region.  get() checks it.
            const RegionNode& old_region =
                ::fixy::WeakRef<const RegionNode>::from_raw(replay_.context().active_region()).get();

            if (div_pos > alt.num_ops) return false;

            for (uint32_t i = 0; i < div_pos; i++) {
                if (old_region.ops[i].schema_hash != alt.ops[i].schema_hash
                    || old_region.ops[i].shape_hash != alt.ops[i].shape_hash)
                    return false;
            }
        }

        if (!replay_.switch_region(alt, div_pos)) return false;
        register_externals_from_region_(fg, alt);
        // This holds only on the success path; every failure returns above.
        // Pinning it here catches a refactor that skips the publication or
        // publishes the wrong region, which would otherwise keep dispatching
        // against the stale one.
        CRUCIBLE_POST(0, replay_.context().active_region() == &alt);
        return true;
    }

    // Declaration order below is destruction order reversed, and that is
    // load-bearing.  bg_ must stay last so it is destroyed first and joins
    // the background thread before anything that thread touches goes away.

    Config cfg_;
    std::unique_ptr<TraceRing> ring_;
    std::unique_ptr<MetaLog> meta_log_;
    // Owned by the publish stage: each member takes that stage's proof, and
    // the foreground reaches the log only through run_on_publish_stage.
    TransactionLog<16, BackgroundThread::PublishStage> tx_log_;
    // The store, when the configuration names a path.  It is held through
    // a pointer, so this header only declares the class.
    std::unique_ptr<Cipher> cipher_;
    // Every dispatch and every record must come from one and the same
    // thread.  Another thread entering breaks the ring's single-producer
    // protocol and can corrupt the head-to-tail relationship.  The claim
    // holds the first dispatching thread, and every later dispatch checks
    // the match in every build mode, release included.  It is also the one
    // production route to a foreground context.
    ::foundation::effects::host::ProducerClaim<Vigil> producer_claim_;
    ::fixy::AtomicMonotonic<uint64_t> step_ = ::fixy::mint_atomic_monotonic<uint64_t>(0);
    Arena load_arena_{1 << 20};

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
    using PendingRegion = ::fixy::Option<::fixy::Ref<const BackgroundThread::PublishedRegion>>;
    alignas(64)::fixy::Atomic<PendingRegion> pending_region_{};
    // The region that the producer thread took last (active_region).  Only
    // the producer thread writes it, and a reader on another thread loads it.
    ::fixy::Atomic<::fixy::WeakRef<RegionNode>> taken_region_{};
    // The five below are foreground-only.
    ::fixy::WeakRef<const RegionNode> pending_activation_{};
    AlignmentPos alignment_pos_ = alignment_pos_at(0);
    // The ops that the alignment of pending_activation_ read (alignment_bound).
    uint32_t alignment_ops_ = 0;

    // The replay context and the mode that an observer reads.  The producer
    // thread changes the context only through this class, and each change
    // of the context publishes the matching mode in the same call.  So the
    // mode and the context never disagree after a call.  A region with no
    // memory plan cannot replay, so its activation leaves the context
    // recording.
    //
    // The mode cell has its own cache line: an observer on another thread
    // reads it while the foreground writes the counters of the context.
    class ReplayState {
    public:
        [[nodiscard]] const CrucibleContext& context() const noexcept { return ctx_; }
        [[nodiscard]] Mode mode() const noexcept { return mode_.load(); }

        // Replays region from its first op, or records when the region has
        // no memory plan.  Returns true when the context replays.
        [[nodiscard]] bool activate(const RegionNode& region) {
            if (!ctx_.activate(&region)) {
                deactivate();
                return false;
            }
            mode_.publish_compiled();
            return true;
        }

        // Records from the next op.
        void deactivate() {
            if (!ctx_.is_compiled()) return;
            ctx_.deactivate();
            mode_.publish_recording_after_divergence();
        }

        // Moves the replay to alt at div_pos.  The context stays compiled,
        // so the mode does not change.
        [[nodiscard]] bool switch_region(const RegionNode& alt, uint32_t div_pos) {
            return ctx_.switch_region(alt, div_pos);
        }

        [[nodiscard, gnu::flatten]] CRUCIBLE_HOT ReplayStatus advance(SchemaHash schema_hash, ShapeHash shape_hash,
                                                                      CrucibleContext::CompiledView const& view) {
            return ctx_.advance(schema_hash, shape_hash, view);
        }

        [[nodiscard]] ReplayStatus advance(SchemaHash schema_hash, ShapeHash shape_hash) {
            return ctx_.advance(schema_hash, shape_hash);
        }

        void register_external(SlotId sid, ::fixy::NonNull<void*> ptr, CrucibleContext::CompiledView const& view) {
            ctx_.register_external(sid, ptr, view);
        }

    private:
        CrucibleContext ctx_;
        alignas(64) ModeCell mode_;
    };
    ReplayState replay_;
    RegionCache region_cache_;

    // The regions that the foreground holds, for the sweep of the region
    // store on the publish stage.  The foreground writes the cells at the end
    // of each cold path that can change one of its holders, and never on the
    // replay path.  The sequence is odd while the foreground writes, and each
    // write adds two.  The publish stage reads the cells between two equal
    // even values of the sequence.
    struct HeldRegions {
        static constexpr uint32_t CAP = 3 + RegionCache::CAP;
        ::fixy::Atomic<uint64_t> sequence{};
        ::fixy::FixedArray<::fixy::Atomic<::fixy::WeakRef<const RegionNode>>, CAP> cells{};
    };
    HeldRegions held_regions_;

    // senses_ must precede wd_, which holds a borrow of it, so that reverse
    // destruction order tears the borrower down first.
    //
    // Both must precede bg_.  The background thread's region-ready callback
    // observes through wd_, which reads senses_.  Destroying bg_ first joins
    // that thread, after which no observation can start, and only then does
    // the subprogram unload.
    //
    // The counters have no lifetime constraint of their own, but sit here
    // for locality: the background thread writes them in the same frame it
    // drives the watchdog.
    //
    // The constructor makes the two objects only when the configuration
    // asks for the watchdog, and each is null otherwise.
    std::unique_ptr<::crucible::perf::Senses> senses_;
    std::unique_ptr<::crucible::warden::DeadlineWatchdog> wd_;
    ::fixy::Atomic<::crucible::warden::WatchdogVerdict> wd_last_verdict_{
        ::crucible::warden::WatchdogVerdict::InsufficientData};
    ::fixy::Atomic<uint32_t> wd_healthy_count_{};
    ::fixy::Atomic<uint32_t> wd_downgrade_count_{};
    ::fixy::Atomic<uint32_t> wd_insufficient_count_{};

    BackgroundThread bg_;  // Must stay last.
};

}  // namespace crucible
