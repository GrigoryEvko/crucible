#pragma once

// Vigil: the Crucible organism.
//
// Vigil is the single orchestration point that owns every runtime component:
//   TraceRing     ← SPSC ring (hot path: ~5ns/op recording)
//   MetaLog       ← parallel tensor metadata buffer
//   BackgroundThread ← drains ring, builds Merkle DAG, signals on_region_ready
//   TransactionLog ← lifecycle records per iteration
//   Cipher (opt)  ← content-addressed persistence (Kafka log retention)
//
// Vigil exposes three operational modes:
//   RECORDING  → Vessel dispatches ops normally; every op calls record_op().
//   COMPILED   → Active region is live; replay() drives execution in ~2ns/iter.
//   DIVERGED   → Guard mismatch during replay; fallback to RECORDING.
//
// The Vessel adapter (PyTorch CrucibleFallback) becomes ~200 lines:
//   if (vigil.is_compiled()) { push_shadow_handles(); return; } // ~2ns
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
#include <crucible/safety/Mutation.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

namespace crucible {

class Vigil {
 public:
    enum class Mode : uint8_t {
        RECORDING,   // recording ops into TraceRing
        COMPILED,    // active RegionNode is live, replay() active
        DIVERGED,    // guard mismatch, fell back from COMPILED
    };

    struct Config {
        int32_t     rank             = -1;
        int32_t     world_size       = 0;
        uint64_t    device_capability = 0;
        std::string cipher_path;   // empty = no persistence
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
    //   Acquire load on pending_region_: must see the region data
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
        // Acquire load matches the release store in on_region_ready
        // (bg thread).  We must see every byte of the region (ops,
        // plan, hashes) the bg thread published before its store.
        auto* pending = pending_region_.load(std::memory_order_acquire);
        if (pending || pending_activation_) [[unlikely]]
            return dispatch_transition_(entry, metas, n_metas,
                                        scope_hash, callsite_hash);

        (void)record_op(ve, metas, n_metas, scope_hash, callsite_hash);
        return {.action = DispatchResult::Action::RECORD, .status = ReplayStatus::MATCH, .pad = {},
                .op_index = OpIndex{}};
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
    // guarantees this). The real synchronization is pending_region_ acquire.
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
    [[nodiscard, gnu::cold]] bool load(fx::Alloc a) {
        if (!cipher_.has_value() || cipher_->empty()) return false;
        auto open_view = cipher_->mint_open_view();
        RegionNode* region = cipher_->load(open_view, a, cipher_->head(), load_arena_);
        if (!region) return false;
        bg_.active_region.store(region, std::memory_order_release);
        mode_.store(Mode::COMPILED, std::memory_order_relaxed);
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
        pending_region_.store(region, std::memory_order_release);
        mode_.store(Mode::COMPILED, std::memory_order_relaxed);

        // Pre-store the object (idempotent) so persist() is instant later.
        if (cipher_.has_value()) {
            auto open_view = cipher_->mint_open_view();
            (void)cipher_->store(open_view, region, meta_log_.get());
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

        mode_.store(Mode::RECORDING, std::memory_order_relaxed);
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
        if (pending_region_.load(std::memory_order_acquire))
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
        auto* region = pending_region_.exchange(nullptr,
                                                std::memory_order_acq_rel);
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

            mode_.store(Mode::COMPILED, std::memory_order_relaxed);
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
    // mode_ is a status flag — real sync is pending_region_ acquire,
    // relaxed ordering is sufficient here (fg-thread-primary).
    // step_ is a monotonic counter advanced by bg thread on each region
    // transition; AtomicMonotonic lifts the monotonicity invariant to
    // the type level (no decrement, no reset, no stale CAS).
    std::atomic<Mode>               mode_{Mode::RECORDING};
    safety::AtomicMonotonic<uint64_t> step_{0};
    Arena                           load_arena_{1 << 20}; // for Cipher::load()

    // ─── Tier 1 dispatch state (fg thread only, except pending_region_) ─

    // NOT relaxed: bg→fg publish. bg writes region data, then
    // store(release). fg's load(acquire) in dispatch_op must see it.
    std::atomic<RegionNode*>        pending_region_{nullptr};
    RegionNode*                     pending_activation_{nullptr}; // fg-only: waiting for alignment
    uint32_t                        alignment_pos_{0};  // consecutive matched ops from region start
    CrucibleContext                 ctx_;               // fg-only replay
    RegionCache                     region_cache_;      // fg-only: cached alternate regions

    BackgroundThread                bg_;  // MUST be declared last
};

// Tier 2 opt-in: nothing inside Vigil may be a ScopedView.  The
// reflection walk proves that neither Vigil nor any of its fields
// (transitively, through known wrappers) stores a safety::ScopedView —
// views must not escape their construction scope.
static_assert(crucible::safety::no_scoped_view_field_check<Vigil>());

} // namespace crucible
