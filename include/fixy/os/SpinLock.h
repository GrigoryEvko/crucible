#pragma once

// The gates: a bare lock for each wait strategy a gate supports, and the
// witnessed gate over it, whose acquisition costs a Permission and a
// context that the strategy admits.
//
// Old spelling: include/crucible/fixy/concurrent/_SpinLock.h for the
// witnessed half and include/crucible/concurrent/_SpinLock.h for the bare
// one.  Both live here, because the witnessed gate embeds the bare lock
// and nothing else in the new tree has a use for it.
//
// Deviations, each deliberate:
//
//  1. Acquiring takes a context.  lock() and try_lock() are private, and
//     the way in is lock_in<Ctx> / try_lock_in<Ctx>, whose clause states
//     which contexts may wait on the gate.  The old header had both doors
//     public, so the eleven production sites in
//     include/crucible/cntp/ConnectionPoolRuntime.h took the ungated one
//     and no context rule ever applied to them.  A gate with a public
//     bypass beside it is not a gate.
//
//  2. The context rule is read off the wait strategy, and WaitLattice
//     splits the strategies at the kernel.  A spin wait stays in user
//     space and burns the waiter's core for as long as the holder takes,
//     so a spin gate refuses a context that owns Effect::Bg: a background
//     context may also allocate, syscall or block, and none of the three
//     belongs inside a section another thread spins on.  A kernel wait
//     puts the waiter to sleep until the holder releases, so a blocking
//     gate requires a context that owns Effect::Block: waiting on the gate
//     is a block.  Background work that needs mutual exclusion takes the
//     blocking gate.
//
//  3. unlock() takes the same witness lock did.  The old one took none,
//     which made the pair asymmetric: the type system priced acquisition
//     and gave release away.  A caller who can reach unlock without a
//     Permission can release a gate it never acquired.
//
//  4. GateGuard carries the context into the lock and the witness to the
//     unlock, so the RAII path and the manual path are gated alike.  It
//     also keeps the reference to the Permission, which deviation 3
//     makes necessary.
//
//  5. cache_tier_hot is gone.  It was a namespace-scope constant that
//     restated SpinLock<Tag>::cache_tier, and nothing read it.
//
//  6. There is no unwitnessed guard and no substrate() accessor.  The old
//     tree had both: a guard over the bare lock, and an escape hatch that
//     handed the bare lock out of the witnessed one "for interoperating
//     with lock adaptors".  Either one locks the gate without a
//     Permission, which is the discipline this header exists to impose,
//     and the accessor had no callers at all.  The bare locks are still
//     here, spelled Unwitnessed so that choosing one reads as the choice
//     it is.  A class that serializes its own members declares a private
//     gate tag and mints a token of it at each acquisition: the token then
//     witnesses that the acquisition comes from inside the class.
//
//  7. One template, Gate<Tag, W>, carries the witnessing for every
//     strategy, so the spin gate and the blocking gate cannot drift apart.
//     SpinLock and BlockingLock name its two instances, SpinGuard and
//     BlockingGuard their guards.  A strategy that no bare lock implements
//     is refused when the gate is named.

#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct spin_lock;
struct spin_guard;
struct blocking_lock;
struct blocking_guard;
}  // namespace fixy::row_discipline

namespace fixy::spin {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

using cache_tier_t = ::foundation::algebra::lattices::HotPathTier;
using ::foundation::algebra::lattices::WaitLattice;
using ::foundation::algebra::lattices::WaitStrategy;

// True for a wait that enters the kernel or the scheduler.  WaitLattice
// orders the strategies by cost, and the three lowest are the ones that
// sleep; AcquireWait is the highest of them.
[[nodiscard]] consteval bool waits_in_kernel(WaitStrategy strategy) noexcept {
    return WaitLattice::leq(strategy, WaitStrategy::AcquireWait);
}

// The alignment lives on each primitive rather than at each embedding
// field, so a lock placed in an array or next to another lock is isolated
// without the embedding site having to remember it.
//
// Every acquisition through a bare lock is unwitnessed: it proves no
// ownership of the data the lock protects.  The gates below are the types
// production code names.

// A test-and-set lock that spins.
class alignas(64) UnwitnessedSpinLock {
public:
    static constexpr WaitStrategy wait_strategy = WaitStrategy::BoundedSpin;

    constexpr UnwitnessedSpinLock() noexcept = default;

    UnwitnessedSpinLock(const UnwitnessedSpinLock&) = delete("a lock is an identity; copying one would make two gates over one region");
    UnwitnessedSpinLock& operator=(const UnwitnessedSpinLock&) =
        delete("a lock is an identity; copying one would make two gates over one region");
    UnwitnessedSpinLock(UnwitnessedSpinLock&&) = delete("a lock is an identity; moving one would leave a waiter spinning on the old address");
    UnwitnessedSpinLock& operator=(UnwitnessedSpinLock&&) =
        delete("a lock is an identity; moving one would leave a waiter spinning on the old address");

    void lock() noexcept {
        // A pause-only spin burns the waiter's core for the holder's whole
        // scheduling quantum whenever the holder is descheduled between
        // acquire and release. The bounded pause phase covers the common case,
        // where the holder releases almost at once, and the escalation to
        // yield lets the scheduler run a holder that has lost its core. That
        // caps the wasted CPU at the pause budget.
        constexpr std::size_t kPauseBeforeYield = 64;
        std::size_t spin_iters = 0;
        while (flag_.test_and_set(std::memory_order_acquire)) {
            if (spin_iters < kPauseBeforeYield) {
                CRUCIBLE_SPIN_PAUSE;
                ++spin_iters;
            } else {
                std::this_thread::yield();
            }
        }
    }

    [[nodiscard]] bool try_lock() noexcept { return !flag_.test_and_set(std::memory_order_acquire); }

    void unlock() noexcept { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// A lock whose waiter sleeps on a futex until the holder releases.  The
// state word is the three-state mutex from Drepper's "Futexes Are
// Tricky": free, held with no waiter, and held with a waiter that the
// release must wake.  An uncontended acquisition and release cost one
// compare-exchange and one exchange, and no system call.
class alignas(64) UnwitnessedBlockingLock {
public:
    static constexpr WaitStrategy wait_strategy = WaitStrategy::AcquireWait;

    constexpr UnwitnessedBlockingLock() noexcept = default;

    UnwitnessedBlockingLock(const UnwitnessedBlockingLock&) =
        delete("a lock is an identity; copying one would make two gates over one region");
    UnwitnessedBlockingLock& operator=(const UnwitnessedBlockingLock&) =
        delete("a lock is an identity; copying one would make two gates over one region");
    UnwitnessedBlockingLock(UnwitnessedBlockingLock&&) =
        delete("a lock is an identity; moving one would leave a waiter asleep on the old address");
    UnwitnessedBlockingLock& operator=(UnwitnessedBlockingLock&&) =
        delete("a lock is an identity; moving one would leave a waiter asleep on the old address");

    void lock() noexcept {
        std::uint32_t observed = kFree;
        if (state_.compare_exchange_strong(observed, kHeld, std::memory_order_acquire, std::memory_order_acquire))
            [[likely]] {
            return;
        }
        lock_contended_(observed);
    }

    [[nodiscard]] bool try_lock() noexcept {
        std::uint32_t expected = kFree;
        return state_.compare_exchange_strong(expected, kHeld, std::memory_order_acquire, std::memory_order_acquire);
    }

    void unlock() noexcept {
        if (state_.exchange(kFree, std::memory_order_release) == kContended) {
            state_.notify_one();
        }
    }

private:
    static constexpr std::uint32_t kFree = 0;
    static constexpr std::uint32_t kHeld = 1;
    static constexpr std::uint32_t kContended = 2;

    // The contended path marks the word contended before each sleep, so
    // the holder's release knows to wake a waiter.  The loop ends when an
    // exchange finds the word free: the thread then holds the lock, and
    // the word stays contended, because another waiter may still sleep.
    [[gnu::cold, gnu::noinline]] void lock_contended_(std::uint32_t observed) noexcept {
        if (observed != kContended) {
            observed = state_.exchange(kContended, std::memory_order_acquire);
        }
        while (observed != kFree) {
            state_.wait(kContended, std::memory_order_acquire);
            observed = state_.exchange(kContended, std::memory_order_acquire);
        }
    }

    std::atomic<std::uint32_t> state_{kFree};
};

static_assert(alignof(UnwitnessedSpinLock) >= 64 && alignof(UnwitnessedBlockingLock) >= 64,
              "a lock must be cache-line-aligned to prevent false sharing across embedded array slots and "
              "adjacent struct members");
static_assert(sizeof(UnwitnessedSpinLock) >= 64 && sizeof(UnwitnessedBlockingLock) >= 64,
              "a lock occupies a full cache line; trailing padding is intentional — adjacent locks in an array "
              "must land on distinct lines");
// The spin lock stores a std::atomic_flag, the one type [atomics.flag]
// guarantees lock-free on every conforming implementation.  The blocking
// lock waits on its word, so the word must be a real atomic and not a
// library mutex in disguise.
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "the blocking lock's state word must be lock-free on this target");

namespace detail {

// The bare lock that implements each strategy a gate supports.  There is
// no primary definition, so naming a gate for any other strategy is an
// incomplete-type error rather than a gate with no lock inside.
template <WaitStrategy Strategy>
struct gate_substrate;

template <>
struct gate_substrate<WaitStrategy::BoundedSpin> {
    using type = UnwitnessedSpinLock;
    using lock_discipline = ::fixy::row_discipline::spin_lock;
    using guard_discipline = ::fixy::row_discipline::spin_guard;
};

template <>
struct gate_substrate<WaitStrategy::AcquireWait> {
    using type = UnwitnessedBlockingLock;
    using lock_discipline = ::fixy::row_discipline::blocking_lock;
    using guard_discipline = ::fixy::row_discipline::blocking_guard;
};

}  // namespace detail

// A context that may wait on a spin gate: one that exists and does not own
// the background capability.
template <typename Ctx>
concept CtxMayAcquireSpin = eff::IsExecCtx<Ctx> && (!eff::CtxOwnsCapability<Ctx, eff::Effect::Bg>);

// A context that may wait on a blocking gate: one that owns Block.
template <typename Ctx>
concept CtxMayBlock = eff::CtxOwnsCapability<Ctx, eff::Effect::Block>;

// The rule of deviation 2, one clause for both doors and both guards.
// A strategy on either side of the kernel split picks its rule, so the
// clause has no case to forget.
template <typename Ctx, WaitStrategy Strategy>
concept CtxMayAcquire = (waits_in_kernel(Strategy) && CtxMayBlock<Ctx>)
                     || (!waits_in_kernel(Strategy) && CtxMayAcquireSpin<Ctx>);

template <typename Tag, WaitStrategy Strategy, typename Brand = ::foundation::brand::DefaultBrand>
class GateGuard;

template <typename Tag, WaitStrategy Strategy>
class alignas(64) Gate {
    static_assert(perm::PermissionTag<Tag>, "fixy::spin::Gate<Tag, Strategy>: Tag must satisfy the PermissionTag "
                                            "concept, that is an empty non-union class type. Typically an "
                                            "empty struct nested in the owning class.");

public:
    using tag_type = Tag;
    using substrate_t = typename detail::gate_substrate<Strategy>::type;
    using row_discipline = typename detail::gate_substrate<Strategy>::lock_discipline;
    using row_payload = ::foundation::diag::row_payloads<>;

    static constexpr WaitStrategy wait_strategy = Strategy;
    // A gate whose waiter sleeps belongs off the hot path.
    static constexpr cache_tier_t cache_tier = waits_in_kernel(Strategy) ? cache_tier_t::Cold : cache_tier_t::Hot;

    static_assert(substrate_t::wait_strategy == Strategy,
                  "fixy::spin::Gate: the bare lock a strategy names must implement that strategy.");

    constexpr Gate() noexcept = default;

    Gate(const Gate&) = delete("a gate is an identity; copying one would make two gates over one region");
    Gate& operator=(const Gate&) = delete("a gate is an identity; copying one would make two gates over one region");
    Gate(Gate&&) = delete("a gate is an identity; moving one would leave a waiter on the old address");
    Gate& operator=(Gate&&) = delete("a gate is an identity; moving one would leave a waiter on the old address");

    // The two doors.  The context is read by the clause and by nothing
    // else; the proof is a compile-time witness and the body ignores it.
    // The proof is taken under whatever brand it carries: the gate is
    // keyed by its tag, and a permission of any instance of that tag
    // witnesses the acquisition.
    template <typename Ctx, typename Brand>
        requires CtxMayAcquire<Ctx, Strategy>
    void lock_in(Ctx const& /*ctx*/, perm::Permission<Tag, Brand>& proof) noexcept {
        lock(proof);
    }

    template <typename Ctx, typename Brand>
        requires CtxMayAcquire<Ctx, Strategy>
    [[nodiscard]] bool try_lock_in(Ctx const& /*ctx*/, perm::Permission<Tag, Brand>& proof) noexcept {
        return try_lock(proof);
    }

    // Release costs the same witness acquisition did, per deviation 3.
    template <typename Brand>
    void unlock(perm::Permission<Tag, Brand>& /*proof*/) noexcept {
        substrate_.unlock();
    }

private:
    // Private, per deviation 1: reaching these without a context is the
    // bypass the old header left open.  lock_in and try_lock_in are the
    // way in, and GateGuard goes through them too.
    template <typename Brand>
    void lock(perm::Permission<Tag, Brand>& /*proof*/) noexcept {
        substrate_.lock();
    }

    template <typename Brand>
    [[nodiscard]] bool try_lock(perm::Permission<Tag, Brand>& /*proof*/) noexcept {
        return substrate_.try_lock();
    }

    [[no_unique_address]] substrate_t substrate_{};
};

template <typename Tag>
using SpinLock = Gate<Tag, WaitStrategy::BoundedSpin>;

template <typename Tag>
using BlockingLock = Gate<Tag, WaitStrategy::AcquireWait>;

// The probe tag must be a complete empty struct. An incomplete type is not
// introspectable by is_empty_v, so PermissionTag cannot be satisfied.
namespace gate_size_probe_ {
struct SizeProbe {};
}  // namespace gate_size_probe_

static_assert(alignof(SpinLock<gate_size_probe_::SizeProbe>) == alignof(UnwitnessedSpinLock)
                  && alignof(BlockingLock<gate_size_probe_::SizeProbe>) == alignof(UnwitnessedBlockingLock),
              "fixy::spin::Gate must inherit substrate alignment (64 bytes). A cross-thread gate relies on "
              "cache-line isolation to stay clear of false sharing.");
static_assert(sizeof(SpinLock<gate_size_probe_::SizeProbe>) == sizeof(UnwitnessedSpinLock)
                  && sizeof(BlockingLock<gate_size_probe_::SizeProbe>) == sizeof(UnwitnessedBlockingLock),
              "fixy::spin::Gate must be zero-overhead over the substrate — the Tag is phantom and must "
              "EBO-collapse to zero bytes.");

// Copy and move are deleted: a second guard over the same gate would
// release it twice and break the acquire/release pairing.
//
// The guard holds a reference to the proof for the release, so it
// carries the proof's brand.  A proof minted by a root mint carries a
// fresh brand, so the guard is deduced rather than spelled:
// `GateGuard guard{ctx, gate, proof};`.  The deduction guides below are
// what make that spelling work.
template <typename Tag, WaitStrategy Strategy, typename Brand>
class GateGuard {
public:
    using lock_type = Gate<Tag, Strategy>;
    using permission_t = perm::Permission<Tag, Brand>;
    using brand_type = Brand;
    using row_discipline = typename detail::gate_substrate<Strategy>::guard_discipline;
    using row_payload = ::foundation::diag::row_payloads<>;

    template <typename Ctx>
        requires CtxMayAcquire<Ctx, Strategy>
    explicit GateGuard(Ctx const& ctx, lock_type& lock, permission_t& proof) noexcept : lock_{lock}, proof_{proof} {
        lock_.lock_in(ctx, proof);
    }

    template <typename Ctx>
        requires CtxMayAcquire<Ctx, Strategy>
    explicit GateGuard(std::try_to_lock_t, Ctx const& ctx, lock_type& lock, permission_t& proof) noexcept
        : lock_{lock}, proof_{proof}, acquired_{lock.try_lock_in(ctx, proof)} {}

    GateGuard(const GateGuard&) = delete("two guards over one gate would release it twice");
    GateGuard& operator=(const GateGuard&) = delete("two guards over one gate would release it twice");
    GateGuard(GateGuard&&) = delete("a moved-from guard would still release on scope exit");
    GateGuard& operator=(GateGuard&&) = delete("a moved-from guard would still release on scope exit");

    ~GateGuard() noexcept {
        if (acquired_) {
            lock_.unlock(proof_);
        }
    }

    [[nodiscard]] bool was_acquired() const noexcept { return acquired_; }

private:
    lock_type& lock_;
    permission_t& proof_;
    bool acquired_ = true;  // the plain constructor always acquires
};

template <typename Ctx, typename Tag, WaitStrategy Strategy, typename Brand>
GateGuard(Ctx const&, Gate<Tag, Strategy>&, perm::Permission<Tag, Brand>&) -> GateGuard<Tag, Strategy, Brand>;

template <typename Ctx, typename Tag, WaitStrategy Strategy, typename Brand>
GateGuard(std::try_to_lock_t, Ctx const&, Gate<Tag, Strategy>&, perm::Permission<Tag, Brand>&)
    -> GateGuard<Tag, Strategy, Brand>;

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
using SpinGuard = GateGuard<Tag, WaitStrategy::BoundedSpin, Brand>;

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
using BlockingGuard = GateGuard<Tag, WaitStrategy::AcquireWait, Brand>;

}  // namespace fixy::spin

namespace fixy::spin::detail::gate_self_test {

struct ProbeTag {};
struct ProbeBrand {};

static_assert(std::is_same_v<SpinLock<ProbeTag>::substrate_t, UnwitnessedSpinLock>
                  && std::is_same_v<BlockingLock<ProbeTag>::substrate_t, UnwitnessedBlockingLock>,
              "each gate must embed the bare lock of its strategy — substrate drift would break the alignas(64) "
              "and acquire/release contract.");

static_assert(std::is_same_v<SpinGuard<ProbeTag, ProbeBrand>::permission_t,
                             ::foundation::permissions::Permission<ProbeTag, ProbeBrand>>,
              "a guard must hold the Permission of its gate's tag under the brand it was given — drift would "
              "break the Permission-witness-at-acquire discipline.");

static_assert(SpinLock<ProbeTag>::cache_tier == cache_tier_t::Hot && BlockingLock<ProbeTag>::cache_tier == cache_tier_t::Cold,
              "a spin gate is Hot and a blocking gate is Cold.  The annotation is how a gate is located.");

static_assert(std::is_same_v<SpinLock<ProbeTag>::tag_type, ProbeTag>);

// The kernel split of deviation 2, read off the lattice.
static_assert(waits_in_kernel(WaitStrategy::Block) && waits_in_kernel(WaitStrategy::Park)
              && waits_in_kernel(WaitStrategy::AcquireWait));
static_assert(!waits_in_kernel(WaitStrategy::UmwaitC01) && !waits_in_kernel(WaitStrategy::BoundedSpin)
              && !waits_in_kernel(WaitStrategy::SpinPause));

// Neither a gate nor a guard can be copied or moved.
static_assert(!std::is_copy_constructible_v<SpinLock<ProbeTag>> && !std::is_move_constructible_v<SpinLock<ProbeTag>>);
static_assert(!std::is_copy_constructible_v<BlockingLock<ProbeTag>>
              && !std::is_move_constructible_v<BlockingLock<ProbeTag>>);
static_assert(!std::is_copy_constructible_v<SpinGuard<ProbeTag, ProbeBrand>>
              && !std::is_move_constructible_v<SpinGuard<ProbeTag, ProbeBrand>>);
static_assert(!std::is_copy_constructible_v<BlockingGuard<ProbeTag, ProbeBrand>>
              && !std::is_move_constructible_v<BlockingGuard<ProbeTag, ProbeBrand>>);
static_assert(!std::is_copy_constructible_v<UnwitnessedSpinLock> && !std::is_move_constructible_v<UnwitnessedSpinLock>);
static_assert(!std::is_copy_constructible_v<UnwitnessedBlockingLock>
              && !std::is_move_constructible_v<UnwitnessedBlockingLock>);

// Deviation 2 as cells: each clause admits the contexts its wait allows
// and refuses the rest.  The contexts are built here, the way the sibling
// os headers build theirs.
using ForegroundCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
using BackgroundCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using BackgroundBlockCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

static_assert(CtxMayAcquire<ForegroundCtx, WaitStrategy::BoundedSpin>,
              "a context without Bg must be able to spin: that is the shape a foreground caller has.");
static_assert(!CtxMayAcquire<BackgroundCtx, WaitStrategy::BoundedSpin>,
              "a context that owns Bg must NOT spin. A background context may also allocate, syscall or block, "
              "and none of the three belongs inside a section another thread spins on.");
static_assert(CtxMayAcquire<BackgroundBlockCtx, WaitStrategy::AcquireWait>,
              "a context that owns Block may sleep on a blocking gate: that is the shape background work has.");
static_assert(!CtxMayAcquire<BackgroundCtx, WaitStrategy::AcquireWait>,
              "a context without Block must NOT sleep on a blocking gate: the wait is a block.");
static_assert(!CtxMayAcquire<ForegroundCtx, WaitStrategy::AcquireWait>,
              "a foreground context owns no Block, so it cannot wait on a blocking gate.");
static_assert(!CtxMayAcquire<int, WaitStrategy::BoundedSpin> && !CtxMayAcquire<int, WaitStrategy::AcquireWait>,
              "a non-context must not pass for one.");

// Five claims about this header cannot be written here.  An access
// failure inside a requires-expression is a hard error in GCC 16 rather
// than a substitution failure, so `!requires { gate.lock(proof); }` does
// not compile even when the answer is the one wanted.  The claims are
// carried by negative-compile fixtures instead:
//
//   neg_spin_lock_door_is_private       — gate.lock(proof) from outside
//   neg_spin_try_lock_door_is_private   — gate.try_lock(proof) from outside
//   neg_spin_unlock_without_witness     — gate.unlock() with no Permission
//   neg_spin_guard_under_bg_ctx         — a Bg context at the spin guard
//   neg_spin_lock_without_permission    — a guard with no Permission at all
//   neg_blocking_guard_without_block    — a context without Block at the
//                                         blocking guard
//   neg_blocking_lock_in_without_block  — the same at the lock_in door
//   neg_blocking_guard_without_permission — a blocking guard with no
//                                         Permission at all

}  // namespace fixy::spin::detail::gate_self_test
