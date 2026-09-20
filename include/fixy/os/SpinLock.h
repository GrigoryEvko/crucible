#pragma once

// The spin gate: a bare test-and-set lock, and the witnessed lock over it
// whose acquisition costs a Permission and a foreground context.
//
// Old spelling: include/crucible/fixy/concurrent/SpinLock.h for the
// witnessed half and include/crucible/concurrent/SpinLock.h for the bare
// one.  Both live here, because the witnessed lock embeds the bare one
// and nothing else in the new tree has a use for it.
//
// Deviations, each deliberate:
//
//  1. Acquiring takes a context.  lock() and try_lock() are private, and
//     the way in is lock_in<Ctx> / try_lock_in<Ctx>, whose clause refuses
//     a context that owns Effect::Bg.  The old header had both doors
//     public, so the eleven production sites in
//     include/crucible/cntp/ConnectionPoolRuntime.h took the ungated one
//     and the Bg exclusion never applied to them.  A gate with a public
//     bypass beside it is not a gate.  Those sites pass their context at
//     Stage C2e.
//
//  2. The Bg exclusion is the point, restated because the old comment
//     buried it: a background context may also allocate, syscall or
//     block, and none of the three belongs inside a critical section
//     another thread is spinning on.  The spin burns a core for as long
//     as the holder takes.
//
//  3. unlock() takes the same witness lock did.  The old one took none,
//     which made the pair asymmetric: the type system priced acquisition
//     and gave release away.  A caller who can reach unlock without a
//     Permission can release a gate it never acquired.
//
//  4. SpinGuard carries the context into the lock and the witness to the
//     unlock, so the RAII path and the manual path are gated alike.  It
//     also keeps the reference to the Permission, which deviation 3
//     makes necessary.
//
//  5. cache_tier_hot is gone.  It was a namespace-scope constant that
//     restated SpinLock<Tag>::cache_tier, and nothing read it.
//
//  6. There is no unwitnessed guard and no substrate() accessor.  The old
//     tree had both: a SpinGuard over the bare lock, and an escape hatch
//     that handed the bare lock out of the witnessed one "for
//     interoperating with lock adaptors".  Either one locks the gate
//     without a Permission, which is the discipline this header exists to
//     impose, and the accessor had no callers at all.  The bare lock is
//     still here, spelled UnwitnessedSpinLock so that choosing it reads
//     as the choice it is; the old-tree sites that take a bare guard
//     (include/crucible/BackgroundThread.h,
//     include/crucible/cntp/BackpressureRuntime.h) mint a Permission for
//     their gate's tag at Stage C2e and use the witnessed guard.

#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>
#include <type_traits>

namespace fixy::spin {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

using cache_tier_t = ::foundation::algebra::lattices::HotPathTier;

// The alignment lives on the primitive rather than at each embedding
// field, so a lock placed in an array or next to another lock is isolated
// without the embedding site having to remember it.
//
// The name says what it is missing.  Every acquisition through this type
// is unwitnessed: it proves no ownership of the data the gate protects.
// SpinLock<Tag> below is the type production code names.
class alignas(64) UnwitnessedSpinLock {
public:
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

static_assert(alignof(UnwitnessedSpinLock) >= 64, "the lock must be cache-line-aligned to prevent false sharing "
                                                  "across embedded array slots and adjacent struct members");
static_assert(sizeof(UnwitnessedSpinLock) >= 64, "the lock occupies a full cache line; trailing padding is "
                                                 "intentional — adjacent locks in an array must land on "
                                                 "distinct lines");
// The storage is std::atomic_flag rather than std::atomic<bool> because
// [atomics.flag] makes it the only type whose operations are guaranteed
// lock-free on every conforming implementation. The guarantee is
// unconditional, which is why the type carries no is_always_lock_free
// member to test.

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class SpinGuard;

// A context that may acquire the gate: one that exists, and one that does
// not own the background capability.  Named so the two doors and the
// guard state one condition rather than three copies of it.
template <typename Ctx>
concept CtxMayAcquireSpin = eff::IsExecCtx<Ctx> && (!eff::CtxOwnsCapability<Ctx, eff::Effect::Bg>);

template <typename Tag>
class alignas(64) SpinLock {
    static_assert(perm::PermissionTag<Tag>, "fixy::spin::SpinLock<Tag>: Tag must satisfy the PermissionTag "
                                            "concept, that is an empty non-union class type. Typically an "
                                            "empty struct nested in the owning class.");

public:
    using tag_type = Tag;
    using substrate_t = UnwitnessedSpinLock;
    using permission_t = perm::Permission<Tag>;

    static constexpr cache_tier_t cache_tier = cache_tier_t::Hot;

    constexpr SpinLock() noexcept = default;

    SpinLock(const SpinLock&) = delete("a gate is an identity; copying one would make two gates over one region");
    SpinLock& operator=(const SpinLock&) =
        delete("a gate is an identity; copying one would make two gates over one region");
    SpinLock(SpinLock&&) = delete("a gate is an identity; moving one would leave a waiter spinning on the old address");
    SpinLock& operator=(SpinLock&&) =
        delete("a gate is an identity; moving one would leave a waiter spinning on the old address");

    // The two doors.  The context is read by the clause and by nothing
    // else; the proof is a compile-time witness and the body ignores it.
    // The proof is taken under whatever brand it carries: the gate is
    // keyed by its tag, and a permission of any instance of that tag
    // witnesses the acquisition.
    template <CtxMayAcquireSpin Ctx, typename Brand>
    void lock_in(Ctx const& /*ctx*/, perm::Permission<Tag, Brand>& proof) noexcept {
        lock(proof);
    }

    template <CtxMayAcquireSpin Ctx, typename Brand>
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
    // way in, and SpinGuard goes through them too.
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

// The probe tag must be a complete empty struct. An incomplete type is not
// introspectable by is_empty_v, so PermissionTag cannot be satisfied.
namespace spinlock_size_probe_ {
struct SizeProbe {};
}  // namespace spinlock_size_probe_

static_assert(alignof(SpinLock<spinlock_size_probe_::SizeProbe>) == alignof(UnwitnessedSpinLock),
              "fixy::spin::SpinLock<Tag> must inherit substrate alignment (64 bytes). A cross-thread spin "
              "gate relies on cache-line isolation to stay clear of false sharing.");
static_assert(sizeof(SpinLock<spinlock_size_probe_::SizeProbe>) == sizeof(UnwitnessedSpinLock),
              "fixy::spin::SpinLock<Tag> must be zero-overhead over the substrate — the Tag is phantom and "
              "must EBO-collapse to zero bytes.");

// Copy and move are deleted: a second guard over the same lock would release
// it twice and break the acquire/release pairing.
//
// The guard holds a reference to the proof for the release, so it
// carries the proof's brand.  A proof minted by a root mint carries a
// fresh brand, so the guard is deduced rather than spelled:
// `SpinGuard guard{ctx, gate, proof};`.  The deduction guides below
// are what make that spelling work.
template <typename Tag, typename Brand>
class SpinGuard {
public:
    using lock_type = SpinLock<Tag>;
    using permission_t = perm::Permission<Tag, Brand>;
    using brand_type = Brand;

    template <CtxMayAcquireSpin Ctx>
    explicit SpinGuard(Ctx const& ctx, lock_type& lock, permission_t& proof) noexcept : lock_{lock}, proof_{proof} {
        lock_.lock_in(ctx, proof);
    }

    template <CtxMayAcquireSpin Ctx>
    explicit SpinGuard(std::try_to_lock_t, Ctx const& ctx, lock_type& lock, permission_t& proof) noexcept
        : lock_{lock}, proof_{proof}, acquired_{lock.try_lock_in(ctx, proof)} {}

    SpinGuard(const SpinGuard&) = delete("two guards over one gate would release it twice");
    SpinGuard& operator=(const SpinGuard&) = delete("two guards over one gate would release it twice");
    SpinGuard(SpinGuard&&) = delete("a moved-from guard would still release on scope exit");
    SpinGuard& operator=(SpinGuard&&) = delete("a moved-from guard would still release on scope exit");

    ~SpinGuard() noexcept {
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

template <typename Ctx, typename Tag, typename Brand>
SpinGuard(Ctx const&, SpinLock<Tag>&, perm::Permission<Tag, Brand>&) -> SpinGuard<Tag, Brand>;

template <typename Ctx, typename Tag, typename Brand>
SpinGuard(std::try_to_lock_t, Ctx const&, SpinLock<Tag>&, perm::Permission<Tag, Brand>&) -> SpinGuard<Tag, Brand>;

}  // namespace fixy::spin

namespace fixy::spin::detail::spinlock_self_test {

struct ProbeTag {};

static_assert(std::is_same_v<SpinLock<ProbeTag>::substrate_t, UnwitnessedSpinLock>,
              "fixy::spin::SpinLock<Tag>::substrate_t must alias UnwitnessedSpinLock — substrate identity "
              "drift would break the alignas(64) and acquire/release contract.");

static_assert(std::is_same_v<SpinLock<ProbeTag>::permission_t, ::foundation::permissions::Permission<ProbeTag>>,
              "fixy::spin::SpinLock<Tag>::permission_t must alias Permission<Tag> — drift would break the "
              "Permission-witness-at-acquire discipline.");

static_assert(SpinLock<ProbeTag>::cache_tier == cache_tier_t::Hot,
              "fixy::spin::SpinLock<Tag>::cache_tier must be Hot. The annotation is how a hot-path spin "
              "gate is located.");

static_assert(std::is_same_v<SpinLock<ProbeTag>::tag_type, ProbeTag>);

// Neither the gate nor the guard can be copied or moved.
static_assert(!std::is_copy_constructible_v<SpinLock<ProbeTag>>);
static_assert(!std::is_move_constructible_v<SpinLock<ProbeTag>>);
static_assert(!std::is_copy_constructible_v<SpinGuard<ProbeTag>>);
static_assert(!std::is_move_constructible_v<SpinGuard<ProbeTag>>);
static_assert(!std::is_copy_constructible_v<UnwitnessedSpinLock>);
static_assert(!std::is_move_constructible_v<UnwitnessedSpinLock>);

// The clause admits a foreground context and refuses a background one,
// which is deviation 1 and deviation 2 stated as cells rather than left
// to the reader.  Both contexts are built here, the way the sibling os
// headers build theirs.
using ForegroundCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
using BackgroundCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

static_assert(CtxMayAcquireSpin<ForegroundCtx>,
              "a context without Bg must be able to acquire: that is the shape a foreground caller has.");
static_assert(!CtxMayAcquireSpin<BackgroundCtx>,
              "a context that owns Bg must NOT acquire. A background context may also allocate, syscall or "
              "block, and none of the three belongs inside a section another thread spins on.");
static_assert(!CtxMayAcquireSpin<int>, "a non-context must not pass for one.");

// Three claims about this header cannot be written here.  An access
// failure inside a requires-expression is a hard error in GCC 16 rather
// than a substitution failure, so `!requires { gate.lock(proof); }` does
// not compile even when the answer is the one wanted.  The claims are
// carried by negative-compile fixtures instead:
//
//   neg_spin_lock_door_is_private       — gate.lock(proof) from outside
//   neg_spin_try_lock_door_is_private   — gate.try_lock(proof) from outside
//   neg_spin_unlock_without_witness     — gate.unlock() with no Permission
//   neg_spin_guard_under_bg_ctx         — a Bg context at the guard
//   neg_spin_lock_without_permission    — a guard with no Permission at all

}  // namespace fixy::spin::detail::spinlock_self_test
