// The hardware-capability ledger: fingerprint, competence, admission,
// store round-trip and the fail-closed read path.
//
// The claims that can be settled at compile time already are — every
// ledger header carries a self-test block, and including them here is
// what makes those blocks part of a build that runs. What is left for
// this file is the behaviour a static_assert cannot reach: the filesystem
// round-trip, the tamper paths through the parser, and the fact that a
// reader handed nothing returns the caller's conservative answer rather
// than a number it invented.

#include <crucible/ledger/Ledger.h>

#include "test_assert.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

using namespace crucible;
using crucible::ledger::Confidence;
using crucible::ledger::CompetenceDefect;
using crucible::ledger::CompetenceReport;
using crucible::ledger::EvidenceFault;
using crucible::ledger::LedgerError;
using crucible::ledger::VerdictEvidence;
using crucible::ledger::VerdictId;
using crucible::ledger::VerdictValue;

namespace {

// ── Fixtures ──────────────────────────────────────────────────────────

[[nodiscard]] CompetenceReport fit_host() noexcept {
    CompetenceReport report{};
    report.isolated_core_count = 8;
    report.online_sibling_count = 0;
    report.load_average_milli = 1000;
    report.allowed_cpu_count = 384;
    report.machine_cpu_count = 384;
    report.perf_event_paranoid = 2;
    report.scaling_min_freq_khz = 4510205;
    report.scaling_max_freq_khz = 4510205;
    report.governor_is_performance = false;
    report.defects = ledger::derive_defects(report, true);
    return report;
}

[[nodiscard]] CompetenceReport unfit_host() noexcept {
    CompetenceReport report = fit_host();
    report.online_sibling_count = 4;  // an isolated core shares its pipeline
    report.defects = ledger::derive_defects(report, true);
    return report;
}

[[nodiscard]] VerdictEvidence sound_evidence() noexcept {
    VerdictEvidence evidence{};
    evidence.quantiles = cog::LatencyQuantiles{20u, 24u, 40u};
    evidence.sample_count = 100000;
    evidence.within_run_cv_ppm = 12000;
    evidence.run_to_run_spread_ppm = 4000;
    return evidence;
}

// ── Fingerprint ───────────────────────────────────────────────────────

void test_fingerprint_is_stable_and_complete() {
    const ledger::HostFacts facts = ledger::probe_host_facts();
    const ledger::HostFingerprint first = ledger::fold_fingerprint(facts);
    const ledger::HostFingerprint second = ledger::fold_fingerprint(facts);

    // The fold is pure: the same facts must give the same key, or the
    // cache would miss on every single lookup.
    assert(first == second);
    assert(first.is_complete());

    // A re-probe of an unchanged host must land on the same key too.
    // This is the property the whole cache rests on.
    assert(ledger::probe_host_fingerprint() == first);

    std::printf("  test_fingerprint_is_stable_and_complete:   PASSED\n");
}

void test_fingerprint_halves_are_independent() {
    ledger::HostFacts facts = ledger::probe_host_facts();
    const ledger::HostFingerprint baseline = ledger::fold_fingerprint(facts);

    // A microcode roll is a hardware change: it can make an instruction
    // faster or slower with nothing else observable.
    ledger::HostFacts rolled = facts;
    rolled.microcode_revision += 1u;
    const ledger::HostFingerprint after_microcode = ledger::fold_fingerprint(rolled);
    assert(after_microcode.hardware != baseline.hardware);
    assert(after_microcode.policy == baseline.policy);
    assert(ledger::compare_fingerprints(baseline, after_microcode) == ledger::FingerprintMatch::HardwareChanged);

    // A governor change is a policy change: the same silicon under
    // different rules, and reversible.
    ledger::HostFacts retuned = facts;
    retuned.governor_digest ^= 0x5555555555555555ull;
    const ledger::HostFingerprint after_governor = ledger::fold_fingerprint(retuned);
    assert(after_governor.hardware == baseline.hardware);
    assert(after_governor.policy != baseline.policy);
    assert(ledger::compare_fingerprints(baseline, after_governor) == ledger::FingerprintMatch::PolicyChanged);

    // The SVE vector length is implementation-defined between 128 and
    // 2048 bits and differs across Graviton 3, Graviton 4 and A64FX, so
    // two arm64 hosts agreeing on everything else still must not share a
    // key. Folded unconditionally so the property holds on any builder.
    ledger::HostFacts wider_vector = facts;
    wider_vector.sve_vector_length_bits = static_cast<std::uint16_t>(facts.sve_vector_length_bits + 128u);
    assert(ledger::fold_hardware(wider_vector) != baseline.hardware);

    // The NUMA distance matrix, not just the node count: two four-node
    // hosts can have different hop costs.
    ledger::HostFacts rewired = facts;
    rewired.numa_distance_digest ^= 0x1ull;
    assert(ledger::fold_hardware(rewired) != baseline.hardware);

    std::printf("  test_fingerprint_halves_are_independent:   PASSED\n");
}

void test_fingerprint_reuses_the_cogmimic_projection() {
    // The stable half must move when the CogMimic caps class moves. If it
    // did not, two Cogs that CogMimic considers binary-incompatible could
    // share ledger entries, which is the drift this reuse exists to stop.
    const ledger::HostFacts facts = ledger::probe_host_facts();
    const cog::CpuSocketTargetCaps socket = ledger::to_socket_caps(facts);

    cog::CpuSocketTargetCaps other = socket;
    other.l3_bytes = safety::Tagged<std::uint64_t, safety::source::Vendor>{socket.l3_bytes.value() * 2u};

    const std::uint64_t left = mimic::detail::caps_class_projection<cog::CogKind::CpuSocket>::fold(socket);
    const std::uint64_t right = mimic::detail::caps_class_projection<cog::CogKind::CpuSocket>::fold(other);
    assert(left != right);

    ledger::HostFacts doubled = facts;
    doubled.l3_total_bytes = facts.l3_total_bytes * 2u;
    assert(ledger::fold_hardware(doubled) != ledger::fold_hardware(facts));

    std::printf("  test_fingerprint_reuses_the_cogmimic_projection: PASSED\n");
}

// ── Competence ────────────────────────────────────────────────────────

void test_competence_names_every_defect() {
    const CompetenceReport fit = fit_host();
    assert(fit.is_competent());

    std::array<char, 256> buffer{};
    assert(ledger::describe_defects(fit, buffer) == "none");

    const CompetenceReport unfit = unfit_host();
    assert(!unfit.is_competent());
    const std::string_view described = ledger::describe_defects(unfit, buffer);
    assert(described.find("SmtSiblingOnline") != std::string_view::npos);

    // A one-byte buffer cannot hold even "none" and must not write past
    // its end or return a dangling view.
    std::array<char, 1> tiny{};
    assert(ledger::describe_defects(fit, tiny).empty());

    std::printf("  test_competence_names_every_defect:        PASSED\n");
}

void test_competence_load_uses_machine_capacity() {
    // Regression. /proc/loadavg is a machine-wide number; dividing it by
    // the process's allowed set reported a job pinned to eight isolated
    // cores on a 384-thread box as overloaded at 1957%, when the load was
    // entirely on the 376 threads it could not reach.
    CompetenceReport pinned = fit_host();
    pinned.load_average_milli = 156640;
    pinned.allowed_cpu_count = 8;
    pinned.machine_cpu_count = 384;
    assert(ledger::derive_defects(pinned, true).raw() == 0u);

    // The bar still bites when the machine itself is busy.
    CompetenceReport busy = fit_host();
    busy.load_average_milli = 300000;
    assert((ledger::derive_defects(busy, true).raw() & static_cast<std::uint16_t>(CompetenceDefect::LoadAverageHigh))
           != 0u);

    std::printf("  test_competence_load_uses_machine_capacity: PASSED\n");
}

void test_competence_accepts_a_pinned_floor_under_any_governor() {
    // The bench host runs powersave with scaling_min == scaling_max. That
    // is pinned in fact, and testing the governor string instead of the
    // frequencies would call it degraded.
    CompetenceReport powersave_but_pinned = fit_host();
    powersave_but_pinned.governor_is_performance = false;
    powersave_but_pinned.scaling_min_freq_khz = 4510205;
    powersave_but_pinned.scaling_max_freq_khz = 4510205;
    assert((ledger::derive_defects(powersave_but_pinned, true).raw()
            & static_cast<std::uint16_t>(CompetenceDefect::ClockNotPinned))
           == 0u);

    CompetenceReport free_to_drop = fit_host();
    free_to_drop.governor_is_performance = false;
    free_to_drop.scaling_min_freq_khz = 1220000;
    free_to_drop.scaling_max_freq_khz = 4510205;
    assert((ledger::derive_defects(free_to_drop, true).raw()
            & static_cast<std::uint16_t>(CompetenceDefect::ClockNotPinned))
           != 0u);

    std::printf("  test_competence_accepts_a_pinned_floor:    PASSED\n");
}

// ── Admission ─────────────────────────────────────────────────────────

void test_admission_refuses_unsound_evidence() {
    const CompetenceReport fit = fit_host();

    // Sound evidence on a fit host is the only way to high confidence.
    auto good = ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, sound_evidence(), fit, 1000u);
    assert(good.has_value());
    assert(good->confidence == Confidence::High);

    // Every bar, and the fault each one reports.
    struct Case {
        VerdictEvidence evidence;
        EvidenceFault expected;
    };
    VerdictEvidence starved = sound_evidence();
    starved.sample_count = 31;
    VerdictEvidence noisy = sound_evidence();
    noisy.within_run_cv_ppm = 60000;
    VerdictEvidence drifting = sound_evidence();
    drifting.run_to_run_spread_ppm = 150000;
    VerdictEvidence inverted = sound_evidence();
    inverted.quantiles = cog::LatencyQuantiles{40u, 24u, 20u};
    VerdictEvidence zeroed = sound_evidence();
    zeroed.quantiles = cog::LatencyQuantiles{0u, 0u, 0u};

    const Case cases[] = {
        {starved, EvidenceFault::TooFewSamples},
        {noisy, EvidenceFault::WithinRunCvTooHigh},
        {drifting, EvidenceFault::RunToRunSpreadTooHigh},
        {inverted, EvidenceFault::QuantilesOutOfOrder},
        {zeroed, EvidenceFault::ZeroMedian},
    };
    for (Case const& probe_case : cases) {
        assert(ledger::audit_evidence(probe_case.evidence) == probe_case.expected);
        auto refused =
            ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, probe_case.evidence, fit, 1000u);
        assert(!refused.has_value());
        assert(refused.error() == LedgerError::ConfidenceBelowBar);
    }

    // A measurement with no timestamp cannot be aged, so it is refused
    // rather than stored looking permanently fresh.
    auto timeless = ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, sound_evidence(), fit, 0u);
    assert(!timeless.has_value());
    assert(timeless.error() == LedgerError::ClockUnavailable);

    std::printf("  test_admission_refuses_unsound_evidence:   PASSED\n");
}

void test_unfit_host_caps_confidence_at_low() {
    const CompetenceReport unfit = unfit_host();
    auto entry = ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, sound_evidence(), unfit, 1000u);

    // Still admitted — a degraded host measures and records. It is the
    // GRADE that is capped, not the measurement that is discarded.
    assert(entry.has_value());
    assert(entry->confidence == Confidence::Low);

    // And the defect word travels with it, so the reason survives even
    // after the host is tidied up.
    assert(entry->competence_defects_at_measurement == unfit.defect_word());
    assert(entry->competence_defects_at_measurement != 0u);

    // A low-confidence entry is never servable, however fresh.
    assert(!entry->is_expired_at(1000u));
    assert(!entry->is_servable_at(1000u));

    std::printf("  test_unfit_host_caps_confidence_at_low:    PASSED\n");
}

void test_expiry_including_a_clock_that_moved_backwards() {
    const CompetenceReport fit = fit_host();
    auto entry = ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, sound_evidence(), fit, 10000u);
    assert(entry.has_value());

    const std::uint32_t ttl = entry->ttl.seconds;
    assert(ttl == ledger::kDefaultTtlSeconds);
    assert(!entry->is_expired_at(10000u));
    assert(!entry->is_expired_at(10000u + ttl - 1u));
    assert(entry->is_expired_at(10000u + ttl));

    // An entry stamped in the future means the clock moved. Treating it
    // as expired forces a remeasure, which is the conservative reading.
    assert(entry->is_expired_at(9999u));

    // A never-expiring verdict is the shape a geometry answer wants: the
    // fingerprint already folds the cache sizes, so the knee cannot move
    // without the key moving first.
    ledger::LedgerEntry geometry = *entry;
    geometry.ttl = ledger::VerdictTtl::never();
    assert(!geometry.is_expired_at(10000u + 10000000u));

    std::printf("  test_expiry_including_backwards_clock:     PASSED\n");
}

// ── The fail-closed read path ─────────────────────────────────────────

void test_reader_returns_the_callers_conservative_answer() {
    // An empty view is a correct state, not an error one: a first boot, a
    // corrupted cache and a fresh kernel upgrade must all produce
    // correct-but-slower rather than wrong.
    const ledger::LedgerView empty = ledger::LedgerView::empty(LedgerError::StoreReadFailed);
    assert(!empty.is_loaded());

    const auto miss = empty.lookup(VerdictId::TimerFloorNanos);
    assert(!miss.is_known());
    assert(miss.confidence() == Confidence::Unknown);

    // The conservative answer comes back verbatim — not a zero the
    // mechanism invented, and not a default.
    assert(miss.value_or_conservative(VerdictValue{999u}) == VerdictValue{999u});
    assert(miss.value_or_conservative(VerdictValue{0u}) == VerdictValue{0u});
    assert(miss.boolean_or_conservative(true) == true);
    assert(miss.boolean_or_conservative(false) == false);

    // A ledger holding only a low-confidence entry serves nothing by
    // default, and serves it only through the explicitly-named door.
    const CompetenceReport unfit = unfit_host();
    ledger::Ledger raw{};
    raw.fingerprint = ledger::HostFingerprint{ledger::HardwareDigest{1u}, ledger::PolicyDigest{2u}};
    raw.competence = unfit;
    auto low = ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{77u}, sound_evidence(), unfit, 10000u);
    assert(low.has_value());
    assert(raw.upsert(*low).has_value());

    const ledger::LedgerView view{raw, 10000u};
    assert(view.is_loaded());
    assert(!view.lookup(VerdictId::TimerFloorNanos).is_known());
    assert(view.lookup(VerdictId::TimerFloorNanos).value_or_conservative(VerdictValue{5u}) == VerdictValue{5u});

    const auto explicit_low = view.lookup_including_low(VerdictId::TimerFloorNanos);
    assert(explicit_low.is_known());
    assert(explicit_low.confidence() == Confidence::Low);
    assert(explicit_low.value_or_conservative(VerdictValue{5u}) == VerdictValue{77u});

    std::printf("  test_reader_returns_conservative_answer:   PASSED\n");
}

// ── Serialization ─────────────────────────────────────────────────────

[[nodiscard]] ledger::Ledger sample_ledger() {
    const CompetenceReport fit = fit_host();
    ledger::Ledger written{};
    written.fingerprint = ledger::HostFingerprint{ledger::HardwareDigest{0x7b92e6be37e8344eull},
                                                  ledger::PolicyDigest{0xa3dfb30cecc936c9ull}};
    written.competence = fit;
    ledger::fingerprint_detail::copy_into(written.cpu_vendor, "AuthenticAMD");
    ledger::fingerprint_detail::copy_into(written.cpu_model, "AMD EPYC 9655 96-Core Processor");
    auto entry = ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, sound_evidence(), fit, 1700000000u);
    assert(entry.has_value());
    assert(written.upsert(*entry).has_value());
    return written;
}

void test_serialization_round_trips() {
    const ledger::Ledger written = sample_ledger();
    const std::string text = ledger::serialize_ledger(written);

    auto parsed = ledger::deserialize_ledger(text);
    assert(parsed.has_value());
    assert(parsed->fingerprint == written.fingerprint);
    assert(parsed->entries.size() == written.entries.size());
    assert(std::string_view{parsed->cpu_vendor.data()} == "AuthenticAMD");
    assert(std::string_view{parsed->cpu_model.data()} == "AMD EPYC 9655 96-Core Processor");

    const ledger::LedgerEntry* recovered = parsed->find(VerdictId::TimerFloorNanos);
    assert(recovered != nullptr);
    assert(recovered->value.value() == VerdictValue{20u});
    assert(recovered->confidence == Confidence::High);
    assert(recovered->evidence == written.entries[0].evidence);
    assert(recovered->measured_at_unix_seconds == 1700000000u);
    assert(recovered->ttl == written.entries[0].ttl);

    // Re-serializing must give byte-identical output, or a no-op refresh
    // would rewrite the file and churn its mtime for nothing.
    assert(ledger::serialize_ledger(*parsed) == text);

    std::printf("  test_serialization_round_trips:            PASSED\n");
}

void test_parser_rejects_malformed_and_tampered_records() {
    const std::string good = ledger::serialize_ledger(sample_ledger());

    // No magic line at all.
    assert(!ledger::deserialize_ledger("fingerprint\t1\t2\n").has_value());
    // Magic but no fingerprint.
    assert(!ledger::deserialize_ledger("crucible-hwledger\t1\n").has_value());
    // A format version this build does not know is refused whole rather
    // than read partially.
    assert(!ledger::deserialize_ledger("crucible-hwledger\t99\nfingerprint\t1\t2\n").has_value());
    // Empty input.
    assert(!ledger::deserialize_ledger("").has_value());

    auto replace_field = [](std::string const& text, std::size_t index, std::string_view with) {
        std::string out;
        std::size_t cursor = 0;
        while (cursor < text.size()) {
            const std::size_t line_end = std::min(text.find('\n', cursor), text.size());
            std::string_view line{text.data() + cursor, line_end - cursor};
            if (line.starts_with("verdict\t")) {
                std::size_t field = 0;
                std::size_t at = 0;
                std::string rebuilt;
                while (at <= line.size()) {
                    const std::size_t tab = std::min(line.find('\t', at), line.size());
                    if (!rebuilt.empty()) rebuilt += '\t';
                    if (field == index) {
                        rebuilt += with;
                    } else {
                        rebuilt += std::string{line.substr(at, tab - at)};
                    }
                    ++field;
                    if (tab == line.size()) break;
                    at = tab + 1u;
                }
                out += rebuilt;
            } else {
                out += std::string{line};
            }
            out += '\n';
            cursor = line_end + 1u;
        }
        return out;
    };

    // Field 4 is the confidence grade, field 11 the defect word. Forging
    // a high grade on an entry whose recorded defects say the host was
    // unfit must not promote it — the parser clamps against the evidence
    // it can see rather than trusting the word on the page.
    const std::string forged = replace_field(replace_field(good, 4, "high"), 11, "0002");
    auto clamped = ledger::deserialize_ledger(forged);
    assert(clamped.has_value());
    assert(clamped->find(VerdictId::TimerFloorNanos) == nullptr);

    // The same forgery in the honest direction — grade low, defects set —
    // is kept, because a writer is allowed to be more cautious than the
    // rule requires.
    const std::string honest = replace_field(replace_field(good, 4, "low"), 11, "0002");
    auto kept = ledger::deserialize_ledger(honest);
    assert(kept.has_value());
    const ledger::LedgerEntry* low_entry = kept->find(VerdictId::TimerFloorNanos);
    assert(low_entry != nullptr);
    assert(low_entry->confidence == Confidence::Low);

    // Evidence edited below a bar is dropped even when the grade says
    // high: an editor cannot promote a verdict by changing one word.
    const std::string starved = replace_field(good, 5, "4");
    auto dropped = ledger::deserialize_ledger(starved);
    assert(dropped.has_value());
    assert(dropped->find(VerdictId::TimerFloorNanos) == nullptr);

    // An "unknown" grade is never written, so reading one back means the
    // file was edited. The line is dropped, which is what unknown means.
    const std::string unknown = replace_field(good, 4, "unknown");
    auto skipped = ledger::deserialize_ledger(unknown);
    assert(skipped.has_value());
    assert(skipped->find(VerdictId::TimerFloorNanos) == nullptr);

    // A verdict name this build has never heard of is skipped rather than
    // failing the file, so a fleet mid-upgrade does not discard its cache.
    const std::string future = replace_field(good, 1, "verdict_from_a_later_build");
    auto forward = ledger::deserialize_ledger(future);
    assert(forward.has_value());
    assert(forward->entries.empty());

    // A record kind from a newer writer is ignored the same way.
    auto with_future_record = ledger::deserialize_ledger(good + "telemetry\t1\t2\t3\n");
    assert(with_future_record.has_value());
    assert(with_future_record->entries.size() == 1u);

    // A truncated verdict line is malformed, not silently short-read.
    assert(!ledger::deserialize_ledger(good + "verdict\ttimer_floor_ns\t1\n").has_value());

    std::printf("  test_parser_rejects_tampered_records:      PASSED\n");
}

// ── Store round-trip on a real filesystem ─────────────────────────────

void test_store_round_trips_through_the_filesystem() {
    char directory_template[] = "/tmp/crucible-ledger-test-XXXXXX";
    const char* directory = ::mkdtemp(directory_template);
    assert(directory != nullptr);
    // setenv rather than a parameter: the cache root is discovered from
    // the environment in production, and a test that bypassed that would
    // not exercise the path sanitizer the discovery runs through.
    const int overrode = ::setenv("XDG_CACHE_HOME", directory, 1);
    assert(overrode == 0);

    constexpr effects::TestRunnerCtx ctx{};
    static_assert(ledger::CtxFitsLedgerStore<effects::TestRunnerCtx>);

    const ledger::Ledger written = sample_ledger();

    // Nothing stored yet: the view is empty and every lookup misses.
    const ledger::LedgerView before = ledger::mint_ledger_view(ctx, written.fingerprint);
    assert(!before.is_loaded());
    assert(!before.lookup(VerdictId::TimerFloorNanos).is_known());

    assert(ledger::commit_ledger(ctx, written).has_value());

    // The file is named for BOTH halves, so a policy change lands beside
    // a good ledger rather than on top of it.
    auto path = ledger::ledger_path_for(written.fingerprint);
    assert(path.has_value());
    assert(std::filesystem::exists(path->value()));
    const std::string filename = path->value().filename().string();
    assert(filename.find("7b92e6be37e8344e") != std::string::npos);
    assert(filename.find("a3dfb30cecc936c9") != std::string::npos);

    // No temporary is left behind by a successful commit.
    std::size_t leftover_temp_count = 0;
    for (auto const& item : std::filesystem::directory_iterator{path->value().parent_path()}) {
        if (item.path().extension() == ".tmp") {
            ++leftover_temp_count;
        }
    }
    assert(leftover_temp_count == 0u);

    auto loaded = ledger::load_ledger(ctx, written.fingerprint);
    assert(loaded.has_value());
    assert(loaded->fingerprint == written.fingerprint);
    const ledger::LedgerEntry* entry = loaded->find(VerdictId::TimerFloorNanos);
    assert(entry != nullptr);
    assert(entry->value.value() == VerdictValue{20u});

    // A different fingerprint gets a different filename and therefore a
    // miss, not the other host's entries.
    const ledger::HostFingerprint other{ledger::HardwareDigest{0xdeadbeefdeadbeefull},
                                        ledger::PolicyDigest{0xa3dfb30cecc936c9ull}};
    assert(!ledger::load_ledger(ctx, other).has_value());
    assert(!ledger::mint_ledger_view(ctx, other).is_loaded());

    // A file whose CONTENT claims a fingerprint its NAME does not is
    // rejected: someone moved or hand-edited it, and trusting the content
    // would serve another machine's verdicts.
    {
        std::string body = ledger::serialize_ledger(written);
        const std::size_t at = body.find("fingerprint\t7b92e6be37e8344e");
        assert(at != std::string::npos);
        body.replace(at, std::strlen("fingerprint\t7b92e6be37e8344e"), "fingerprint\t0000000000000001");
        std::filesystem::path target = path->value();
        auto handle = safety::open_write_truncate(target.c_str(), 0644);
        assert(handle.has_value());
        assert(safety::write_full(*handle, std::as_bytes(std::span<const char>{body.data(), body.size()})).has_value());
    }
    auto mismatched = ledger::load_ledger(ctx, written.fingerprint);
    assert(!mismatched.has_value());
    assert(mismatched.error() == LedgerError::FingerprintMismatch);
    assert(!ledger::mint_ledger_view(ctx, written.fingerprint).is_loaded());

    std::error_code ignored{};
    std::filesystem::remove_all(directory, ignored);
    std::printf("  test_store_round_trips_through_fs:         PASSED\n");
}

// ── The refresh seam ──────────────────────────────────────────────────

std::expected<ledger::VerdictMeasurement, LedgerError> stub_probe(CompetenceReport const&) noexcept {
    return ledger::VerdictMeasurement{.value = VerdictValue{42u}, .evidence = sound_evidence()};
}

std::expected<ledger::VerdictMeasurement, LedgerError> failing_probe(CompetenceReport const&) noexcept {
    return std::unexpected(LedgerError::StoreReadFailed);
}

void test_refresh_plan_and_run() {
    const CompetenceReport fit = fit_host();
    constexpr VerdictId wanted[] = {VerdictId::TimerFloorNanos};

    // Absent.
    ledger::Ledger empty{};
    auto plan = ledger::refresh_plan(empty, wanted, 10000u);
    assert(plan.size() == 1u);
    assert(plan[0].reason == ledger::RefreshReason::Absent);

    // Present, fresh and trusted: nothing to do. This is the property
    // that makes the second run of the tool measure nothing.
    ledger::Ledger warm{};
    auto high = ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, sound_evidence(), fit, 10000u);
    assert(high.has_value());
    assert(warm.upsert(*high).has_value());
    assert(ledger::refresh_plan(warm, wanted, 10000u).empty());

    // Past its TTL.
    auto stale_plan = ledger::refresh_plan(warm, wanted, 10000u + ledger::kDefaultTtlSeconds);
    assert(stale_plan.size() == 1u);
    assert(stale_plan[0].reason == ledger::RefreshReason::Expired);

    // Present but low: queued, because the reason it is low is usually
    // transient and asking again is how it gets promoted.
    ledger::Ledger degraded{};
    auto low =
        ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, sound_evidence(), unfit_host(), 10000u);
    assert(low.has_value());
    assert(degraded.upsert(*low).has_value());
    auto low_plan = ledger::refresh_plan(degraded, wanted, 10000u);
    assert(low_plan.size() == 1u);
    assert(low_plan[0].reason == ledger::RefreshReason::LowConfidence);

    // Running the plan.
    constexpr ledger::ProbeRegistration registry[] = {
        {.id = VerdictId::TimerFloorNanos, .run = &stub_probe},
    };
    ledger::Ledger target{};
    const auto outcome = ledger::run_refresh(target, plan, registry, fit, 10000u);
    assert(outcome.measured_count == 1u);
    assert(outcome.admitted_count == 1u);
    assert(outcome.refused_count == 0u);
    assert(target.find(VerdictId::TimerFloorNanos) != nullptr);
    assert(target.find(VerdictId::TimerFloorNanos)->value.value() == VerdictValue{42u});

    // An upsert replaces rather than appending, so one question never
    // gets two answers in one file.
    const auto again = ledger::run_refresh(target, plan, registry, fit, 20000u);
    assert(again.admitted_count == 1u);
    assert(target.entries.size() == 1u);
    assert(target.find(VerdictId::TimerFloorNanos)->measured_at_unix_seconds == 20000u);

    // A queued verdict with no registered probe is counted, not crashed
    // on, and the record says which one had no answer.
    ledger::Ledger orphan{};
    const auto no_probe = ledger::run_refresh(orphan, plan, {}, fit, 10000u);
    assert(no_probe.no_probe_count == 1u);
    assert(no_probe.measured_count == 0u);
    assert(no_probe.log.size() == 1u);
    assert(!no_probe.log[0].had_probe);

    // A probe that fails outright is recorded with its error rather than
    // being folded into an anonymous refusal count.
    constexpr ledger::ProbeRegistration broken[] = {
        {.id = VerdictId::TimerFloorNanos, .run = &failing_probe},
    };
    ledger::Ledger unfilled{};
    const auto failed = ledger::run_refresh(unfilled, plan, broken, fit, 10000u);
    assert(failed.refused_count == 1u);
    assert(failed.admitted_count == 0u);
    assert(failed.log.size() == 1u);
    assert(failed.log[0].had_probe);
    assert(failed.log[0].error == LedgerError::StoreReadFailed);

    std::printf("  test_refresh_plan_and_run:                 PASSED\n");
}

void test_refusals_name_the_bar_they_missed() {
    // A bare refusal count is the failure this ledger exists to prevent:
    // a bench harness on this box once collected 128 samples instead of
    // 100 000 and reported nothing but "off".
    const CompetenceReport fit = fit_host();
    constexpr VerdictId wanted[] = {VerdictId::TimerFloorNanos};
    ledger::Ledger empty{};
    const auto plan = ledger::refresh_plan(empty, wanted, 10000u);

    struct NoisyProbe {
        static std::expected<ledger::VerdictMeasurement, LedgerError> run(CompetenceReport const&) noexcept {
            VerdictEvidence noisy = sound_evidence();
            noisy.within_run_cv_ppm = 83440;  // the 8.344% this host produced under load
            return ledger::VerdictMeasurement{.value = VerdictValue{1u}, .evidence = noisy};
        }
    };
    constexpr ledger::ProbeRegistration registry[] = {
        {.id = VerdictId::TimerFloorNanos, .run = &NoisyProbe::run},
    };

    ledger::Ledger target{};
    const auto outcome = ledger::run_refresh(target, plan, registry, fit, 10000u);
    assert(outcome.refused_count == 1u);
    assert(outcome.admitted_count == 0u);
    assert(outcome.log.size() == 1u);
    assert(outcome.log[0].fault == EvidenceFault::WithinRunCvTooHigh);
    assert(outcome.log[0].error == LedgerError::ConfidenceBelowBar);
    // The evidence that missed is carried out, so the diagnostic can show
    // the number next to the bar it missed.
    assert(outcome.log[0].evidence.within_run_cv_ppm == 83440u);
    assert(ledger::evidence_fault_name(outcome.log[0].fault) == "WithinRunCvTooHigh");

    // Nothing reached the ledger.
    assert(target.entries.empty());

    std::printf("  test_refusals_name_the_bar_they_missed:    PASSED\n");
}

void test_store_is_bounded() {
    // A hostile or corrupt file must not make the reader allocate without
    // limit, and the in-memory ledger must refuse rather than grow.
    ledger::Ledger full{};
    const CompetenceReport fit = fit_host();
    auto entry = ledger::admit_entry(VerdictId::TimerFloorNanos, VerdictValue{1u}, sound_evidence(), fit, 10000u);
    assert(entry.has_value());
    for (std::size_t index = 0; index < ledger::kMaxLedgerEntries; ++index) {
        ledger::LedgerEntry distinct = *entry;
        // upsert matches on id, so forcing distinct ids is the only way
        // to fill the container. Only one id exists today, so the fill
        // uses raw pushes and the cap is checked directly.
        if (full.entries.size() < full.entries.capacity()) {
            full.entries.push_back(distinct);
        }
    }
    assert(full.entries.size() == ledger::kMaxLedgerEntries);
    // Every stored entry shares the one id that exists, so an upsert
    // replaces rather than overflowing.
    assert(full.upsert(*entry).has_value());
    assert(full.entries.size() == ledger::kMaxLedgerEntries);

    std::printf("  test_store_is_bounded:                     PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_ledger:\n");
    test_fingerprint_is_stable_and_complete();
    test_fingerprint_halves_are_independent();
    test_fingerprint_reuses_the_cogmimic_projection();
    test_competence_names_every_defect();
    test_competence_load_uses_machine_capacity();
    test_competence_accepts_a_pinned_floor_under_any_governor();
    test_admission_refuses_unsound_evidence();
    test_unfit_host_caps_confidence_at_low();
    test_expiry_including_a_clock_that_moved_backwards();
    test_reader_returns_the_callers_conservative_answer();
    test_serialization_round_trips();
    test_parser_rejects_malformed_and_tampered_records();
    test_store_round_trips_through_the_filesystem();
    test_refresh_plan_and_run();
    test_refusals_name_the_bar_they_missed();
    test_store_is_bounded();
    std::printf("test_ledger: 16 groups, all passed\n");
    return 0;
}
