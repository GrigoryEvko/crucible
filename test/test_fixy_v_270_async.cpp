// FIXY-V-270 sentinel — fixy/Async.h grant::async::{copy,mbarrier_arrive,
// mbarrier_wait} + the three §XXI mints.  Including the header fires the
// embedded `v270_self_test` static_asserts under the project warning flags
// (the header-only-static_assert blind-spot guard); this TU additionally
// invokes runtime_smoke_test() so the inline mint bodies are exercised with
// non-constant args, and re-asserts the load-bearing properties locally.

#include <crucible/fixy/Async.h>

#include <crucible/effects/ExecCtx.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace as  = ::crucible::fixy::async;
namespace ga  = ::crucible::fixy::grant::async;
namespace gr  = ::crucible::fixy::grant;
namespace eff = ::crucible::effects;
using D  = ::crucible::fixy::dim::DimensionAxis;
using MS = as::MemoryScope;

// ── Grant well-formedness + Synchronization-axis routing ──────────────
static_assert(gr::IsGrantTag<ga::copy<2, MS::Cta, 16>>);
static_assert(gr::IsGrantTag<ga::mbarrier_arrive<MS::Cta>>);
static_assert(gr::IsGrantTag<ga::mbarrier_wait<MS::Cluster>>);
static_assert(gr::which_dim_v<ga::copy<2, MS::Cta, 16>>       == D::Synchronization);
static_assert(gr::which_dim_v<ga::mbarrier_arrive<MS::Gpu>>   == D::Synchronization);
static_assert(gr::which_dim_v<ga::mbarrier_wait<MS::Cta>>     == D::Synchronization);

// ── Accel-scope gate: accel trunk admitted, sentinels + ARM rejected ──
static_assert( as::CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MS::Cta, 16>);
static_assert(!as::CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MS::Inner, 16>);
static_assert(!as::CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 0, MS::Cta, 16>);
static_assert( as::CtxFitsMbarrierMint<eff::TestRunnerCtx, MS::Cluster>);
static_assert(!as::CtxFitsMbarrierMint<eff::TestRunnerCtx, MS::System>);

// ── Mints synthesize the right concrete grant types ───────────────────
static_assert(std::is_same_v<
    decltype(as::mint_async_copy<2, MS::Cta, 16>(std::declval<eff::TestRunnerCtx const&>())),
    ga::copy<2, MS::Cta, 16>>);
static_assert(std::is_same_v<
    decltype(as::mint_mbarrier_wait<MS::Cta>(std::declval<eff::TestRunnerCtx const&>())),
    ga::mbarrier_wait<MS::Cta>>);

}  // namespace

int main() {
    ::crucible::fixy::async::detail::v270_self_test::runtime_smoke_test();
    return 0;
}
