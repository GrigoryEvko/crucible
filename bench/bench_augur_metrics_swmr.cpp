#include <crucible/augur/Metrics.h>
#include <crucible/permissions/Permission.h>

#include "bench_harness.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace augur = ::crucible::augur;
namespace safety = ::crucible::safety;

int main() {
    augur::AugurMetrics metrics{};
    metrics.delta_g_count = 1;
    metrics.delta_g[0] = 1.0;

    augur::AugurMetricsChannel channel{
        augur::fresh_metrics_sample(metrics)};
    auto writer_perm = safety::mint_permission_root<augur::AugurWriterTag>();
    auto writer = augur::mint_augur_metrics_writer(
        channel, std::move(writer_perm));
    auto reader = augur::mint_keeper_metrics_reader(channel);
    if (!reader) {
        std::fprintf(stderr, "failed to mint Augur metrics reader\n");
        return EXIT_FAILURE;
    }

    auto sample = augur::fresh_metrics_sample(metrics);
    std::uint64_t seq = 0;
    auto report = bench::Run{"AugurMetrics SWMR publish"}
        .samples(100'000)
        .warmup(10'000)
        .max_wall_ms(3'000)
        .measure([&] {
        metrics.meb_lambda_max = static_cast<double>(seq++);
        sample = augur::fresh_metrics_sample(metrics);
        writer.publish(sample);
        bench::do_not_optimize(seq);
    });

    auto observed = reader->load();

    bench::emit_reports_text(std::span{&report, 1});
    std::printf("\nbench_augur_metrics_swmr:\n");
    std::printf("  target_p99_lt_100ns: %s\n",
                report.pct.p99 < 100.0 ? "yes" : "no");
    std::printf("  final_lambda_max:    %.0f\n",
                observed.peek().meb_lambda_max);
    bench::emit_reports_json(std::span{&report, 1}, bench::env_json());

    return EXIT_SUCCESS;
}
