// The recording and shadow-dispatch leaves, measured so the number means
// something, plus a gate that fails when it moves.
//
// Why this file exists next to bench_dispatch.cpp
// ------------------------------------------------
// bench_dispatch's "dispatch_op [RECORDING, no pending]" case dispatches one
// unchanging op. The detector reads that repetition as an iteration, builds
// a region, and the Vigil is in COMPILED mode after about 128 calls —
// measured, not inferred. Everything after that is the compiled leaf, so the
// case reports a recording latency it stopped measuring almost immediately.
// The cases here hold RECORD mode with a schema hash that never repeats, and
// abort if the mode changes under them.
//
// The COMPILED leaf has no such problem: that path never touches the ring at
// all, so bench_dispatch's compiled cases were already sound. One is
// repeated here so the gate covers both leaves from a single binary.
//
// The four cases, and which of them to believe
// --------------------------------------------
// "record leaf [2 appends, no drainer]" is the producer-side cost of one
// record: the MetaLog append and the ring append that record_op makes, with
// no background thread reading behind them. This is the number the
// foreground-recording claim is about.
//
// The two Vigil cases keep the real drainer. They are the faithful shape and
// they are also the unusable ones: a consumer doing graph building cannot
// keep up with a producer that has no work at all between ops, so the
// consumer's stalls arrive through the shared counters as tail samples two
// to four orders of magnitude above the median. Measured, their
// coefficient of variation runs from 200% to 4,400%. Production puts
// microseconds of model compute between two ops and the drainer keeps up; a
// tight loop is the one shape the design never has to survive. They are kept
// because the contrast is the evidence, not because their numbers are
// quotable, and the baseline marks them advisory so they report without ever
// failing the build.
//
// A batch of eight on the Vigil cases, because the timer costs about 26
// cycles and the body is shorter than that at batch one. Each recorded
// sample is then a mean of eight, which attenuates the tail: p99.9 and max
// on those two are floors, not estimates.

#include "bench_harness.h"

#include <crucible/effects/_Capabilities.h>
#include <crucible/Vigil.h>

#include <sched.h>

#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace crucible;

namespace {

// Certifies an Entry this file built field by field, so it can reach
// record_op and dispatch_op, which take the second trust tag. It runs no
// check, because a literal written here is whatever this file wrote. An
// adapter filling an Entry from a foreign runtime runs the checks in
// vessel_api_typed.h and crosses the retag edge; this is file-local so
// there is nothing shorter for an adapter to reach for.
[[nodiscard]] TraceRing::ValidatedEntryPtr certify_synthetic_entry(const TraceRing::Entry& entry
                                                                   CRUCIBLE_LIFETIMEBOUND) noexcept {
    return TraceRing::ValidatedEntryPtr{&entry};
}

// ── Shape of the traced op ─────────────────────────────────────────

constexpr uint32_t NUM_OPS = 8;
constexpr uint32_t K = Vigil::ALIGNMENT_K;

constexpr SchemaHash SCHEMA[NUM_OPS] = {
    SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102}, SchemaHash{0x103},
    SchemaHash{0x104}, SchemaHash{0x105}, SchemaHash{0x106}, SchemaHash{0x107},
};
constexpr ShapeHash SHAPE[NUM_OPS] = {
    ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
    ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207},
};

// The ring bounds the run. Warmup plus samples must stay below it so that
// every timed body appends rather than being refused.
constexpr uint32_t RING_CAPACITY = TraceRing::CAPACITY;
constexpr size_t RECORD_WARMUP = 1000;
constexpr size_t RECORD_SAMPLES = 7000;
// Eight appends inside one timed region. At batch 1 the timer costs about
// 26 cycles, which is more than the append being timed, and subtracting a
// mean overhead from each sample leaves a variance far larger than the
// signal. Eight puts roughly 120 ns under the bracket against a ~10 ns
// overhead. The price is that each recorded sample is a mean of eight, so
// the tail figures are attenuated: a single slow append is averaged with
// seven fast ones. p99.9 and max below are floors on the real tail, not
// estimates of it.
constexpr size_t RECORD_BATCH = 8;
constexpr size_t RECORD_BODIES = RECORD_WARMUP + RECORD_SAMPLES * RECORD_BATCH;
static_assert(RECORD_BODIES < RING_CAPACITY,
              "A recording case must finish before the ring fills, or it measures the reject path "
              "instead of an append. Lower RECORD_SAMPLES or drain between samples.");

// Confines the whole process to the isolated CPUs and returns the one to
// measure on, or -1 when the host isolates nothing.
//
// Two separate reasons, and skipping either one costs a factor of two.
//
// Process-wide, because affinity has to precede the first allocation. Pinning
// only the measuring thread leaves the buffers on whatever NUMA node the
// process started on while the reads come from a core that may be on another,
// and the record leaf then measures the interconnect: 22.4 ns against 11.1 ns
// for the same binary launched under taskset. Setting it here, before
// anything is allocated and before the Vigil starts its drain thread, makes
// the binary give the same answer however it was launched. That matters more
// for the committed test than for a person at a prompt, who would remember
// the taskset; ctest does not.
//
// A named core within the set, rather than the harness's auto-pick, because a
// Vigil's drain thread is free to land anywhere in the set. Sharing a core
// with it turns the producer into something that waits on the consumer, which
// arrives as millisecond outliers. The measuring thread takes the last
// isolated CPU and the drain thread gets the rest.
[[nodiscard]] int pin_process_to_isolated_set() noexcept {
    std::FILE* f = std::fopen("/sys/devices/system/cpu/isolated", "rb");
    if (f == nullptr) return -1;
    char line[512] = {};
    const bool read_ok = std::fgets(line, sizeof(line), f) != nullptr;
    std::fclose(f);
    if (!read_ok) return -1;

    cpu_set_t set;
    CPU_ZERO(&set);
    int last = -1;
    // The file is a comma-separated list of ranges: "88-95" or "4,6,88-95".
    for (const char* p = line; *p != '\0';) {
        if (*p < '0' || *p > '9') {
            ++p;
            continue;
        }
        char* end = nullptr;
        const long lo = std::strtol(p, &end, 10);
        long hi = lo;
        if (end != nullptr && *end == '-') {
            hi = std::strtol(end + 1, &end, 10);
        }
        for (long c = lo; c <= hi && c < CPU_SETSIZE; ++c) {
            CPU_SET(static_cast<size_t>(c), &set);
            last = static_cast<int>(c);
        }
        p = (end != nullptr && end != p) ? end : p + 1;
    }
    if (last < 0) return -1;
    if (sched_setaffinity(0, sizeof(set), &set) != 0) return -1;
    return last;
}

void* fake_ptr(uint32_t iter, uint32_t op) noexcept {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

TensorMeta make_meta(void* data_ptr) noexcept {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = tensor_dim(1024);
    m.strides[0] = tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = external_data_ptr(data_ptr);
    return m;
}

struct OpData {
    TraceRing::Entry entry{};
    TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

OpData make_op(uint32_t iter, uint32_t op_idx) noexcept {
    OpData d;
    d.entry.schema_hash = SCHEMA[op_idx];
    d.entry.shape_hash = SHAPE[op_idx];
    d.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    d.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0) d.metas[idx++] = make_meta(fake_ptr(iter, op_idx - 1));
    d.metas[idx++] = make_meta(fake_ptr(iter, op_idx));
    d.n_metas = static_cast<uint16_t>(d.entry.num_inputs + d.entry.num_outputs);
    return d;
}

void feed_record(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
}

void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
}

void wait_region_published(Vigil& vigil) {
    uint64_t spins = 0;
    while (!vigil.has_pending_region()) {
        if (++spins > 100000000) {
            std::fprintf(stderr, "bench_record_leaf: Vigil never published a region\n");
            std::abort();
        }
        CRUCIBLE_SPIN_PAUSE;
    }
}

void align_and_activate(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(iter, i);
        [[maybe_unused]] auto r = vigil.dispatch_op(certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        [[maybe_unused]] auto r = vigil.dispatch_op(certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
}

void setup_compiled_vigil(Vigil& vigil) {
    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);
    vigil.flush();
    wait_region_published(vigil);
    align_and_activate(vigil, 3);
}

// ── The gate ───────────────────────────────────────────────────────
//
// A ceiling per case, in nanoseconds, read from a committed file, plus a
// flag saying whether that case is allowed to fail the build. The numbers
// were measured on one machine, so a case only gates where the run still
// looks like the one that produced them.

struct Ceiling {
    std::string name;
    double p50_ns = 0.0;
    double p99_ns = 0.0;
    bool gate = false;  // false = report the case, never fail on it
};

// Sized from the observed run-to-run spread of p50 across ten interleaved
// runs on a loaded box: 0.0% for the compiled leaf, 11% for the recording
// leaf. A tighter bound turns a busy machine red, which trains people to
// ignore it.
constexpr double kTolerance = 1.30;

// The gate compares p50 and nothing else, and the reason is measured rather
// than assumed. Over ten interleaved runs the compiled leaf held p50 to a
// 0.0% spread while its p99 ranged 4.64 to 8.12 ns, a factor of 1.75. A
// ceiling wide enough not to redden on that p99 would be too wide to catch
// any regression worth catching. p99 is printed on every line and recorded
// in the baseline so a person can see it move; it just does not vote.
constexpr bool kGateOnP99 = false;

// Within-run cv is NOT the criterion, though it is the obvious one to reach
// for. Measured across ten runs of each case: the recording leaf held its
// p50 to a 0.0% run-to-run spread while its within-run cv read 483%. A
// bimodal operation — an append that crosses a cache line against one that
// does not — has a large sample spread and a perfectly reproducible median.
// Skipping on cv would skip exactly the case that reproduces best.
//
// drift_flag is the criterion instead. The harness computes it by comparing
// the first half of the samples against the second, which is what catches a
// machine that changed under the run: a frequency step, another tenant
// arriving, a cache going cold. It needs 200 samples to compute, so before
// the wall-cap unit fix in bench_harness.h — which left every run with 64 —
// it could never fire at all.

// A deliberately small reader: the file is a flat list of
// {"name":..,"p50_ns":..,"p99_ns":..} objects and nothing else, so a full
// JSON parser would be a dependency bought for no reason.
[[nodiscard]] std::vector<Ceiling> read_baseline(const char* path) {
    std::vector<Ceiling> out;
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return out;
    std::string text;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    std::fclose(f);

    size_t pos = 0;
    auto field = [&](const char* key, size_t from, double& into) -> bool {
        const std::string needle = std::string("\"") + key + "\":";
        size_t k = text.find(needle, from);
        if (k == std::string::npos) return false;
        into = std::strtod(text.c_str() + k + needle.size(), nullptr);
        return true;
    };
    while ((pos = text.find("\"name\":", pos)) != std::string::npos) {
        size_t q1 = text.find('"', pos + 7);
        if (q1 == std::string::npos) break;
        size_t q2 = text.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        Ceiling c;
        c.name = text.substr(q1 + 1, q2 - q1 - 1);
        if (!field("p50_ns", q2, c.p50_ns) || !field("p99_ns", q2, c.p99_ns)) break;
        double gate_flag = 0.0;
        c.gate = field("gate", q2, gate_flag) && gate_flag != 0.0;
        out.push_back(std::move(c));
        pos = q2 + 1;
    }
    return out;
}

}  // namespace

int main() {
    // Before anything allocates and before the first Vigil starts its drain
    // thread. -1 means the host isolates nothing, and every case then falls
    // back to the harness's own pin; the numbers will be noisier and the
    // gate will say so.
    const int record_core = pin_process_to_isolated_set();

    bench::print_system_info();
    if (record_core < 0) {
        std::printf("  NOTE: no isolated CPUs on this host; measuring unpinned and expecting noise.\n");
    } else {
        std::printf("  measuring on cpu%d, process confined to the isolated set\n", record_core);
    }
    bench::elevate_priority();

    std::vector<bench::Report> reports;

    // ── The recording leaf with no drainer behind it ────────────────
    //
    // This is the number the "foreground records at N ns/op" claim is
    // about, and it is the only recording case here that holds still.
    //
    // record_op is two calls and nothing else: MetaLog::try_append then
    // TraceRing::try_append. Both are reproduced against standalone rings
    // so that no background thread is reading the lines being written. The
    // Vigil-based cases further down keep the drainer, and pay for it: a
    // consumer doing graph building cannot keep up with a producer that has
    // no work between ops, so its stalls arrive through the shared counters
    // as microsecond outliers. Production puts microseconds of model
    // compute between two ops and the drainer keeps up; a tight loop is the
    // one shape the design does not have to survive.
    //
    // Reset on full rather than a bounded run, so the sample count is free.
    // The ring takes 65,536 entries and the log 1,048,576, so a reset lands
    // roughly once in 65,536 bodies and rounds to nothing per op.
    reports.push_back([&] {
        auto ring = std::make_unique<TraceRing>();
        auto log = std::make_unique<MetaLog>();
        ring->reset();
        log->reset();

        auto d = make_op(0, 1);
        uint64_t tick = 0;
        return bench::Run("record leaf [2 appends, no drainer]")
            .core(record_core >= 0 ? record_core : bench::env_core())
            .measure([&] {
                d.entry.schema_hash = SchemaHash{0x1000000u + tick++};
                MetaIndex meta_start = log->try_append(d.metas, d.n_metas);
                if (!meta_start.is_valid()) [[unlikely]] {
                    log->reset();
                    meta_start = log->try_append(d.metas, d.n_metas);
                }
                if (!ring->try_append(d.entry, meta_start, ScopeHash{}, CallsiteHash{})) [[unlikely]] {
                    ring->reset();
                }
            });
    }());

    // ── The recording leaf, ring guaranteed to have room ────────────
    //
    // record_op is the whole of what dispatch_op does in RECORD mode
    // besides the mode test, so the pair of cases below brackets the leaf
    // from underneath and from the caller's side.
    reports.push_back([&] {
        Vigil vigil;
        auto d = make_op(0, 1);
        uint64_t refused = 0;
        uint64_t tick = 0;
        auto report = bench::Run("record_op [2 metas, ring has room]")
                          .warmup(RECORD_WARMUP)
                          .samples(RECORD_SAMPLES)
                          .batch(RECORD_BATCH)
                          .core(record_core >= 0 ? record_core : bench::env_core())
                          .measure([&] {
                              // Same non-repeating stream as the dispatch_op case below, so the
                              // difference between the two reports is that case's mode test and
                              // pending-region check and nothing else.
                              d.entry.schema_hash = SchemaHash{0x1000000u + tick++};
                              const bool took = vigil.record_op(certify_synthetic_entry(d.entry), d.metas, d.n_metas);
                              refused += static_cast<uint64_t>(!took);
                              bench::do_not_optimize(took);
                          });
        if (refused != 0) {
            std::fprintf(stderr,
                         "bench_record_leaf: %llu of %zu appends were refused. The ring filled, so "
                         "this case measured the reject path. Lower RECORD_SAMPLES.\n",
                         static_cast<unsigned long long>(refused), RECORD_BODIES);
            std::abort();
        }
        return report;
    }());

    // Holding dispatch_op in RECORD mode takes a stream with no period in
    // it. Feeding one op over and over is what a first draft does, and the
    // detector reads the repetition as an iteration, builds a region and
    // flips the Vigil to COMPILED after about 128 calls — after which this
    // case would be measuring the compiled leaf twice under two names. A
    // schema hash that never repeats gives the detector nothing to confirm.
    //
    // The hash only travels: nothing on the recording path looks it up, and
    // the detector that reads it runs on the background thread. The cost
    // this adds to the body is the one store below.
    reports.push_back([&] {
        Vigil vigil;
        auto d = make_op(0, 1);
        uint64_t tick = 0;
        uint64_t recorded = 0;
        auto report = bench::Run("dispatch_op [RECORD leaf, ring has room]")
                          .warmup(RECORD_WARMUP)
                          .samples(RECORD_SAMPLES)
                          .batch(RECORD_BATCH)
                          .core(record_core >= 0 ? record_core : bench::env_core())
                          .measure([&] {
                              d.entry.schema_hash = SchemaHash{0x1000000u + tick++};
                              const auto r = vigil.dispatch_op(certify_synthetic_entry(d.entry), d.metas, d.n_metas);
                              recorded += static_cast<uint64_t>(r.is_record());
                              bench::do_not_optimize(r);
                          });
        // The count is the instrument, not the ring's head: the background
        // thread resets the ring, so head is a level rather than a total and
        // reads far below the number of ops that went through it.
        const uint64_t bodies = RECORD_BODIES;
        if (recorded < bodies) {
            std::fprintf(stderr,
                         "bench_record_leaf: only %llu of %llu calls took the RECORD path (%.1f%%). The "
                         "Vigil switched to COMPILED mid-run, so this case stopped measuring the "
                         "recording leaf.\n",
                         static_cast<unsigned long long>(recorded), static_cast<unsigned long long>(bodies),
                         100.0 * static_cast<double>(recorded) / static_cast<double>(bodies));
            std::abort();
        }
        return report;
    }());

    // ── The shadow-dispatch leaf ────────────────────────────────────
    //
    // COMPILED mode returns a shadow handle without touching the ring, so
    // this one carries no capacity constraint and runs at the harness's
    // own defaults.
    reports.push_back([&] {
        Vigil vigil;
        setup_compiled_vigil(vigil);
        OpData ops[NUM_OPS];
        for (uint32_t i = 0; i < NUM_OPS; i++)
            ops[i] = make_op(10, i);

        uint32_t op_idx = 0;
        return bench::run("dispatch_op [COMPILED leaf, cyclic]", [&] {
            auto& d = ops[op_idx];
            bench::do_not_optimize(vigil.dispatch_op(certify_synthetic_entry(d.entry), d.metas, d.n_metas));
            op_idx = (op_idx + 1) % NUM_OPS;
        });
    }());

    bench::emit_reports(reports, bench::env_json());

    // ── Gate ────────────────────────────────────────────────────────

    const std::vector<Ceiling> baseline = read_baseline(CRUCIBLE_RECORD_LEAF_BASELINE);
    if (baseline.empty()) {
        std::fprintf(stderr, "\nbench_record_leaf: no baseline at %s — nothing to gate against.\n",
                     CRUCIBLE_RECORD_LEAF_BASELINE);
        return 1;
    }

    std::printf("\n=== gate (ceiling = baseline x %.2f) ===\n", kTolerance);
    int failures = 0;
    int skipped = 0;
    for (const auto& report : reports) {
        const Ceiling* want = nullptr;
        for (const auto& c : baseline)
            if (c.name == report.name) want = &c;
        if (want == nullptr) {
            std::printf("  %-44s NO-BASELINE\n", report.name.c_str());
            failures++;
            continue;
        }
        if (!want->gate) {
            std::printf("  %-44s ADVISORY  p50 %8.2f ns   p99 %8.2f ns   (recorded %6.2f / %6.2f)\n",
                        report.name.c_str(), report.pct.p50, report.pct.p99, want->p50_ns, want->p99_ns);
            continue;
        }
        if (report.drift_flag) {
            std::printf("  %-44s SKIP      the run drifted %.1f%% between its first and second half; "
                        "the machine changed under it\n",
                        report.name.c_str(), report.drift_pct);
            skipped++;
            continue;
        }
        const double p50_max = want->p50_ns * kTolerance;
        const double p99_max = want->p99_ns * kTolerance;
        const bool ok = report.pct.p50 <= p50_max && (!kGateOnP99 || report.pct.p99 <= p99_max);
        std::printf("  %-44s %s  p50 %6.2f / %6.2f ns   p99 %6.2f (recorded %6.2f, not gated)\n", report.name.c_str(),
                    ok ? "PASS" : "FAIL", report.pct.p50, p50_max, report.pct.p99, want->p99_ns);
        if (!ok) failures++;
    }

    std::printf("=== verdict: %s (%d failed, %d skipped for drift) ===\n", failures == 0 ? "PASS" : "FAIL", failures,
                skipped);
    return failures == 0 ? 0 : 1;
}
