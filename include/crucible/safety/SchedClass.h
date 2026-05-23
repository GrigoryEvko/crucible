#pragma once

// ── crucible::safety::SchedClass<SchedulerPolicy Policy, T, ...> ─────
//
// FIXY-V-186 (Agent 6 §3.3 item 2): value-level Graded carrier for the
// V-183 SchedulerPolicy axis (SchedulerPolicyLattice — a six-element
// preemption-rank chain Idle ⊑ Batch ⊑ Other ⊑ RoundRobin ⊑ Fifo ⊑
// Deadline).  Tags a work-item with the scheduler class it EXPECTS, so a
// thread pool can verify pool-vs-task fit at compile time: a jthread
// spawned under SCHED_OTHER cannot host SCHED_FIFO-tagged work without
// explicit promotion through the privileged V-191 mint_scheduler_policy
// factory.
//
//   Substrate: Graded<ModalityKind::Absolute, SchedulerPolicyLattice::At<Policy>, T>
//   Regime:    1 (zero-cost EBO collapse — At<Policy>::element_type is
//              empty, sizeof(SchedClass<Policy, T>) == sizeof(T) at -O3).
//              The three SCHED_DEADLINE budget NTTPs add NO storage.
//
// ── Pool-vs-task fit (the load-bearing gate) ────────────────────────
//
// SchedClass tags a TASK; a POOL has its own scheduler policy.  A task at
// `Policy` is runnable on a pool at `PoolPolicy` iff the pool's preemption
// strength SUBSUMES the task's requirement — `Policy ⊑ PoolPolicy` on the
// chain.  A SCHED_FIFO pool hosts a SCHED_OTHER task (Other ⊑ Fifo); a
// SCHED_OTHER pool does NOT host a SCHED_FIFO task (Fifo ⋤ Other).
//
//   runnable_on<PoolPolicy> := SchedulerPolicyLattice::leq(Policy, PoolPolicy)
//
//   FifoTask::runnable_on<Fifo>     = TRUE
//   FifoTask::runnable_on<Deadline> = TRUE  (Fifo ⊑ Deadline)
//   FifoTask::runnable_on<Other>    = FALSE (the V-186 pool-fit rejection)
//
// ── SCHED_DEADLINE budget (RuntimeNs, DeadlineNs, PeriodNs) ──────────
//
// SCHED_DEADLINE is CBS-admitted EDF: the kernel admits a task only if
// `runtime < deadline <= period`.  The DEADLINE case of SchedClass carries
// the three budgets as NTTPs and asserts the admission inequality at
// COMPILE time, so an inadmissible budget is a build error, not a runtime
// `sched_setattr(EINVAL)`.  Non-DEADLINE policies MUST leave the three
// budgets zero (a FIFO/OTHER task has no CBS budget) — a second guard
// enforces that, keeping the budgets DEADLINE-exclusive.
//
// ── §XVI canonical wrapper-nesting position ─────────────────────────
//
// SchedClass sits in the Synchronization neighborhood alongside Wait<> /
// MemOrder<> (it constrains HOW a work-item is scheduled, like Wait<>
// constrains how a thread blocks).  The
// row_hash_contribution<SchedClass<Policy, Inner, ...>> federation-cache
// discriminator (salt 0x31) ships in safety/diag/RowHashFold.h — the
// row_hash key is the WRAPPER, never the lattice At<> (FIXY-V-183 deferred
// its row_hash here).  The Deadline budgets are folded into the hash so
// distinct CBS budgets occupy distinct cache slots.
//
//   Axiom coverage:
//     TypeSafe — SchedulerPolicy is a strong scoped enum; two
//                different-policy wrappers are DISTINCT types, so a
//                cross-policy assignment / a too-weak pool is a compile
//                error (the HS14 fixtures pin both).
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//     DetSafe — the policy pin is the type-level WITNESS of a work-item's
//                scheduling requirement; the CBS budget assert is checked
//                at compile time.
//   Runtime cost: sizeof(SchedClass<Policy, T>) == sizeof(T); verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.
//
// §XXI: `mint_sched_class<Policy, T[, R, D, P]>(args...)` is the value-
// wrapping TOKEN mint.  It is DISTINCT from the V-191 privileged
// `mint_scheduler_policy` ctx-bound syscall mint (which calls
// sched_setattr to PROMOTE a thread's actual policy).  HS14 fixtures:
// neg_sched_class_fifo_task_on_other_pool / neg_sched_class_deadline_runtime_gt_deadline
// / neg_sched_class_batch_on_hotpath.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/SchedulerPolicyLattice.h>

#include <concepts>
#include <cstdint>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::SchedulerPolicyLattice;
using SchedulerPolicy_v = ::crucible::algebra::lattices::SchedulerPolicy;

template <SchedulerPolicy_v Policy, typename T,
          std::uint64_t RuntimeNs = 0, std::uint64_t DeadlineNs = 0,
          std::uint64_t PeriodNs = 0>
class [[nodiscard]] SchedClass {
    // CBS admission — only fires for the SCHED_DEADLINE case.
    static_assert(Policy != SchedulerPolicy_v::Deadline
                  || (RuntimeNs < DeadlineNs && DeadlineNs <= PeriodNs),
        "SchedClass<Deadline, ...>: SCHED_DEADLINE requires "
        "RuntimeNs < DeadlineNs <= PeriodNs (the CBS admission inequality "
        "the kernel enforces in sched_setattr).");
    // Budgets are DEADLINE-exclusive — a FIFO/OTHER task has no CBS budget.
    static_assert(Policy == SchedulerPolicy_v::Deadline
                  || (RuntimeNs == 0 && DeadlineNs == 0 && PeriodNs == 0),
        "SchedClass<Policy, ...>: only SCHED_DEADLINE carries the "
        "(RuntimeNs, DeadlineNs, PeriodNs) CBS budget; leave them zero for "
        "every other policy.");

public:
    // ── Public type aliases (GradedWrapper uniform surface) ─────────
    using value_type   = T;
    using lattice_type = SchedulerPolicyLattice::template At<Policy>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned scheduler policy + the CBS budget (zero unless Deadline).
    static constexpr SchedulerPolicy_v policy      = Policy;
    static constexpr std::uint64_t     runtime_ns  = RuntimeNs;
    static constexpr std::uint64_t     deadline_ns = DeadlineNs;
    static constexpr std::uint64_t     period_ns   = PeriodNs;

private:
    graded_type impl_;

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr SchedClass() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit SchedClass(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit SchedClass(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr SchedClass(const SchedClass&)            = default;
    constexpr SchedClass(SchedClass&&)                 = default;
    constexpr SchedClass& operator=(const SchedClass&) = default;
    constexpr SchedClass& operator=(SchedClass&&)      = default;
    ~SchedClass()                                      = default;

    // Equality: compares value bytes within the SAME scheduler class.
    [[nodiscard]] friend constexpr bool operator==(
        SchedClass const& a, SchedClass const& b)
        noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only / mutable access ──────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return std::move(impl_).consume(); }
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(SchedClass& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }
    friend constexpr void swap(SchedClass& a, SchedClass& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // ── runnable_on<PoolPolicy> — pool-vs-task fit ──────────────────
    //
    // TRUE iff a pool at PoolPolicy can host this Policy-tagged task
    // (Policy ⊑ PoolPolicy — the pool's preemption strength subsumes the
    // task's requirement).  The V-186 pool-fit gate; mint_thread_pool
    // (existing or V-191) admits a task only when runnable_on<pool> holds.
    template <SchedulerPolicy_v PoolPolicy>
    static constexpr bool runnable_on =
        SchedulerPolicyLattice::leq(Policy, PoolPolicy);
};

// ── §XXI mint factory (token mint — wraps a value, NOT a syscall) ───
template <SchedulerPolicy_v Policy, typename T,
          std::uint64_t RuntimeNs = 0, std::uint64_t DeadlineNs = 0,
          std::uint64_t PeriodNs = 0, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr SchedClass<Policy, T, RuntimeNs, DeadlineNs, PeriodNs>
mint_sched_class(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return SchedClass<Policy, T, RuntimeNs, DeadlineNs, PeriodNs>{
        std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases ─────────────────────────────────────────────
namespace sched_class {
    template <typename T> using Idle       = SchedClass<SchedulerPolicy_v::Idle,       T>;
    template <typename T> using Batch      = SchedClass<SchedulerPolicy_v::Batch,      T>;
    template <typename T> using Other      = SchedClass<SchedulerPolicy_v::Other,      T>;
    template <typename T> using RoundRobin = SchedClass<SchedulerPolicy_v::RoundRobin, T>;
    template <typename T> using Fifo       = SchedClass<SchedulerPolicy_v::Fifo,       T>;
    template <typename T, std::uint64_t RuntimeNs, std::uint64_t DeadlineNs, std::uint64_t PeriodNs>
    using Deadline = SchedClass<SchedulerPolicy_v::Deadline, T, RuntimeNs, DeadlineNs, PeriodNs>;
}  // namespace sched_class

// ── Layout invariants — regime-1 EBO collapse ───────────────────────
namespace detail::sched_class_layout {

template <typename T> using OtherSc = SchedClass<SchedulerPolicy_v::Other, T>;
template <typename T> using FifoSc  = SchedClass<SchedulerPolicy_v::Fifo,  T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(OtherSc, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(OtherSc, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoSc,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoSc,  double);

}  // namespace detail::sched_class_layout

static_assert(sizeof(SchedClass<SchedulerPolicy_v::Other,    int>)    == sizeof(int));
static_assert(sizeof(SchedClass<SchedulerPolicy_v::Fifo,     double>) == sizeof(double));
static_assert(sizeof(SchedClass<SchedulerPolicy_v::Idle,     char>)   == sizeof(char));
// The DEADLINE budget NTTPs add zero storage — still sizeof(T).
static_assert(
    sizeof(SchedClass<SchedulerPolicy_v::Deadline, int, 5'000, 10'000, 20'000>) == sizeof(int));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::sched_class_self_test {

using OtherInt    = SchedClass<SchedulerPolicy_v::Other,      int>;
using FifoInt     = SchedClass<SchedulerPolicy_v::Fifo,       int>;
using BatchInt    = SchedClass<SchedulerPolicy_v::Batch,      int>;
using RrInt       = SchedClass<SchedulerPolicy_v::RoundRobin, int>;
using IdleInt     = SchedClass<SchedulerPolicy_v::Idle,       int>;
using DeadlineInt = SchedClass<SchedulerPolicy_v::Deadline,   int, 5'000, 10'000, 20'000>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr FifoInt f_default{};
static_assert(f_default.peek() == 0);
static_assert(FifoInt::policy == SchedulerPolicy_v::Fifo);

inline constexpr FifoInt f_explicit{42};
static_assert(f_explicit.peek() == 42);

inline constexpr OtherInt o_in_place{std::in_place, 7};
static_assert(o_in_place.peek() == 7);

static_assert(FifoInt::modality == ::crucible::algebra::ModalityKind::Absolute);

// ── DEADLINE budget exposure + zero-budget for non-Deadline ─────────
static_assert(DeadlineInt::runtime_ns  == 5'000);
static_assert(DeadlineInt::deadline_ns == 10'000);
static_assert(DeadlineInt::period_ns   == 20'000);
static_assert(FifoInt::runtime_ns == 0 && FifoInt::deadline_ns == 0 && FifoInt::period_ns == 0,
    "Non-DEADLINE policies carry a zero CBS budget.");

// ── runnable_on<PoolPolicy> — pool-vs-task fit (task ⊑ pool) ────────
//
// A SCHED_FIFO task runs on a Fifo or Deadline pool, NOT on an Other pool.
static_assert( FifoInt::runnable_on<SchedulerPolicy_v::Fifo>);
static_assert( FifoInt::runnable_on<SchedulerPolicy_v::Deadline>,
    "A FIFO task runs on a DEADLINE pool — Fifo ⊑ Deadline.");
static_assert(!FifoInt::runnable_on<SchedulerPolicy_v::Other>,
    "FIXY-V-186: a SCHED_FIFO task MUST NOT run on a SCHED_OTHER pool — "
    "the pool is too weak (Fifo ⋤ Other).  This is the pool-fit rejection "
    "that the thread pool depends on.");
static_assert(!FifoInt::runnable_on<SchedulerPolicy_v::RoundRobin>,
    "Fifo ⋤ RoundRobin — a round-robin pool cannot host a FIFO task.");
// An OTHER task runs on any pool at-or-above Other, and on a weaker pool
// only if that pool subsumes it (Other ⋤ Batch, so NOT a Batch pool).
static_assert( OtherInt::runnable_on<SchedulerPolicy_v::Other>);
static_assert( OtherInt::runnable_on<SchedulerPolicy_v::Fifo>);
static_assert(!OtherInt::runnable_on<SchedulerPolicy_v::Batch>);
// An IDLE task runs on every pool (Idle is ⊥).
static_assert( IdleInt::runnable_on<SchedulerPolicy_v::Idle>);
static_assert( IdleInt::runnable_on<SchedulerPolicy_v::Deadline>);

// ── HotPath eligibility (BATCH/IDLE are background-only) ────────────
//
// A HotPath-eligible work-item needs AT LEAST Other-strength scheduling
// (leq(Other, Policy)) — Idle/Batch (below Other) are non-interactive
// background classes and are rejected.  Mirrors the V-183 CtxFitsTscReader
// threshold; the neg_sched_class_batch_on_hotpath fixture pins it.
template <typename Task>
concept hot_path_eligible =
    SchedulerPolicyLattice::leq(SchedulerPolicy_v::Other, Task::policy);

static_assert( hot_path_eligible<OtherInt>);
static_assert( hot_path_eligible<FifoInt>);
static_assert( hot_path_eligible<DeadlineInt>);
static_assert(!hot_path_eligible<BatchInt>,
    "FIXY-V-186: SCHED_BATCH is non-interactive background work and MUST be "
    "rejected at a HotPath stance (Batch ⊏ Other).");
static_assert(!hot_path_eligible<IdleInt>);

// ── Distinct types per policy + per budget ──────────────────────────
static_assert(!std::is_same_v<FifoInt, OtherInt>);
static_assert(!std::is_same_v<DeadlineInt,
    SchedClass<SchedulerPolicy_v::Deadline, int, 5'000, 10'000, 30'000>>,
    "Two SCHED_DEADLINE tasks with different periods are DISTINCT types.");

// ── Diagnostic forwarders ──────────────────────────────────────────
static_assert(FifoInt::lattice_name()  == "SchedulerPolicyLattice::At<Fifo>");
static_assert(OtherInt::lattice_name() == "SchedulerPolicyLattice::At<Other>");
static_assert(FifoInt::value_type_name().ends_with("int"));

// ── swap / peek_mut / operator== ───────────────────────────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_policy() noexcept {
    FifoInt a{10}; FifoInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_policy());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    FifoInt a{42}; FifoInt b{42}; FifoInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// ── mint_sched_class factory ───────────────────────────────────────
inline constexpr auto minted_fifo = mint_sched_class<SchedulerPolicy_v::Fifo, int>(99);
static_assert(minted_fifo.peek() == 99 && minted_fifo.policy == SchedulerPolicy_v::Fifo);
inline constexpr auto minted_dl =
    mint_sched_class<SchedulerPolicy_v::Deadline, int, 5'000, 10'000, 20'000>(7);
static_assert(minted_dl.peek() == 7 && minted_dl.deadline_ns == 10'000);

// ── Thread-pool fit simulation (the mint_thread_pool consumer shape) ─
template <typename Task, SchedulerPolicy_v PoolPolicy>
concept hostable_on = Task::template runnable_on<PoolPolicy>;

static_assert( hostable_on<OtherInt, SchedulerPolicy_v::Fifo>,
    "A FIFO pool MUST host an OTHER task.");
static_assert(!hostable_on<FifoInt, SchedulerPolicy_v::Other>,
    "An OTHER pool MUST reject a FIFO task — the V-186 pool-fit gate.");

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: runtime ops with
// non-constant arguments catch consteval/SFINAE/inline-body bugs.
inline void runtime_smoke_test() {
    int seed = 21;
    FifoInt f{seed * 2};
    if (f.peek() != 42) std::abort();
    f.peek_mut() = 9;
    if (f.peek() != 9) std::abort();

    auto m = mint_sched_class<SchedulerPolicy_v::Other, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    FifoInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool g1 = FifoInt::runnable_on<SchedulerPolicy_v::Deadline>;
    [[maybe_unused]] bool g2 = FifoInt::runnable_on<SchedulerPolicy_v::Other>;
    if (!g1 || g2) std::abort();

    // DEADLINE budget carrier at runtime.
    DeadlineInt dl{seed};
    if (dl.peek() != 21 || dl.runtime_ns != 5'000) std::abort();

    sched_class::Idle<int> idle_task{0};
    sched_class::RoundRobin<int> rr_task{456};
    if (idle_task.peek() != 0 || rr_task.peek() != 456) std::abort();
}

}  // namespace detail::sched_class_self_test

}  // namespace crucible::safety
