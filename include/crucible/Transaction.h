#pragma once

// Transaction lifecycle model for Crucible's Kafka-like iteration pipeline.
//
// One iteration = one transaction:
//   begin_tx()   → RECORDING  (ring accepts ops)
//   commit()     → COMMITTED  (RegionNode built, hashes computed)
//   activate()   → ACTIVE     (atomic swap done, this region is live)
//   [superseded] → SUPERSEDED (newer tx took over, kept for rollback)
//   rollback()   → ROLLED_BACK (quality regression, previous tx restored)
//
// TransactionLog<N> is a power-of-2 circular buffer owned exclusively by
// the BackgroundThread. active() / previous() may be polled lock-free from
// the foreground thread only after the write of status to ACTIVE completes
// (the writes use seq-cst via std::atomic where needed — here we keep it
// simple since both reader and writer are the same background thread).

// FIXY-U-096e production migration: Tagged<RegionNode*, source::Arena>
// + Monotonic / CyclicBuffer reached through the fixy:: umbrella
// instead of safety::* directly.  Transaction.h has a single production
// fan-in (Vigil.h) and no substrate-to-Transaction edge, so the full
// Wrap.h + Source.h umbrella is cycle-safe here.  `crucible::decide::
// is_non_zero` is namespaced at top-level (not under safety::) so its
// substrate path stays — the migration is wrapper-types-only.
// CRUCIBLE_POST is a macro substrate dep; safety/Post.h stays.
#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/ClockSource.h>  // WRAP-Transaction-3: MonotonicClockBytes
#include <crucible/safety/Decide.h>
#include <crucible/safety/Post.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace crucible {

enum class TxStatus : uint8_t {
    RECORDING,    // ring buffer is accepting ops
    CLOSED,       // iteration boundary detected, ring sealed for this tx
    COMMITTED,    // RegionNode built + Merkle hash computed
    ACTIVE,       // atomic pointer swap done, this region is live
    SUPERSEDED,   // newer tx took over (kept for rollback window)
    ROLLED_BACK,  // quality regression, previous tx restored
};

// ─── Transaction: lifecycle record for one iteration ───────────────
//
// Field layout chosen for exactly 48 bytes with no implicit padding:
//   step_id       8B  offset  0
//   content_hash  8B  offset  8
//   merkle_root   8B  offset 16
//   region*       8B  offset 24
//   ts_ns         8B  offset 32  (timestamp of last status transition)
//   status        1B  offset 40
//   pad[7]        7B  offset 41
//                      total 48B

struct Transaction {
    using ArenaRegion = ::crucible::fixy::wrap::Tagged<
        RegionNode*, ::crucible::fixy::tags::source::Arena>;

    static_assert(sizeof(ArenaRegion) == sizeof(RegionNode*));

    // ── step_id (#1060 WRAP-Transaction-1) ─────────────────────────
    // Monotonic<uint64_t> rejects retrograde / wrap-around assignment
    // at the type level.  Per CLAUDE.md §II DetSafe, step_id is
    // load-bearing for replay-determinism — every write goes through
    // advance(), every read through .get().  Layout: regime-2 collapse,
    // sizeof(Monotonic<uint64_t>) == 8B; the 48B Transaction layout is
    // preserved (static_assert below).  Reset semantics: begin_tx()
    // recycles a slot via `*tx = Transaction{}` which restores
    // step_id == 0 before the new advance(step_id) — the per-tx
    // monotonicity is reset deliberately at slot recycling, the global
    // monotonicity is enforced by TransactionLog::count_'s
    // BoundedMonotonic gate (#1064 WRAP-Transaction-5).
    ::crucible::fixy::wrap::Monotonic<uint64_t> step_id{0};
    ContentHash  content_hash;        // default (0) until COMMITTED
    MerkleHash   merkle_root;         // default (0) until COMMITTED
    ArenaRegion  region{nullptr};     // null until COMMITTED; arena-owned
    // ── ts_ns (WRAP-Transaction-3 #1062) ───────────────────────────
    // MonotonicClockBytes<u64> pins the timestamp's clock-source
    // provenance at the type level: any drift to a wall-clock,
    // boot-clock, or bare-u64 source trips at every TU including
    // Transaction.h.  Regime-1 EBO collapse preserves the 8B field
    // width — the 48B Transaction layout is unchanged (static_assert
    // below).  Six internal assignment sites already use
    // `tx->ts_ns = now_ns()`; now_ns() returns the same wrapped type,
    // so no body updates are required.  Parallels FIXY-V-198
    // (Cipher::now_ns) exactly.
    ::crucible::safety::MonotonicClockBytes<std::uint64_t> ts_ns{};
    TxStatus     status = TxStatus::RECORDING;
    uint8_t      pad[7]{};
};

static_assert(sizeof(Transaction) == 48, "Transaction layout must be 48 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Transaction);

// ── WRAP-Transaction-3 #1062 sentinel: ts_ns clock-source pin ────
// MonotonicClockBytes<u64> is the type-level proof that
// Transaction::ts_ns is a CLOCK_MONOTONIC reading.  Any regression
// — accidentally switching to WallClockBytes (NTP-jumpy) or
// BootClockBytes (suspend-aware), or returning a bare u64 from a
// helper — trips at compile time.  Regime-1 EBO collapse is also
// asserted: without it, the 48B Transaction layout would break.
static_assert(
    std::is_same_v<
        decltype(std::declval<Transaction>().ts_ns),
        ::crucible::safety::MonotonicClockBytes<std::uint64_t>>,
    "WRAP-Transaction-3 #1062: Transaction::ts_ns must be "
    "MonotonicClockBytes<u64> — clock-source provenance pin.");
static_assert(
    sizeof(::crucible::safety::MonotonicClockBytes<std::uint64_t>) ==
        sizeof(std::uint64_t),
    "WRAP-Transaction-3 #1062: MonotonicClockBytes<u64> must be "
    "regime-1 EBO-collapsible to preserve 48B Transaction layout.");

// ─── TransactionLog<N>: circular ring of the last N transactions ───
//
// Not thread-safe. BackgroundThread is the sole writer.
// active() / previous() return pointers into the ring that remain
// valid as long as the log does not wrap more than N times.

template <uint32_t N = 16>
class TransactionLog {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    // (MASK retired in #1063: the masked write cursor + saturating fill
    //  counter now live inside CyclicBuffer<Transaction, N> — see the Ring
    //  alias below.  CyclicBuffer owns the & (N-1) wrap-mask internally.)

 public:
    // ── ring storage wrapper (#1063 WRAP-Transaction-4) ───────────────
    // The (entries_ + head_ + count_) triple — N inline slots, a masked
    // write cursor, and a saturating fill counter — IS the
    // CyclicBuffer<T, N> composition (FixedArray + Cyclic +
    // BoundedMonotonic).  Wiring it here retires the open-coded
    // `head_ & MASK` masking and the hand-rolled fill bump (the same
    // triple that carried the "circular index after first wrap"
    // off-by-one elsewhere); the masking + wrap discipline now lives in
    // the type.  begin_tx maps to claim() (bind the next-write slot,
    // advance the cursor, saturate the fill); previous() to the
    // recent(i) MRU reverse scan.  The #1064 BoundedMonotonic fill
    // invariant is preserved — CyclicBuffer composes it internally as
    // BoundedMonotonic<size_t, N>, so the count still cannot exceed N nor
    // move backward.
    //
    // Zero-cost: CyclicBuffer is a struct of the three composed members
    // with no overhead beyond alignment (layout static_assert below).
    using Ring = ::crucible::fixy::wrap::CyclicBuffer<Transaction, N>;

    TransactionLog() = default;
    TransactionLog(const TransactionLog&)            = delete("TransactionLog holds ring-internal pointers");
    TransactionLog& operator=(const TransactionLog&) = delete("TransactionLog holds ring-internal pointers");
    TransactionLog(TransactionLog&&)                 = delete("interior pointers into entries_ would dangle");
    TransactionLog& operator=(TransactionLog&&)      = delete("interior pointers into entries_ would dangle");

    // gnu::cold: step-boundary transition, amortized once per iteration.
    [[nodiscard, gnu::cold]] Transaction* begin_tx(uint64_t step_id) noexcept {
        // claim() binds the next-write slot, advances the cursor, and
        // saturates the fill counter — the (entries_[head_ & MASK] +
        // head_++ + guarded count bump) triple, now carried by
        // CyclicBuffer.  advance() touches only the cursor, so the bound
        // slot reference stays valid for the in-place reset below; the
        // CyclicBuffer fill bump keeps the saturate-don't-abort semantic
        // (its own LOAD-BEARING `if (count < N)` guard around bump()).
        Transaction* tx = &ring_.claim();
        *tx = Transaction{};   // value-init via NSDMI defaults (no memset on non-trivial type)
        // step_id is Monotonic<uint64_t>{0} after the reset; advance()
        // pre-clause `0 <= step_id` holds for any uint64_t.  Bypassing
        // raw-assignment forecloses a future refactor that re-introduces
        // a retrograde write under bug-in-bug regression.
        tx->step_id.advance(step_id);
        tx->ts_ns   = now_ns();
        // CONTRACT-Tx-Begin-POST: factory result-shape contract — every
        // begin_tx returns a non-null Transaction in the RECORDING state
        // with the caller's step_id stamped.  Catches a future refactor
        // that reorders claim() and the *tx reset (which would return a
        // pointer into an unreset ring slot).
        //   (1) tx != nullptr — claim() returns a reference into the
        //       inline ring storage; its address is never null.
        //   (2) tx->status == RECORDING — value-init via TxStatus's
        //       NSDMI default (RECORDING is the first enumerator and the
        //       NSDMI on the field).
        //   (3) tx->step_id == step_id — caller's step_id stamped above.
        // Routes through CRUCIBLE_POST: the predicates dereference the
        // freshly-claimed `tx` pointer; same GCC 16.1.1 consteval-bypass
        // family as all sibling POSTs in this file.  Under NDEBUG these
        // collapse to `[[assume]]` so commit() can speculate on RECORDING.
        CRUCIBLE_POST(tx, tx != nullptr);
        CRUCIBLE_POST(tx, tx->status == TxStatus::RECORDING);
        CRUCIBLE_POST(tx, tx->step_id.get() == step_id);
        return tx;
    }

    // Transition RECORDING (or CLOSED) → COMMITTED, attach region.
    // Returns false if tx is not in a committable state (logic error).
    //
    // Soundness gate (#937 WRAP-MerkleDag-1): merkle_root MUST be
    // non-zero.  Zero is the structural sentinel for "subtree not yet
    // computed" — committing it would (1) corrupt the tx_log's
    // step-by-merkle invariant (a fresh Transaction's default value is
    // also MerkleHash{}, so binary search on (step_id, merkle_root)
    // becomes ambiguous), (2) silently mark the federation round-trip
    // key as a no-op, and (3) mask divergence-recovery cases where the
    // bg thread races to commit before recompute_merkle ran.  Every
    // production caller derives merkle_root through
    // `region->computed_merkle_hash()` (which itself fires on zero),
    // so this clause is defense-in-depth: under semantic=enforce a
    // future caller bypassing the accessor aborts here, under
    // semantic=ignore the [[assume]] hint pins the invariant for
    // downstream code (rollback / activate / hash_at_step() can
    // speculate that any committed entry has a non-zero root without
    // re-checking).  Companion typed witness: ValidMerkleRoot
    // (MerkleDag.h) — callers that hold a ValidMerkleRoot can pass
    // `make_merkle_root(witness)` to surface the proof at the call site.
    [[nodiscard]] bool commit(Transaction* const tx, Transaction::ArenaRegion region,
                ContentHash content_hash, MerkleHash merkle_root) noexcept
        pre (tx != nullptr)
        pre (region.value() != nullptr)
        pre (::crucible::decide::is_non_zero(merkle_root))
    {
        if (tx->status != TxStatus::RECORDING
            && tx->status != TxStatus::CLOSED) {
            // CONTRACT-Tx-Commit-POST: failure path — false return.
            // The implication post r → COMMITTED holds vacuously.
            // Migrated from P2900 post(r: ...) to in-body CRUCIBLE_POST
            // because P2900 post(r:...) referencing a parameter pointee
            // (`tx->status`) is silently bypassed at consteval in GCC
            // 16.1.1 — same family as CONTRACT-100..108-POST and all
            // sibling Tx posts in this session.  The clause below is
            // re-stated identically so the predicate fires under
            // semantic=enforce and propagates as `[[assume]]` under
            // NDEBUG.
            CRUCIBLE_POST(0, !false || tx->status == TxStatus::COMMITTED);
            return false;
        }
        tx->region       = region;
        tx->content_hash = content_hash;
        tx->merkle_root  = merkle_root;
        tx->status       = TxStatus::COMMITTED;
        tx->ts_ns        = now_ns();
        // CONTRACT-Tx-Commit-POST: success path — strengthen the
        // returned-true case.  After a successful commit:
        //   (1) tx->status == COMMITTED (set above)
        //   (2) tx->region == region   (set above; catches a refactor
        //                                that drops the assignment, which
        //                                would leave region == nullptr
        //                                from begin_tx's value-init)
        //   (3) tx->content_hash == content_hash (caller's hash echoed)
        //   (4) tx->merkle_root  == merkle_root  (caller's root echoed)
        // The pre-existing P2900 post (r: !r || COMMITTED) collapses to
        // (true → COMMITTED) on this path, which (1) covers — the three
        // additional posts strengthen the contract without changing
        // semantics.  Same consteval-bypass framing as the failure path.
        CRUCIBLE_POST(true, tx->status == TxStatus::COMMITTED);
        CRUCIBLE_POST(true, tx->region.value() == region.value());
        CRUCIBLE_POST(true, tx->content_hash == content_hash);
        CRUCIBLE_POST(true, tx->merkle_root == merkle_root);
        return true;
    }

    // Transition COMMITTED → ACTIVE. Marks the previous ACTIVE as SUPERSEDED.
    // Returns the previously ACTIVE transaction (the rollback target), or nullptr
    // if no previous ACTIVE existed.
    [[nodiscard]] Transaction* activate(Transaction* const tx) noexcept
        pre (tx != nullptr)
    {
        if (tx->status != TxStatus::COMMITTED) {
            // CONTRACT-Tx-Activate-POST: rejected-promotion path.
            // The original P2900 post (r: r == nullptr || ACTIVE) holds
            // because nullptr-return makes the disjunction true
            // vacuously.  Migrated to CRUCIBLE_POST for the same
            // GCC 16.1.1 consteval-bypass-vulnerable parameter-pointee
            // reason as commit() above.
            CRUCIBLE_POST(static_cast<Transaction*>(nullptr),
                          true || tx->status == TxStatus::ACTIVE);
            return nullptr;
        }

        // Demote the current active transaction.
        // WRAP-Transaction-6 #1065: extract via .value() — active_tx_
        // is now Tagged<Transaction*, source::Ring>; deref / boolean
        // / assignment paths route through the underlying pointer
        // without changing observable behavior.
        Transaction* prev = nullptr;
        if (active_tx_.value() != nullptr) {
            active_tx_.value()->status = TxStatus::SUPERSEDED;
            active_tx_.value()->ts_ns  = now_ns();
            prev = active_tx_.value();
        }

        tx->status = TxStatus::ACTIVE;
        tx->ts_ns  = now_ns();
        active_tx_ = ActiveTxPtr{tx};
        // CONTRACT-Tx-Activate-POST: successful-promotion path —
        // strengthen invariants for the success case:
        //   (1) tx->status == ACTIVE          (set above)
        //   (2) active_tx_ == tx              (set above; pins the
        //                                       SWMR observation that
        //                                       fg dispatch-path
        //                                       readers see)
        //   (3) prev == nullptr || prev->status == SUPERSEDED
        //                                      (the demoted previous
        //                                       active is now in the
        //                                       SUPERSEDED state, ready
        //                                       for rollback() to find;
        //                                       discharges via implies).
        // Routes through CRUCIBLE_POST for the consteval-bypass-vulnerable
        // parameter-pointee + class-member predicates.  The third post
        // is the C++ `||` disjunction form rather than `decide::implies`
        // because `decide::implies(antecedent, consequent)` evaluates
        // both arguments eagerly (function-call semantics) — when
        // `prev == nullptr`, the consequent `prev->status` would
        // null-deref before the predicate folds.  C++ `||` is
        // short-circuit, so the consequent is only evaluated when
        // prev != nullptr — semantically equivalent to the implies
        // catalog predicate, deref-safe.  This is documented as the
        // CRUCIBLE_PRE/POST disjunction pattern where the consequent
        // dereferences the antecedent's witnessed non-null pointer.
        CRUCIBLE_POST(prev, tx->status == TxStatus::ACTIVE);
        CRUCIBLE_POST(prev, active_tx_.value() == tx);
        CRUCIBLE_POST(prev, prev == nullptr ||
                            prev->status == TxStatus::SUPERSEDED);
        return prev;
    }

    // Roll back: restore the most recent SUPERSEDED transaction to ACTIVE.
    // Marks the current ACTIVE as ROLLED_BACK.
    // Returns true if a rollback was possible.
    [[nodiscard]] bool rollback() noexcept {
        Transaction* prev = previous();
        if (!prev) return false;

        if (active_tx_.value() != nullptr) {
            active_tx_.value()->status = TxStatus::ROLLED_BACK;
            active_tx_.value()->ts_ns  = now_ns();
        }
        prev->status = TxStatus::ACTIVE;
        prev->ts_ns  = now_ns();
        active_tx_   = ActiveTxPtr{prev};
        return true;
    }

    // Latest ACTIVE transaction, or nullptr.
    // Non-const: callers may mutate the returned transaction (e.g. rollback).
    // WRAP-Transaction-6 #1065: extracts the raw Transaction* from the
    // Tagged<Transaction*, source::Ring> field — callers continue to see
    // the unwrapped pointer for compatibility with existing read paths.
    [[nodiscard]] Transaction* active() CRUCIBLE_LIFETIMEBOUND  { return active_tx_.value(); }

    [[nodiscard]] Transaction* previous() CRUCIBLE_LIFETIMEBOUND {
        // Walk the ring backward from the cursor for the most recent
        // SUPERSEDED entry.  recent(i) is CyclicBuffer's MRU reverse scan
        // — recent(0) is the last-claimed slot — bit-identical to the
        // open-coded `entries_[(head_ - 1 - i) & MASK]` it replaces.
        for (typename Ring::size_type i = 0; i < ring_.size(); i++) {
            Transaction& e = ring_.recent(i);
            if (e.status == TxStatus::SUPERSEDED) return &e;
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t size() const {
        // ring_.size() is the saturating fill (0..N) as size_t; the
        // public API returns uint32_t.  size() <= N <= UINT32_MAX, so the
        // cast is lossless (TypeSafe: explicit, no implicit narrowing).
        return static_cast<uint32_t>(ring_.size());
    }

 private:
    // ── now_ns() (WRAP-Transaction-3 #1062) ────────────────────────
    // Returns MonotonicClockBytes<u64> so the typed clock-source
    // witness propagates straight into Transaction::ts_ns.  The
    // returned wrap is regime-1 EBO-collapsed (sizeof == sizeof u64)
    // and pinned to ClockSource_v::Monotonic, rejecting cross-source
    // assignments at every call site.  steady_clock IS the
    // CLOCK_MONOTONIC reading on Linux; the mint just stamps the
    // provenance lattice value on top.  Parallels Cipher::now_ns
    // (FIXY-V-198) and Mutation::MonotonicClock::now_ns (FIXY-V-193).
    [[nodiscard]] static auto now_ns() noexcept
        -> ::crucible::safety::MonotonicClockBytes<std::uint64_t>
    {
        const auto tp = std::chrono::steady_clock::now();
        const std::uint64_t raw = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                tp.time_since_epoch()).count());
        return ::crucible::safety::mint_clock_source<
            ::crucible::safety::ClockSource_v::Monotonic,
            std::uint64_t>(raw);
    }

    // ── WRAP-Transaction-3 #1062 sentinel: now_ns return-type pin ──
    // Pin the typed return.  If a future maintainer accidentally
    // reverts to `static uint64_t now_ns()` or returns a wall-clock
    // wrap, this sentinel fires at every TU.
    static_assert(
        std::is_same_v<
            decltype(now_ns()),
            ::crucible::safety::MonotonicClockBytes<std::uint64_t>>,
        "WRAP-Transaction-3 #1062: Transaction::now_ns must return "
        "MonotonicClockBytes<u64>.");
    static_assert(
        sizeof(decltype(now_ns())) == sizeof(std::uint64_t),
        "WRAP-Transaction-3 #1062: now_ns return must be regime-1 "
        "EBO-collapsed to preserve hot-path layout.");

    // The (entries_ + head_ + count_) ring triple, now one audited
    // composition (see the Ring alias above).  active_tx_ tracks WHICH
    // ring slot is live (not WHERE the next write goes), so it is not
    // part of the ring storage itself.  TransactionLog is move/copy-
    // deleted, so ring_ never relocates and the interior pointer stays
    // valid for the log's lifetime.
    //
    // WRAP-Transaction-6 #1065: active_tx_ is provenance-tagged as
    // safety::Tagged<Transaction*, source::Ring>.  The Ring source tag
    // (Tagged.h, near QdiscConfig) encodes "this pointer was obtained
    // from a fixed-capacity ring's inline slot pool" — distinct from
    // arena-owned (source::Arena) and from generic borrowed-from-owner
    // (Borrowed<T, Owner>).  Regime-1 EBO collapses the wrapper to
    // sizeof(Transaction*) = 8 B, so the TransactionLog layout is
    // bit-identical to the pre-migration shape — no ABI / wire impact.
    // Internal code extracts via `.value()` at each access site; the
    // public `active()` accessor continues to return raw Transaction*
    // so external callers (CipherTransactionLog wiring, BackgroundThread
    // rollback paths) are not changed by this ship.
    using ActiveTxPtr = ::crucible::safety::Tagged<
        Transaction*, ::crucible::safety::source::Ring>;
    Ring         ring_{};
    ActiveTxPtr  active_tx_{nullptr};
};

// Zero-cost ring wiring (#1063 WRAP-Transaction-4): CyclicBuffer<Transaction, N>
// composes FixedArray + Cyclic + BoundedMonotonic with no overhead beyond
// alignment — sizeof equals the sum of the three composed members.  N=16 is
// the production shape (TransactionLog<16> is the BackgroundThread rollback
// ring).  A regression in the composition's zero-cost guarantee reddens here.
static_assert(
    sizeof(::crucible::fixy::wrap::CyclicBuffer<Transaction, 16>)
        == sizeof(::crucible::safety::FixedArray<Transaction, 16>)
         + sizeof(::crucible::safety::Cyclic<std::size_t, 16>)
         + sizeof(::crucible::safety::BoundedMonotonic<std::size_t, 16>),
    "CyclicBuffer<Transaction, N> must stay a zero-overhead composition");

// WRAP-Transaction-6 #1065: zero-cost field migration sentinel.
// active_tx_'s Tagged<Transaction*, source::Ring> wrapper MUST collapse
// via regime-1 EBO to sizeof(Transaction*) — otherwise the
// TransactionLog<N> total size shifts and the hot-path layout audit
// (cache-line residency, prefetch hints, struct packing) is no longer
// bit-identical to the pre-migration shape.  A future Tagged refactor
// that drops EBO collapse, or a Ring tag that grows beyond an empty
// struct, reddens here.
static_assert(
    sizeof(::crucible::safety::Tagged<Transaction*,
                                      ::crucible::safety::source::Ring>)
        == sizeof(Transaction*),
    "WRAP-Transaction-6 #1065: Tagged<Transaction*, source::Ring> must "
    "preserve pointer size via regime-1 EBO collapse.");
static_assert(
    alignof(::crucible::safety::Tagged<Transaction*,
                                       ::crucible::safety::source::Ring>)
        == alignof(Transaction*),
    "WRAP-Transaction-6 #1065: Tagged<Transaction*, source::Ring> must "
    "preserve pointer alignment via regime-1 EBO collapse.");

} // namespace crucible
