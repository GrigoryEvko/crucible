// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_metrics_writer takes the permission of the writer role.  The
// permission of the reader role does not convert to it, so a reader
// cannot become the writer of the metrics channel.

#include <crucible/observe/Metrics.h>
#include <crucible/permissions/_Permission.h>

#include <utility>

int main() {
    crucible::observe::RuntimeMetricsChannel channel;
    auto reader_permission = crucible::safety::mint_permission_root<crucible::observe::RuntimeMetricsReaderTag>();
    auto writer = crucible::observe::mint_metrics_writer(channel, std::move(reader_permission));
    (void)writer;
    return 0;
}
