#include <crucible/safety/diag/JsonEmitter.h>

#include "bench_harness.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

int main() {
    namespace diag = ::crucible::safety::diag;

    bench::print_system_info();
    bench::elevate_priority();

    std::FILE* sink = std::fopen("/dev/null", "w");
    if (sink == nullptr) {
        std::perror("fopen(/dev/null)");
        return EXIT_FAILURE;
    }
    std::setvbuf(sink, nullptr, _IONBF, 0);

    constexpr auto cat = diag::Category::EffectRowMismatch;
    constexpr std::string_view loc =
        "bench_diag_emission.cpp:32:5@bench_diag_emission";
    constexpr std::string_view detail =
        "caller row lacks Bg while callee requires Bg";

    auto text = bench::Run{"diag.legacy_text.devnull"}
        .samples(2'000)
        .warmup(200)
        .no_pin()
        .max_wall_ms(1'000)
        .measure([&] {
            const bool ok =
                diag::emit_legacy_text_violation(sink, cat, loc, detail);
            bench::do_not_optimize(ok);
        });

    auto json = bench::Run{"diag.json.devnull"}
        .samples(2'000)
        .warmup(200)
        .no_pin()
        .max_wall_ms(1'000)
        .measure([&] {
            const bool ok =
                diag::emit_json_violation(sink, cat, loc, detail);
            bench::do_not_optimize(ok);
        });

    text.print_text(stdout);
    json.print_text(stdout);

    const auto cmp = bench::compare(text, json);
    std::puts("\n=== compare ===");
    cmp.print_text(stdout);

    std::fclose(sink);
    return EXIT_SUCCESS;
}
