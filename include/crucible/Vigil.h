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
#include <crucible/Cipher.h>
#include <crucible/CrucibleContext.h>
#include <crucible/ForegroundCtx.h>
#include <crucible/IterationDetector.h>
#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/RegionCache.h>
#include <crucible/TraceRing.h>
#include <crucible/Transaction.h>
#include <crucible/perf/Senses.h>
#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>
#include <fixy/Aliases.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/ScopedView.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/handle/PublishOnce.h>
#include <fixy/session/VigilMode.h>
#include <foundation/contracts/Post.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>  // std::unreachable

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
    // The cell is private, and only the foreground publishes a mode, at the
    // same step that activates or deactivates the replay context.  A session
    // over the cell would drive those transitions, so Vigil hands out no
    // session handle.  An observer reads mode().
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

    // No persistence, no distributed context.
    [[gnu::cold]] Vigil() : Vigil(Config{}) {}

    // The constructor runs at process startup, so it opens the init door one
    // time and hands that one context to every startup step below.
    [[gnu::cold]] explicit Vigil(Config cfg)
        : Vigil(std::move(cfg), ::fixy::InitLoadCtx{::foundation::effects::host::InitOwner::mint_init_context()}) {}

private:
    // The transaction log reads the monotonic clock, and the program load
    // of the watchdog waits in the kernel for the verifier, so both take the
    // startup load context.
    [[gnu::cold]] Vigil(Config cfg, ::fixy::InitLoadCtx const& startup) : cfg_(std::move(cfg)), tx_log_{startup} {
        ring_ = std::make_unique<TraceRing>();
        ring_->reset();

        meta_log_ = std::make_unique<MetaLog>();
        meta_log_->reset();

        if (!cfg_.cipher_path.empty()) {
            // The configured path is operator-supplied, so it crosses the
            // trust boundary here and is declared external until the store's
            // own sanitizer promotes it.
            cipher_.emplace(Cipher::open(
                startup, ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{cfg_.cipher_path})));
        }

        bg_.set_region_ready_callback(
            this, [](void* self, ::foundation::effects::Bg const& bg, BackgroundThread::PublishStage stage,
                     RegionNode* region) noexcept { static_cast<Vigil*>(self)->on_region_ready(bg, stage, region); });

        // The watchdog is constructed before the background thread starts,
        // so on_region_ready can observe on the very first region transition
        // without a null check.  Attach failure is not an error here: every
        // observation then returns InsufficientData.
        if (cfg_.enable_deadline_watchdog) {
            senses_.emplace(
                ::crucible::perf::Senses::load_subset(startup, ::crucible::perf::SensesMask{.sched_switch = true}));
            wd_.emplace(::crucible::warden::mint_deadline_watchdog(startup, &*senses_, cfg_.watchdog_policy));
        }

        bg_.start(ring_.get(), meta_log_.get(), cfg_.rank, cfg_.world_size, cfg_.device_capability);
    }

public:
    ~Vigil() = default;

    Vigil(const Vigil&) = delete("Vigil owns the runtime organism; not copyable");
    Vigil& operator=(const Vigil&) = delete("Vigil owns the runtime organism; not copyable");
    Vigil(Vigil&&) = delete("interior pointers from CrucibleContext and ring would dangle");
    Vigil& operator=(Vigil&&) = delete("interior pointers from CrucibleContext and ring would dangle");

    // Appends the tensor metadata, then pushes an op fingerprint to the
    // ring.  Returns false when either is full, in which case the op is
    // dropped and the next iteration re-records everything.
    //
    // This is a producer entry point in its own right, not a helper of
    // dispatch_op, so it carries the same thread gate.  Without one, a
    // caller that reaches the ring through here from a second thread gets
    // the torn append dispatch_op refuses, and the two entry points state
    // different ownership rules for one ring.
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool record_op(TraceRing::ValidatedEntryPtr ve, const TensorMeta* metas,
                                                           uint32_t n_metas, ScopeHash scope_hash = {},
                                                           CallsiteHash callsite_hash = {}) pre(ve.value() != nullptr) {
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
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE DispatchResult
    dispatch_op(TraceRing::ValidatedEntryPtr ve, const TensorMeta* metas, uint32_t n_metas, ScopeHash scope_hash = {},
                CallsiteHash callsite_hash = {}) pre(ve.value() != nullptr) {
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
        if (ctx_.is_compiled()) [[likely]] {
            auto compiled_view = ctx_.mint_compiled_view(fg);
            auto status = ctx_.advance(entry.schema_hash, entry.shape_hash, compiled_view);
            if (status == ReplayStatus::DIVERGED) [[unlikely]]
                return handle_divergence_(fg, entry.schema_hash, entry.shape_hash);
            return {.action = DispatchResult::Action::COMPILED, .status = status, .pad = {}, .op_index = OpIndex{}};
        }

        // The acquire observe pairs with the release publish on the
        // background thread, so every byte of the region it wrote before
        // that store is visible here.  A relaxed load could miss the store
        // for one op and record it instead of aligning, which costs an extra
        // op before alignment completes.
        auto* pending = pending_region_.observe();
        if (pending || pending_activation_) [[unlikely]]
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
    // This facade pins that at the type level by demanding an empty caller
    // row.  What it catches: an eager fallback path, an init-time helper, a
    // background pumping helper or a test fixture that reaches the
    // foreground recording site by mistake.  The row mismatch fires at
    // compile time, before the ring head can advance.
    template <typename CallerRow = ::foundation::effects::Row<>>
        requires ::fixy::IsPure<CallerRow>
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE DispatchResult
    dispatch_op_pure(TraceRing::ValidatedEntryPtr ve, const TensorMeta* metas, uint32_t n_metas,
                     ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {}) pre(ve.value() != nullptr) {
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
    [[nodiscard]] CRUCIBLE_INLINE VigilFgCtx mint_producer_context() noexcept {
        return assert_producer_thread_();
    }

    // Relaxed suffices: only the foreground writes the mode, at each
    // activation and deactivation of the replay context, and a cross-thread
    // reader needs only eventual visibility.  The real synchronization is
    // the pending-region observe.
    //
    // Not gnu::pure, despite reading nothing else: another thread can change
    // the value between two loads, so common-subexpression elimination would
    // be wrong.
    [[nodiscard]] Mode mode() const noexcept { return mode_.load(std::memory_order_relaxed); }

    // True while the foreground replays a region.  The mode follows the
    // context: every path that activates or deactivates it publishes the
    // matching mode on the same thread, so this and context().is_compiled()
    // agree whenever the foreground reads them.
    [[nodiscard]] bool is_compiled() const noexcept { return mode() == Mode::COMPILED; }

    // True when the background thread published a region that the
    // foreground has not taken yet.  The foreground takes it on its next
    // dispatch and aligns to it before replay can start, so this is true
    // earlier than is_compiled.  The acquire load pairs with the release
    // publish, and flush() orders that publish before it returns.
    [[nodiscard]] bool has_pending_region() const noexcept { return pending_region_.has_pending(); }

    [[nodiscard]] const RegionNode* active_region() const noexcept {
        return bg_.active_region.load(std::memory_order_acquire);
    }

    // The background thread published a writable region.  replay needs that
    // writable view, because the caller's execution lambda mutates the
    // region as it writes outputs.  The const accessor above serves
    // introspection.
    [[nodiscard]] RegionNode* active_region_mut() noexcept { return bg_.active_region.load(std::memory_order_acquire); }

    // Advanced only by the background thread, once per region transition.
    [[nodiscard]] uint64_t current_step() const noexcept { return step_.get(); }

    [[nodiscard]] ContentHash head_hash() const noexcept {
        return cipher_.has_value() ? cipher_->head() : ContentHash{};
    }

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
    [[gnu::cold]] void flush() {
        const uint64_t target_produced = ring_->total_produced();
        while (bg_.total_processed.get() < target_produced) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    [[nodiscard]] bool flush_complete() const { return bg_.total_processed.get() >= ring_->total_produced(); }

    // Makes the previously superseded transaction active again, restoring
    // the replay state to match.
    //
    // The publish stage owns the transaction log, and on_region_ready writes
    // it for every region it publishes.  So the rollback runs there, as a job
    // between two publications, and never beside one.  The job copies out the
    // restored region, because a transaction pointer belongs to the owning
    // thread and a later publication can recycle its slot.  The foreground
    // part below then acts on that copy alone, on the producer thread.
    [[nodiscard, gnu::cold]] bool rollback() {
        bool restored_one = false;
        RegionNode* restored = nullptr;
        bg_.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
            restored_one = tx_log_.rollback(stage);
            if (!restored_one) return;
            if (const Transaction* tx = tx_log_.active(stage)) restored = tx->region.value();
        });
        if (!restored_one) return false;
        const VigilFgCtx fg = assert_producer_thread_();
        // The deactivation abandons the replayed region the way a divergence
        // does, so the mode takes the same transition.  A restored region
        // with a memory plan takes it back to COMPILED below.
        if (ctx_.is_compiled()) ctx_.deactivate();
        mode_.publish_recording_after_divergence();
        if (restored != nullptr) {
            bg_.active_region.store(restored, std::memory_order_release);
            if (ctx_.activate(restored)) {
                register_externals_from_region_(fg, restored);
                mode_.publish_compiled();
            }
        }
        return true;
    }

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
    // flushes files, so the caller's context must admit IO and Block.
    template <class Ctx>
        requires ::foundation::effects::CtxAdmits<Ctx, Cipher::open_view_required_row>
    [[nodiscard, gnu::cold]] bool persist(Ctx const& ctx) {
        if (!cipher_.has_value()) return false;
        const RegionNode* region = active_region();
        if (!region) return false;
        // The store is only ever emplaced from open(), so holding a value
        // already proves it is open.  One mint serves both calls below.
        auto open_view = cipher_->mint_open_view(ctx);
        const ContentHash hash = cipher_->store(open_view, Cipher::content_addressed(region), meta_log_.get());
        if (!hash) return false;
        cipher_->advance_head(open_view, hash, step_.get());
        return true;
    }

    // Loads the most recent stored region and makes it active, activating
    // replay too when that region carries a memory plan.  The activation is
    // foreground state, so this runs on the producer thread.
    template <class Ctx>
        requires ::foundation::effects::CtxAdmits<Ctx, Cipher::open_view_required_row>
    [[nodiscard, gnu::cold]] bool load(Ctx const& ctx, ::foundation::effects::Alloc a) {
        if (!cipher_.has_value() || cipher_->empty()) return false;
        auto open_view = cipher_->mint_open_view(ctx);
        RegionNode* region = cipher_->load_content_addressed(open_view, a, cipher_->head(), load_arena_).get();
        if (!region) return false;
        const VigilFgCtx fg = assert_producer_thread_();
        bg_.active_region.store(region, std::memory_order_release);
        // COMPILED only when the context activates.  A region without a
        // memory plan cannot replay, and the mode must not say it does.
        if (ctx_.activate(region)) {
            register_externals_from_region_(fg, region);
            region_cache_.insert(region);
            mode_.publish_compiled();
        }
        return true;
    }

    // Pre-allocated pointer for output j of the current op.  Valid only
    // after dispatch_op returned COMPILED with a matching or complete
    // status, which is what the minted view's precondition checks.  The
    // context proves that the caller holds this Vigil's producer claim.
    [[nodiscard]] void* output_ptr(VigilFgCtx const& fg, uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
        auto compiled_view = ctx_.mint_compiled_view(fg);
        return ctx_.output_ptr(j, compiled_view);
    }

    [[nodiscard]] void* input_ptr(VigilFgCtx const& fg, uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
        auto compiled_view = ctx_.mint_compiled_view(fg);
        return ctx_.input_ptr(j, compiled_view);
    }

    void register_external(VigilFgCtx const& fg, SlotId sid, ::fixy::NonNull<void*> ptr) {
        auto compiled_view = ctx_.mint_compiled_view(fg);
        ctx_.register_external(sid, ptr, compiled_view);
    }

    [[nodiscard]] uint32_t compiled_iterations() const { return ctx_.compiled_iterations(); }
    [[nodiscard]] uint32_t diverged_count() const { return ctx_.diverged_count(); }
    [[nodiscard]] const CrucibleContext& context() const CRUCIBLE_LIFETIMEBOUND { return ctx_; }
    [[nodiscard]] const RegionCache& region_cache() const CRUCIBLE_LIFETIMEBOUND { return region_cache_; }

    // These two hand out the producer surface of the ring and of the
    // metadata log.  A caller that appends through one of them is the
    // producer, so each asks for the context of this Vigil's producer claim,
    // as record_op and dispatch_op do.  No op path calls them, so each also
    // checks the calling thread against the claim at run time.
    [[nodiscard]] TraceRing& ring(VigilFgCtx const&) CRUCIBLE_LIFETIMEBOUND {
        CRUCIBLE_FATAL_INVARIANT(producer_claim_.is_claimable_by_caller());
        return *ring_;
    }
    [[nodiscard]] MetaLog& meta_log(VigilFgCtx const&) CRUCIBLE_LIFETIMEBOUND {
        CRUCIBLE_FATAL_INVARIANT(producer_claim_.is_claimable_by_caller());
        return *meta_log_;
    }

    // Counters that any thread can read with no context.  Each value is a
    // snapshot, and it can be stale by the time the caller reads it.
    [[nodiscard]] ::fixy::Stale<uint32_t> ring_size() const noexcept { return ring_->size(); }
    [[nodiscard]] uint64_t ring_total_produced() const noexcept { return ring_->total_produced(); }
    [[nodiscard]] ::fixy::Stale<uint32_t> meta_log_size() const noexcept { return meta_log_->size(); }

    [[nodiscard]] uint32_t bg_iterations_completed() const { return bg_.iterations_completed.get(); }
    [[nodiscard]] uint32_t bg_last_iteration_length() const { return bg_.last_iteration_length; }
    [[nodiscard]] uint32_t bg_detector_boundaries() const { return bg_.detector.boundaries_detected.get(); }
    [[nodiscard]] bool bg_detector_confirmed() const { return bg_.detector.confirmed; }

    // True only when the configuration asked for the watchdog and the
    // tracing subprogram actually attached.
    //
    // True does not imply a usable verdict yet.  The first window always
    // reports InsufficientData while the baseline is captured.
    [[nodiscard]] bool watchdog_enabled() const noexcept { return wd_.has_value(); }

    // The acquire load pairs with the release store on the background
    // thread.  Reads InsufficientData when no region transition has happened
    // yet, and when the watchdog is disabled.
    [[nodiscard]] ::crucible::warden::WatchdogVerdict last_watchdog_verdict() const noexcept {
        return wd_last_verdict_.load(std::memory_order_acquire);
    }

    // While the watchdog is enabled, each region transition increments
    // exactly one of these three.  The acquire load pairs with the release
    // increment on the background thread.
    [[nodiscard]] uint32_t watchdog_healthy_count() const noexcept {
        return wd_healthy_count_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint32_t watchdog_downgrade_count() const noexcept {
        return wd_downgrade_count_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint32_t watchdog_insufficient_count() const noexcept {
        return wd_insufficient_count_.load(std::memory_order_acquire);
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

    // Runs on the background thread when a new region is ready.  It must
    // not touch the persistence store: that owns mutable resident-cache and
    // log state and belongs to the foreground.  The background thread hands
    // over its own context, so the observation below acts under a context the
    // caller holds, not one built here.
    [[gnu::cold]] void on_region_ready(::foundation::effects::Bg const& bg, BackgroundThread::PublishStage const& stage,
                                       RegionNode* region) {
        // bump returns the previous value, which is the index this call
        // reserved.  The background thread is the sole writer.
        const uint64_t step = step_.bump();

        auto* tx = tx_log_.begin_tx(stage, step);
        // The result is discarded because a state-machine logic error here
        // is not something the background thread can recover from.
        //
        // The merkle root goes through the checked accessor so the non-zero
        // invariant is witnessed at this call site: its precondition fires
        // here if the hash was never recomputed.
        (void)tx_log_.commit(stage, tx, ::fixy::mint_tagged<::fixy::tags::source::Arena>(region), region->content_hash,
                             ::crucible::make_merkle_root(region->computed_merkle_hash()));
        (void)tx_log_.activate(stage, tx);

        // Publishing the region signals the foreground, which picks it up
        // on its next dispatch.  The mode stays RECORDING here.  The
        // foreground still records until it aligns to this region, and it
        // publishes COMPILED when it activates the context, so is_compiled
        // never reports a replay that has not started.  An observer that
        // waits for this publication reads has_pending_region.
        pending_region_.publish(region);

        // The observation is presented with a background-drain context
        // because this runs on the region-publishing thread, not the
        // foreground.
        if (wd_) {
            const auto v = wd_->observe(::fixy::BgDrainCtx{bg});
            wd_last_verdict_.store(v, std::memory_order_release);
            switch (v) {
                case ::crucible::warden::WatchdogVerdict::Healthy:
                    wd_healthy_count_.fetch_add(1, std::memory_order_release);
                    break;
                case ::crucible::warden::WatchdogVerdict::Downgrade:
                    wd_downgrade_count_.fetch_add(1, std::memory_order_release);
                    break;
                case ::crucible::warden::WatchdogVerdict::InsufficientData:
                    wd_insufficient_count_.fetch_add(1, std::memory_order_release);
                    break;
                default:
                    // The verdict enum has exactly the three values handled
                    // above, so this arm is deleted in release.
                    std::unreachable();
            }
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
        const uint32_t div_pos = ctx_.engine().ops_matched();

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
        // Take it before the reset signal below.  A region the background
        // thread publishes after that signal belongs to the new trace, and
        // stays in the slot for the next dispatch to observe.
        if (auto* stale_pending = pending_region_.consume()) region_cache_.insert(stale_pending);

        // Alignment state is foreground-only, and it describes a partial walk
        // into a region this reset abandons.  This is not dead code: both
        // rollback() and load() activate a context without clearing a walk in
        // progress, which is how a divergence becomes reachable with one
        // still open.
        pending_activation_ = nullptr;
        alignment_pos_ = alignment_pos_at(0);

        region_cache_.insert(ctx_.active_region());

        // Look for a cached region that matches at the divergence position,
        // excluding the current one.
        auto* alt = region_cache_.find_alternate(div_pos, schema_hash, shape_hash, ctx_.active_region());

        if (alt && try_switch_region_(fg, alt, div_pos)) {
            // The switch leaves the context compiled, so advance past the
            // divergent op.
            auto compiled_view = ctx_.mint_compiled_view(fg);
            auto status = ctx_.advance(schema_hash, shape_hash, compiled_view);
            if (status != ReplayStatus::DIVERGED) {
                return {.action = DispatchResult::Action::COMPILED,
                        .status = status,
                        .pad = {},
                        .op_index = OpIndex{ctx_.engine().ops_matched()}};
            }
            // Diverging twice in a row falls through to the reset below.
        }

        if (ctx_.is_compiled()) ctx_.deactivate();

        mode_.publish_recording_after_divergence();
        bg_.reset_requested.signal();
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
                         uint32_t n_metas, ScopeHash scope_hash, CallsiteHash callsite_hash)
        pre(ve.value() != nullptr) {
        const TraceRing::Entry& entry = *ve.value();
        // A newer region arriving mid-alignment replaces the pending one and
        // restarts the alignment from zero.  That is correct: the newer
        // region can carry different ops, for instance after a divergence
        // recovery cycle, so the old partial match means nothing.
        if (pending_region_.observe()) consume_pending_region_();

        if (pending_activation_) {
            // Nothing is recorded during alignment, because it would create
            // false iteration boundaries in the background detector.
            try_align_(fg, entry.schema_hash, entry.shape_hash);
        } else {
            (void)record_op(ve, metas, n_metas, scope_hash, callsite_hash);
        }

        return {
            .action = DispatchResult::Action::RECORD, .status = ReplayStatus::MATCH, .pad = {}, .op_index = OpIndex{}};
    }

    // Moves the published region into foreground-only alignment state.  It
    // does not activate replay; the alignment phase does that.
    [[gnu::cold]] void consume_pending_region_() {
        auto* region = pending_region_.consume();
        if (!region) return;
        if (region->num_ops == 0) return;

        pending_activation_ = region;
        alignment_pos_ = alignment_pos_at(0);
    }

    // A sliding-window match against the region's first K ops.
    //
    // When the background thread publishes a region, the foreground's
    // position within the iteration is unknown.  K consecutive matches
    // locate the boundary, at which point replay activates and the engine
    // skips forward over the ops already matched.  One op could match by
    // coincidence; K in a row will not.
    [[gnu::cold]] void try_align_(VigilFgCtx const& fg, SchemaHash schema, ShapeHash shape) {
        // Debug-only: the single caller reaches this helper from inside a
        // branch that has already tested pending_activation_, so a null here
        // means that branch was rewritten, not that a caller misused the
        // class. The region read below faults on its own in a release build,
        // so the failure stays loud without a check.
        //
        // The member is read into the local first because a contract
        // predicate that reaches a member through `this` is rejected as
        // non-constant when the compiler folds this body.
        const auto* region = pending_activation_;
        CRUCIBLE_DEBUG_ASSERT(region != nullptr);

        const uint8_t pos = alignment_pos_.value();
        if (schema == region->ops[pos].schema_hash && shape == region->ops[pos].shape_hash) {
            // The increment cannot exceed the bound: once the previous call
            // reached the threshold it cleared pending_activation_, so this
            // function is not called again past that point.
            alignment_pos_ = alignment_pos_at(static_cast<uint8_t>(pos + uint8_t{1}));
        } else {
            // A mismatch resets, but this op may itself be a fresh start.
            alignment_pos_ = alignment_pos_at(0);
            if (region->num_ops > 0 && schema == region->ops[0].schema_hash && shape == region->ops[0].shape_hash) {
                alignment_pos_ = alignment_pos_at(1);
            }
        }

        // A region shorter than K is matched in full instead.
        const uint32_t threshold = (region->num_ops < ALIGNMENT_K) ? region->num_ops : ALIGNMENT_K;

        if (uint32_t{alignment_pos_.value()} >= threshold) {
            if (!ctx_.activate(region)) {
                // No memory plan means the region cannot be compiled.
                pending_activation_ = nullptr;
                return;
            }

            register_externals_from_region_(fg, region);
            region_cache_.insert(region);

            // The matched ops already executed eagerly, so the engine has
            // to skip them for the next op to check against the right entry.
            for (uint32_t i = 0; i < uint32_t{alignment_pos_.value()}; i++) {
                auto status = ctx_.advance(region->ops[i].schema_hash, region->ops[i].shape_hash);
                // Debug-only: the hashes fed in here are the region's own,
                // and alignment already compared each one against the same
                // entry, so a mismatch means the engine's cursor is out of
                // step rather than that the data is bad. A release build
                // carries no check because the next advance diverges and the
                // recovery path already handles that.
                CRUCIBLE_DEBUG_ASSERT(status == ReplayStatus::MATCH || status == ReplayStatus::COMPLETE);
                (void)status;
            }

            mode_.publish_compiled();
            pending_activation_ = nullptr;
        }
    }

    // Walks the region's ops to recover each external slot's data pointer
    // from the recorded tensor metadata.  Runs once per activation.
    [[gnu::cold]] void register_externals_from_region_(VigilFgCtx const& fg, const RegionNode* region) {
        if (!region->plan) return;

        for (uint32_t slot_idx = 0; slot_idx < region->plan->num_slots; slot_idx++) {
            if (!region->plan->slots[slot_idx].is_external) continue;

            SlotId target = region->plan->slots[slot_idx].slot_id;
            void* ptr = nullptr;

            for (uint32_t i = 0; i < region->num_ops && !ptr; i++) {
                const auto& te = region->ops[i];
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
                auto compiled_view = ctx_.mint_compiled_view(fg);
                ctx_.register_external(target, ::fixy::mint_refined<::fixy::non_null>(ptr), compiled_view);
            }
        }
    }

    // Verifies the prefix match, then delegates the pool detach, the slot
    // migration and the engine advance.  Returns true when the switch
    // succeeded and the engine sits at div_pos.
    [[nodiscard, gnu::cold]] bool try_switch_region_(VigilFgCtx const& fg, const RegionNode* alt, uint32_t div_pos)
        pre(alt != nullptr) {
        if (!alt->plan) return false;

        // Every op before the divergence point must carry an identical
        // schema and shape in both regions.
        if (div_pos > 0) {
            const auto* old_region = ctx_.active_region();
            // Debug-only: a divergence position above zero can only come
            // from a compiled context, which by construction holds an active
            // region. The loop below reads old_region->ops, so a release
            // build faults on the null rather than continuing past it.
            CRUCIBLE_DEBUG_ASSERT(old_region != nullptr);

            if (div_pos > alt->num_ops) return false;

            for (uint32_t i = 0; i < div_pos; i++) {
                if (old_region->ops[i].schema_hash != alt->ops[i].schema_hash
                    || old_region->ops[i].shape_hash != alt->ops[i].shape_hash)
                    return false;
            }
        }

        if (!ctx_.switch_region(alt, div_pos)) return false;
        register_externals_from_region_(fg, alt);
        // These hold only on the success path; every failure returns above.
        // Pinning them here catches a refactor that skips the publication or
        // publishes the wrong region, which would otherwise keep dispatching
        // against the stale one.  Under NDEBUG they become assumptions the
        // next dispatch can speculate on.
        CRUCIBLE_POST(0, alt != nullptr);
        CRUCIBLE_POST(0, ctx_.active_region() == alt);
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
    std::optional<Cipher> cipher_;
    // Every dispatch and every record must come from one and the same
    // thread.  Another thread entering breaks the ring's single-producer
    // protocol and can corrupt the head-to-tail relationship.  The claim
    // holds the first dispatching thread, and every later dispatch checks
    // the match in every build mode, release included.  It is also the one
    // production route to a foreground context.
    ::foundation::effects::host::ProducerClaim<Vigil> producer_claim_;
    ModeCell mode_;
    ::fixy::AtomicMonotonic<uint64_t> step_ = ::fixy::mint_atomic_monotonic<uint64_t>(0);
    Arena load_arena_{1 << 20};

    // The publication is release on the background side and acquire on the
    // foreground side, so the region data written before the publish is
    // visible after the observe.  It is a reusable latest-wins slot rather
    // than a publish-once cell because divergence recovery publishes
    // several regions over one process lifetime.
    ::fixy::handle::PublishSlot<RegionNode> pending_region_;
    // The four below are foreground-only.
    RegionNode* pending_activation_{nullptr};
    AlignmentPos alignment_pos_ = alignment_pos_at(0);
    CrucibleContext ctx_;
    RegionCache region_cache_;

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
    std::optional<::crucible::perf::Senses> senses_;
    std::optional<::crucible::warden::DeadlineWatchdog> wd_;
    std::atomic<::crucible::warden::WatchdogVerdict> wd_last_verdict_{
        ::crucible::warden::WatchdogVerdict::InsufficientData};
    std::atomic<uint32_t> wd_healthy_count_{0};
    std::atomic<uint32_t> wd_downgrade_count_{0};
    std::atomic<uint32_t> wd_insufficient_count_{0};

    BackgroundThread bg_;  // Must stay last.
};

// A scoped view must not outlive the scope that minted it, so no field of
// Vigil, transitively, is allowed to be one.
static_assert(::fixy::no_scoped_view_field_check<Vigil>());

}  // namespace crucible
