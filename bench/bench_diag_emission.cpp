#include <foundation/core/Text.h>
#include <foundation/diag/JsonEmitter.h>

#include "bench_harness.h"

#include <cstdio>
#include <cstdlib>

// The cost of the JSON record of a violation.  Each run writes the record into
// a FixedText on the stack through the format form of the emitter.  The run
// then measures the writer and no system call.  The line of text of a violation has
// no format form: its one writer is the sink write of the Report family.
int main() {
    namespace diag = ::foundation::diag;
    namespace core = ::foundation::core;

    bench::print_system_info();
    bench::elevate_priority();

    constexpr auto cat = diag::Category::EffectRowMismatch;

    auto json = bench::Run{"diag.json.buffer"}.samples(2000).warmup(200).no_pin().max_wall_ms(1000).measure([&] {
        core::FixedText<4096> record;
        const bool is_formatted =
            diag::format_json_record(
                record, diag::record_from_violation(cat, "bench_diag_emission.cpp:32:5@bench_diag_emission",
                                                    "caller row lacks Bg while callee requires Bg"))
                .is_some();
        bench::do_not_optimize(is_formatted);
    });

    json.print_text(stdout);

    return EXIT_SUCCESS;
}
