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
#include <crucible/TraceRing.h>
#include <crucible/Transaction.h>

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

    // ─── Construction / Destruction ────────────────────────────────

    // Default constructor: no persistence, no distributed context.
    Vigil() : Vigil(Config{}) {}

    explicit Vigil(Config cfg) : cfg_(std::move(cfg)) {
        ring_ = std::make_unique<TraceRing>();
        ring_->reset();

        meta_log_ = std::make_unique<MetaLog>();
        meta_log_->reset();

        if (!cfg_.cipher_path.empty()) {
            cipher_.emplace(Cipher::open(cfg_.cipher_path));
        }

        // Wire the background thread callback to our on_region_ready.
        bg_.region_ready_cb = [this](RegionNode* r) { on_region_ready(r); };

        bg_.start(ring_.get(), meta_log_.get(),
                  cfg_.rank, cfg_.world_size, cfg_.device_capability);
    }

    ~Vigil() = default; // bg_ is declared last → destroyed first → stops thread

    Vigil(const Vigil&)             = delete("Vigil owns the runtime organism; not copyable");
    Vigil& operator=(const Vigil&)  = delete("Vigil owns the runtime organism; not copyable");

    // ─── Hot path: record one op (RECORDING mode) ──────────────────
    //
    // Called by the Vessel adapter for every ATen op.
    // Appends tensor metadata to MetaLog, then pushes a fingerprint to TraceRing.
    // Returns false if the ring or MetaLog is full (op silently dropped;
    // next iteration re-records everything).
    //
    // Hot path: ~5ns + MetaLog write (~10ns for metas) = ~15ns total.
    [[nodiscard]] bool record_op(
        const TraceRing::Entry& e,
        const TensorMeta*       metas,
        uint32_t                n_metas,
        ScopeHash               scope_hash    = {},
        CallsiteHash            callsite_hash = {})
    {
        MetaIndex meta_start;  // default = none()
        if (metas && n_metas > 0) {
            meta_start = meta_log_->try_append(metas, n_metas);
        }
        return ring_->try_append(e, meta_start, scope_hash, callsite_hash);
    }

    // ─── Per-op dispatch (Tier 1 entry point) ─────────────────────
    //
    // The Vessel adapter calls this once per ATen op. Returns:
    //   RECORD   → execute eagerly, Vigil recorded the op
    //   COMPILED → outputs pre-allocated, use output_ptr(j) / input_ptr(j)
    //
    // On divergence: deactivates CrucibleContext, falls back to RECORD,
    //   records the divergent op so the bg thread sees it.
    //
    // Activation lifecycle:
    //   1. bg thread signals pending_region_ (atomic)
    //   2. fg thread consumes it into pending_activation_ (fg-only)
    //   3. Alignment phase: scan for K consecutive matches against
    //      region ops[0..K-1] to find the iteration boundary
    //   4. Once aligned: activate CrucibleContext, advance engine
    //      past already-matched ops, enter COMPILED mode
    //   5. Subsequent ops take the fast COMPILED path (~2ns)
    [[nodiscard]] DispatchResult dispatch_op(
        const TraceRing::Entry& entry,
        const TensorMeta*       metas,
        uint32_t                n_metas,
        ScopeHash               scope_hash    = {},
        CallsiteHash            callsite_hash = {})
    {
        // ── COMPILED path (confirmed alignment, hot) ──
        if (ctx_.is_compiled()) [[likely]] {
            auto status = ctx_.advance(entry.schema_hash, entry.shape_hash);

            if (status == ReplayStatus::DIVERGED) [[unlikely]] {
                ctx_.deactivate();
                mode_.store(Mode::RECORDING, std::memory_order_relaxed);
                (void)record_op(entry, metas, n_metas, scope_hash, callsite_hash);
                return {DispatchResult::Action::RECORD, status, {}, 0};
            }

            return {DispatchResult::Action::COMPILED, status, {},
                    ctx_.engine().ops_matched()};
        }

        // ── RECORDING path ──
        (void)record_op(entry, metas, n_metas, scope_hash, callsite_hash);

        // Check if background thread signaled a ready region.
        auto* pending = pending_region_.load(std::memory_order_acquire);
        if (pending) [[unlikely]]
            consume_pending_region_();

        // ── Alignment phase (seek iteration boundary) ──
        if (pending_activation_) [[unlikely]]
            try_align_(entry.schema_hash, entry.shape_hash);

        return {DispatchResult::Action::RECORD, ReplayStatus::MATCH, {}, 0};
    }

    // ─── Queries (lock-free reads) ─────────────────────────────────

    [[nodiscard]] Mode mode() const {
        return mode_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool is_compiled() const { return mode() == Mode::COMPILED; }

    [[nodiscard]] const RegionNode* active_region() const {
        return bg_.active_region.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint64_t current_step() const {
        return step_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] ContentHash head_hash() const {
        return cipher_.has_value() ? cipher_->head() : ContentHash{};
    }

    // ─── Control ───────────────────────────────────────────────────

    // Spin-wait until TraceRing is drained (background thread has consumed
    // all pending entries). Useful in tests and after the last forward pass.
    // Returns when the ring is empty or after a 1-second timeout.
    void flush() {
        using namespace std::chrono;
        const auto deadline = steady_clock::now() + seconds(1);
        while (ring_->size() > 0) {
            if (steady_clock::now() > deadline) break;
            std::this_thread::sleep_for(microseconds(100));
        }
    }

    // Restore the previous SUPERSEDED transaction as the active one.
    // Also deactivates/re-activates CrucibleContext as needed.
    // Returns true if rollback succeeded.
    [[nodiscard]] bool rollback() {
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
        const RegionNode* r = active_region();
        if (!r) return false;
        // crucible::replay() is defined in MerkleDag.h.
        // RegionNode : TraceNode, so the pointer upcast is implicit.
        return crucible::replay(
            const_cast<RegionNode*>(r),
            std::forward<GuardEval>(eval_guard),
            std::forward<RegionExec>(exec_region));
    }

    // ─── Persistence ───────────────────────────────────────────────

    // Serialize the active region to Cipher and advance HEAD.
    // No-op if cipher_path was not set in Config.
    // Returns true if the region was successfully stored.
    [[nodiscard]] bool persist() {
        if (!cipher_.has_value()) return false;
        const RegionNode* r = active_region();
        if (!r) return false;
        const ContentHash hash = cipher_->store(r, meta_log_.get());
        if (!hash) return false;
        cipher_->advance_head(hash,
                              step_.load(std::memory_order_relaxed));
        return true;
    }

    // Load the most recent region from Cipher and activate it.
    // No-op if no Cipher or the Cipher is empty.
    // Also activates CrucibleContext if the region has a MemoryPlan.
    [[nodiscard]] bool load() {
        if (!cipher_.has_value() || cipher_->empty()) return false;
        RegionNode* r = cipher_->load(cipher_->head(), load_arena_);
        if (!r) return false;
        bg_.active_region.store(r, std::memory_order_release);
        mode_.store(Mode::COMPILED, std::memory_order_release);
        // Activate per-op replay if the loaded region has a plan.
        if (ctx_.activate(r))
            register_externals_from_region_(r);
        return true;
    }

    // ─── CrucibleContext forwarding (Tier 1 compiled replay) ───────

    // Pre-allocated output pointer for output j of the current op.
    // Valid only after dispatch_op() returned COMPILED with MATCH/COMPLETE.
    [[nodiscard]] void* output_ptr(uint16_t j) const { return ctx_.output_ptr(j); }

    // Pre-allocated input pointer for input j of the current op.
    [[nodiscard]] void* input_ptr(uint16_t j) const { return ctx_.input_ptr(j); }

    // Register an external tensor's data pointer with the pool.
    void register_external(SlotId sid, void* ptr) { ctx_.register_external(sid, ptr); }

    // Number of complete iterations replayed in COMPILED mode.
    [[nodiscard]] uint32_t compiled_iterations() const { return ctx_.compiled_iterations(); }

    // Number of divergences detected during COMPILED replay.
    [[nodiscard]] uint32_t diverged_count() const { return ctx_.diverged_count(); }

    // Direct access to the CrucibleContext (diagnostics, not hot path).
    [[nodiscard]] const CrucibleContext& context() const { return ctx_; }

    // ─── Introspection ─────────────────────────────────────────────

    const TransactionLog<16>& tx_log()   const { return tx_log_; }
    TraceRing&                ring()           { return *ring_; }
    MetaLog&                  meta_log()       { return *meta_log_; }

    // Background thread diagnostics.
    [[nodiscard]] uint32_t bg_iterations_completed() const { return bg_.iterations_completed; }
    [[nodiscard]] uint32_t bg_last_iteration_length() const { return bg_.last_iteration_length; }
    [[nodiscard]] uint32_t bg_detector_boundaries() const { return bg_.detector.boundaries_detected; }
    [[nodiscard]] bool     bg_detector_confirmed() const { return bg_.detector.confirmed; }

 private:
    // ─── Background thread callback ────────────────────────────────
    //
    // Called on the background thread when a new RegionNode is ready.
    // Transitions the transaction to ACTIVE, updates the execution mode,
    // and optionally pre-stores the object in the Cipher.
    void on_region_ready(RegionNode* region) {
        const uint64_t step = step_.fetch_add(1, std::memory_order_relaxed);

        auto* tx = tx_log_.begin_tx(step);
        tx_log_.commit(tx, region,
                       region->content_hash,
                       region->merkle_hash);
        (void)tx_log_.activate(tx);

        // Signal fg thread: a region with a MemoryPlan is available.
        // fg thread picks it up in dispatch_op() via try_activate_().
        // Also set mode_=COMPILED for backward compat — existing code/tests
        // poll is_compiled() without calling dispatch_op().
        pending_region_.store(region, std::memory_order_release);
        mode_.store(Mode::COMPILED, std::memory_order_release);

        // Pre-store the object (idempotent) so persist() is instant later.
        if (cipher_.has_value()) {
            (void)cipher_->store(region, meta_log_.get());
        }
    }

    // ─── Private helpers ────────────────────────────────────────────

    // Consume the bg→fg pending region into fg-only alignment state.
    // Does NOT activate CrucibleContext — alignment phase handles that.
    void consume_pending_region_() {
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
    void try_align_(SchemaHash schema, ShapeHash shape) {
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
        static constexpr uint32_t ALIGNMENT_K = 5;
        const uint32_t threshold = (region->num_ops < ALIGNMENT_K)
                                 ? region->num_ops : ALIGNMENT_K;

        if (alignment_pos_ >= threshold) {
            if (!ctx_.activate(region)) {
                // No plan → can't compile. Clear pending state.
                pending_activation_ = nullptr;
                return;
            }

            register_externals_from_region_(region);

            // Advance the engine past the ops we already matched during
            // alignment. These ops executed eagerly; the engine needs to
            // be at position alignment_pos_ so the NEXT op checks against
            // the correct region op.
            for (uint32_t i = 0; i < alignment_pos_; i++) {
                auto s = ctx_.advance(region->ops[i].schema_hash,
                                      region->ops[i].shape_hash);
                // Must match — we verified these during alignment.
                assert(s == ReplayStatus::MATCH || s == ReplayStatus::COMPLETE);
                (void)s;
            }

            mode_.store(Mode::COMPILED, std::memory_order_relaxed);
            pending_activation_ = nullptr;
        }
    }

    // Walk region ops to find external slot data_ptrs from recorded
    // TensorMeta. O(num_ext × num_ops × max_inputs) — cold path,
    // runs once per activation.
    void register_externals_from_region_(const RegionNode* region) {
        if (!region->plan) return;

        for (uint32_t s = 0; s < region->plan->num_slots; s++) {
            if (!region->plan->slots[s].is_external) continue;

            SlotId target = region->plan->slots[s].slot_id;
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

            if (ptr) ctx_.register_external(target, ptr);
        }
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
    std::atomic<Mode>               mode_{Mode::RECORDING};
    std::atomic<uint64_t>           step_{0};
    Arena                           load_arena_{1 << 20}; // for Cipher::load()

    // ─── Tier 1 dispatch state (fg thread only, except pending_region_) ─

    std::atomic<RegionNode*>        pending_region_{nullptr}; // bg→fg signal
    RegionNode*                     pending_activation_{nullptr}; // fg-only: waiting for alignment
    uint32_t                        alignment_pos_{0};  // consecutive matched ops from region start
    CrucibleContext                 ctx_;               // fg-only replay

    BackgroundThread                bg_;  // MUST be declared last
};

} // namespace crucible
