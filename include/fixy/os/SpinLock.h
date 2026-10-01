#pragma once

// The gates: a bare lock for each wait strategy a gate supports, and the
// witnessed gate over it, whose acquisition costs a Permission and a
// context that the strategy admits.
//
// The bare lock and the witnessed gate live in one header, because the
// witnessed gate embeds the bare lock and nothing else has a use for it.
//
// Design notes:
//
//  1. The guard is the one way in and the one way out.  lock(), try_lock()
//     and unlock() are private, and GateGuard is the only friend.  The
//     guard takes a context and a Permission, and its clause states which
//     contexts may wait on the gate.  A gate with a public bypass beside
//     it is not a gate.
//
//  2. The context rule is read off the wait strategy, and WaitLattice
//     splits the strategies at the kernel.  A spin wait stays in user
//     space and burns the waiter's core for as long as the holder takes.
//     So a spin gate refuses a context that owns Bg, Alloc, IO or Block:
//     each of the four lets the holder allocate, make a system call or
//     sleep inside a section that another thread spins on.  A kernel wait
//     puts the waiter to sleep until the holder releases, so a blocking
//     gate requires a context that owns Effect::Block: waiting on the gate
//     is a block.  Background work that needs mutual exclusion takes the
//     blocking gate.
//
//  3. No release is public.  A public unlock that takes any Permission of
//     the tag lets a thread that does not hold the gate release it, and
//     lets one holder release it two times.  The guard releases in its
//     destructor, and only when it acquired, so each acquisition has one
//     release, by its holder.  A guard neither copies nor moves.
//
//  4. The spin wait reads the flag and pauses while the flag is set, and
//     tries the exchange only when a read finds the flag clear.  A wait
//     that does the exchange at each turn takes the cache line in
//     exclusive state at each turn, away from the holder.  The spin never
//     yields, because sched_yield is a system call, and the context rule
//     of design note 2 keeps the holder out of the kernel as well.
//
//  5. There is no unwitnessed guard, and no accessor hands the bare lock
//     out of the witnessed one.  Either one locks the gate without a
//     Permission, and the Permission is the discipline this header exists
//     to impose.  The bare locks are here, spelled Unwitnessed so that
//     choosing one reads as the choice it is.  A class that serializes
//     its own members declares a private gate tag and mints a token of it
//     at each acquisition: the token then witnesses that the acquisition
//     comes from inside the class.  A bare lock is not a gate, so the
//     rules of design notes 1 and 3 do not apply to it.
//
//  6. One template, Gate<Tag, W>, carries the witnessing for every
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
#include <cstdint>
#include <mutex>
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

// A test-and-test-and-set lock that spins.
class alignas(64) UnwitnessedSpinLock {
public:
    static constexpr WaitStrategy wait_strategy = WaitStrategy::BoundedSpin;

    constexpr UnwitnessedSpinLock() noexcept = default;

    UnwitnessedSpinLock(const UnwitnessedSpinLock&) =
        delete("a lock is an identity; copying one would make two gates over one region");
    UnwitnessedSpinLock& operator=(const UnwitnessedSpinLock&) =
        delete("a lock is an identity; copying one would make two gates over one region");
    UnwitnessedSpinLock(UnwitnessedSpinLock&&) =
        delete("a lock is an identity; moving one would leave a waiter spinning on the old address");
    UnwitnessedSpinLock& operator=(UnwitnessedSpinLock&&) =
        delete("a lock is an identity; moving one would leave a waiter spinning on the old address");

    // The waiter reads the flag and pauses while it is set, so the cache
    // line stays shared while the holder runs.  Only a read that finds
    // the flag clear is followed by the exchange, which takes the line
    // in exclusive state.  An exchange at each turn takes the line away
    // from the holder at each turn, and the holder then pays a miss to
    // release.  The wait does not yield, because sched_yield is a system
    // call and a spin gate is a hot-path gate.  A holder that loses its
    // core keeps its waiters spinning until it runs again.  Work that can
    // lose its core in the section takes the blocking lock.
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            while (flag_.test(std::memory_order_acquire)) {
                CRUCIBLE_SPIN_PAUSE;
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

// A context that may wait on a spin gate: one that exists and owns none of
// Bg, Alloc, IO and Block.  Each of the four lets the holder allocate,
// make a system call or sleep inside the section, and the waiters spin
// for all of it.  A cold init context owns Alloc and IO, so it takes the
// blocking gate.
template <typename Ctx>
concept CtxMayAcquireSpin =
    eff::IsExecCtx<Ctx>
    && !eff::CtxOwnsAnyOf<Ctx, eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>;

// A context that may wait on a blocking gate: one that owns Block.
template <typename Ctx>
concept CtxMayBlock = eff::CtxOwnsCapability<Ctx, eff::Effect::Block>;

// The rule of design note 2, one clause for both guards.
// A strategy on either side of the kernel split picks its rule, so the
// clause has no case to forget.
template <typename Ctx, WaitStrategy Strategy>
concept CtxMayAcquire =
    (waits_in_kernel(Strategy) && CtxMayBlock<Ctx>) || (!waits_in_kernel(Strategy) && CtxMayAcquireSpin<Ctx>);

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

private:
    // Private, per design notes 1 and 3.  The guard is the only friend: it
    // checks the context and takes the Permission before it calls lock or
    // try_lock, and it calls unlock once, and only after an acquisition.
    template <typename, WaitStrategy, typename>
    friend class GateGuard;

    void lock() noexcept { substrate_.lock(); }

    [[nodiscard]] bool try_lock() noexcept { return substrate_.try_lock(); }

    void unlock() noexcept { substrate_.unlock(); }

    [[no_unique_address]] substrate_t substrate_{};
};

template <typename Tag>
using SpinLock = Gate<Tag, WaitStrategy::BoundedSpin>;

template <typename Tag>
using BlockingLock = Gate<Tag, WaitStrategy::AcquireWait>;

// Copy and move are deleted: a second guard over the same gate would
// release it twice and break the acquire/release pairing.
//
// The guard takes the proof as the witness of the acquisition, so it
// carries the proof's brand.  The context is read by the clause and by
// nothing else, and the proof is a compile-time witness that the body
// does not read.  The gate is keyed by its tag, and a permission of any
// instance of that tag witnesses the acquisition.  A proof minted by a
// root mint carries a fresh brand, so the guard is deduced rather than
// spelled: `GateGuard guard{ctx, gate, proof};`.  The deduction guides
// below are what make that spelling work.
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
    explicit GateGuard(Ctx const& /*ctx*/, lock_type& lock, permission_t& /*proof*/) noexcept : lock_{lock} {
        lock_.lock();
    }

    template <typename Ctx>
        requires CtxMayAcquire<Ctx, Strategy>
    explicit GateGuard(std::try_to_lock_t, Ctx const& /*ctx*/, lock_type& lock, permission_t& /*proof*/) noexcept
        : lock_{lock}, acquired_{lock.try_lock()} {}

    GateGuard(const GateGuard&) = delete("two guards over one gate would release it twice");
    GateGuard& operator=(const GateGuard&) = delete("two guards over one gate would release it twice");
    GateGuard(GateGuard&&) = delete("a moved-from guard would still release on scope exit");
    GateGuard& operator=(GateGuard&&) = delete("a moved-from guard would still release on scope exit");

    ~GateGuard() noexcept {
        if (acquired_) {
            lock_.unlock();
        }
    }

    [[nodiscard]] bool was_acquired() const noexcept { return acquired_; }

private:
    lock_type& lock_;
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
