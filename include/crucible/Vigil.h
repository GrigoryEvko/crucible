#pragma once

// Vigil: the Crucible organism.
//
// Vigil is the single orchestration point that owns every runtime component:
//   TraceRing     ← SPSC ring (hot-path op recording)
//   MetaLog       ← parallel tensor metadata buffer
//   BackgroundThread ← drains ring, builds Merkle DAG, signals on_region_ready
//   TransactionLog ← lifecycle records per iteration
//   Cipher (opt)  ← content-addressed persistence (Kafka log retention)
//
// Vigil exposes three operational modes:
//   RECORDING  → Vessel dispatches ops normally; every op calls record_op().
//   COMPILED   → Active region is live; replay() drives execution.
//   DIVERGED   → Guard mismatch during replay; fallback to RECORDING.
//
// The Vessel adapter (PyTorch CrucibleFallback) becomes ~200 lines:
//   if (vigil.is_compiled()) { push_shadow_handles(); return; }
//   else { vigil.record_op(...); redispatch to eager backend; }
//
// Member declaration order is load-bearing for destruction safety:
//   ring_, meta_log_, tx_log_, cipher_, mode_, step_, load_arena_
//   are all declared BEFORE bg_. Since C++ destroys members in reverse
//   declaration order, bg_ is destroyed FIRST, joining the background
//   thread before the above members are invalidated.

#include <crucible/BackgroundThread.h>
#include <crucible/Cipher.h>
#include <crucible/CrucibleContext.h>
#include <crucible/MetaLog.h>
#include <crucible/MerkleDag.h>
#include <crucible/RegionCache.h>
#include <crucible/TraceRing.h>
#include <crucible/Transaction.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/handles/PublishOnce.h>
#include <crucible/perf/Senses.h>
#include <crucible/rt/DeadlineWatchdog.h>
#include <crucible/rt/Policy.h>
#include <crucible/safety/Mutation.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>  // std::unreachable

namespace crucible {

class Vigil {
 public:
    enum class Mode : uint8_t {
        RECORDING,   // recording ops into TraceRing
        COMPILED,    // active RegionNode is live, replay() active
        DIVERGED,    // guard mismatch, fell back from COMPILED
    };

 private:
    class ModeCell {
        std::atomic<Mode> value_{Mode::RECORDING};

    public:
        constexpr ModeCell() noexcept = default;

        ModeCell(const ModeCell&)            = delete("Vigil mode cell is process-local state");
        ModeCell& operator=(const ModeCell&) = delete("Vigil mode cell is process-local state");
        ModeCell(ModeCell&&)                 = delete("atomic mode cell is the channel identity");
        ModeCell& operator=(ModeCell&&)      = delete("atomic mode cell is the channel identity");

        [[nodiscard]] Mode load(std::memory_order order = std::memory_order_relaxed)
            const noexcept
        {
            return value_.load(order);
        }

        void publish_compiled() noexcept {
            value_.store(Mode::COMPILED, std::memory_order_relaxed);
        }

        void publish_recording_after_divergence() noexcept {
            value_.store(Mode::RECORDING, std::memory_order_relaxed);
        }
    };

    static_assert(sizeof(ModeCell) == sizeof(std::atomic<Mode>));

 public:
    struct Config {
        int32_t     rank             = -1;
        int32_t     world_size       = 0;
        uint64_t    device_capability = 0;
        std::string cipher_path;   // empty = no persistence

        // GAPS-004h follow-up: scheduler deadline-miss watchdog wiring.
        //
        // When `enable_deadline_watchdog` is true, Vigil loads the
        // SchedSwitch BPF subprogram and instantiates a DeadlineWatchdog
        // observing the dispatcher tid's preempt count.  observe() runs
        // on the background thread inside on_region_ready (per-iteration
        // cadence, ~10-100ms — well within the watchdog's documented
        // 100ms-1s tick budget), and the verdict is published via
        // atomic counters readable through watchdog_*() accessors.
        //
        // Vigil is an OBSERVER — it does NOT actuate.  Callers consume
        // the verdict (e.g. Keeper sees Downgrade → re-applies Policy
        // with `demote_one_step(hot_sched)`).  Vigil itself never
        // changes scheduler class; the bg-thread cost of observe() is
        // bounded to ~1 µs per iteration boundary regardless of verdict.
        //
        // Disabled by default — requires CAP_BPF and a kernel with
        // sched_switch tracepoint BTF support.  CI runs / restricted
        // environments / unit tests should leave this off; the
        // Senses::load_subset call is graceful (returns a Senses with
        // sched_switch() == nullptr), but the watchdog's observe()
        // returns InsufficientData every call when the facade is
        // unattached, so the cost is paid for no signal.
        bool        enable_deadline_watchdog = false;

        // Policy supplied to the watchdog.  Only `deadline_miss_budget`
        // and `watchdog_window_sec` are read; the rest of Policy is
        // ignored at this site (Hardening.h would consume the rest).
        // Default: production() — 10 misses per 60-second window before
        // verdict flips to Downgrade.
        rt::Policy  watchdog_policy = rt::Policy::production();
    };

    // Number of consecutive op matches required to confirm an iteration
    // boundary before activating CrucibleContext.  Matches IterationDetector::K.
    static constexpr uint32_t ALIGNMENT_K = 5;

    // ─── Construction / Destruction ────────────────────────────────

    // Default constructor: no persistence, no distributed context.
    [[gnu::cold]] Vigil() : Vigil(Config{}) {}

    [[gnu::cold]] explicit Vigil(Config cfg) : cfg_(std::move(cfg)) {
        ring_ = std::make_unique<TraceRing>();
        ring_->reset();

        meta_log_ = std::make_unique<MetaLog>();
        meta_log_->reset();

        if (!cfg_.cipher_path.empty()) {
            cipher_.emplace(Cipher::open(cfg_.cipher_path));
        }

        // Wire the background thread callback to our on_region_ready.
        bg_.region_ready_cb = [this](RegionNode* region) { on_region_ready(region); };

        // GAPS-004h follow-up: load Senses + construct DeadlineWatchdog
        // BEFORE bg_.start() so on_region_ready can call wd_->observe()
        // on the very first region transition without nullptr-checks at
        // the call site.  Senses::load_subset is noexcept; if libbpf
        // can't attach SchedSwitch the watchdog's observe() returns
        // InsufficientData every call (recorded in
        // wd_insufficient_count_), which is correct behaviour.  Both
        // wd_ and senses_ are declared BEFORE bg_, so destruction
        // order (reverse declaration) joins the bg thread first, then
        // tears down wd_, then senses_ — no UAF on the borrow.
        if (cfg_.enable_deadline_watchdog) {
            senses_.emplace(::crucible::perf::Senses::load_subset(
                ::crucible::effects::Init{},
                ::crucible::perf::SensesMask{ .sched_switch = true }));
            wd_.emplace(&*senses_, cfg_.watchdog_policy,
                        ::crucible::effects::Init{});
        }

        bg_.start(ring_.get(), meta_log_.get(),
                  cfg_.rank, cfg_.world_size, cfg_.device_capability);
    }

    ~Vigil() = default; // bg_ is declared last → destroyed first → stops thread

    Vigil(const Vigil&)             = delete("Vigil owns the runtime organism; not copyable");
    Vigil& operator=(const Vigil&)  = delete("Vigil owns the runtime organism; not copyable");
    Vigil(Vigil&&)                  = delete("interior pointers from CrucibleContext and ring would dangle");
    Vigil& operator=(Vigil&&)       = delete("interior pointers from CrucibleContext and ring would dangle");

    // ─── Hot path: record one op (RECORDING mode) ──────────────────
    //
    // Called by the Vessel adapter for every ATen op.
    // Appends tensor metadata to MetaLog, then pushes a fingerprint to TraceRing.
    // Returns false if the ring or MetaLog is full (op silently dropped;
    // next iteration re-records everything).
    //
    // Hot path: ~5ns + MetaLog write (~10ns for metas) = ~15ns total.
    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool record_op(
        TraceRing::ValidatedEntryPtr ve,
        const TensorMeta*            metas,
        uint32_t                     n_metas,
        ScopeHash                    scope_hash    = {},
        CallsiteHash                 callsite_hash = {})
        pre(ve.value() != nullptr)
    {
        MetaIndex meta_start;  // default = none()
        if (metas && n_metas > 0) {
            meta_start = meta_log_->try_append(metas, n_metas);
        }
        return ring_->try_append(*ve.value(), meta_start, scope_hash, callsite_hash);
    }

    // ─── Per-op dispatch (Tier 1 entry point) ─────────────────────
    //
    // The Vessel adapter calls this once per ATen op. Returns:
    //   RECORD   → execute eagerly, Vigil recorded the op
    //   COMPILED → outputs pre-allocated, use output_ptr(j) / input_ptr(j)
    //
    // Thin inline wrapper: only the hot paths live here. Cold paths
    // (divergence recovery, pending-region consume, alignment) are
    // in NOINLINE helpers so the compiler doesn't need callee-saved
    // registers for the hot path.
    //
    // COMPILED hot path:
    //   is_compiled() → ctx_.advance() → return result
    //   No OpIndex computation (would require division by 96).
    //
    // RECORDING hot path:
    //   is_compiled() → pending check → record_op() → return result
    //   Acquire observe on pending_region_: must see the region data
    //   stored by the bg thread (release pairing).  Free on x86 (same
    //   as relaxed for an aligned load); emits DMB ISH on ARM only
    //   when needed.  A relaxed load could miss the bg's store for
    //   one op, recording it instead of aligning — alignment then
    //   needs an extra op to reach K, delaying ctx_ activation.
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE DispatchResult dispatch_op(
        TraceRing::ValidatedEntryPtr ve,
        const TensorMeta*            metas,
        uint32_t                     n_metas,
        ScopeHash                    scope_hash    = {},
        CallsiteHash                 callsite_hash = {})
        pre(ve.value() != nullptr)
    {
#ifndef NDEBUG
        // Debug-only SPSC producer-thread check — first dispatch claims
        // the thread, subsequent dispatches must come from the same one.
        // Release builds skip this entirely (zero hot-path cost).
        assert_producer_thread_();
#endif
        const TraceRing::Entry& entry = *ve.value();

        // ── COMPILED path (hot) ──
        //
        // The is_compiled() branch proves the context is in COMPILED mode.
        // Mint a ScopedView once per dispatch so the advance() call
        // below uses the typed overload — type-system guarantee that
        // the engine transition is only reachable from this branch.
        // View construction is a single pointer-copy in release (contract
        // check in debug builds); the typed advance() overload is
        // otherwise identical to the legacy one.
        if (ctx_.is_compiled()) [[likely]] {
            auto compiled_view = ctx_.mint_compiled_view();
            auto status = ctx_.advance(entry.schema_hash, entry.shape_hash, compiled_view);
            if (status == ReplayStatus::DIVERGED) [[unlikely]]
                return handle_divergence_(entry, metas, n_metas,
                                          scope_hash, callsite_hash);
            return {.action = DispatchResult::Action::COMPILED, .status = status, .pad = {}, .op_index = OpIndex{}};
        }

        // ── RECORDING fast path (hot) ──
        //
        // Acquire observe matches the release publish in on_region_ready
        // (bg thread).  We must see every byte of the region (ops,
        // plan, hashes) the bg thread published before its store.
        auto* pending = pending_region_.observe();
        if (pending || pending_activation_) [[unlikely]]
            return dispatch_transition_(entry, metas, n_metas,
                                        scope_hash, callsite_hash);

        (void)record_op(ve, metas, n_metas, scope_hash, callsite_hash);
        return {.action = DispatchResult::Action::RECORD, .status = ReplayStatus::MATCH, .pad = {},
                .op_index = OpIndex{}};
    }

    // ═══════════════════════════════════════════════════════════════
    // FOUND-I19: row-typed facade — pins dispatch_op as Pure
    // ═══════════════════════════════════════════════════════════════
    //
    // Sibling of TraceRing::try_append_pure (FOUND-I16) and
    // MetaLog::try_append_pure (FOUND-I17).  Per CRUCIBLE.md §L4,
    // dispatch_op is the canonical foreground per-op recording site —
    // shape-budgeted at ~5 ns per call, forbidden from any class of
    // heavy operation (alloc, syscall, block, futex).  Functionally
    // pure at the C++ level: a single SPSC-ring append (RECORD path)
    // OR a guard-check + ScopedView mint + advance (COMPILED path) —
    // both leaves are memory-only, no I/O, no blocking, no allocation,
    // no init/test/bg context.  In F* effect terms, it is `Pure`
    // (the bottom of the OsUniverse effect-row lattice).  This facade
    // pins that fact at the type level via an `IsPure<CallerRow>`
    // constraint.
    //
    // Caller-row contract: CallerRow MUST satisfy `Subrow<CallerRow,
    // Row<>>`, i.e., the caller's row must be empty.  A caller in any
    // of {Alloc, IO, Block, Bg, Init, Test} context is REJECTED at
    // compile time.  The bug class caught: a fallback eager-execution
    // path (Block context), an init-time setup helper, a bg pumping
    // helper, or a test-only fixture that inadvertently invokes
    // dispatch_op on the foreground hot path; the row mismatch
    // catches the miscategorization at compile time before the SPSC
    // ring head advances.
    //
    // The facade is ADDITIVE: existing dispatch_op() callers stay
    // unchanged.  Production hot-path callers can migrate by replacing
    // `dispatch_op(...)` with `dispatch_op_pure<>(...)` to gain the
    // compile-time check.
    //
    // Implementation: thin forwarder to dispatch_op; zero runtime
    // cost (one inlined branchless tail-call under -O3).  The IsPure
    // constraint is checked at substitution time, not at runtime.
    //
    // Default template argument is `Row<>` so `dispatch_op_pure(...)`
    // (no template-arg) is equivalent to `dispatch_op_pure<Row<>>(...)`
    // — the most common case at production hot-path call sites.
    template <typename CallerRow = ::crucible::effects::Row<>>
        requires ::crucible::effects::IsPure<CallerRow>
    [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE DispatchResult dispatch_op_pure(
        TraceRing::ValidatedEntryPtr ve,
        const TensorMeta*            metas,
        uint32_t                     n_metas,
        ScopeHash                    scope_hash    = {},
        CallsiteHash                 callsite_hash = {})
        pre(ve.value() != nullptr)
    {
        return dispatch_op(ve, metas, n_metas, scope_hash, callsite_hash);
    }

#ifndef NDEBUG
    // First call claims; subsequent calls verify match.  Debug-only.
    CRUCIBLE_INLINE void assert_producer_thread_() noexcept {
        const auto current_tid = std::this_thread::get_id();
        auto claimed = producer_tid_.load(std::memory_order_relaxed);
        if (claimed == current_tid) return;
        if (claimed == std::thread::id{}) {
            // First dispatch — try to claim.  Relaxed: this is a
            // debug-only lifecycle gate, no cross-thread sync needed.
            if (producer_tid_.compare_exchange_strong(
                    claimed, current_tid, std::memory_order_relaxed)) {
                return;
            }
            // Lost the race — another thread claimed first; fall through.
        }
        contract_assert(claimed == current_tid &&
                        "Vigil::dispatch_op called from a thread other than the "
                        "foreground producer — SPSC invariant violated");
    }
#endif

    // ─── Queries (lock-free reads) ─────────────────────────────────

    // Relaxed: mode_ is set and read primarily by fg thread. Tests spin
    // on it cross-thread but only need eventual visibility (relaxed
    // guarantees this). The real synchronization is pending_region_ observe().
    // Note: these cross-thread queries load atomics — not gnu::pure
    // (the atomic load is not side-effect-free from the optimizer's
    // POV; another thread may change the value between loads, so CSE
    // would be incorrect).  noexcept only.
    [[nodiscard]] Mode mode() const noexcept {
        return mode_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool is_compiled() const noexcept { return mode() == Mode::COMPILED; }

    [[nodiscard]] const RegionNode* active_region() const noexcept {
        return bg_.active_region.load(std::memory_order_acquire);
    }

    // Monotonic counter advanced exclusively by the bg thread on each
    // region transition (see on_region_ready).  AtomicMonotonic's get()
    // is acquire — strictly stronger than the pre-migration relaxed
    // load, but step_ is informational only (tests + Cipher persist) so
    // the extra ARM dmb on a non-hot read is free.
    [[nodiscard]] uint64_t current_step() const noexcept {
        return step_.get();
    }

    [[nodiscard]] ContentHash head_hash() const noexcept {
        return cipher_.has_value() ? cipher_->head() : ContentHash{};
    }

    // ─── Control ───────────────────────────────────────────────────

    // Wait until the background thread has FULLY PROCESSED all entries
    // that were in the ring at the time of this call.
    //
    // "Fully processed" = drained from ring + fed to IterationDetector
    // + on_iteration_boundary() completed (build_trace, make_region,
    // region_ready_cb all finished).
    //
    // Previous implementation waited for ring_->size() == 0, which is
    // wrong: drain() empties the ring BEFORE processing starts, so
    // flush() could return while the bg thread was still building the
    // trace/region. Under CPU contention (parallel ctest), this race
    // caused test_vigil_dispatch to see mode_ == RECORDING after flush.
    //
    // New: snapshot total_produced, wait until total_processed catches up.
    // Release/acquire on total_processed ensures all bg side effects
    // (pending_region_, mode_) are visible to fg after flush returns.
    [[gnu::cold]] void flush() {
        const uint64_t target_produced = ring_->total_produced();
        while (bg_.total_processed.get() < target_produced) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // Query: has the bg thread fully processed all entries ever produced?
    // Tests use this to verify flush() semantics explicitly.
    [[nodiscard]] bool flush_complete() const {
        return bg_.total_processed.get() >= ring_->total_produced();
    }

    // Restore the previous SUPERSEDED transaction as the active one.
    // Also deactivates/re-activates CrucibleContext as needed.
    // Returns true if rollback succeeded.
    [[nodiscard, gnu::cold]] bool rollback() {
        if (!tx_log_.rollback()) return false;
        // Deactivate current per-op replay state.
        if (ctx_.is_compiled())
            ctx_.deactivate();
        // Restore active region pointer from the rolled-back transaction.
        const Transaction* tx = tx_log_.active();
        if (tx && tx->region) {
            bg_.active_region.store(tx->region, std::memory_order_release);
            // Re-activate CrucibleContext with the rolled-back region.
            if (ctx_.activate(tx->region))
                register_externals_from_region_(tx->region);
        }
        return true;
    }

    // ─── Replay: traverse the compiled DAG (no Vessel required) ────
    //
    // GuardEval:  (const Guard&)      → int64_t  (return current observed value)
    // RegionExec: (const RegionNode*) → bool     (execute region, return ok)
    //
    // Returns true if replay completed without guard mismatches.
    template <typename GuardEval, typename RegionExec>
    [[nodiscard]] bool replay(GuardEval&& eval_guard, RegionExec&& exec_region) {
        const RegionNode* region = active_region();
        if (!region) return false;
        // crucible::replay() is defined in MerkleDag.h.
        // RegionNode : TraceNode, so the pointer upcast is implicit.
        return crucible::replay(
            const_cast<RegionNode*>(region),
            std::forward<GuardEval>(eval_guard),
            std::forward<RegionExec>(exec_region));
    }

    // ─── Persistence ───────────────────────────────────────────────

    // Serialize the active region to Cipher and advance HEAD.
    // No-op if cipher_path was not set in Config.
    // Returns true if the region was successfully stored.
    [[nodiscard, gnu::cold]] bool persist() {
        if (!cipher_.has_value()) return false;
        const RegionNode* region = active_region();
        if (!region) return false;
        // cipher_.has_value() guarantees Open (we only emplace via open()).
        // Mint the view once and thread it through both typed calls —
        // one acquire load instead of two redundant mints.
        auto open_view = cipher_->mint_open_view();
        const ContentHash hash = cipher_->store(open_view, region, meta_log_.get());
        if (!hash) return false;
        cipher_->advance_head(open_view, hash, step_.get());
        return true;
    }

    // Load the most recent region from Cipher and activate it.
    // No-op if no Cipher or the Cipher is empty.
    // Also activates CrucibleContext if the region has a MemoryPlan.
    [[nodiscard, gnu::cold]] bool load(effects::Alloc a) {
        if (!cipher_.has_value() || cipher_->empty()) return false;
        auto open_view = cipher_->mint_open_view();
        RegionNode* region = cipher_->load(open_view, a, cipher_->head(), load_arena_);
        if (!region) return false;
        bg_.active_region.store(region, std::memory_order_release);
        mode_.publish_compiled();
        // Activate per-op replay if the loaded region has a plan.
        if (ctx_.activate(region)) {
            register_externals_from_region_(region);
            region_cache_.insert(region);
        }
        return true;
    }

    // ─── CrucibleContext forwarding (Tier 1 compiled replay) ───────

    // Pre-allocated output pointer for output j of the current op.
    // Valid only after dispatch_op() returned COMPILED with MATCH/COMPLETE.
    // Mints a CompiledView locally so the typed ctx overload is taken;
    // the view's pre() check confirms the precondition the public API
    // documents.  Compiles to the same machine code as the untyped path.
    [[nodiscard]] void* output_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
        auto compiled_view = const_cast<CrucibleContext&>(ctx_).mint_compiled_view();
        return ctx_.output_ptr(j, compiled_view);
    }

    // Pre-allocated input pointer for input j of the current op.
    [[nodiscard]] void* input_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
        auto compiled_view = const_cast<CrucibleContext&>(ctx_).mint_compiled_view();
        return ctx_.input_ptr(j, compiled_view);
    }

    // Register an external tensor's data pointer with the pool.
    void register_external(SlotId sid, crucible::safety::NonNull<void*> ptr) {
        auto compiled_view = ctx_.mint_compiled_view();
        ctx_.register_external(sid, ptr, compiled_view);
    }

    // Number of complete iterations replayed in COMPILED mode.
    [[nodiscard]] uint32_t compiled_iterations() const { return ctx_.compiled_iterations(); }

    // Number of divergences detected during COMPILED replay.
    [[nodiscard]] uint32_t diverged_count() const { return ctx_.diverged_count(); }

    // Direct access to the CrucibleContext (diagnostics, not hot path).
    [[nodiscard]] const CrucibleContext& context() const CRUCIBLE_LIFETIMEBOUND { return ctx_; }

    // Direct access to the RegionCache (diagnostics).
    [[nodiscard]] const RegionCache& region_cache() const CRUCIBLE_LIFETIMEBOUND { return region_cache_; }

    // ─── Introspection ─────────────────────────────────────────────

    [[nodiscard]] const TransactionLog<16>& tx_log() const CRUCIBLE_LIFETIMEBOUND { return tx_log_; }
    [[nodiscard]] TraceRing&               ring()       CRUCIBLE_LIFETIMEBOUND { return *ring_; }
    [[nodiscard]] MetaLog&                 meta_log()   CRUCIBLE_LIFETIMEBOUND { return *meta_log_; }

    // Background thread diagnostics.
    [[nodiscard]] uint32_t bg_iterations_completed() const { return bg_.iterations_completed.get(); }
    [[nodiscard]] uint32_t bg_last_iteration_length() const { return bg_.last_iteration_length; }
    [[nodiscard]] uint32_t bg_detector_boundaries() const { return bg_.detector.boundaries_detected.get(); }
    [[nodiscard]] bool     bg_detector_confirmed() const { return bg_.detector.confirmed; }

    // ─── Deadline watchdog diagnostics (GAPS-004h follow-up) ───────
    //
    // True iff this Vigil was constructed with
    // `Config::enable_deadline_watchdog = true` AND Senses::load_subset
    // attached the SchedSwitch subprogram successfully.  False in all
    // other cases (disabled by Config, libbpf missing, SchedSwitch
    // failed to attach).  Use this to gate diagnostic output / Augur
    // attribution paths.
    //
    // Note: a true return does NOT guarantee that observe() has
    // produced a non-InsufficientData verdict — the very first
    // window's observations always yield InsufficientData while the
    // baseline is captured.  Check `last_watchdog_verdict()` for the
    // current verdict.
    [[nodiscard]] bool watchdog_enabled() const noexcept {
        return wd_.has_value();
    }

    // Most recent verdict published by the bg-thread observe() call.
    // Acquire load — pairs with the release store in on_region_ready.
    // Returns InsufficientData on a freshly-constructed Vigil (no
    // region transitions yet) or on a Vigil with watchdog disabled.
    [[nodiscard]] ::crucible::rt::WatchdogVerdict
    last_watchdog_verdict() const noexcept {
        return wd_last_verdict_.load(std::memory_order_acquire);
    }

    // Cumulative verdict counters.  Each region transition increments
    // exactly one of these (when the watchdog is enabled).  Acquire
    // load — pairs with release fetch_add on the bg thread.  Useful
    // for Augur attribution ("how many Healthy ticks since last
    // Downgrade?") and for tests that assert observe() ran.
    [[nodiscard]] uint32_t watchdog_healthy_count() const noexcept {
        return wd_healthy_count_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint32_t watchdog_downgrade_count() const noexcept {
        return wd_downgrade_count_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint32_t watchdog_insufficient_count() const noexcept {
        return wd_insufficient_count_.load(std::memory_order_acquire);
    }

    // Note: DeadlineWatchdog itself is NOT exposed by const-ref.  It
    // documents "SINGLE-THREAD ONLY" — its `baseline_count_`,
    // `latest_count_`, and `window_started_ns_` fields are non-atomic
    // and the bg thread mutates them inside observe().  A main-thread
    // read via a leaked pointer would be a data race per the C++
    // memory model (TSan would flag).  The atomic verdict + 3
    // cumulative counters above are the diagnostic surface; if Augur
    // needs more state (e.g. baseline_count for window-start
    // attribution), publish it from on_region_ready as another atomic.

 private:
    // ─── Background thread callback ────────────────────────────────
    //
    // Called on the background thread when a new RegionNode is ready.
    // Transitions the transaction to ACTIVE, updates the execution mode,
    // and optionally pre-stores the object in the Cipher.
    [[gnu::cold]] void on_region_ready(RegionNode* region) {
        // step_ is a monotonic counter for tx_log sequencing.  bg thread
        // is the sole writer; fg/test readers see an approximate value
        // via current_step().  AtomicMonotonic's bump() returns the
        // PREVIOUS value (the index this caller reserved) and uses
        // acq_rel — one extra dmb on ARM per region transition.  Cold
        // path; cost is negligible amortized over thousands of ops
        // between region boundaries.
        const uint64_t step = step_.bump();

        auto* tx = tx_log_.begin_tx(step);
        // commit is nodiscard — bg-thread fast path cannot recover
        // from a state-machine logic error here.  Cast away.
        (void)tx_log_.commit(tx, region,
                             region->content_hash,
                             region->merkle_hash);
        (void)tx_log_.activate(tx);

        // Signal fg thread: a region with a MemoryPlan is available.
        // fg thread picks it up in dispatch_op() via dispatch_transition_().
        // Also set mode_=COMPILED for backward compat — existing code/tests
        // poll is_compiled() without calling dispatch_op().
        pending_region_.publish(region);
        mode_.publish_compiled();

        // Pre-store the object (idempotent) so persist() is instant later.
        if (cipher_.has_value()) {
            auto open_view = cipher_->mint_open_view();
            (void)cipher_->store(open_view, region, meta_log_.get());
        }

        // GAPS-004h follow-up: poll the deadline watchdog at the
        // iteration boundary (bg-thread cold path).  observe() reads
        // SchedSwitch::context_switches() and emits a verdict; we
        // publish it via atomic counters for fg-thread / Augur /
        // Keeper consumers.  ~1 µs per call (single bpf_map_lookup +
        // one steady_clock::now()), invoked at most once per region
        // transition (~10-100ms steady-state) — total overhead well
        // under 0.01 % even on a tight inference loop.
        if (wd_) {
            const auto v = wd_->observe();
            wd_last_verdict_.store(v, std::memory_order_release);
            switch (v) {
                case ::crucible::rt::WatchdogVerdict::Healthy:
                    wd_healthy_count_.fetch_add(1, std::memory_order_release);
                    break;
                case ::crucible::rt::WatchdogVerdict::Downgrade:
                    wd_downgrade_count_.fetch_add(1, std::memory_order_release);
                    break;
                case ::crucible::rt::WatchdogVerdict::InsufficientData:
                    wd_insufficient_count_.fetch_add(1, std::memory_order_release);
                    break;
                default:
                    // WatchdogVerdict's underlying type is uint8_t with
                    // exactly three named values; observe() never returns
                    // anything else.  std::unreachable lets the compiler
                    // delete the default arm in release.
                    std::unreachable();
            }
        }
    }

    // ─── Cold dispatch paths (NOINLINE to keep hot path register-light) ─

    // Divergence handler: region cache lookup, switch attempt, fallback.
    // Called when ctx_.advance() returns DIVERGED.  ~50-400ns cold path.
    [[nodiscard, gnu::cold]] CRUCIBLE_NOINLINE DispatchResult handle_divergence_(
        const TraceRing::Entry& entry,
        [[maybe_unused]] const TensorMeta* metas,
        [[maybe_unused]] uint32_t          n_metas,
        [[maybe_unused]] ScopeHash         scope_hash,
        [[maybe_unused]] CallsiteHash      callsite_hash)
    {
        const uint32_t div_pos = ctx_.engine().ops_matched();

        // Cache the diverging region for future shape switches.
        region_cache_.insert(ctx_.active_region());

        // Try to find a cached region that matches at the divergence
        // position.  Excludes the current region.
        auto* alt = region_cache_.find_alternate(
            div_pos, entry.schema_hash, entry.shape_hash,
            ctx_.active_region());

        if (alt && try_switch_region_(alt, div_pos)) {
            // Switched successfully.  Advance past the divergent op.
            // try_switch_region_ leaves ctx_ in COMPILED mode.
            auto compiled_view = ctx_.mint_compiled_view();
            auto status = ctx_.advance(entry.schema_hash, entry.shape_hash, compiled_view);
            if (status != ReplayStatus::DIVERGED) {
                return {.action = DispatchResult::Action::COMPILED, .status = status, .pad = {},
                        .op_index = OpIndex{ctx_.engine().ops_matched()}};
            }
            // Double divergence — shouldn't happen.  Fall through.
        }

        // No cached alternate, switch failed, or double divergence.
        if (ctx_.is_compiled())
            ctx_.deactivate();

        mode_.publish_recording_after_divergence();
        // Signal bg thread to reset its detector and accumulated trace.
        bg_.reset_requested.signal();
        // Don't record the divergent op — it poisons the bg thread's
        // iteration detector.
        return {.action = DispatchResult::Action::RECORD,
                .status = ReplayStatus::DIVERGED, .pad = {}, .op_index = OpIndex{}};
    }

    // Transition handler: consume pending region, run alignment, or
    // record while a transition is in progress.  Called when
    // pending_region_ or pending_activation_ is non-null.
    [[nodiscard, gnu::cold]] CRUCIBLE_NOINLINE DispatchResult dispatch_transition_(
        const TraceRing::Entry& entry,
        const TensorMeta*       metas,
        uint32_t                n_metas,
        ScopeHash               scope_hash,
        CallsiteHash            callsite_hash)
    {
        // Always try to consume pending_region_.
        // If a newer region arrives while alignment is in progress,
        // consume_pending_region_ replaces pending_activation_ and
        // resets alignment_pos_ to 0. This is correct: the newer
        // region may have different ops (e.g. after a divergence
        // recovery cycle), so we must re-align from scratch.
        if (pending_region_.observe())
            consume_pending_region_();

        if (pending_activation_) {
            // Alignment phase: don't record (prevents false iteration
            // boundaries in the bg thread's detector).
            try_align_(entry.schema_hash, entry.shape_hash);
        } else {
            // entry is a live reference to a ValidatedEntryPtr's target
            // in dispatch_op's caller frame; re-vouch at the typed API.
            (void)record_op(vouch(entry), metas, n_metas, scope_hash, callsite_hash);
        }

        return {.action = DispatchResult::Action::RECORD, .status = ReplayStatus::MATCH, .pad = {},
                .op_index = OpIndex{}};
    }

    // ─── Private helpers ────────────────────────────────────────────

    // Consume the bg→fg pending region into fg-only alignment state.
    // Does NOT activate CrucibleContext — alignment phase handles that.
    [[gnu::cold]] void consume_pending_region_() {
        auto* region = pending_region_.consume();
        if (!region) return;
        if (region->num_ops == 0) return;  // degenerate region

        // Start alignment phase: scan for iteration boundary.
        pending_activation_ = region;
        alignment_pos_ = 0;
    }

    // Alignment phase: sliding window match against region ops[0..K-1].
    //
    // When the bg thread signals a region, we don't know where in the
    // iteration the fg thread currently is. We scan incoming ops for K
    // consecutive matches against the region's first K ops to find the
    // iteration boundary. Once found, activate CrucibleContext and
    // advance the engine past the matched ops.
    //
    // K=5 matches the IterationDetector's signature length — sufficient
    // to avoid false positives from a single op coincidence.
    [[gnu::cold]] void try_align_(SchemaHash schema, ShapeHash shape) {
        assert(pending_activation_ && "try_align_ called without pending region");
        const auto* region = pending_activation_;

        // Check if current op matches the expected alignment position.
        if (schema == region->ops[alignment_pos_].schema_hash &&
            shape  == region->ops[alignment_pos_].shape_hash)
        {
            alignment_pos_++;
        } else {
            // Mismatch — reset. But check if this op could be a new start (op 0).
            alignment_pos_ = 0;
            if (region->num_ops > 0 &&
                schema == region->ops[0].schema_hash &&
                shape  == region->ops[0].shape_hash)
            {
                alignment_pos_ = 1;
            }
        }

        // Once K consecutive ops match (or entire region if smaller),
        // we've confirmed the iteration boundary. Activate and advance.
        const uint32_t threshold = (region->num_ops < ALIGNMENT_K)
                                 ? region->num_ops : ALIGNMENT_K;

        if (alignment_pos_ >= threshold) {
            if (!ctx_.activate(region)) {
                // No plan → can't compile. Clear pending state.
                pending_activation_ = nullptr;
                return;
            }

            register_externals_from_region_(region);
            region_cache_.insert(region);

            // Advance the engine past the ops we already matched during
            // alignment. These ops executed eagerly; the engine needs to
            // be at position alignment_pos_ so the NEXT op checks against
            // the correct region op.
            for (uint32_t i = 0; i < alignment_pos_; i++) {
                auto status = ctx_.advance(region->ops[i].schema_hash,
                                           region->ops[i].shape_hash);
                // Must match — we verified these during alignment.
                assert(status == ReplayStatus::MATCH || status == ReplayStatus::COMPLETE);
                (void)status;
            }

            mode_.publish_compiled();
            pending_activation_ = nullptr;
        }
    }

    // Walk region ops to find external slot data_ptrs from recorded
    // TensorMeta. O(num_ext × num_ops × max_inputs) — cold path,
    // runs once per activation.
    [[gnu::cold]] void register_externals_from_region_(const RegionNode* region) {
        if (!region->plan) return;

        for (uint32_t slot_idx = 0; slot_idx < region->plan->num_slots; slot_idx++) {
            if (!region->plan->slots[slot_idx].is_external) continue;

            SlotId target = region->plan->slots[slot_idx].slot_id;
            void* ptr = nullptr;

            // Search region ops for the first input that reads from this slot.
            for (uint32_t i = 0; i < region->num_ops && !ptr; i++) {
                const auto& te = region->ops[i];
                if (!te.input_slot_ids) continue;
                for (uint16_t j = 0; j < te.num_inputs; j++) {
                    if (te.input_slot_ids[j] == target) {
                        ptr = te.input_metas[j].data_ptr;
                        break;
                    }
                }
            }

            if (ptr != nullptr) {
                // ctx_ has just been activated by activate(region) at the
                // call sites of register_externals_from_region_, so we
                // know it's in COMPILED mode.  Mint the view inline.
                auto compiled_view = ctx_.mint_compiled_view();
                ctx_.register_external(target, crucible::safety::NonNull<void*>{ptr}, compiled_view);
            }
        }
    }

    // ─── Region switching (divergence recovery via cache) ────────
    //
    // Verifies prefix compatibility, then delegates to
    // CrucibleContext::switch_region() which handles pool detach,
    // selective slot migration, and engine advancement.
    //
    // Returns true if switch succeeded and engine is at position div_pos.
    [[nodiscard, gnu::cold]] bool try_switch_region_(const RegionNode* alt, uint32_t div_pos)
        pre (alt != nullptr)
    {
        if (!alt->plan) return false;

        // For div_pos>0, verify prefix compatibility: ops 0..div_pos-1
        // must have identical schema+shape in both regions.
        if (div_pos > 0) {
            const auto* old_region = ctx_.active_region();
            assert(old_region && "no active region to switch from");

            if (div_pos > alt->num_ops) return false;

            for (uint32_t i = 0; i < div_pos; i++) {
                if (old_region->ops[i].schema_hash != alt->ops[i].schema_hash ||
                    old_region->ops[i].shape_hash  != alt->ops[i].shape_hash)
                    return false;
            }
        }

        if (!ctx_.switch_region(alt, div_pos)) return false;
        register_externals_from_region_(alt);
        return true;
    }

    // ─── Members (declaration order is destruction-order-critical) ─
    //
    // bg_ MUST be last: it is destroyed first, stopping the background
    // thread before all other members are invalidated.

    Config                          cfg_;
    std::unique_ptr<TraceRing>      ring_;
    std::unique_ptr<MetaLog>        meta_log_;
    TransactionLog<16>              tx_log_;
    std::optional<Cipher>           cipher_;
    // SPSC invariant: every dispatch_op / record_op must come from the
    // SAME thread (the foreground producer).  A different thread entering
    // violates the ring's single-producer protocol and can corrupt the
    // head-tail relationship.  producer_tid_ captures the first
    // dispatching thread's id and debug-asserts match on every subsequent
    // dispatch.  In release builds the contract collapses under
    // semantic=ignore.
    std::atomic<std::thread::id>    producer_tid_{};
    // mode_ is a status flag — real sync is pending_region_ observe(),
    // relaxed ordering is sufficient here (fg-thread-primary).  The
    // wrapper removes raw store/load vocabulary from Vigil and leaves
    // only named transitions matching the actual cyclic lifecycle.
    // step_ is a monotonic counter advanced by bg thread on each region
    // transition; AtomicMonotonic lifts the monotonicity invariant to
    // the type level (no decrement, no reset, no stale CAS).
    ModeCell                        mode_;
    safety::AtomicMonotonic<uint64_t> step_{0};
    Arena                           load_arena_{1 << 20}; // for Cipher::load()

    // ─── Tier 1 dispatch state (fg thread only, except pending_region_) ─

    // NOT relaxed: bg→fg publish. bg writes region data, then
    // publish(release). fg's observe/consume(acquire) in dispatch_op
    // must see it.  This is reusable latest-wins publication, not
    // PublishOnce: divergence recovery can publish multiple regions.
    safety::PublishSlot<RegionNode> pending_region_;
    RegionNode*                     pending_activation_{nullptr}; // fg-only: waiting for alignment
    uint32_t                        alignment_pos_{0};  // consecutive matched ops from region start
    CrucibleContext                 ctx_;               // fg-only replay
    RegionCache                     region_cache_;      // fg-only: cached alternate regions

    // ─── GAPS-004h follow-up: deadline watchdog state ──────────────
    //
    // senses_ MUST precede wd_: DeadlineWatchdog stores a const Senses*
    // borrow.  Destruction is reverse declaration order, so wd_ goes
    // before senses_ — the borrow's lifetime is upheld.
    //
    // Both MUST precede bg_: the bg thread's on_region_ready callback
    // calls wd_->observe(), which reads senses_'s SchedSwitch subprog.
    // bg_'s destruction joins the thread first; then wd_ tears down
    // (no more observe() calls possible); then senses_ unloads the
    // BPF subprograms (no more borrow holders).
    //
    // wd_*_count_ + wd_last_verdict_ are atomics — no inter-field
    // lifetime constraint, but kept here for cache-line locality with
    // wd_ (the bg thread writes wd_'s state and these counters in the
    // same on_region_ready frame).
    std::optional<::crucible::perf::Senses>             senses_;
    std::optional<::crucible::rt::DeadlineWatchdog>     wd_;
    std::atomic<::crucible::rt::WatchdogVerdict>        wd_last_verdict_{
        ::crucible::rt::WatchdogVerdict::InsufficientData};
    std::atomic<uint32_t>                               wd_healthy_count_{0};
    std::atomic<uint32_t>                               wd_downgrade_count_{0};
    std::atomic<uint32_t>                               wd_insufficient_count_{0};

    BackgroundThread                bg_;  // MUST be declared last
};

// Tier 2 opt-in: nothing inside Vigil may be a ScopedView.  The
// reflection walk proves that neither Vigil nor any of its fields
// (transitively, through known wrappers) stores a safety::ScopedView —
// views must not escape their construction scope.
static_assert(crucible::safety::no_scoped_view_field_check<Vigil>());

} // namespace crucible
