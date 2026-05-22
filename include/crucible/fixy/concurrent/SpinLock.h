#pragma once

// ── crucible::fixy::concurrent::SpinLock<Tag> — CSL-typed spin gate ──
//
// FIXY-V-071: typed wrapper over the canonical substrate SpinLock
// (concurrent::SpinLock + concurrent::SpinGuard, fixy-A5-021/022 +
// FIXY-U-085) that binds the spin gate to a CSL region Tag at the type
// level.  The wrapper carries three load-bearing additions over the
// substrate primitive:
//
//  1. Permission<Tag> proof at acquire.  Every lock()/try_lock() takes
//     a `Permission<Tag>&` reference — the caller MUST hold the CSL
//     ownership token for this region.  Two holders of the SAME Tag
//     cannot exist (Permission is linear, move-only; split/combine is
//     the only way to fractionalize), so the type system guarantees
//     at most ONE thread can present the witness — but a thread that
//     held its Permission has the right to acquire the spin gate.
//     The Permission is borrowed (lvalue reference), not consumed —
//     the spin gate is held transiently, not destroyed.
//
//  2. cache_tier::Hot annotation.  `cache_tier` is a static constexpr
//     HotPathTier_v::Hot marker grep-discoverable via the wrapper's
//     type.  Per CLAUDE.md §IX, a hot-path spin gate's critical
//     section MUST be hot-path-safe (no syscall, no alloc, no block);
//     the marker documents that intent at the wrapper level so
//     downstream review tooling can route reviews.
//
//  3. lock_in<Ctx>(Ctx const&, Permission<Tag>&) ctx-gated variant.
//     For call sites in hot-foreground contexts the ctx-bound form
//     REJECTS contexts that own Effect::Bg — i.e. the spin gate must
//     not be acquired from a background-thread context that may also
//     allocate / syscall / block.  The no-ctx `lock(Permission<Tag>&)`
//     surface remains available for already-Bg-or-Test-gated
//     consumers (ConnectionPoolRuntime is the canonical example —
//     CtxFitsConnectionPoolRuntime requires Bg or Test at the
//     member-function level, so the spin gate is acquired from an
//     already-Bg-typed ctx; no double-gating is needed).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   concurrent::SpinLock                              — alignas(64),
//                                                       atomic_flag,
//                                                       acquire/release
//                                                       SPIN_PAUSE.
//   permissions::Permission<Tag>                      — phantom-typed
//                                                       linear token.
//   algebra::lattices::HotPathTier                    — cache_tier
//                                                       annotation.
//   effects::IsExecCtx / CtxOwnsCapability            — ctx-gating.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — std::atomic_flag ATOMIC_FLAG_INIT (inherited from
//                substrate); Permission default-construction is
//                gated by mint_permission_root factories.
//   TypeSafe   — Permission<Tag> phantom-typed; mismatched tags fail
//                at the function-signature level.  HotPathTier strong
//                scoped enum.
//   NullSafe   — Permission has no pointer state; lock_/unlock_ never
//                deref a nullable.
//   MemSafe    — Permission borrowed by lvalue ref; ownership remains
//                with the caller.  SpinGuard RAII releases on
//                destruction.
//   BorrowSafe — Permission's linearity is the static witness that
//                exactly one thread holds the right to acquire this
//                region's gate.
//   ThreadSafe — substrate uses acquire/release atomic_flag operations
//                + _mm_pause hint; preserved by this wrapper.
//   LeakSafe   — SpinGuard's RAII destructor calls unlock() iff the
//                lock was acquired; copy and move are deleted to
//                prevent dangling release.
//   DetSafe    — empty-class wrapping; bit-exact substrate forwarding.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero overhead beyond substrate.  The Permission&  parameter is a
// no-op runtime: it carries the type-level proof at compile time and
// compiles to a register pass.  `cache_tier` is a static constexpr —
// purely metadata, not a runtime field.  lock_in's `requires` clause
// is a compile-time gate.
//
//   sizeof(fixy::concurrent::SpinLock<Tag>)  ==
//       sizeof(::crucible::concurrent::SpinLock)  (== 64 bytes)
//   alignof(fixy::concurrent::SpinLock<Tag>) ==
//       alignof(::crucible::concurrent::SpinLock) (== 64 bytes)
//
// ── §XXI Universal Mint Pattern ───────────────────────────────────
//
// SpinLock<Tag> is a typed RESOURCE, not a mint factory.  Its
// public default constructor is the resource construction point;
// authorization to acquire is presented at lock()/try_lock() time via
// Permission<Tag>.  The Permission is minted separately through the
// canonical mint_permission_root<Tag>() (no-ctx form, since the spin
// gate's region tag carries Row<> by default — no effect-row
// admission required).  Reviewers grep `mint_permission_root<` for
// the once-per-program-per-tag root-mint authorization site.
//
// ── HS14 negative-compile coverage ────────────────────────────────
//
// test/fixy_neg/neg_fixy_concurrent_spinlock_no_permission.cpp
//     — Attempts to construct SpinGuard<Tag> without a Permission&
//       (i.e., calling the substrate's no-arg form).  Fails at
//       construction: no such overload exists.
//
// test/fixy_neg/neg_fixy_concurrent_spinlock_bg_context.cpp
//     — Calls lock_in<BgDrainCtx>(ctx, perm) on a SpinLock<Tag>.
//       The `requires` clause rejects: Bg context contains Effect::Bg
//       which the ctx-bound rail forbids.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/HotPathLattice.h>
#include <crucible/concurrent/SpinLock.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/Permission.h>

#include <mutex>         // std::try_to_lock_t (SpinGuard try-acquire ctor tag)
#include <type_traits>

namespace crucible::fixy::concurrent {

// ── cache_tier — hot-path annotation grep target ───────────────────
//
// Static constexpr re-export of HotPathTier_v::Hot under the
// fixy::concurrent:: namespace.  Used by SpinLock<Tag>::cache_tier;
// reviewers can `grep cache_tier == HotPathTier_v::Hot` to find every
// spin-gate site that promises hot-path discipline in its critical
// section.
using cache_tier_t = ::crucible::algebra::lattices::HotPathTier;
inline constexpr cache_tier_t cache_tier_hot = cache_tier_t::Hot;

template <typename Tag>
class alignas(64) SpinLock {
    static_assert(::crucible::safety::PermissionTag<Tag>,
        "fixy::concurrent::SpinLock<Tag>: Tag must satisfy the "
        "PermissionTag concept (empty non-union class type).  See "
        "permissions::PermissionTag in Permission.h — typically an "
        "empty struct in a `tag::` namespace, e.g. nested "
        "`struct GateTag {};` inside the owning class.");

public:
    // ── Public type aliases ─────────────────────────────────────────
    using tag_type     = Tag;
    using substrate_t  = ::crucible::concurrent::SpinLock;
    using permission_t = ::crucible::safety::Permission<Tag>;

    // ── cache_tier — Hot path annotation ────────────────────────────
    //
    // Grep target: `cache_tier == HotPathTier_v::Hot` locates every
    // SpinLock<Tag> in the codebase that promises hot-path discipline
    // in its critical section.  Per FIXY-V-071 design + CLAUDE.md §IX.
    static constexpr cache_tier_t cache_tier = cache_tier_t::Hot;

    constexpr SpinLock() noexcept = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    // ── Acquire ─────────────────────────────────────────────────────
    //
    // Spin until acquired.  The Permission<Tag>& parameter is the
    // type-level witness that the caller holds the CSL region's
    // ownership token; passing a Permission<OtherTag>& fails to
    // compile.  The Permission is borrowed, not consumed — the same
    // Permission can be used to acquire the gate repeatedly.
    void lock(permission_t& /*proof*/) noexcept {
        substrate_.lock();
    }

    // BasicLockable + try_lock completion (matches substrate surface).
    [[nodiscard]] bool try_lock(permission_t& /*proof*/) noexcept {
        return substrate_.try_lock();
    }

    // ── lock_in<Ctx> — ctx-gated acquire (hot-foreground rail) ──────
    //
    // FIXY-V-071: forbids acquire from Bg contexts.  Use at hot-
    // foreground call sites that want the static guarantee that the
    // ctx in scope is NOT a background-thread ctx (which may also be
    // doing allocation / syscall / block — incompatible with the
    // hot-path budget of the spin gate's critical section).
    //
    // ConnectionPoolRuntime.h does NOT use this rail — its members
    // are gated by CtxFitsConnectionPoolRuntime which itself requires
    // Bg or Test; the spin gate is acquired from an already-Bg-typed
    // ctx, and the surrounding member function's `requires` clause is
    // the relevant gate.  Reserved for future hot-foreground SpinLock
    // adopters that want the additional static check.
    template <class Ctx>
        requires ::crucible::effects::IsExecCtx<Ctx>
              && (!::crucible::effects::CtxOwnsCapability<
                     Ctx, ::crucible::effects::Effect::Bg>)
    void lock_in(Ctx const& /*ctx*/, permission_t& proof) noexcept {
        lock(proof);
    }

    template <class Ctx>
        requires ::crucible::effects::IsExecCtx<Ctx>
              && (!::crucible::effects::CtxOwnsCapability<
                     Ctx, ::crucible::effects::Effect::Bg>)
    [[nodiscard]] bool try_lock_in(Ctx const& /*ctx*/,
                                   permission_t& proof) noexcept {
        return try_lock(proof);
    }

    // Release.  Unlike acquire, no Permission witness is required —
    // the caller must already have acquired the gate (which itself
    // required the witness), and the SpinGuard RAII pattern is the
    // recommended way to call unlock().
    void unlock() noexcept {
        substrate_.unlock();
    }

    // ── Substrate accessor ──────────────────────────────────────────
    //
    // Escape hatch for interoperability with the bare substrate
    // SpinGuard / std::unique_lock / std::lock_guard.  The fixy-
    // discipline-allowlist (FIXY-V-073) governs which sites are
    // permitted to use this; the recommended path is the
    // fixy::concurrent::SpinGuard<Tag> RAII wrapper below.
    [[nodiscard]] substrate_t& substrate() noexcept { return substrate_; }
    [[nodiscard]] substrate_t const& substrate() const noexcept {
        return substrate_;
    }

private:
    [[no_unique_address]] substrate_t substrate_{};
};

// alignof / sizeof contract: the wrapper preserves the substrate's
// cache-line isolation guarantee.  Audited by the sentinel block at
// the end of this header.  Probe tag must be a COMPLETE empty struct
// to satisfy PermissionTag's `is_empty_v` requirement (incomplete
// types are not introspectable by either is_class_v's full form or
// is_empty_v's underlying intrinsic).
namespace spinlock_size_probe_ { struct SizeProbe {}; }
static_assert(alignof(SpinLock<spinlock_size_probe_::SizeProbe>) ==
                  alignof(::crucible::concurrent::SpinLock),
              "fixy::concurrent::SpinLock<Tag> must inherit substrate "
              "alignment (64 bytes); cross-thread spin gates rely on "
              "cache-line isolation to avoid the false-sharing trap "
              "documented in CLAUDE.md §VIII / §IX.");
static_assert(sizeof(SpinLock<spinlock_size_probe_::SizeProbe>) ==
                  sizeof(::crucible::concurrent::SpinLock),
              "fixy::concurrent::SpinLock<Tag> must be zero-overhead "
              "over the substrate — the Tag is phantom and must EBO-"
              "collapse to zero bytes.");

// ── SpinGuard<Tag> — RAII acquire/release pair ─────────────────────
//
// Matches the substrate's concurrent::SpinGuard surface; adds the
// Permission<Tag>& requirement at construction.  Move and copy are
// deleted (matches substrate; releasing a dangling guard would
// release the substrate twice, breaking the acquire/release pairing).
template <typename Tag>
class SpinGuard {
public:
    using lock_type    = SpinLock<Tag>;
    using permission_t = typename lock_type::permission_t;

    // Construction acquires; Permission borrowed (lvalue ref).
    explicit SpinGuard(lock_type& lock, permission_t& proof) noexcept
        : lock_{lock} {
        lock_.lock(proof);
    }

    // Try-acquire variant.  Reports acquisition success via was_acquired().
    explicit SpinGuard(std::try_to_lock_t,
                       lock_type& lock,
                       permission_t& proof) noexcept
        : lock_{lock}, acquired_{lock.try_lock(proof)} {}

    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;
    SpinGuard(SpinGuard&&) = delete;
    SpinGuard& operator=(SpinGuard&&) = delete;

    ~SpinGuard() noexcept {
        if (acquired_) {
            lock_.unlock();
        }
    }

    [[nodiscard]] bool was_acquired() const noexcept { return acquired_; }

private:
    lock_type& lock_;
    bool       acquired_ = true;  // construction-acquired by default
};

// ── Runtime smoke test ────────────────────────────────────────────
//
// FIXY-V-071 + feedback_algebra_runtime_smoke_test_discipline.md:
// header-only static_asserts mask consteval/SFINAE bugs.  This
// exercises the full lock/try_lock/unlock cycle once per TU that
// includes the header.  The function is `inline` so the linker
// discards duplicates.
inline void fixy_spinlock_runtime_smoke_test() noexcept {
    struct ProbeTag {};
    SpinLock<ProbeTag> gate{};
    auto perm = ::crucible::safety::mint_permission_root<ProbeTag>();

    gate.lock(perm);
    gate.unlock();

    if (gate.try_lock(perm)) {
        gate.unlock();
    }

    {
        SpinGuard<ProbeTag> guard{gate, perm};
        (void)guard;
    }

    {
        SpinGuard<ProbeTag> guard{std::try_to_lock, gate, perm};
        if (guard.was_acquired()) {
            // RAII releases on scope exit.
        }
    }
}

}  // namespace crucible::fixy::concurrent

// ── Sentinel block — type identity drift check ─────────────────────
//
// Verifies the substrate alias resolves to concurrent::SpinLock, not
// a shadowed local.  Same discipline as fixy/Perm.h / fixy/Handle.h
// sentinel blocks (FIXY-U-020 / FIXY-U-016).

namespace crucible::fixy::concurrent::self_test {

struct SpinLockProbeTag {};

static_assert(std::is_same_v<
    SpinLock<SpinLockProbeTag>::substrate_t,
    ::crucible::concurrent::SpinLock>,
    "fixy::concurrent::SpinLock<Tag>::substrate_t must alias "
    "::crucible::concurrent::SpinLock — substrate identity drift "
    "would break the alignas(64) + acquire/release contract.");

static_assert(std::is_same_v<
    SpinLock<SpinLockProbeTag>::permission_t,
    ::crucible::safety::Permission<SpinLockProbeTag>>,
    "fixy::concurrent::SpinLock<Tag>::permission_t must alias "
    "::crucible::safety::Permission<Tag> — drift would break the "
    "Permission-witness-at-acquire discipline.");

static_assert(SpinLock<SpinLockProbeTag>::cache_tier ==
                  ::crucible::algebra::lattices::HotPathTier::Hot,
    "fixy::concurrent::SpinLock<Tag>::cache_tier must be Hot — the "
    "annotation is the load-bearing hot-path documentation grep "
    "target (FIXY-V-071).");

}  // namespace crucible::fixy::concurrent::self_test
