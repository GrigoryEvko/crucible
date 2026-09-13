// Sentinel TU: compiles the header under the project warning flags so its
// static_asserts run.  main() calls the runtime smoke test as well, because a
// constant-folded static_assert never exercises the inline mint bodies with
// non-constant arguments.

#include <crucible/fixy/Async.h>

#include <crucible/effects/ExecCtx.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace as = ::crucible::fixy::async;
namespace ga = ::crucible::fixy::grant::async;
namespace gr = ::crucible::fixy::grant;
namespace eff = ::crucible::effects;
using D = ::crucible::fixy::dim::DimensionAxis;
using MS = as::MemoryScope;

static_assert(gr::IsGrantTag<ga::copy<2, MS::Cta, 16>>);
static_assert(gr::IsGrantTag<ga::mbarrier_arrive<MS::Cta>>);
static_assert(gr::IsGrantTag<ga::mbarrier_wait<MS::Cluster>>);
static_assert(gr::which_dim_v<ga::copy<2, MS::Cta, 16>> == D::Synchronization);
static_assert(gr::which_dim_v<ga::mbarrier_arrive<MS::Gpu>> == D::Synchronization);
static_assert(gr::which_dim_v<ga::mbarrier_wait<MS::Cta>> == D::Synchronization);

// Only the accelerator scopes are admitted.  The ARM shareability scopes and
// the zero sentinel are rejected.
static_assert(as::CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MS::Cta, 16>);
static_assert(!as::CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 2, MS::Inner, 16>);
static_assert(!as::CtxFitsAsyncCopyMint<eff::TestRunnerCtx, 0, MS::Cta, 16>);
static_assert(as::CtxFitsMbarrierMint<eff::TestRunnerCtx, MS::Cluster>);
static_assert(!as::CtxFitsMbarrierMint<eff::TestRunnerCtx, MS::System>);

static_assert(std::is_same_v<decltype(as::mint_async_copy<2, MS::Cta, 16>(std::declval<eff::TestRunnerCtx const&>())),
                             ga::copy<2, MS::Cta, 16>>);
static_assert(std::is_same_v<decltype(as::mint_mbarrier_wait<MS::Cta>(std::declval<eff::TestRunnerCtx const&>())),
                             ga::mbarrier_wait<MS::Cta>>);

}  // namespace

int main() {
    ::crucible::fixy::async::detail::v270_self_test::runtime_smoke_test();
    return 0;
}
