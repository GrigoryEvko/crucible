#include <crucible/permissions/Permission.h>
#include <crucible/rt/Metrics.h>

#include "bench_harness.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace rt = ::crucible::rt;
namespace safety = ::crucible::safety;

int main() {
    rt::RuntimeMetrics metrics{};
    metrics.delta_g_count = 1;
    metrics.delta_g[0] = 1.0;

    rt::RuntimeMetricsChannel channel{
        rt::fresh_metrics_sample(metrics)};
    auto writer_perm =
        safety::mint_permission_root<rt::RuntimeMetricsWriterTag>();
    auto writer = rt::mint_rt_metrics_writer(
        channel, std::move(writer_perm));
    auto reader = rt::mint_keeper_metrics_reader(channel);
    if (!reader) {
        std::fprintf(stderr, "failed to mint runtime metrics reader\n");
        return EXIT_FAILURE;
    }

    auto sample = rt::fresh_metrics_sample(metrics);
    std::uint64_t seq = 0;
    auto report = bench::Run{"RuntimeMetrics SWMR publish"}
        .samples(100'000)
        .warmup(10'000)
        .max_wall_ms(3'000)
        .measure([&] {
        metrics.meb_lambda_max = static_cast<double>(seq++);
        sample = rt::fresh_metrics_sample(metrics);
        writer.publish(sample);
        bench::do_not_optimize(seq);
    });

    auto observed = reader->load();

    bench::emit_reports_text(std::span{&report, 1});
    std::printf("\nbench_rt_metrics_swmr:\n");
    std::printf("  target_p99_lt_100ns: %s\n",
                report.pct.p99 < 100.0 ? "yes" : "no");
    std::printf("  final_lambda_max:    %.0f\n",
                observed.peek().meb_lambda_max);
    bench::emit_reports_json(std::span{&report, 1}, bench::env_json());

    return EXIT_SUCCESS;
}
