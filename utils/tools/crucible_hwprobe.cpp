// crucible-hwprobe — the one-shot writer for the hardware-capability ledger.
//
// Run once at deploy. It fingerprints the host, judges whether the host is
// fit to measure anything, measures whatever the ledger is missing or has
// let go stale, and commits. The runtime then reads that file and never
// measures on its own.
//
// The refresh thread in ledger/RefreshDaemon.h refreshes from inside the
// runtime. It calls the same refresh_plan, supplies the same kind of probe
// table, and commits through the same store. It adds only the policy of
// when to ask: a thread, a schedule and a backoff.
//
// The probe table holds the timer-floor probe of the tool and the probes of
// ledger/probes/: vector width, cache tier with the NUMA hop, and
// transparent hugepages. Each probe family compiles in a translation unit of
// its own, and crucible_hwprobe_probes.h declares the door to each one.

#include "crucible_hwprobe_probes.h"

#include <crucible/ledger/Ledger.h>
#include <crucible/ledger/ProbeSettings.h>
#include <foundation/reflect/EnumName.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <charconv>
#include <span>
#include <string_view>

namespace {

using namespace crucible;
using crucible::ledger::CompetenceReport;
using crucible::ledger::VerdictId;

// ── Options ───────────────────────────────────────────────────────────

struct Options {
    bool show_only = false;
    bool force_remeasure = false;
    int pin_core = -1;
    std::size_t sample_count = 50000;
    // Cores the cache-tier probe splits work onto. Left empty, the probe
    // picks cores that share a last-level cache with the measuring core,
    // which is what an unattended refresh gets.
    std::array<int, crucible::ledger::kMaxHelperCores> helper_cores{-1, -1, -1, -1};
    bool is_valid = true;
};

// Parses "90,91,92" into the helper-core list. A core that does not parse
// ends the list rather than being skipped, because a typo that silently
// dropped one core would change which cluster the split ran on without
// saying so.
void parse_helper_cores(std::string_view text, std::span<int> into) noexcept {
    std::size_t filled = 0;
    std::size_t cursor = 0;
    while (filled < into.size() && cursor <= text.size()) {
        const std::size_t comma = std::min(text.find(',', cursor), text.size());
        const std::string_view field = text.substr(cursor, comma - cursor);
        int value = -1;
        const auto outcome = std::from_chars(field.data(), field.data() + field.size(), value);
        if (outcome.ec != std::errc{} || value < 0) {
            break;
        }
        into[filled] = value;
        ++filled;
        cursor = comma + 1u;
    }
}

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
        } else if (argument == "--helpers" && index + 1 < argc) {
            parse_helper_cores(std::string_view{argv[++index]}, options.helper_cores);
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
               "  crucible-hwprobe --helpers A,B   cores the cache-tier probe splits onto\n"
               "                                   (default: cores sharing the L3 of --core)\n"
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

// ── Options shared with the probes ────────────────────────────────────
//
// The probe signature takes only the competence report, because a probe has
// no business knowing about command-line flags. The knobs that are
// genuinely measurement parameters reach every probe through the settings
// block in ProbeSettings.h, which the probes read and nothing else writes.

constexpr ledger::ProbeRegistration kProbeTable[] = {
    {.id = VerdictId::TimerFloorNanos, .run = &hwprobe::probe_timer_floor},
    {.id = VerdictId::VectorWidthPreferredBits, .run = &hwprobe::probe_vector_width_preferred_bits},
    {.id = VerdictId::VectorWidthComputeGainPercent, .run = &hwprobe::probe_vector_width_compute_gain},
    {.id = VerdictId::VectorWidthMemoryGainPercent, .run = &hwprobe::probe_vector_width_memory_gain},
    {.id = VerdictId::ParallelKneeBytes, .run = &hwprobe::probe_parallel_knee_bytes},
    {.id = VerdictId::ParallelCeilingBytes, .run = &hwprobe::probe_parallel_ceiling_bytes},
    {.id = VerdictId::NumaRemoteCostPercent, .run = &hwprobe::probe_numa_remote_cost},
    {.id = VerdictId::ThpFaultCostNanosPerMib, .run = &hwprobe::probe_thp_fault_cost},
    {.id = VerdictId::ThpFaultGainPercent, .run = &hwprobe::probe_thp_fault_gain},
    {.id = VerdictId::ThpAccessGainPercent, .run = &hwprobe::probe_thp_access_gain},
};

// Order matters here and nowhere else. Each probe of ledger/probes/ answers
// several verdicts from one measurement, held behind a short-lived memo in
// ProbeSupport.h, so the sibling ids have to be queued together for the
// memo to be the thing that shares them. Interleaving two probes' ids
// would expire each memo before its siblings read it and measure every
// shape twice.
constexpr VerdictId kWantedVerdicts[] = {
    VerdictId::TimerFloorNanos,
    VerdictId::VectorWidthPreferredBits,
    VerdictId::VectorWidthComputeGainPercent,
    VerdictId::VectorWidthMemoryGainPercent,
    VerdictId::ParallelKneeBytes,
    VerdictId::ParallelCeilingBytes,
    VerdictId::NumaRemoteCostPercent,
    VerdictId::ThpFaultCostNanosPerMib,
    VerdictId::ThpFaultGainPercent,
    VerdictId::ThpAccessGainPercent,
};

static_assert(std::size(kProbeTable) == ledger::kVerdictIdCount,
              "every verdict id needs a probe, or a refresh queues a question nothing can answer");
static_assert(std::size(kWantedVerdicts) == ledger::kVerdictIdCount);

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
    ledger::set_probe_settings(ledger::ProbeSettings{
        .sample_count = static_cast<std::uint32_t>(std::min<std::size_t>(options.sample_count, 0xFFFFFFFFull)),
        .pin_core = options.pin_core,
        .helper_cores = options.helper_cores,
    });

    // A background capability, even though this is a one-shot tool at
    // process start. Reading and writing the store blocks on a disk, and
    // an initialization context deliberately does not admit Effect::Block.
    // The tool runs no dispatch thread, so main is the one scope that takes
    // the background door, and utils/scripts/ctx-bg-door-allowlist.txt lists it.
    constexpr ledger::LedgerIoCtx ctx{::foundation::effects::host::BackgroundOwner::mint_background_context()};

    const ledger::HostFacts facts = ledger::probe_host_facts(ctx);
    const ledger::HostFingerprint fingerprint = ledger::fold_fingerprint(facts);
    const CompetenceReport competence = ledger::probe_competence(ctx);

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

    const ledger::RefreshOutcome outcome = ledger::run_refresh(ctx, working, queue, kProbeTable, competence, now);

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
        const std::string_view fault = ::foundation::reflect::enum_name(record.fault);
        const std::string_view error = ::foundation::reflect::enum_name(record.error);
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
        const std::string_view reason = ::foundation::reflect::enum_name(committed.error());
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
