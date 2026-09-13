#pragma once

// A bounded-overtaking fairness layer over the plain shared-permission
// pool, whose protocol guarantees correctness but says nothing about
// liveness.  Whichever thread wins the next compare-exchange proceeds,
// so a spinning writer pinned to a core can hold the pool indefinitely
// and starve every reader.
//
// The guarantee is per pool, not per reader.  Between any two successful
// lends, from any readers, the writer wins at most BurstLimit times.
// With N readers contending, one unlucky reader can still be overtaken
// up to N times BurstLimit before its own lend succeeds, because every
// other reader's success replenishes the writer's budget on its behalf.
// A per-reader bounded wait needs queued waiters, which is a different
// primitive with a different cost model, and is not this one.
//
// The bound assumes nothing about the scheduler.  It needs only that the
// writer's exclusive section terminates, because a reader cannot lend
// until the writer deposits.

#include <crucible/Platform.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

namespace crucible::safety {

template <typename Tag, std::uint32_t BurstLimit = 8>
class FairSharedPermissionPool : public Pinned<FairSharedPermissionPool<Tag, BurstLimit>> {
public:
    using tag_type = Tag;
    static constexpr std::uint32_t writer_burst_limit = BurstLimit;

    static_assert(BurstLimit > 0, "FairSharedPermissionPool BurstLimit must be > 0; "
                                  "BurstLimit=0 would forbid every writer upgrade, indefinite starvation.");

    constexpr explicit FairSharedPermissionPool(Permission<Tag>&& exc) noexcept : inner_{std::move(exc)} {}

    // Acquire, so a reader's release-store of zero in lend is visible
    // to the next writer attempt.
    [[nodiscard, gnu::hot]] std::optional<Permission<Tag>> try_upgrade() noexcept {
        if (consecutive_writer_wins_.load(std::memory_order_acquire) >= BurstLimit) {
            return std::nullopt;
        }
        return try_upgrade_unchecked();
    }

    template <::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<Tag, Ctx>
    [[nodiscard, gnu::hot]] std::optional<Permission<Tag>> try_upgrade(Ctx const& ctx) noexcept {
        if (consecutive_writer_wins_.load(std::memory_order_acquire) >= BurstLimit) {
            return std::nullopt;
        }
        return try_upgrade_unchecked(ctx);
    }

    // Only a reader's progress replenishes the budget, so a writer that
    // exhausts its burst with no reader contending would wait for a reset
    // that never comes.  This path skips the gate for that case.  Reach it
    // after observing that no reader is present, and note that it still
    // counts the win, so a bypass stays visible in the diagnostics.
    [[nodiscard, gnu::hot]] std::optional<Permission<Tag>> try_upgrade_unchecked() noexcept {
        auto upgrade = inner_.try_upgrade();
        if (upgrade) {
            consecutive_writer_wins_.fetch_add(1, std::memory_order_acq_rel);
        }
        return upgrade;
    }

    template <::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<Tag, Ctx>
    [[nodiscard, gnu::hot]] std::optional<Permission<Tag>> try_upgrade_unchecked(Ctx const& ctx) noexcept {
        auto upgrade = inner_.try_upgrade(ctx);
        if (upgrade) {
            consecutive_writer_wins_.fetch_add(1, std::memory_order_acq_rel);
        }
        return upgrade;
    }

    // Returning the permission is not reader progress, so this leaves the
    // burst counter alone and the writer may immediately re-upgrade while
    // budget remains.
    void deposit_exclusive(Permission<Tag>&& exc) noexcept { inner_.deposit_exclusive(std::move(exc)); }

    [[nodiscard, gnu::hot]] std::optional<SharedPermissionGuard<Tag>> lend() noexcept {
        auto guard = inner_.lend();
        if (guard) {
            consecutive_writer_wins_.store(0, std::memory_order_release);
        }
        return guard;
    }

    template <::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<Tag, Ctx>
    [[nodiscard, gnu::hot]] std::optional<SharedPermissionGuard<Tag>> lend(Ctx const& ctx) noexcept {
        auto guard = inner_.lend(ctx);
        if (guard) {
            consecutive_writer_wins_.store(0, std::memory_order_release);
        }
        return guard;
    }

    // Writer wins since the last successful lend.  The gated path holds
    // this at or below BurstLimit.  An unchecked upgrade can push it
    // past.
    [[nodiscard]] std::uint64_t consecutive_writer_wins() const noexcept {
        return consecutive_writer_wins_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_burst_exhausted() const noexcept { return consecutive_writer_wins() >= BurstLimit; }

    [[nodiscard]] std::uint64_t outstanding() const noexcept { return inner_.outstanding(); }
    [[nodiscard]] bool is_exclusive_out() const noexcept { return inner_.is_exclusive_out(); }

private:
    SharedPermissionPool<Tag> inner_;

    // Its own cache line, so the reset on every reader success does not
    // invalidate the inner pool's state word.
    //
    // The width is load-bearing.  A 32-bit counter under sustained
    // contention wraps from the limit back to zero, which silently hands
    // the writer a full budget again and voids the fairness guarantee.
    alignas(64) std::atomic<std::uint64_t> consecutive_writer_wins_{0};
};

// The scoped-borrow helpers for the plain pool are constrained to that
// pool's type, so a caller handing one a fair pool would not compile
// without these.

template <typename Tag, std::uint32_t K, typename Body>
    requires std::is_invocable_v<Body, SharedPermission<Tag>>
[[nodiscard]] auto with_shared_read(FairSharedPermissionPool<Tag, K>& pool,
                                    Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>)
    -> std::optional<std::invoke_result_t<Body, SharedPermission<Tag>>>
    requires(!std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>)
{
    auto guard_opt = pool.lend();
    if (!guard_opt) return std::nullopt;
    return std::optional{std::forward<Body>(body)(guard_opt->token())};
}

template <typename Tag, std::uint32_t K, ::crucible::effects::IsExecCtx Ctx, typename Body>
    requires CtxAdmitsPermission<Tag, Ctx>
          && std::is_invocable_v<Body, SharedPermission<Tag>>
             [[nodiscard]] auto
             with_shared_read(Ctx const& ctx, FairSharedPermissionPool<Tag, K>& pool,
                              Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>)
                 -> std::optional<std::invoke_result_t<Body, SharedPermission<Tag>>>
                 requires(!std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>)
{
    auto guard_opt = pool.lend(ctx);
    if (!guard_opt) return std::nullopt;
    return std::optional{std::forward<Body>(body)(guard_opt->token())};
}

template <typename Tag, std::uint32_t K, typename Body>
    requires std::is_invocable_v<Body, SharedPermission<Tag>>
          && std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>
bool with_shared_read(FairSharedPermissionPool<Tag, K>& pool,
                      Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>) {
    auto guard_opt = pool.lend();
    if (!guard_opt) return false;
    std::forward<Body>(body)(guard_opt->token());
    return true;
}

template <typename Tag, std::uint32_t K, ::crucible::effects::IsExecCtx Ctx, typename Body>
    requires CtxAdmitsPermission<Tag, Ctx> && std::is_invocable_v<Body, SharedPermission<Tag>>
          && std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>
bool with_shared_read(Ctx const& ctx, FairSharedPermissionPool<Tag, K>& pool,
                      Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>) {
    auto guard_opt = pool.lend(ctx);
    if (!guard_opt) return false;
    std::forward<Body>(body)(guard_opt->token());
    return true;
}

namespace detail::fair_pool_self_test {

struct TestRegion {};

static_assert(!std::is_copy_constructible_v<FairSharedPermissionPool<TestRegion>>);
static_assert(!std::is_move_constructible_v<FairSharedPermissionPool<TestRegion>>);

static_assert(FairSharedPermissionPool<TestRegion>::writer_burst_limit == 8);
static_assert(FairSharedPermissionPool<TestRegion, 1>::writer_burst_limit == 1);
static_assert(FairSharedPermissionPool<TestRegion, 32>::writer_burst_limit == 32);

static_assert(std::is_same_v<FairSharedPermissionPool<TestRegion>::tag_type, TestRegion>);

static_assert(sizeof(FairSharedPermissionPool<TestRegion>) == sizeof(SharedPermissionPool<TestRegion>) + 64,
              "FairSharedPermissionPool should add exactly one cache line "
              "(the fairness counter) over the inner pool.  If sizeof drifts, "
              "false sharing or padding regression has occurred.");

static_assert(std::is_same_v<decltype(std::declval<FairSharedPermissionPool<TestRegion>>().consecutive_writer_wins()),
                             std::uint64_t>);

}  // namespace detail::fair_pool_self_test

[[gnu::cold]] inline void runtime_smoke_test_fair_shared_permission_pool() noexcept {
    struct SmokeTag {};
    constexpr std::uint32_t kBurst = 4;

    auto exc = mint_permission_root<SmokeTag>();
    FairSharedPermissionPool<SmokeTag, kBurst> pool{std::move(exc)};

    if (pool.consecutive_writer_wins() != 0) std::abort();
    if (pool.is_burst_exhausted()) std::abort();

    for (std::uint32_t i = 0; i < kBurst; ++i) {
        auto u = pool.try_upgrade();
        if (!u) std::abort();
        pool.deposit_exclusive(std::move(*u));
        if (pool.consecutive_writer_wins() != i + 1) std::abort();
    }
    if (!pool.is_burst_exhausted()) std::abort();

    {
        auto u = pool.try_upgrade();
        if (u) std::abort();
    }
    {
        auto u = pool.try_upgrade_unchecked();
        if (!u) std::abort();
        pool.deposit_exclusive(std::move(*u));
    }
    if (pool.consecutive_writer_wins() != kBurst + 1) std::abort();

    {
        auto g = pool.lend();
        if (!g) std::abort();
    }
    if (pool.consecutive_writer_wins() != 0) std::abort();
    if (pool.is_burst_exhausted()) std::abort();

    {
        auto u = pool.try_upgrade();
        if (!u) std::abort();
        pool.deposit_exclusive(std::move(*u));
    }
    if (pool.consecutive_writer_wins() != 1) std::abort();
}

}  // namespace crucible::safety
