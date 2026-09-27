// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_metrics_writer takes the permission of the writer role.  The
// permission of the reader role does not convert to it, so a reader
// cannot become the writer of the metrics channel.

#include <crucible/observe/Metrics.h>
#include <foundation/Brand.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
[[nodiscard]] auto reader_root() noexcept {
    return ::foundation::permissions::mint_permission_root<crucible::observe::RuntimeMetricsReaderTag>();
}
[[nodiscard]] auto writer_root() noexcept {
    return ::foundation::permissions::mint_permission_root<crucible::observe::RuntimeMetricsWriterTag>();
}
}  // namespace

int main() {
    crucible::observe::RuntimeMetricsChannel<::foundation::brand::brand_of_t<decltype(reader_root())>,
                                             ::foundation::brand::brand_of_t<decltype(writer_root())>>
        channel{reader_root()};
    auto reader_permission =
        ::foundation::permissions::mint_permission_root<crucible::observe::RuntimeMetricsReaderTag>();
    auto writer = crucible::observe::mint_metrics_writer(channel, std::move(reader_permission));
    (void)writer;
    return 0;
}
