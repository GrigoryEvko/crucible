#pragma once

#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Dim.h>
#include <crucible/algebra/lattices/_MemoryScopeLattice.h>
#include <crucible/effects/_ExecCtx.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::async {

using MemoryScope = ::crucible::algebra::lattices::MemoryScope;

// cp.async and mbarrier are accelerator-only constructs. A cp.async fill is
// visible at Cta scope at minimum, and an mbarrier object is addressed at
// .shared::cta or .shared::cluster, so only the accelerator-trunk scopes are
// realizable.
[[nodiscard]] constexpr bool async_scope_realizable(MemoryScope scope) noexcept {
    return ::crucible::algebra::lattices::mem_scope_is_accel(scope);
}

}  // namespace crucible::fixy::async

namespace crucible::fixy::grant::async {

namespace fa = ::crucible::fixy::async;

// Bytes is the per-stage transfer size, not the pipeline total.
template <std::uint8_t Stages, fa::MemoryScope Scope, std::uint32_t Bytes>
struct copy final : grant_base {};

template <fa::MemoryScope Scope>
struct mbarrier_arrive final : grant_base {};

template <fa::MemoryScope Scope>
struct mbarrier_wait final : grant_base {};

}  // namespace crucible::fixy::grant::async

namespace crucible::fixy::grant {

namespace fa = ::crucible::fixy::async;

template <std::uint8_t Stages, fa::MemoryScope Scope, std::uint32_t Bytes>
struct which_dim<async::copy<Stages, Scope, Bytes>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Synchronization> {};

template <fa::MemoryScope Scope>
struct which_dim<async::mbarrier_arrive<Scope>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Synchronization> {};

template <fa::MemoryScope Scope>
struct which_dim<async::mbarrier_wait<Scope>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Synchronization> {};

using accept_default_strict_for_Synchronization = accept_default_strict_for<dim::DimensionAxis::Synchronization>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::async {

namespace ga = ::crucible::fixy::grant::async;

template <std::uint8_t Stages, std::uint32_t Bytes>
using copy_cta = ga::copy<Stages, MemoryScope::Cta, Bytes>;  // cp.async at .shared::cta
template <std::uint8_t Stages, std::uint32_t Bytes>
using copy_cluster = ga::copy<Stages, MemoryScope::Cluster, Bytes>;  // TMA bulk copy at .shared::cluster

using mbarrier_arrive_cta = ga::mbarrier_arrive<MemoryScope::Cta>;
using mbarrier_wait_cta = ga::mbarrier_wait<MemoryScope::Cta>;
using mbarrier_arrive_cluster = ga::mbarrier_arrive<MemoryScope::Cluster>;
using mbarrier_wait_cluster = ga::mbarrier_wait<MemoryScope::Cluster>;

template <typename Ctx>
concept CtxFitsAsyncGrant = ::crucible::effects::IsExecCtx<Ctx>;

template <typename Ctx, std::uint8_t Stages, MemoryScope Scope, std::uint32_t Bytes>
concept CtxFitsAsyncCopyMint = CtxFitsAsyncGrant<Ctx> && (Stages >= 1) && (Bytes > 0) && async_scope_realizable(Scope);

template <typename Ctx, MemoryScope Scope>
concept CtxFitsMbarrierMint = CtxFitsAsyncGrant<Ctx> && async_scope_realizable(Scope);

template <std::uint8_t Stages, MemoryScope Scope, std::uint32_t Bytes, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsyncCopyMint<Ctx, Stages, Scope, Bytes>
[[nodiscard]] constexpr ga::copy<Stages, Scope, Bytes> mint_async_copy(Ctx const&) noexcept {
    return {};
}

template <MemoryScope Scope, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsMbarrierMint<Ctx, Scope>
[[nodiscard]] constexpr ga::mbarrier_arrive<Scope> mint_mbarrier_arrive(Ctx const&) noexcept {
    return {};
}

template <MemoryScope Scope, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsMbarrierMint<Ctx, Scope>
[[nodiscard]] constexpr ga::mbarrier_wait<Scope> mint_mbarrier_wait(Ctx const&) noexcept {
    return {};
}

}  // namespace crucible::fixy::async

namespace crucible::fixy::async::detail::v270_self_test {

namespace ga = ::crucible::fixy::grant::async;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;
namespace eff = ::crucible::effects;

static_assert(IsGrantTag<ga::copy<2, MemoryScope::Cta, 16>>);
static_assert(IsGrantTag<ga::mbarrier_arrive<MemoryScope::Cta>>);
static_assert(IsGrantTag<ga::mbarrier_wait<MemoryScope::Cluster>>);

static_assert(sizeof(ga::copy<4, MemoryScope::Gpu, 256>) == 1);
static_assert(sizeof(ga::mbarrier_arrive<MemoryScope::Warp>) == 1);
static_assert(sizeof(ga::mbarrier_wait<MemoryScope::Cta>) == 1);

static_assert(which_dim_v<ga::copy<2, MemoryScope::Cta, 16>> == D::Synchronization);
static_assert(which_dim_v<ga::mbarrier_arrive<MemoryScope::Cta>> == D::Synchronization);
static_assert(which_dim_v<ga::mbarrier_wait<MemoryScope::Cta>> == D::Synchronization);
static_assert(which_dim_v<copy_cta<2, 16>> == D::Synchronization);
static_assert(which_dim_v<mbarrier_arrive_cluster> == D::Synchronization);

// fix-36: Synchronization grant tags live here, so the axis is not grantless.
static_assert(::crucible::fixy::grant::audit::grant_family_witnessed_v<ga::copy<2, MemoryScope::Cta, 16>>,
              "Async.h ships a grant family for Synchronization, so Synchronization "
              "must not appear in grant::kAxesWithoutNonDefaultGrants.");

static_assert(!std::is_same_v<ga::copy<2, MemoryScope::Cta, 16>, ga::copy<4, MemoryScope::Cta, 16>>);
static_assert(!std::is_same_v<ga::copy<2, MemoryScope::Cta, 16>, ga::copy<2, MemoryScope::Cluster, 16>>);
static_assert(!std::is_same_v<ga::copy<2, MemoryScope::Cta, 16>, ga::copy<2, MemoryScope::Cta, 32>>);
static_assert(!std::is_same_v<ga::mbarrier_arrive<MemoryScope::Cta>, ga::mbarrier_wait<MemoryScope::Cta>>);
static_assert(!std::is_same_v<mbarrier_arrive_cta, mbarrier_arrive_cluster>);
static_assert(std::is_same_v<copy_cta<2, 16>, ga::copy<2, MemoryScope::Cta, 16>>);

static_assert(async_scope_realizable(MemoryScope::Warp));
static_assert(async_scope_realizable(MemoryScope::Cta));
static_assert(async_scope_realizable(MemoryScope::Cluster));
static_assert(async_scope_realizable(MemoryScope::Gpu));
static_assert(!async_scope_realizable(MemoryScope::Thread));
static_assert(!async_scope_realizable(MemoryScope::System));
static_assert(!async_scope_realizable(MemoryScope::Inner));
static_assert(!async_scope_realizable(MemoryScope::Outer));

constexpr eff::TestRunnerCtx ctx{};

static_assert(
    std::is_same_v<decltype(mint_async_copy<2, MemoryScope::Cta, 16>(ctx)), ga::copy<2, MemoryScope::Cta, 16>>);
static_assert(std::is_same_v<decltype(mint_mbarrier_arrive<MemoryScope::Cluster>(ctx)),
                             ga::mbarrier_arrive<MemoryScope::Cluster>>);
static_assert(std::is_same_v<decltype(mint_mbarrier_wait<MemoryScope::Cta>(ctx)), ga::mbarrier_wait<MemoryScope::Cta>>);

static_assert(CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MemoryScope::Cta, 16>);
static_assert(!CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 0, MemoryScope::Cta, 16>);
static_assert(!CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MemoryScope::Cta, 0>);
static_assert(!CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MemoryScope::Inner, 16>);
static_assert(!CtxFitsAsyncCopyMint<int, 2, MemoryScope::Cta, 16>);
static_assert(CtxFitsMbarrierMint<eff::TestRunnerCtx, MemoryScope::Cta>);
static_assert(CtxFitsMbarrierMint<eff::TestRunnerCtx, MemoryScope::Gpu>);
static_assert(!CtxFitsMbarrierMint<eff::TestRunnerCtx, MemoryScope::System>);
static_assert(!CtxFitsMbarrierMint<eff::TestRunnerCtx, MemoryScope::Inner>);
static_assert(!CtxFitsMbarrierMint<int, MemoryScope::Cta>);

static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_Synchronization> == D::Synchronization);

// The locals below are non-constant so the calls are not folded at compile
// time.
inline void runtime_smoke_test() {
    eff::TestRunnerCtx live_ctx{};

    [[maybe_unused]] auto copy_grant = mint_async_copy<3, MemoryScope::Cta, 128>(live_ctx);
    [[maybe_unused]] auto arrive_grant = mint_mbarrier_arrive<MemoryScope::Cta>(live_ctx);
    [[maybe_unused]] auto wait_grant = mint_mbarrier_wait<MemoryScope::Cluster>(live_ctx);

    [[maybe_unused]] copy_cta<2, 16> dbuf{};
    [[maybe_unused]] mbarrier_arrive_cta arr{};
    [[maybe_unused]] mbarrier_wait_cta wt{};
}

}  // namespace crucible::fixy::async::detail::v270_self_test
