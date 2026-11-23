// Negative-compile test. This file must fail to compile.
//
// with_canopy_metrics_reader lends a share of the reader pool of the
// channel.  The lend fails while the reader root is out, and the schedule
// decides when the root comes back, so the door takes a context that holds
// Nondet.  The foreground context holds no atom, so the door refuses it.

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
    (void)crucible::observe::with_canopy_metrics_reader(ctx, channel,
                                                        [](auto reader) noexcept -> void { (void)reader.version(); });
    return 0;
}
