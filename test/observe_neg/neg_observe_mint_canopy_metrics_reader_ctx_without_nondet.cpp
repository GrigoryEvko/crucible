// Negative-compile test. This file must fail to compile.
//
// mint_canopy_metrics_reader lends a share of the reader pool of the
// channel.  The lend fails while the reader root is out, and the schedule
// decides when the root comes back, so the mint takes a context that holds
// Nondet.  The foreground context holds no atom, so the mint refuses it.

#include <crucible/observe/Metrics.h>
#include <fixy/Ctx.h>
#include <foundation/Brand.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct WriterBrand {};
}  // namespace

int main() {
    const ::fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    auto reader_root = ::foundation::permissions::mint_permission_root<crucible::observe::RuntimeMetricsReaderTag>();
    using Channel =
        crucible::observe::RuntimeMetricsChannel<::foundation::brand::brand_of_t<decltype(reader_root)>, WriterBrand>;
    Channel channel{std::move(reader_root)};
    auto reader = crucible::observe::mint_canopy_metrics_reader(ctx, channel);
    (void)reader;
    return 0;
}
