#pragma once

// ═══════════════════════════════════════════════════════════════════
// FairSharedPermissionPool<Tag, BurstLimit>
// — bounded-overtaking fairness layer over SharedPermissionPool
//
// SharedPermissionPool's protocol guarantees CORRECTNESS (no torn
// state, no two writers, no writer + readers simultaneous) but makes
// NO LIVENESS or FAIRNESS claim.  Whoever wins the next atomic CAS
// wins.  No queue, no priority, no scheduler-fairness promise.  In
// production this manifests as: a spin-loop writer that's pinned to a
// core can monopolize the pool indefinitely, starving every reader.
//
// FairSharedPermissionPool layers a BURST LIMIT over the underlying
// pool: the writer's `try_upgrade()` succeeds AT MOST `BurstLimit`
// times in a row before being forced to yield to readers.  Once any
// reader's `lend()` succeeds, the burst counter resets and the
// writer regains its full burst budget.
//
// THE GUARANTEE (under contention):
//
//   PER-POOL bounded overtaking: between any two successful reader
//   lends (from ANY reader), the writer wins AT MOST `BurstLimit`
//   times.  Equivalently: the wrapper's internal counter
//   `consecutive_writer_wins` is always ≤ BurstLimit on the gated
//   path.  No POOL-level monopolization possible.
//
//   This is NOT per-reader fairness.  With N concurrent readers, an
//   unlucky reader R may be overtaken by up to N·BurstLimit writer
//   wins between two of its OWN successful lends — because each
//   intervening reader (R', R'', ...) resets the counter on R's
//   behalf, allowing the writer K more wins before R itself gets in.
//
//   Per-reader FIFO fairness requires queued waiters (a ticket-lock
//   primitive) and is intentionally NOT shipped here — it has a
//   different cost model and a different correctness story.  Use
//   this wrapper when "no pool-level monopolization" is enough; use
//   a FIFO ticket lock when "per-thread bounded wait" is required.
//
// THE COST:
//
//   Hot path: ONE additional acquire-load on `try_upgrade()`, ONE
//   acq_rel fetch_add on success, ONE release-store on `lend()`
//   success.  All on a single cache-line-aligned counter.  ~2-3 ns
//   amortized; negligible vs the inner pool's CAS cost.
//
// ─── When to use ────────────────────────────────────────────────────
//
//   * SWMR mode-transition pattern where readers MUST eventually get
//     CPU time even under aggressive writer pressure.
//   * Production deployments where writer threads may be pinned to a
//     core / boosted in priority and could otherwise starve readers.
//   * Any pool where you want a TYPE-LEVEL guarantee of bounded
//     reader latency, not just an empirical one.
//
// ─── When NOT to use ────────────────────────────────────────────────
//
//   * Pure throughput-bound writer with no readers: the burst limit
//     introduces unnecessary latency.  Use plain SharedPermissionPool.
//   * Strict FIFO ordering required: this primitive gives bounded
//     overtaking, not FIFO.  A FIFO ticket lock is a different
//     primitive (not shipped here).
//   * Single-thread or naturally-fair primitives (MpmcRing's FAA,
//     ChaseLevDeque's owner/thief separation, AtomicSnapshot's
//     wait-free reads): no fairness wrapper needed.
//
// ─── The escape hatch (for the no-reader case) ─────────────────────
//
// Edge case: writer hits the burst limit, but no reader is actually
// contending.  The writer would be starved waiting for a reader that
// never comes.  Solution: `try_upgrade_unchecked()` bypasses the
// burst gate.  Call after backoff / poll if `outstanding() == 0`
// indicates no readers are present.  The unchecked path STILL
// increments the burst counter, so even bypass calls are observable
// in `consecutive_writer_wins()` for diagnostics.
//
// Idiomatic writer hot-path:
//
//   for (;;) {
//       auto u = pool.try_upgrade();
//       if (u) { /* exclusive section */; break; }
//       if (pool.outstanding() == 0 && !pool.is_exclusive_out()) {
//           // No actual reader contention; bypass fairness gate.
//           auto u2 = pool.try_upgrade_unchecked();
//           if (u2) { /* exclusive section */; break; }
//       }
//       std::this_thread::yield();
//   }
//
// ─── Bounded-overtaking proof sketch ───────────────────────────────
//
// Let N = BurstLimit.  Assume a reader R is contending (calling
// lend() in a loop with bounded inter-call latency).
//
//   * After at most N successful writer upgrades in a row,
//     try_upgrade() returns nullopt unconditionally.
//   * The writer's deposit_exclusive() then clears the inner pool's
//     state.  Subsequent try_upgrade() calls return nullopt by the
//     burst gate.
//   * R's lend() call now finds inner state == 0, succeeds, and
//     resets the burst counter via the wrapper.
//   * R has been overtaken AT MOST N times.  ∎
//
// The bound holds independent of OS scheduling: even if the writer
// is pinned and never preempted, the writer's `try_upgrade()` will
// return nullopt after N wins, and the writer must call
// deposit_exclusive() to clear the inner state, which happens in
// bounded time (the writer's exclusive section is bounded by
// caller).  The reader then gets a window to lend.
//
// ─── Axiom coverage ────────────────────────────────────────────────
//
//   TypeSafe   — Tag identifies the protected region; cannot be
//                interchanged with other Tags' pools.
//   InitSafe   — atomic counter NSDMI-initialized to 0.
//   MemSafe    — composition over Pinned inner; wrapper itself Pinned.
//   BorrowSafe — guards / Permission tokens flow through unchanged.
//   ThreadSafe — counter ops are acq_rel; pair with inner pool's atomics.
//   LeakSafe   — no resource ownership; just a counter.
//   DetSafe    — same inputs → same outputs (modulo the inner pool's
//                CAS resolution).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

namespace crucible::safety {

// ── FairSharedPermissionPool<Tag, BurstLimit> ──────────────────────
//
// BurstLimit = N: writer can win at most N times in a row before the
// fairness gate refuses further upgrades.  Reasonable defaults:
//
//   N = 1   — strict alternation (one writer, then one reader; max
//             fairness, max contention overhead).
//   N = 8   — default; balances throughput vs reader latency.
//   N = 32  — write-burst-friendly; tolerates mostly-writer workloads
//             with occasional reader checks.
//   N = 0   — INVALID; writer would never be allowed.  Static-asserted.

template <typename Tag, std::uint32_t BurstLimit = 8>
class FairSharedPermissionPool : public Pinned<FairSharedPermissionPool<Tag, BurstLimit>> {
public:
    using tag_type = Tag;
    static constexpr std::uint32_t writer_burst_limit = BurstLimit;

    static_assert(BurstLimit > 0,
        "FairSharedPermissionPool BurstLimit must be > 0; "
        "BurstLimit=0 would forbid every writer upgrade, indefinite starvation.");

    // Construct from an exclusive Permission, just like
    // SharedPermissionPool.  Burst counter starts at 0 (full budget
    // available).
    constexpr explicit FairSharedPermissionPool(Permission<Tag>&& exc) noexcept
        : inner_{std::move(exc)} {}

    // ── try_upgrade — fair, gated by the burst limit ───────────────
    //
    // Returns nullopt if EITHER the burst limit is reached OR the
    // inner pool's CAS fails (e.g., readers are outstanding).  On
    // success, increments the burst counter.
    //
    // Memory ordering:
    //   - acquire load on the burst counter — pairs with lend()'s
    //     release-store of 0 (so a successful reader's reset is
    //     visible to subsequent writer attempts).
    //   - acq_rel fetch_add on the success path — synchronizes with
    //     lend() readers checking the wins count.
    [[nodiscard, gnu::hot]] std::optional<Permission<Tag>> try_upgrade() noexcept {
        if (consecutive_writer_wins_.load(std::memory_order_acquire) >= BurstLimit) {
            return std::nullopt;  // fairness gate
        }
        return try_upgrade_unchecked();
    }

    // ── try_upgrade_unchecked — bypass the burst gate ──────────────
    //
    // Use AFTER detecting that no reader is contending (e.g., spin a
    // few times then check `outstanding() == 0 && !is_exclusive_out()`).
    // The unchecked path still increments the burst counter so that
    // bypass usage is observable in the diagnostics; it does NOT
    // forbid the upgrade.
    //
    // Cost: identical to `SharedPermissionPool::try_upgrade()` plus
    // one fetch_add on success.
    [[nodiscard, gnu::hot]] std::optional<Permission<Tag>> try_upgrade_unchecked() noexcept {
        auto upgrade = inner_.try_upgrade();
        if (upgrade) {
            consecutive_writer_wins_.fetch_add(1, std::memory_order_acq_rel);
        }
        return upgrade;
    }

    // ── deposit_exclusive — return the upgraded permission ─────────
    //
    // Forwards to the inner pool.  Does NOT touch the burst counter
    // — the counter only decreases when readers progress.  The writer
    // can deposit and immediately re-upgrade if (a) the burst budget
    // is not yet exhausted AND (b) inner CAS succeeds.
    void deposit_exclusive(Permission<Tag>&& exc) noexcept {
        inner_.deposit_exclusive(std::move(exc));
    }

    // ── lend — succeeds iff no exclusive is out; resets burst ──────
    //
    // Forwards to the inner pool's CAS-loop.  On success, resets the
    // burst counter to 0 — the reader has progressed, the writer's
    // bounded-overtaking budget is replenished.
    //
    // Memory ordering: release-store of 0 pairs with try_upgrade()'s
    // acquire-load.  No false sharing because the counter has its
    // own cache line.
    [[nodiscard, gnu::hot]] std::optional<SharedPermissionGuard<Tag>> lend() noexcept {
        auto guard = inner_.lend();
        if (guard) {
            consecutive_writer_wins_.store(0, std::memory_order_release);
        }
        return guard;
    }

    // ── Diagnostics ────────────────────────────────────────────────

    // Number of consecutive writer wins since the last successful
    // reader lend.  Always ≤ BurstLimit + (in-flight unchecked calls).
    //
    // uint64_t (not uint32_t) — at 100M wins/sec, uint32_t wraps in
    // ~40 seconds and the gate fails to fire after wrap (a wraparound
    // FROM K back to 0 would silently restore the writer's full burst
    // budget, defeating the fairness guarantee).  uint64_t at the
    // same rate takes 5840 years to wrap; effectively impossible.
    [[nodiscard]] std::uint64_t consecutive_writer_wins() const noexcept {
        return consecutive_writer_wins_.load(std::memory_order_acquire);
    }

    // True iff the next try_upgrade() would refuse due to the burst
    // gate (regardless of whether the inner CAS would succeed).
    [[nodiscard]] bool is_burst_exhausted() const noexcept {
        return consecutive_writer_wins() >= BurstLimit;
    }

    // Forwarded inner-pool diagnostics — useful for the no-reader
    // detection idiom in the writer's hot loop.
    [[nodiscard]] std::uint64_t outstanding() const noexcept {
        return inner_.outstanding();
    }
    [[nodiscard]] bool is_exclusive_out() const noexcept {
        return inner_.is_exclusive_out();
    }

private:
    SharedPermissionPool<Tag> inner_;

    // Cache-line aligned to prevent false sharing with the inner pool's
    // state_ atomic (which is also alignas(64) inside SharedPermissionPool).
    // Reader-write-cycle on this counter must not invalidate the inner
    // pool's state cache line on every reset.
    //
    // uint64_t (not uint32_t) — see consecutive_writer_wins() doc:
    // wraparound on uint32_t after ~40s @ 100M wins/sec would silently
    // restore the writer's full burst budget (counter wraps K→0).
    // uint64_t makes wraparound take 5840 years; effectively impossible.
    alignas(64) std::atomic<std::uint64_t> consecutive_writer_wins_{0};
};

// ── with_shared_read — convenience scoped-borrow helper ─────────────
//
// Mirror of safety::with_shared_read for SharedPermissionPool, but
// for FairSharedPermissionPool.  Without these overloads, production
// callers using `with_shared_read(pool, body)` would fail to compile
// when given a Fair* pool — the underlying free function templates are
// constrained to SharedPermissionPool<Tag>&.
//
// Semantics: lend() succeeds → invoke body with the share token;
// release on body return.  Returns nullopt iff lend() failed.

template <typename Tag, std::uint32_t K, typename Body>
    requires std::is_invocable_v<Body, SharedPermission<Tag>>
[[nodiscard]] auto with_shared_read(
    FairSharedPermissionPool<Tag, K>& pool, Body&& body)
    noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>)
    -> std::optional<std::invoke_result_t<Body, SharedPermission<Tag>>>
    requires (!std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>)
{
    auto guard_opt = pool.lend();
    if (!guard_opt) return std::nullopt;
    return std::optional{std::forward<Body>(body)(guard_opt->token())};
}

template <typename Tag, std::uint32_t K, typename Body>
    requires std::is_invocable_v<Body, SharedPermission<Tag>>
          && std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>
bool with_shared_read(
    FairSharedPermissionPool<Tag, K>& pool, Body&& body)
    noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>)
{
    auto guard_opt = pool.lend();
    if (!guard_opt) return false;
    std::forward<Body>(body)(guard_opt->token());
    return true;
}

// ── Self-test block ───────────────────────────────────────────────
//
// Compile-time checks: structural properties of the wrapper, sizeof,
// move/copy semantics, BurstLimit static_assert.

namespace detail::fair_pool_self_test {

struct TestRegion {};

// Pinned: no copy, no move.
static_assert(!std::is_copy_constructible_v<FairSharedPermissionPool<TestRegion>>);
static_assert(!std::is_move_constructible_v<FairSharedPermissionPool<TestRegion>>);

// BurstLimit defaults to 8.
static_assert(FairSharedPermissionPool<TestRegion>::writer_burst_limit == 8);
static_assert(FairSharedPermissionPool<TestRegion, 1>::writer_burst_limit == 1);
static_assert(FairSharedPermissionPool<TestRegion, 32>::writer_burst_limit == 32);

// Tag is preserved.
static_assert(std::is_same_v<FairSharedPermissionPool<TestRegion>::tag_type, TestRegion>);

// Layout: wrapper adds exactly one cache line on top of the inner
// pool (the fairness counter, alignas(64) to prevent false sharing
// with the inner pool's alignas(64) state_ atomic).  This pins the
// no-false-sharing claim.
static_assert(sizeof(FairSharedPermissionPool<TestRegion>) ==
              sizeof(SharedPermissionPool<TestRegion>) + 64,
    "FairSharedPermissionPool should add exactly one cache line "
    "(the fairness counter) over the inner pool.  If sizeof drifts, "
    "false sharing or padding regression has occurred.");

// Counter type is uint64_t — overflow at 100M wins/sec takes 5840
// years.  Effectively impossible.  Pinning this prevents accidental
// downgrade to uint32_t (which would wrap in ~40s of contention).
static_assert(std::is_same_v<
    decltype(std::declval<FairSharedPermissionPool<TestRegion>>()
        .consecutive_writer_wins()),
    std::uint64_t>);

}  // namespace detail::fair_pool_self_test

// ── Runtime smoke test ───────────────────────────────────────────────
//
// Single-thread exercise of the core fairness machinery:
//   - try_upgrade succeeds while burst budget remains
//   - after BurstLimit wins, try_upgrade returns nullopt
//   - try_upgrade_unchecked still works
//   - lend resets the counter
//   - diagnostics agree

[[gnu::cold]] inline void runtime_smoke_test_fair_shared_permission_pool() noexcept {
    struct SmokeTag {};
    constexpr std::uint32_t kBurst = 4;

    auto exc = mint_permission_root<SmokeTag>();
    FairSharedPermissionPool<SmokeTag, kBurst> pool{std::move(exc)};

    if (pool.consecutive_writer_wins() != 0)            std::abort();
    if (pool.is_burst_exhausted())                      std::abort();

    // Fill the burst budget — kBurst successful try_upgrades.
    for (std::uint32_t i = 0; i < kBurst; ++i) {
        auto u = pool.try_upgrade();
        if (!u) std::abort();
        pool.deposit_exclusive(std::move(*u));
        if (pool.consecutive_writer_wins() != i + 1)    std::abort();
    }
    if (!pool.is_burst_exhausted())                     std::abort();

    // Burst gate fires.
    {
        auto u = pool.try_upgrade();
        if (u) std::abort();
    }
    // ...but unchecked still works (and increments past the limit).
    {
        auto u = pool.try_upgrade_unchecked();
        if (!u) std::abort();
        pool.deposit_exclusive(std::move(*u));
    }
    if (pool.consecutive_writer_wins() != kBurst + 1)   std::abort();

    // A successful lend resets the counter.
    {
        auto g = pool.lend();
        if (!g) std::abort();
    }
    if (pool.consecutive_writer_wins() != 0)            std::abort();
    if (pool.is_burst_exhausted())                      std::abort();

    // Writer can upgrade again with full budget.
    {
        auto u = pool.try_upgrade();
        if (!u) std::abort();
        pool.deposit_exclusive(std::move(*u));
    }
    if (pool.consecutive_writer_wins() != 1)            std::abort();
}

}  // namespace crucible::safety
