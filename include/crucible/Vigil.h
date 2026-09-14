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
// The mode cell is also reachable as a single-party session for cold
// observers.  The foreground dispatch path reads the status flag directly.
//
// Member declaration order is load-bearing for destruction safety.  bg_ is
// declared last, so it is destroyed first and joins the background thread
// before every member that thread touches is invalidated.

#include <crucible/BackgroundThread.h>
#include <crucible/Cipher.h>
#include <crucible/CrucibleContext.h>
#include <crucible/IterationDetector.h>
#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/RegionCache.h>
#include <crucible/TraceRing.h>
#include <crucible/Transaction.h>
#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/bridges/VigilModeHandle.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/handles/PublishOnce.h>
#include <crucible/perf/Senses.h>
#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Refined.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>  // std::unreachable

// This header spells its safety wrappers through the fixy namespaces but
// deliberately does not pull the fixy umbrella header.  Re-opening the
// namespaces installs the aliases from the narrow includes already listed
// above, and adds no include.  The umbrella declares the same aliases
// independently, so the two are idempotent.
//
// mint_atomic_session below is the exception: it is a substrate session mint
// rather than a wrapper, so it keeps its own spelling.
namespace crucible::fixy::wrap {
using ::crucible::safety::AtomicMonotonic;
using ::crucible::safety::bounded_above;
using ::crucible::safety::NonNull;
using ::crucible::safety::no_scoped_view_field_check;
using ::crucible::safety::Refined;
}  // namespace crucible::fixy::wrap
namespace crucible::fixy::handle {
using ::crucible::safety::PublishSlot;
}  // namespace crucible::fixy::handle

namespace crucible {

class Vigil {
public:
    // The mode types live in a smaller header so the bridge that re-exports
    // the mint does not drag this header's whole dependency closure with it.
    // The aliases below keep every caller resolving the same types.
    //
    // A monotonic wrapper would be wrong for the mode: the lifecycle is
    // deliberately cyclic, running RECORDING to COMPILED and back to
    // RECORDING after a divergence.  ModeCell keeps the atomic private and
    // exposes only the two transitions the runtime actually performs.
    //
    // The pending region uses a latest-wins publication slot rather than a
    // publish-once cell for the same reason: divergence recovery publishes
    // several regions over one process lifetime.
    using Mode = ::crucible::vigil_mode::Mode;
    using ModeCell = ::crucible::vigil_mode::ModeCell;

    template <Mode From, Mode To>
    using ModeTransition = ::crucible::vigil_mode::ModeTransition<From, To>;

    using ModeRecordingToCompiled = ::crucible::vigil_mode::ModeRecordingToCompiled;
    using ModeCompiledToRecording = ::crucible::vigil_mode::ModeCompiledToRecording;

    using ModeProtocol = ::crucible::vigil_mode::ModeProtocol;
    using ModeSessionHandle = ::crucible::vigil_mode::ModeSessionHandle;

    static consteval bool mode_transition_allowed(Mode from, Mode to) noexcept {
        return ::crucible::vigil_mode::mode_transition_allowed(from, to);
    }

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
    using AlignmentPos =
        ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<ALIGNMENT_POS_MAX>, uint8_t>;

    // No persistence, no distributed context.
    [[gnu::cold]] Vigil() : Vigil(Config{}) {}

    [[gnu::cold]] explicit Vigil(Config cfg) : cfg_(std::move(cfg)) {
        ring_ = std::make_unique<TraceRing>();
        ring_->reset();

        meta_log_ = std::make_unique<MetaLog>();
        meta_log_->reset();

        if (!cfg_.cipher_path.empty()) {
            // The configured path is operator-supplied, so it crosses the
            // trust boundary here and is declared external until the store's
            // own sanitizer promotes it.
            cipher_.emplace(
                Cipher::open(crucible::fixy::wrap::Path<crucible::fixy::tags::source::External>{cfg_.cipher_path}));
        }

        bg_.set_region_ready_callback(
            this, [](void* self, RegionNode* region) noexcept { static_cast<Vigil*>(self)->on_region_ready(region); });

        // The watchdog is constructed before the background thread starts,
        // so on_region_ready can observe on the very first region transition
        // without a null check.  Attach failure is not an error here: every
        // observation then returns InsufficientData.
        if (cfg_.enable_deadline_watchdog) {
            senses_.emplace(::crucible::perf::Senses::load_subset(
                ::crucible::effects::mint_init_context(::crucible::effects::detail::ctx_mint::init_key{}),
                ::crucible::perf::SensesMask{.sched_switch = true}));
            wd_.emplace(&*senses_, cfg_.watchdog_policy,
                        ::crucible::effects::mint_init_context(::crucible::effects::detail::ctx_mint::init_key{}));
        }

        bg_.start(ring_.get(), meta_log_.get(), cfg_.rank, cfg_.world_size, cfg_.device_capability);
    }

    ~Vigil() = default;

    Vigil(const Vigil&) = delete("Vigil owns the runtime organism; not copyable");
    Vigil& operator=(const Vigil&) = delete("Vigil owns the runtime organism; not copyable");
    Vigil(Vigil&&) = delete("interior pointers from CrucibleContext and ring would dangle");
    Vigil& operator=(Vigil&&) = delete("interior pointers from CrucibleContext and ring would dangle");

    // Appends the tensor metadata, then pushes an op fingerprint to the
    // ring.  Returns false when either is full, in which case the op is
    // dropped and the next iteration re-records everything.
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool record_op(TraceRing::ValidatedEntryPtr ve, const TensorMeta* metas,
                                                           uint32_t n_metas, ScopeHash scope_hash = {},
                                                           CallsiteHash callsite_hash = {}) pre(ve.value() != nullptr) {
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
        // part of its operations on a worker thread of its own, so this is
        // reachable from a first run rather than from a rewrite.
        //
        // The cost is a relaxed load and a comparison on a path that already
        // writes a full cache line and issues two release stores. It is a
        // mitigation, not a repair; a multi-producer ring is separate work.
        assert_producer_thread_();
        const TraceRing::Entry& entry = *ve.value();

        // The branch itself proves the context is compiled.  Minting the
        // view once here is what makes the engine transition below reachable
        // only from inside this branch.
        if (ctx_.is_compiled()) [[likely]] {
            auto compiled_view = ctx_.mint_compiled_view();
            auto status = ctx_.advance(entry.schema_hash, entry.shape_hash, compiled_view);
            if (status == ReplayStatus::DIVERGED) [[unlikely]]
                return handle_divergence_(entry, metas, n_metas, scope_hash, callsite_hash);
            return {.action = DispatchResult::Action::COMPILED, .status = status, .pad = {}, .op_index = OpIndex{}};
        }

        // The acquire observe pairs with the release publish on the
        // background thread, so every byte of the region it wrote before
        // that store is visible here.  A relaxed load could miss the store
        // for one op and record it instead of aligning, which costs an extra
        // op before alignment completes.
        auto* pending = pending_region_.observe();
        if (pending || pending_activation_) [[unlikely]]
            return dispatch_transition_(entry, metas, n_metas, scope_hash, callsite_hash);

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
    template <typename CallerRow = ::crucible::effects::Row<>>
        requires ::crucible::effects::IsPure<CallerRow>
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE DispatchResult
    dispatch_op_pure(TraceRing::ValidatedEntryPtr ve, const TensorMeta* metas, uint32_t n_metas,
                     ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {}) pre(ve.value() != nullptr) {
        return dispatch_op(ve, metas, n_metas, scope_hash, callsite_hash);
    }

    // The first call claims the thread; every later call verifies the match.
    //
    // Split in two so the part that inlines into dispatch_op is a relaxed
    // load, a comparison and a branch. The claim and the failure report sit
    // out of line and cold, which keeps them out of the instruction cache
    // lines the recording path occupies.
    CRUCIBLE_INLINE void assert_producer_thread_() noexcept {
        const auto current_tid = std::this_thread::get_id();
        if (producer_tid_.load(std::memory_order_relaxed) == current_tid) [[likely]] return;
        claim_or_reject_producer_thread_(current_tid);
    }

    [[gnu::cold, gnu::noinline]] void claim_or_reject_producer_thread_(std::thread::id current_tid) noexcept {
        auto claimed = producer_tid_.load(std::memory_order_relaxed);
        if (claimed == std::thread::id{}) {
            // Relaxed is enough: this gate synchronizes nothing, it only
            // records which thread arrived first. A failed exchange leaves
            // the winner's id in `claimed`, which the check below reports.
            if (producer_tid_.compare_exchange_strong(claimed, current_tid, std::memory_order_relaxed)) {
                return;
            }
            // Another thread claimed first, so fall through and fail.
        }
        // Not a contract clause, for two reasons. The predicate is about a
        // thread identity this function just read, not about an argument the
        // caller passed, so a precondition cannot state it. And a contract
        // evaluates to nothing in a target built with the semantic set to
        // `ignore`, as one target in this tree is, whereas this is the one
        // check standing between a second producer and a ring that tears
        // without a diagnostic: both threads claim the same slot and the
        // later write erases the earlier one.
        CRUCIBLE_FATAL_INVARIANT(claimed == current_tid);
    }

    // Relaxed suffices: the mode is a status flag written and read mostly
    // by the foreground, and a cross-thread reader needs only eventual
    // visibility.  The real synchronization is the pending-region observe.
    //
    // Not gnu::pure, despite reading nothing else: another thread can change
    // the value between two loads, so common-subexpression elimination would
    // be wrong.
    [[nodiscard]] Mode mode() const noexcept { return mode_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool is_compiled() const noexcept { return mode() == Mode::COMPILED; }

    [[nodiscard]] constexpr ModeSessionHandle mode_session() const noexcept {
        return safety::mint_atomic_session<ModeProtocol>(mode_);
    }

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
    [[nodiscard, gnu::cold]] bool rollback() {
        if (!tx_log_.rollback()) return false;
        if (ctx_.is_compiled()) ctx_.deactivate();
        const Transaction* tx = tx_log_.active();
        if (tx && tx->region.value()) {
            bg_.active_region.store(tx->region.value(), std::memory_order_release);
            if (ctx_.activate(tx->region.value())) register_externals_from_region_(tx->region.value());
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
    // A no-op when no store path was configured.
    [[nodiscard, gnu::cold]] bool persist() {
        if (!cipher_.has_value()) return false;
        const RegionNode* region = active_region();
        if (!region) return false;
        // The store is only ever emplaced from open(), so holding a value
        // already proves it is open.  One mint serves both calls below.
        auto open_view = cipher_->mint_open_view();
        const ContentHash hash = cipher_->store(open_view, Cipher::content_addressed(region), meta_log_.get());
        if (!hash) return false;
        cipher_->advance_head(open_view, hash, step_.get());
        return true;
    }

    // Loads the most recent stored region and makes it active, activating
    // replay too when that region carries a memory plan.
    [[nodiscard, gnu::cold]] bool load(effects::Alloc a) {
        if (!cipher_.has_value() || cipher_->empty()) return false;
        auto open_view = cipher_->mint_open_view();
        RegionNode* region = cipher_->load_content_addressed(open_view, a, cipher_->head(), load_arena_).get();
        if (!region) return false;
        bg_.active_region.store(region, std::memory_order_release);
        mode_.publish_compiled();
        if (ctx_.activate(region)) {
            register_externals_from_region_(region);
            region_cache_.insert(region);
        }
        return true;
    }

    // Pre-allocated pointer for output j of the current op.  Valid only
    // after dispatch_op returned COMPILED with a matching or complete
    // status, which is what the minted view's precondition checks.
    [[nodiscard]] void* output_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
        auto compiled_view = ctx_.mint_compiled_view();
        return ctx_.output_ptr(j, compiled_view);
    }

    [[nodiscard]] void* input_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
        auto compiled_view = ctx_.mint_compiled_view();
        return ctx_.input_ptr(j, compiled_view);
    }

    void register_external(SlotId sid, crucible::fixy::wrap::NonNull<void*> ptr) {
        auto compiled_view = ctx_.mint_compiled_view();
        ctx_.register_external(sid, ptr, compiled_view);
    }

    [[nodiscard]] uint32_t compiled_iterations() const { return ctx_.compiled_iterations(); }
    [[nodiscard]] uint32_t diverged_count() const { return ctx_.diverged_count(); }
    [[nodiscard]] const CrucibleContext& context() const CRUCIBLE_LIFETIMEBOUND { return ctx_; }
    [[nodiscard]] const RegionCache& region_cache() const CRUCIBLE_LIFETIMEBOUND { return region_cache_; }

    [[nodiscard]] const TransactionLog<16>& tx_log() const CRUCIBLE_LIFETIMEBOUND { return tx_log_; }
    [[nodiscard]] TraceRing& ring() CRUCIBLE_LIFETIMEBOUND { return *ring_; }
    [[nodiscard]] MetaLog& meta_log() CRUCIBLE_LIFETIMEBOUND { return *meta_log_; }

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
    // Runs on the background thread when a new region is ready.  It must
    // not touch the persistence store: that owns mutable resident-cache and
    // log state and belongs to the foreground.
    [[gnu::cold]] void on_region_ready(RegionNode* region) {
        // bump returns the previous value, which is the index this call
        // reserved.  The background thread is the sole writer.
        const uint64_t step = step_.bump();

        auto* tx = tx_log_.begin_tx(step);
        // The result is discarded because a state-machine logic error here
        // is not something the background thread can recover from.
        //
        // The merkle root goes through the checked accessor so the non-zero
        // invariant is witnessed at this call site: its precondition fires
        // here if the hash was never recomputed.
        (void)tx_log_.commit(tx, Transaction::ArenaRegion{region}, region->content_hash,
                             ::crucible::make_merkle_root(region->computed_merkle_hash()));
        (void)tx_log_.activate(tx);

        // Publishing the region signals the foreground, which picks it up
        // on its next dispatch.  The mode flip is separate so an observer
        // polling is_compiled sees the transition without waiting for that
        // dispatch.
        pending_region_.publish(region);
        mode_.publish_compiled();

        // The observation is presented with a background-drain context
        // because this runs on the region-publishing thread, not the
        // foreground.
        if (wd_) {
            const auto v = wd_->observe(::crucible::effects::BgDrainCtx{});
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
    [[nodiscard, gnu::cold]] CRUCIBLE_NOINLINE DispatchResult handle_divergence_(
        const TraceRing::Entry& entry, [[maybe_unused]] const TensorMeta* metas, [[maybe_unused]] uint32_t n_metas,
        [[maybe_unused]] ScopeHash scope_hash, [[maybe_unused]] CallsiteHash callsite_hash) {
        const uint32_t div_pos = ctx_.engine().ops_matched();

        region_cache_.insert(ctx_.active_region());

        // Look for a cached region that matches at the divergence position,
        // excluding the current one.
        auto* alt = region_cache_.find_alternate(div_pos, entry.schema_hash, entry.shape_hash, ctx_.active_region());

        if (alt && try_switch_region_(alt, div_pos)) {
            // The switch leaves the context compiled, so advance past the
            // divergent op.
            auto compiled_view = ctx_.mint_compiled_view();
            auto status = ctx_.advance(entry.schema_hash, entry.shape_hash, compiled_view);
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

    [[nodiscard, gnu::cold]] CRUCIBLE_NOINLINE DispatchResult dispatch_transition_(const TraceRing::Entry& entry,
                                                                                   const TensorMeta* metas,
                                                                                   uint32_t n_metas,
                                                                                   ScopeHash scope_hash,
                                                                                   CallsiteHash callsite_hash) {
        // A newer region arriving mid-alignment replaces the pending one and
        // restarts the alignment from zero.  That is correct: the newer
        // region can carry different ops, for instance after a divergence
        // recovery cycle, so the old partial match means nothing.
        if (pending_region_.observe()) consume_pending_region_();

        if (pending_activation_) {
            // Nothing is recorded during alignment, because it would create
            // false iteration boundaries in the background detector.
            try_align_(entry.schema_hash, entry.shape_hash);
        } else {
            (void)record_op(vouch(entry), metas, n_metas, scope_hash, callsite_hash);
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
        alignment_pos_ = AlignmentPos{uint8_t{0}};
    }

    // A sliding-window match against the region's first K ops.
    //
    // When the background thread publishes a region, the foreground's
    // position within the iteration is unknown.  K consecutive matches
    // locate the boundary, at which point replay activates and the engine
    // skips forward over the ops already matched.  One op could match by
    // coincidence; K in a row will not.
    [[gnu::cold]] void try_align_(SchemaHash schema, ShapeHash shape) {
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
            alignment_pos_ = AlignmentPos{static_cast<uint8_t>(pos + uint8_t{1})};
        } else {
            // A mismatch resets, but this op may itself be a fresh start.
            alignment_pos_ = AlignmentPos{uint8_t{0}};
            if (region->num_ops > 0 && schema == region->ops[0].schema_hash && shape == region->ops[0].shape_hash) {
                alignment_pos_ = AlignmentPos{uint8_t{1}};
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

            register_externals_from_region_(region);
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
    [[gnu::cold]] void register_externals_from_region_(const RegionNode* region) {
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
                auto compiled_view = ctx_.mint_compiled_view();
                ctx_.register_external(target, crucible::fixy::wrap::NonNull<void*>{ptr}, compiled_view);
            }
        }
    }

    // Verifies the prefix match, then delegates the pool detach, the slot
    // migration and the engine advance.  Returns true when the switch
    // succeeded and the engine sits at div_pos.
    [[nodiscard, gnu::cold]] bool try_switch_region_(const RegionNode* alt, uint32_t div_pos) pre(alt != nullptr) {
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
        register_externals_from_region_(alt);
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
    TransactionLog<16> tx_log_;
    std::optional<Cipher> cipher_;
    // Every dispatch and every record must come from one and the same
    // thread.  Another thread entering breaks the ring's single-producer
    // protocol and can corrupt the head-to-tail relationship.  This holds
    // the first dispatching thread's id, and every later dispatch checks the
    // match in every build mode, release included.
    //
    // An atomic thread id is not lock-free on every target.  Where the
    // underlying handle has no native atomic instruction, the standard
    // library falls back to a mutex-backed atomic, and a hidden mutex on
    // this check would invert the hot path's whole latency budget.  Refuse
    // to build rather than regress silently.
    static_assert(std::atomic<std::thread::id>::is_always_lock_free,
                  "std::atomic<std::thread::id> must be lock-free on this target.");
    std::atomic<std::thread::id> producer_tid_{};
    ModeCell mode_;
    fixy::wrap::AtomicMonotonic<uint64_t> step_{0};
    Arena load_arena_{1 << 20};

    // The publication is release on the background side and acquire on the
    // foreground side, so the region data written before the publish is
    // visible after the observe.  It is a reusable latest-wins slot rather
    // than a publish-once cell because divergence recovery publishes
    // several regions over one process lifetime.
    fixy::handle::PublishSlot<RegionNode> pending_region_;
    // The four below are foreground-only.
    RegionNode* pending_activation_{nullptr};
    AlignmentPos alignment_pos_{uint8_t{0}};
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

// A convenience overload of the mode-cell mint, so a caller that already
// holds a Vigil does not have to expose the cell.
//
// It is constexpr for uniformity with every other mint.  Vigil is not a
// literal type, so this is never actually evaluated in a constant
// expression.
[[nodiscard]] constexpr Vigil::ModeSessionHandle mint_vigil_mode_bridge(const Vigil& vigil) noexcept {
    return vigil.mode_session();
}

// A scoped view must not outlive the scope that minted it, so no field of
// Vigil, transitively, is allowed to be one.
static_assert(crucible::fixy::wrap::no_scoped_view_field_check<Vigil>());

}  // namespace crucible
