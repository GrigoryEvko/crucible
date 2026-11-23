#include <crucible/observe/Metrics.h>
#include <fixy/Ctx.h>
#include <fixy/Stale.h>
#include <foundation/Brand.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

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
    metrics.delta_g.with_element(::fixy::index<0>(), [](double& element) noexcept { element = 1.0; });

    // Each sample of the loop is at the current step of the producer.
    ::fixy::SequenceSource const steps =
        ::fixy::mint_sequence_source(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    Channel channel{metrics_reader_root(), observe::fresh_metrics_sample(metrics, steps)};
    auto writer = observe::mint_metrics_writer(channel, metrics_writer_root());
    // The context of the reader: the lend of its share and its load take
    // Nondet, and the load spins, so it also takes Div.
    const ::fixy::TestRunnerCtx spin{::foundation::effects::testing::test()};
    auto minted = observe::mint_keeper_metrics_reader(spin, channel);
    if (minted.is_none()) {
        ::fixy::report(::fixy::Sink::Err, "failed to mint runtime metrics reader\n");
        return 1;
    }
    auto reader = std::move(minted).expect("the reader share is present");

    auto sample = observe::fresh_metrics_sample(metrics, steps);
    std::uint64_t seq = 0;
    ::fixy::FixedArray<bench::Report, 1> const reports{
        bench::Run{"RuntimeMetrics SWMR publish"}.samples(100000).warmup(10000).max_wall_ms(3000).measure([&] {
            metrics.meb_lambda_max = static_cast<double>(seq++);
            sample = observe::fresh_metrics_sample(metrics, steps);
            writer.publish(sample);
            bench::mark_changed(seq);
        })};

    auto observed = reader.load(spin);

    using ::fixy::TextView;
    bench::emit_reports_text(reports.view());
    bench::print("\nbench_observe_metrics_swmr:\n");
    bench::print("  target_p99_lt_100ns: {}\n",
                 reports.at(::fixy::index<0>()).pct.p99 < 100.0 ? TextView{"yes"} : TextView{"no"});
    bench::print("  final_lambda_max:    {}\n", ::fixy::Fixed<0>{observed.peek().meb_lambda_max});
    bench::emit_reports_json(reports.view(), bench::env_json());

    return 0;
}
