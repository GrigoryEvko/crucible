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
        uint64_t                scope_hash    = 0,
        uint64_t                callsite_hash = 0)
    {
        uint32_t meta_start = UINT32_MAX;
        if (metas && n_metas > 0) {
            meta_start = meta_log_->try_append(metas, n_metas);
        }
        return ring_->try_append(e, meta_start, scope_hash, callsite_hash);
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

    [[nodiscard]] uint64_t head_hash() const {
        return cipher_.has_value() ? cipher_->head() : 0;
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
    // Returns true if rollback succeeded.
    [[nodiscard]] bool rollback() {
        if (!tx_log_.rollback()) return false;
        // Restore active region pointer from the rolled-back transaction.
        const Transaction* tx = tx_log_.active();
        if (tx && tx->region) {
            bg_.active_region.store(tx->region, std::memory_order_release);
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
        const uint64_t hash = cipher_->store(r, meta_log_.get());
        if (hash == 0) return false;
        cipher_->advance_head(hash,
                              step_.load(std::memory_order_relaxed));
        return true;
    }

    // Load the most recent region from Cipher and activate it.
    // No-op if no Cipher or the Cipher is empty.
    [[nodiscard]] bool load() {
        if (!cipher_.has_value() || cipher_->empty()) return false;
        RegionNode* r = cipher_->load(cipher_->head(), load_arena_);
        if (!r) return false;
        bg_.active_region.store(r, std::memory_order_release);
        mode_.store(Mode::COMPILED, std::memory_order_release);
        return true;
    }

    // ─── Introspection ─────────────────────────────────────────────

    const TransactionLog<16>& tx_log()   const { return tx_log_; }
    TraceRing&                ring()           { return *ring_; }
    MetaLog&                  meta_log()       { return *meta_log_; }

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

        mode_.store(Mode::COMPILED, std::memory_order_release);

        // Pre-store the object (idempotent) so persist() is instant later.
        if (cipher_.has_value()) {
            (void)cipher_->store(region, meta_log_.get());
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
    BackgroundThread                bg_;  // MUST be declared last
};

} // namespace crucible
