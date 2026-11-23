// Negative-compile test. This file must fail to compile.
//
// with_metrics_writer takes the permission of the writer role.  The
// permission of the reader role does not convert to it, so a reader
// cannot become the writer of the metrics channel.

#include <crucible/observe/Metrics.h>
#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct WriterBrand {};
}  // namespace

int main() {
    auto reader_root = ::foundation::permissions::mint_permission_root<crucible::observe::RuntimeMetricsReaderTag>();
    using Channel =
        crucible::observe::RuntimeMetricsChannel<::foundation::brand::brand_of_t<decltype(reader_root)>, WriterBrand>;
    Channel channel{std::move(reader_root)};
    auto reader_permission =
        ::foundation::permissions::mint_permission_root<crucible::observe::RuntimeMetricsReaderTag>();
    crucible::observe::with_metrics_writer(channel, std::move(reader_permission), [](auto writer) noexcept -> void {
        ::foundation::linear::drop(std::move(writer).release());
    });
    return 0;
}
