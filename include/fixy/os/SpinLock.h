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
//     The waiter has no exit until the holder releases, so a spin gate
//     requires a context that owns Effect::Div, and the hot path, which
//     owns no Div, cannot take one.  A spin gate refuses a context that
//     owns IO or Block: each of the two lets the holder make a system call
//     or sleep inside a section that another thread spins on.  A kernel wait
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
//     choosing one reads as the choice it is.  Their lock and unlock are
//     private, so no code outside this header takes a bare lock.  A class
//     that serializes its own members holds them in a Locked (design note
//     7), which owns them.
//
//  6. One template, Gate<Tag, W>, carries the witnessing for every
//     strategy, so the spin gate and the blocking gate cannot drift apart.
//     SpinLock and BlockingLock name its two instances, SpinGuard and
//     BlockingGuard their guards.  A strategy that no bare lock implements
//     is refused when the gate is named.
//
//  7. A gate holds no data, so a member beside a gate can still be read with
//     no guard.  Locked<T, Tag, W> owns its data: it holds the bare lock of
//     W and one T, both private, and it lends the T only to a body inside
//     the lock, under the context rule of design note 2.  The result of a
//     body holds no borrow, so no reference to the T leaves the lock through
//     the return.  Code that guards data takes Locked.  The lock and the
//     unlock of a bare lock are private, and Gate, Locked and its guard are
//     their only friends.
//
//       locked.with(ctx, body)       takes the lock, lends T& to body, and
//                                    gives the lock back
//       locked.try_with(ctx, body)   the same when the lock is free, and no
//                                    value when another holder has it
//       auto guard = locked.lock(ctx);
//       guard.with(body);            holds the lock across some bodies
//
//     A const Locked lends T const&.  What stays open, as for each frame
//     door (foundation/core/Frame.h): a body can store a borrow of the T in
//     an object outside the lock through a capture by reference, in a type
//     that names no brand.  Poisoning is not necessary, because a fatal ends
//     the process and no code unwinds out of a body.

#include <fixy/os/Futex.h>
#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Record.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>
#include <type_traits>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct spin_lock;
struct spin_guard;
struct blocking_lock;
struct blocking_guard;
struct spin_locked;
struct blocking_locked;
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

// The friends of the bare locks: only these take or give back a bare lock.
template <typename Tag, WaitStrategy Strategy>
class Gate;
template <class T, typename Tag, WaitStrategy Strategy>
class Locked;
template <class Data, class Lock>
class LockedGuard;

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

private:
    template <typename, WaitStrategy>
    friend class Gate;
    template <class, typename, WaitStrategy>
    friend class Locked;
    template <class, class>
    friend class LockedGuard;

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
        while (is_held_.exchange_acq_rel(true)) {
            while (is_held_.load_acquire()) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    }

    [[nodiscard]] bool try_lock() noexcept { return !is_held_.exchange_acq_rel(true); }

    void unlock() noexcept { is_held_.store_release(false); }

    ::foundation::core::Atomic<bool> is_held_{false};
};

// A lock whose waiter sleeps on a futex until the holder releases.  The
// state word is the three-state mutex from Drepper's "Futexes Are
// Tricky": free, held with no waiter, and held with a waiter that the
// release must wake.  An uncontended acquisition and release cost one
// compare-exchange and one exchange, and no system call.  The sleep and
// the wake go through the futex door of fixy/os/Futex.h.  Its gate is the
// gate of the blocking guard below, which asks for a context that owns
// Block.
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

private:
    template <typename, WaitStrategy>
    friend class Gate;
    template <class, typename, WaitStrategy>
    friend class Locked;
    template <class, class>
    friend class LockedGuard;

    void lock() noexcept {
        state_.cas_acq_rel(kFree, kHeld)
            .match([](::foundation::core::Unit) noexcept {},
                   [this](::foundation::core::CasRefusal<std::uint32_t> refusal) noexcept {
                       lock_contended_(refusal.observed);
                   });
    }

    [[nodiscard]] bool try_lock() noexcept { return state_.cas_acq_rel(kFree, kHeld).is_ok(); }

    void unlock() noexcept {
        if (state_.exchange_acq_rel(kFree) == kContended) {
            wake_one_waiter_();
        }
    }

    static constexpr std::uint32_t kFree = 0;
    static constexpr std::uint32_t kHeld = 1;
    static constexpr std::uint32_t kContended = 2;

    // The contended path marks the word contended before each sleep, so
    // the holder's release knows to wake a waiter.  The loop ends when an
    // exchange finds the word free: the thread then holds the lock, and
    // the word stays contended, because another waiter may still sleep.
    // Each end of a sleep reads the word again with the exchange, so the
    // way that the sleep ended does not matter.  The word is a cell of this
    // lock, so the kernel can refuse a sleep only for a broken process.
    [[gnu::cold, gnu::noinline]] void lock_contended_(std::uint32_t observed) noexcept {
        if (observed != kContended) {
            observed = state_.exchange_acq_rel(kContended);
        }
        while (observed != kFree) {
            static_cast<void>(::fixy::futex::detail::wait_on(state_, kContended, ::foundation::core::none)
                                  .expect("the kernel refused the futex wait of a blocking lock on its own word"));
            observed = state_.exchange_acq_rel(kContended);
        }
    }

    [[gnu::cold, gnu::noinline]] void wake_one_waiter_() noexcept {
        static_cast<void>(::fixy::futex::detail::wake_waiters(state_, 1).expect(
            "the kernel refused the futex wake of a blocking lock on its own word"));
    }

    ::fixy::futex::FutexWord state_{kFree};
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

// A context that may wait on a spin gate: one that owns Div, because the
// wait has no exit until the release, and owns neither IO nor Block.  Each
// of the two lets the holder make a system call or sleep inside the
// section, and the waiters spin for all of it.  A context that owns IO
// takes the blocking gate, or narrows its row before it takes the spin
// gate.
template <typename Ctx>
concept CtxMayAcquireSpin = eff::IsExecCtx<Ctx> && eff::CtxOwnsCapability<Ctx, eff::Effect::Div>
                         && !eff::CtxOwnsAnyOf<Ctx, eff::Effect::IO, eff::Effect::Block>;

// A context that may wait on a blocking gate: one that owns Block.
template <typename Ctx>
concept CtxMayBlock = eff::CtxOwnsCapability<Ctx, eff::Effect::Block>;

// The rule of design note 2, one clause for both guards.
// A strategy on either side of the kernel split picks its rule, so the
// clause has no case to forget.
template <typename Ctx, WaitStrategy Strategy>
concept CtxMayAcquire =
    (waits_in_kernel(Strategy) && CtxMayBlock<Ctx>) || (!waits_in_kernel(Strategy) && CtxMayAcquireSpin<Ctx>);

// The tag of a guard that tries the gate one time and does not wait.  Its
// constructor is explicit, so a braced list {} does not make one.
struct try_acquire_t {
    explicit constexpr try_acquire_t() noexcept = default;
};
inline constexpr try_acquire_t try_acquire{};

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
    explicit GateGuard(try_acquire_t, Ctx const& /*ctx*/, lock_type& lock, permission_t& /*proof*/) noexcept
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
GateGuard(try_acquire_t, Ctx const&, Gate<Tag, Strategy>&, perm::Permission<Tag, Brand>&)
    -> GateGuard<Tag, Strategy, Brand>;

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
using SpinGuard = GateGuard<Tag, WaitStrategy::BoundedSpin, Brand>;

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
using BlockingGuard = GateGuard<Tag, WaitStrategy::AcquireWait, Brand>;

// The body of a door of a lock that owns its data (design note 7), which gets
// Data&: it takes the data with no throw, its result holds no borrow and fits
// a Record, it holds no linear value by value, and it stores no borrow
// outside the frame of the lock (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).  A borrow of the data that leaves the
// lock in a capture names data that another holder writes after the unlock,
// so the caller would read it in a data race.
template <class Body, class Data>
concept LockedBody = std::is_nothrow_invocable_v<Body&&, Data&>
                  && (std::is_void_v<std::invoke_result_t<Body&&, Data&>>
                      || (::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Data&>>
                          && ::foundation::core::RecordElement<std::remove_cv_t<std::invoke_result_t<Body&&, Data&>>>))
                  && ::foundation::linear::HoldsNoLinearValue<Body> && ::foundation::core::StoresNoBorrowOutside<Body>;

// A body of a door of a lock whose result holds a borrow: the refused twin of
// the door binds to it.
template <class Body, class Data>
concept LockedBorrowGivingBody =
    std::is_nothrow_invocable_v<Body&&, Data&> && !std::is_void_v<std::invoke_result_t<Body&&, Data&>>
    && !::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Data&>>;

// A body of a door of a lock that holds a linear value by value: the refused
// twin of the door binds to it.
template <class Body, class Data>
concept LockedLinearBody = std::is_invocable_v<Body&&, Data&> && (!::foundation::linear::HoldsNoLinearValue<Body>);

// A body of a door of a lock that stores a borrow outside the frame of the
// lock: the store twin of the door binds to it.  A body whose result holds a
// borrow binds to the twin of the result, so one twin gives the refusal.
template <class Body, class Data>
concept LockedStoringBody = std::is_invocable_v<Body&&, Data&> && ::foundation::linear::HoldsNoLinearValue<Body>
                         && (!::foundation::core::StoresNoBorrowOutside<Body>)
                         && (std::is_void_v<std::invoke_result_t<Body&&, Data&>>
                             || ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Data&>>);

// The value of try_with for a body that gives Given: Given, or Unit for a
// body that gives void.
template <class Given>
using locked_value_t = std::conditional_t<std::is_void_v<Given>, ::foundation::core::Unit, Given>;

namespace detail {

// The class that a tag of a Locked names in start_or_end_owner, or no class.
// Only that class calls the door of the lock with no context
// (with_at_owner_start_or_end of Locked).
struct NoStartOrEndOwner final {};

template <typename Tag>
struct start_or_end_owner {
    using type = NoStartOrEndOwner;
};
template <typename Tag>
    requires requires { typename Tag::start_or_end_owner; }
struct start_or_end_owner<Tag> {
    using type = typename Tag::start_or_end_owner;
};

}  // namespace detail

// The key of the constructor of a guard of a Locked.  Only a Locked builds
// one, so only a Locked builds a guard.  The seal closes the byte routes to a
// key.
class LockedGuardKey final {
    template <class, typename, WaitStrategy>
    friend class Locked;

    constexpr LockedGuardKey() noexcept = default;

    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    LockedGuardKey(LockedGuardKey const&) = delete("a key of a guard lives for one construction of a guard");
    LockedGuardKey& operator=(LockedGuardKey const&) = delete("a key of a guard lives for one construction of a guard");
};

// The guard of a Locked: it holds the bare lock of the Locked and lends its
// data, Data& with Data the T or T const.  Only the Locked builds one, after
// it took the lock.  The guard does not copy and does not move, and its end
// gives the lock back, so each acquisition has one release, by its holder.
template <class Data, class Lock>
class [[nodiscard]] LockedGuard {
public:
    using data_type = Data;

    LockedGuard(const LockedGuard&) = delete("a guard is the one holder of its lock: a copy would give the lock back "
                                             "two times");
    LockedGuard& operator=(const LockedGuard&) = delete("a guard holds one lock for its life");
    LockedGuard(LockedGuard&&) = delete("a guard gives its lock back at the end of its scope: a moved guard would give "
                                        "the lock back two times");
    LockedGuard& operator=(LockedGuard&&) = delete("a guard holds one lock for its life");

    ~LockedGuard() noexcept { lock_.unlock(); }

    // body gets the data, and the door gives the result of body.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires LockedBody<Body, Data>
    constexpr auto with(Body&& body) & noexcept {
        return static_cast<Body&&>(body)(data_);
    }

    // A body whose result holds a borrow, or that holds a linear value by
    // value, binds to these overloads, so the refusal gives its reason.
    template <class Body>
        requires LockedBorrowGivingBody<Body, Data>
    void with(Body&& body) & = delete("the result of a body of a lock leaves the lock, so it holds no reference and no "
                                      "address: give a plain value");
    template <class Body>
        requires LockedLinearBody<Body, Data>
    void with(Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires LockedStoringBody<Body, Data>
    void with(Body&& body) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);

private:
    template <class, typename, WaitStrategy>
    friend class Locked;

    // The guard of a lock that the Locked took, with the key that only a
    // Locked builds.  The lock and the data are members of one Locked.
    constexpr LockedGuard(LockedGuardKey const& /*key*/, Lock& lock, Data& data) noexcept : lock_{lock}, data_{data} {}

    Lock& lock_;
    Data& data_;
};

// A lock that owns its data (design note 7): the bare lock of the strategy and
// one T, both private.  The tag names the data, as the tag of a gate does.
template <class T, typename Tag, WaitStrategy Strategy>
class alignas(64) Locked {
    static_assert(perm::PermissionTag<Tag>, "fixy::spin::Locked<T, Tag, Strategy>: Tag must satisfy the PermissionTag "
                                            "concept, that is an empty non-union class type, such as an empty struct "
                                            "nested in the owning class.");

    using substrate_t = typename detail::gate_substrate<Strategy>::type;
    using guard_t = LockedGuard<T, substrate_t>;
    using read_guard_t = LockedGuard<T const, substrate_t>;

public:
    using value_type = T;
    using tag_type = Tag;
    using row_discipline = std::conditional_t<waits_in_kernel(Strategy), ::fixy::row_discipline::blocking_locked,
                                              ::fixy::row_discipline::spin_locked>;
    using row_payload = T;

    static constexpr WaitStrategy wait_strategy = Strategy;
    // A lock whose waiter sleeps belongs off the hot path.
    static constexpr cache_tier_t cache_tier = waits_in_kernel(Strategy) ? cache_tier_t::Cold : cache_tier_t::Hot;

    // The T that the arguments build.
    template <class... Args>
        requires std::is_nothrow_constructible_v<T, Args&&...>
    explicit constexpr Locked(Args&&... args) noexcept : data_(static_cast<Args&&>(args)...) {}

    Locked(const Locked&) = delete("a lock is an identity; copying one would make two locks over one value");
    Locked& operator=(const Locked&) = delete("a lock is an identity; copying one would make two locks over one value");
    Locked(Locked&&) = delete("a lock is an identity; moving one would leave a waiter on the old address");
    Locked& operator=(Locked&&) = delete("a lock is an identity; moving one would leave a waiter on the old address");
    ~Locked() = default;

    // Takes the lock and gives the guard that holds it.  The guard comes back
    // as a prvalue, so it does not move:
    //   auto guard = locked.lock(ctx);
    // A const Locked gives a guard that lends T const&.
    template <typename Ctx>
        requires CtxMayAcquire<Ctx, Strategy>
    [[nodiscard]] guard_t lock(Ctx const& /*ctx*/) & noexcept {
        substrate_.lock();
        return guard_t{LockedGuardKey{}, substrate_, data_};
    }
    template <typename Ctx>
        requires CtxMayAcquire<Ctx, Strategy>
    [[nodiscard]] read_guard_t lock(Ctx const& /*ctx*/) const& noexcept {
        substrate_.lock();
        return read_guard_t{LockedGuardKey{}, substrate_, data_};
    }

    // Takes the lock, lends T& to body, gives the lock back, and gives the
    // result of body.  A const Locked lends T const&.  Complexity: the wait
    // for the lock, O(1), and the cost of body.
    template <typename Ctx, class Body>
        requires CtxMayAcquire<Ctx, Strategy> && LockedBody<Body, T>
    constexpr auto with(Ctx const& ctx, Body&& body) & noexcept {
        guard_t guard = lock(ctx);
        return guard.with(static_cast<Body&&>(body));
    }
    template <typename Ctx, class Body>
        requires CtxMayAcquire<Ctx, Strategy> && LockedBody<Body, T const>
    constexpr auto with(Ctx const& ctx, Body&& body) const& noexcept {
        read_guard_t guard = lock(ctx);
        return guard.with(static_cast<Body&&>(body));
    }

    // with, when the lock is free: the value of body, or Unit for a body that
    // gives void.  No value when another holder has the lock, and then body
    // does not run.  Complexity: O(1), and the cost of body.
    template <typename Ctx, class Body>
        requires CtxMayAcquire<Ctx, Strategy> && LockedBody<Body, T>
    [[nodiscard]] constexpr ::foundation::core::Option<locked_value_t<std::invoke_result_t<Body&&, T&>>>
    try_with(Ctx const& /*ctx*/, Body&& body) & noexcept {
        return try_lend_(substrate_, data_, static_cast<Body&&>(body));
    }
    template <typename Ctx, class Body>
        requires CtxMayAcquire<Ctx, Strategy> && LockedBody<Body, T const>
    [[nodiscard]] constexpr ::foundation::core::Option<locked_value_t<std::invoke_result_t<Body&&, T const&>>>
    try_with(Ctx const& /*ctx*/, Body&& body) const& noexcept {
        return try_lend_(substrate_, data_, static_cast<Body&&>(body));
    }

    // A body whose result holds a borrow, that holds a linear value by value,
    // or that stores a borrow outside the frame, binds to these overloads, so
    // the refusal gives its reason.
    template <typename Ctx, class Body>
        requires LockedBorrowGivingBody<Body, T>
    void with(Ctx const& ctx, Body&& body) & = delete("the result of a body of a lock leaves the lock, so it holds no "
                                                      "reference and no address: give a plain value");
    template <typename Ctx, class Body>
        requires LockedLinearBody<Body, T>
    void with(Ctx const& ctx, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <typename Ctx, class Body>
        requires CtxMayAcquire<Ctx, Strategy> && LockedStoringBody<Body, T>
    void with(Ctx const& ctx, Body&& body) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);
    template <typename Ctx, class Body>
        requires CtxMayAcquire<Ctx, Strategy> && LockedStoringBody<Body, T const>
    void with(Ctx const& ctx, Body&& body) const& = delete(CRUCIBLE_STORING_BODY_REFUSAL);

private:
    friend typename detail::start_or_end_owner<Tag>::type;

    // The door of the owner that the tag names in start_or_end_owner, at the
    // start or the end of an owner that holds no context, such as a
    // constructor or a destructor that registers or withdraws a resource.
    // It takes the lock with no context, so it asks no Block, and a waiter
    // sleeps.  Only a blocking lock has it, because a spin waiter with no
    // context could stand on the hot path.  It is private, and only the
    // named owner is a friend.  The residual: a call of this door blocks with
    // no Block row in the type of its caller.
    template <class Body>
        requires(waits_in_kernel(Strategy)) && LockedBody<Body, T>
    constexpr auto with_at_owner_start_or_end(Body&& body) & noexcept {
        substrate_.lock();
        guard_t guard{LockedGuardKey{}, substrate_, data_};
        return guard.with(static_cast<Body&&>(body));
    }
    template <class Body>
        requires(waits_in_kernel(Strategy)) && LockedBody<Body, T const>
    constexpr auto with_at_owner_start_or_end(Body&& body) const& noexcept {
        substrate_.lock();
        read_guard_t guard{LockedGuardKey{}, substrate_, data_};
        return guard.with(static_cast<Body&&>(body));
    }

    // The try of try_with: the guard of the lock when the try takes it, and
    // the value of body.
    template <class Data, class Body>
    [[nodiscard]] static constexpr ::foundation::core::Option<locked_value_t<std::invoke_result_t<Body&&, Data&>>>
    try_lend_(substrate_t& lock, Data& data, Body&& body) noexcept {
        using Given = std::invoke_result_t<Body&&, Data&>;
        using Value = locked_value_t<Given>;
        if (!lock.try_lock()) {
            return ::foundation::core::Option<Value>{::foundation::core::none};
        }
        LockedGuard<Data, substrate_t> guard{LockedGuardKey{}, lock, data};
        if constexpr (std::is_void_v<Given>) {
            guard.with(static_cast<Body&&>(body));
            return ::foundation::core::Option<Value>::some(::foundation::core::Unit{});
        } else {
            return ::foundation::core::Option<Value>::some(guard.with(static_cast<Body&&>(body)));
        }
    }

    // The lock word changes under a const Locked, so it is mutable.  The data
    // follows the lock, on the same line when it fits.
    mutable substrate_t substrate_{};
    T data_;
};

template <class T, typename Tag>
using SpinLocked = Locked<T, Tag, WaitStrategy::BoundedSpin>;

template <class T, typename Tag>
using BlockingLocked = Locked<T, Tag, WaitStrategy::AcquireWait>;

}  // namespace fixy::spin
