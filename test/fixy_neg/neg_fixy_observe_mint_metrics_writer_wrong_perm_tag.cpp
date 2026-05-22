#include <crucible/fixy/Observe.h>
#include <crucible/permissions/Permission.h>

// FIXY-V-214 fixture #1: the fixy::observe::mint_metrics_writer
// re-export MUST preserve the substrate's Permission<WriterTag>
// parameter-type gate.  Handing the writer factory a reader-tag
// Permission token must fail compilation through the fixy:: surface
// with the same conversion diagnostic as the bare substrate call.

int main() {
    using crucible::fixy::observe::RuntimeMetricsChannel;
    using crucible::fixy::observe::RuntimeMetricsReaderTag;

    RuntimeMetricsChannel channel;
    auto reader_perm = crucible::safety::mint_permission_root<
        RuntimeMetricsReaderTag>();

    // Wrong Permission tag — writer mint takes Permission<WriterTag>.
    auto writer = crucible::fixy::observe::mint_metrics_writer(
        channel, std::move(reader_perm));
    (void)writer;
    return 0;
}
