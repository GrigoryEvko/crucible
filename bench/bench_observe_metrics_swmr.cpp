#include <crucible/observe/Metrics.h>
#include <fixy/Ctx.h>
#include <fixy/Stale.h>
#include <foundation/Brand.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace observe = ::crucible::observe;
namespace perm = ::foundation::permissions;

namespace {
[[nodiscard]] auto metrics_reader_root() noexcept {
    return perm::mint_permission_root<observe::RuntimeMetricsReaderTag>();
}
[[nodiscard]] auto metrics_writer_root() noexcept {
    return perm::mint_permission_root<observe::RuntimeMetricsWriterTag>();
}
using Channel = observe::RuntimeMetricsChannel<::foundation::brand::brand_of_t<decltype(metrics_reader_root())>,
                                               ::foundation::brand::brand_of_t<decltype(metrics_writer_root())>>;
}  // namespace

int main() {
    observe::RuntimeMetrics metrics{};
    metrics.delta_g_count = 1;
    metrics.delta_g.at(::fixy::index<0>()) = 1.0;

    // Each sample of the loop is at the current step of the producer.
    ::fixy::SequenceSource const steps =
        ::fixy::mint_sequence_source(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    Channel channel{metrics_reader_root(), observe::fresh_metrics_sample(metrics, steps)};
    auto writer = observe::mint_metrics_writer(channel, metrics_writer_root());
    auto minted = observe::mint_keeper_metrics_reader(channel);
    if (minted.is_none()) {
        std::fprintf(stderr, "failed to mint runtime metrics reader\n");
        return EXIT_FAILURE;
    }
    auto reader = std::move(minted).expect("the reader share is present");

    auto sample = observe::fresh_metrics_sample(metrics, steps);
    std::uint64_t seq = 0;
    auto report =
        bench::Run{"RuntimeMetrics SWMR publish"}.samples(100000).warmup(10000).max_wall_ms(3000).measure([&] {
            metrics.meb_lambda_max = static_cast<double>(seq++);
            sample = observe::fresh_metrics_sample(metrics, steps);
            writer.publish(sample);
            bench::do_not_optimize(seq);
        });

    auto observed = reader.load();

    bench::emit_reports_text(std::span{&report, 1});
    std::printf("\nbench_observe_metrics_swmr:\n");
    std::printf("  target_p99_lt_100ns: %s\n", report.pct.p99 < 100.0 ? "yes" : "no");
    std::printf("  final_lambda_max:    %.0f\n", observed.peek().meb_lambda_max);
    bench::emit_reports_json(std::span{&report, 1}, bench::env_json());

    return EXIT_SUCCESS;
}
