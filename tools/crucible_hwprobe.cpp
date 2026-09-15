// crucible-hwprobe — the one-shot writer for the hardware-capability ledger.
//
// Run once at deploy. It fingerprints the host, judges whether the host is
// fit to measure anything, measures whatever the ledger is missing or has
// let go stale, and commits. The runtime then reads that file and never
// measures on its own.
//
// Refreshing from inside the runtime — a thread, a schedule, a backoff — is
// #67 and is not built here. Everything it needs is: it calls the same
// refresh_plan, supplies the same kind of probe table, and commits through
// the same store. The only thing missing is the policy of when to ask.
//
// There is one probe, and it is deliberately useless. It measures the floor
// of the timing rig itself: the cost of touching a register and reading the
// cycle counter around it. That number is real, is stable, and decides
// nothing. Its job is to prove the loop closes — measure, judge, store, read
// back, serve — so that #68-#70 can add questions worth asking without also
// having to debug the mechanism. Real probes are theirs.
//
// Timing goes through bench/bench_harness.h rather than a rig written here.
// The harness already reports the quantiles, the within-run coefficient of
// variation and the first-half-versus-second-half drift that the evidence
// record wants, and it already knows how to pin, warm up and cap wall time.
// A second rig would be a second set of bugs.

#include <crucible/ledger/Ledger.h>

#include "bench_harness.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

namespace {

using namespace crucible;
using crucible::ledger::CompetenceReport;
using crucible::ledger::LedgerError;
using crucible::ledger::VerdictEvidence;
using crucible::ledger::VerdictId;
using crucible::ledger::VerdictMeasurement;
using crucible::ledger::VerdictValue;

// ── Options ───────────────────────────────────────────────────────────

struct Options {
    bool show_only = false;
    bool force_remeasure = false;
    int pin_core = -1;
    std::size_t sample_count = 50000;
    bool is_valid = true;
};

[[nodiscard]] Options parse_options(int argc, char** argv) noexcept {
    Options options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--show") {
            options.show_only = true;
        } else if (argument == "--force") {
            options.force_remeasure = true;
        } else if (argument == "--core" && index + 1 < argc) {
            options.pin_core = std::atoi(argv[++index]);
        } else if (argument == "--samples" && index + 1 < argc) {
            const long requested = std::atol(argv[++index]);
            options.sample_count = (requested > 0) ? static_cast<std::size_t>(requested) : options.sample_count;
        } else if (argument == "--help" || argument == "-h") {
            options.is_valid = false;
        } else {
            std::fprintf(stderr, "crucible-hwprobe: unknown argument: %.*s\n", static_cast<int>(argument.size()),
                         argument.data());
            options.is_valid = false;
        }
    }
    return options;
}

void print_usage() noexcept {
    std::fputs("crucible-hwprobe — write the hardware-capability ledger for this host.\n"
               "\n"
               "Usage:\n"
               "  crucible-hwprobe                 measure what is missing or stale, then commit\n"
               "  crucible-hwprobe --force         remeasure every verdict regardless of age\n"
               "  crucible-hwprobe --show          print the stored ledger; measure nothing\n"
               "  crucible-hwprobe --core N        pin the measurement to CPU N\n"
               "  crucible-hwprobe --samples N     samples per run (default 50000)\n"
               "\n"
               "The ledger lives at $XDG_CACHE_HOME/crucible/hwledger (or ~/.cache/...),\n"
               "one file per host fingerprint.\n"
               "\n"
               "Exit status:\n"
               "  0  the ledger is up to date\n"
               "  1  bad invocation\n"
               "  2  the store could not be read or written\n"
               "  3  every probe ran and every result was refused as untrustworthy\n",
               stderr);
}

// ── Options shared with the probe ─────────────────────────────────────
//
// The probe signature takes only the competence report, because a probe has
// no business knowing about command-line flags. The two knobs that are
// genuinely measurement parameters reach it through this translation unit.

std::size_t g_sample_count = 50000;
int g_pin_core = -1;

// ── Evidence from a bench report ──────────────────────────────────────

[[nodiscard]] std::uint32_t saturating_nanos(double value) noexcept {
    if (!(value > 0.0)) {
        return 0u;
    }
    // Round up. A floor of zero would read as "the timer did not run" at
    // the admission, which is the correct reading for a failed measurement
    // and the wrong one for a sub-nanosecond result.
    const double rounded = value + 0.5;
    if (rounded >= 4294967295.0) {
        return 4294967295u;
    }
    const auto nanos = static_cast<std::uint32_t>(rounded);
    return (nanos == 0u) ? 1u : nanos;
}

[[nodiscard]] std::uint32_t to_ppm(double ratio) noexcept {
    if (!(ratio > 0.0)) {
        return 0u;
    }
    const double parts = ratio * 1'000'000.0;
    if (parts >= 4294967295.0) {
        return 4294967295u;
    }
    return static_cast<std::uint32_t>(parts);
}

// Two runs, not one. The within-run coefficient of variation says how
// steady the samples were inside a single burst; it says nothing about
// whether the burst itself was representative. A part that throttles
// between runs, a scheduler that places the second run on a colder cache,
// an operator who changes the governor mid-flight — all of those show up as
// run-to-run spread and none of them show up within a run. The evidence
// record has a field for each because they fail differently, and filling
// the second one from the first would be a lie the reader cannot detect.
[[nodiscard]] std::expected<VerdictMeasurement, LedgerError> probe_timer_floor(CompetenceReport const&) noexcept {
    int sink = 0;

    auto one_run = [&](const char* name) {
        auto run = bench::Run{name};
        auto& configured = run.samples(g_sample_count).warmup(2000).max_wall_ms(4000);
        if (g_pin_core >= 0) {
            (void)configured.core(g_pin_core);
        } else {
            (void)configured.no_pin();
        }
        return run.measure([&] {
            sink += 1;
            bench::do_not_optimize(sink);
        });
    };

    const bench::Report first = one_run("ledger.timer_floor.run1");
    const bench::Report second = one_run("ledger.timer_floor.run2");

    if (first.pct.n == 0u || second.pct.n == 0u) {
        return std::unexpected(LedgerError::ConfidenceBelowBar);
    }

    const double first_p50 = first.pct.p50;
    const double second_p50 = second.pct.p50;
    const double midpoint = (first_p50 + second_p50) * 0.5;
    const double spread =
        (midpoint > 0.0) ? (std::max(first_p50, second_p50) - std::min(first_p50, second_p50)) / midpoint : 1.0;

    // The reported value is the better of the two runs, and the evidence is
    // the worse of the two. Reporting the best number with the worst
    // supporting statistics is the conservative pairing: a reader that
    // trusts the value has already been shown the least flattering account
    // of how it was obtained.
    const bench::Report& worse = (first.pct.cv >= second.pct.cv) ? first : second;

    VerdictEvidence evidence{};
    evidence.quantiles.p50_ns = saturating_nanos(std::min(first_p50, second_p50));
    evidence.quantiles.p99_ns = saturating_nanos(worse.pct.p99);
    evidence.quantiles.p999_ns = saturating_nanos(worse.pct.p99_9);
    // Saturation can flatten a sub-nanosecond spread into three equal
    // values, and it can also invert them when p50 comes from the better
    // run and the tail from the worse one. The ordering predicate rejects
    // an inverted triple, so the tails are lifted to at least the median
    // rather than being allowed to fail an admission for a rounding
    // artefact.
    evidence.quantiles.p99_ns = std::max(evidence.quantiles.p99_ns, evidence.quantiles.p50_ns);
    evidence.quantiles.p999_ns = std::max(evidence.quantiles.p999_ns, evidence.quantiles.p99_ns);

    evidence.sample_count = static_cast<std::uint32_t>(std::min<std::size_t>(worse.pct.n, 0xFFFFFFFFull));
    evidence.within_run_cv_ppm = to_ppm(worse.pct.cv);
    evidence.run_to_run_spread_ppm = to_ppm(spread);

    return VerdictMeasurement{.value = VerdictValue{evidence.quantiles.p50_ns}, .evidence = evidence};
}

constexpr ledger::ProbeRegistration kProbeTable[] = {
    {.id = VerdictId::TimerFloorNanos, .run = &probe_timer_floor},
};

constexpr VerdictId kWantedVerdicts[] = {
    VerdictId::TimerFloorNanos,
};

// ── Reporting ─────────────────────────────────────────────────────────

void print_host(ledger::HostFacts const& facts, ledger::HostFingerprint fingerprint,
                CompetenceReport const& competence) noexcept {
    std::array<char, 256> defect_text{};
    const std::string_view defects = ledger::describe_defects(competence, defect_text);

    std::printf("host        %s / %s\n", facts.cpu_vendor.data(), facts.cpu_model.data());
    std::printf("fingerprint hardware=%016llx policy=%016llx\n",
                static_cast<unsigned long long>(fingerprint.hardware.raw()),
                static_cast<unsigned long long>(fingerprint.policy.raw()));
    std::printf("geometry    l1d=%uK l2=%uK l3=%lluM line=%u cores=%u threads=%u ccd=%u numa=%u sve=%u bits\n",
                facts.l1d_bytes / 1024u, facts.l2_bytes / 1024u,
                static_cast<unsigned long long>(facts.l3_total_bytes / (1024u * 1024u)), facts.cache_line_bytes,
                facts.physical_core_count, facts.hw_thread_count, facts.l3_instance_count, facts.numa_node_count,
                facts.sve_vector_length_bits);
    std::printf("competence  %s (defects=0x%04x)\n", competence.is_competent() ? "FIT" : "DEGRADED",
                competence.defect_word());
    std::printf("            %.*s\n", static_cast<int>(defects.size()), defects.data());
    std::printf("            isolated=%u online_siblings=%u load=%u.%03u allowed_cpus=%u paranoid=%d\n",
                competence.isolated_core_count, competence.online_sibling_count, competence.load_average_milli / 1000u,
                competence.load_average_milli % 1000u, competence.allowed_cpu_count, competence.perf_event_paranoid);
    std::printf("            clock %llu-%llu kHz, governor=%s\n",
                static_cast<unsigned long long>(competence.scaling_min_freq_khz),
                static_cast<unsigned long long>(competence.scaling_max_freq_khz),
                competence.governor_is_performance ? "performance" : "other");
}

void print_entries(ledger::LedgerView const& view) noexcept {
    if (!view.is_loaded()) {
        std::printf("ledger      absent or unreadable — every lookup returns unknown\n");
        return;
    }
    std::printf("ledger      %zu entr%s\n", view.entry_count(), view.entry_count() == 1u ? "y" : "ies");
    for (ledger::LedgerEntry const& entry : view.raw_ledger().entries) {
        const ledger::VerdictTrait trait = ledger::verdict_trait(entry.id);
        const auto lookup = view.lookup(entry.id);
        std::printf("  %-18.*s %llu %-6.*s  confidence=%-4.*s served=%-3s  n=%u p50=%u p99=%u cv=%.2f%% rr=%.2f%%\n",
                    static_cast<int>(trait.name.size()), trait.name.data(),
                    static_cast<unsigned long long>(entry.value.value().raw()),
                    static_cast<int>(ledger::verdict_unit_name(trait.unit).size()),
                    ledger::verdict_unit_name(trait.unit).data(),
                    static_cast<int>(ledger::confidence_name(entry.confidence).size()),
                    ledger::confidence_name(entry.confidence).data(), lookup.is_known() ? "yes" : "no",
                    entry.evidence.sample_count, entry.evidence.quantiles.p50_ns, entry.evidence.quantiles.p99_ns,
                    static_cast<double>(entry.evidence.within_run_cv_ppm) / 10'000.0,
                    static_cast<double>(entry.evidence.run_to_run_spread_ppm) / 10'000.0);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    if (!options.is_valid) {
        print_usage();
        return 1;
    }
    g_sample_count = options.sample_count;
    g_pin_core = options.pin_core;

    // A background capability, even though this is a one-shot tool at
    // process start. Reading and writing the store blocks on a disk, and
    // an initialization context deliberately does not admit Effect::Block.
    constexpr ledger::LedgerIoCtx ctx{};

    const ledger::HostFacts facts = ledger::probe_host_facts();
    const ledger::HostFingerprint fingerprint = ledger::fold_fingerprint(facts);
    const CompetenceReport competence = ledger::probe_competence();

    print_host(facts, fingerprint, competence);

    if (!fingerprint.is_complete()) {
        std::fprintf(stderr, "crucible-hwprobe: the host fingerprint is incomplete; refusing to write.\n");
        return 2;
    }

    ledger::LedgerView view = ledger::mint_ledger_view(ctx, fingerprint);
    print_entries(view);

    if (options.show_only) {
        return 0;
    }

    const std::uint64_t now = ledger::wall_clock_unix_seconds();
    if (now == 0u) {
        std::fprintf(stderr, "crucible-hwprobe: no usable wall clock; refusing to write.\n");
        return 2;
    }

    // Start from what is on disk for this fingerprint, or from a clean
    // ledger carrying this host's identity. A fingerprint change is never a
    // merge: the old file keeps its own name and this one starts empty.
    ledger::Ledger working = view.is_loaded() ? view.raw_ledger() : ledger::seed_ledger(facts, fingerprint, competence);
    working.competence = competence;
    working.cpu_vendor = facts.cpu_vendor;
    working.cpu_model = facts.cpu_model;

    ledger::RefreshQueue queue{};
    if (options.force_remeasure) {
        for (const VerdictId id : kWantedVerdicts) {
            queue.push_back(ledger::RefreshRequest{.id = id, .reason = ledger::RefreshReason::Absent});
        }
    } else {
        queue = ledger::refresh_plan(working, kWantedVerdicts, now);
    }

    if (queue.empty()) {
        std::printf("refresh     nothing to measure — every verdict is present, fresh and trusted\n");
        return 0;
    }

    std::printf("refresh     %zu verdict%s to measure:\n", queue.size(), queue.size() == 1u ? "" : "s");
    for (ledger::RefreshRequest const& request : queue) {
        const std::string_view name = ledger::verdict_id_name(request.id);
        const std::string_view reason = ledger::refresh_reason_name(request.reason);
        std::printf("              %.*s (%.*s)\n", static_cast<int>(name.size()), name.data(),
                    static_cast<int>(reason.size()), reason.data());
    }

    const ledger::RefreshOutcome outcome = ledger::run_refresh(working, queue, kProbeTable, competence, now);

    std::printf("refresh     measured=%u admitted=%u refused=%u no_probe=%u\n", outcome.measured_count,
                outcome.admitted_count, outcome.refused_count, outcome.no_probe_count);

    // Every refusal names the bar it missed and shows the evidence that
    // missed it. A bare count would be the same silent failure that let a
    // bench harness on this box collect 128 samples instead of 100 000
    // without anyone noticing for as long as it took to bisect.
    for (ledger::RefreshRecord const& record : outcome.log) {
        if (record.was_admitted) {
            continue;
        }
        const std::string_view name = ledger::verdict_id_name(record.id);
        const std::string_view fault = ledger::evidence_fault_name(record.fault);
        const std::string_view error = ledger::ledger_error_name(record.error);
        std::printf("  REFUSED   %.*s: %.*s (%.*s)\n", static_cast<int>(name.size()), name.data(),
                    static_cast<int>(fault.size()), fault.data(), static_cast<int>(error.size()), error.data());
        if (record.had_probe) {
            std::printf("            evidence n=%u p50=%u p99=%u p999=%u cv=%.3f%% (bar %.1f%%) "
                        "run-to-run=%.3f%% (bar %.1f%%)\n",
                        record.evidence.sample_count, record.evidence.quantiles.p50_ns,
                        record.evidence.quantiles.p99_ns, record.evidence.quantiles.p999_ns,
                        static_cast<double>(record.evidence.within_run_cv_ppm) / 10'000.0,
                        static_cast<double>(ledger::kMaxWithinRunCvPpm) / 10'000.0,
                        static_cast<double>(record.evidence.run_to_run_spread_ppm) / 10'000.0,
                        static_cast<double>(ledger::kMaxRunToRunSpreadPpm) / 10'000.0);
        }
    }

    if (outcome.admitted_count == 0u) {
        // Nothing cleared the bar. Committing an unchanged ledger would
        // rewrite the file for no reason and would reset nothing; leaving
        // it alone keeps whatever was already there, which is at least as
        // good as what this run produced.
        std::fprintf(stderr,
                     "crucible-hwprobe: every measurement was refused as untrustworthy; the ledger is unchanged.\n");
        return 3;
    }

    auto committed = ledger::commit_ledger(ctx, working);
    if (!committed.has_value()) {
        const std::string_view reason = ledger::ledger_error_name(committed.error());
        std::fprintf(stderr, "crucible-hwprobe: commit failed: %.*s\n", static_cast<int>(reason.size()), reason.data());
        return 2;
    }

    auto path = ledger::ledger_path_for(fingerprint);
    if (path.has_value()) {
        std::printf("committed   %s\n", path->value().c_str());
    }

    // Read it back through the same door the runtime uses, so the run that
    // wrote the file is also the run that proves the file can be served.
    const ledger::LedgerView reread = ledger::mint_ledger_view(ctx, fingerprint);
    print_entries(reread);
    return 0;
}
