// The recording and shadow-dispatch leaves, measured so the number means
// something, plus a gate that fails when it moves.
//
// Why this file exists next to bench_dispatch.cpp
// ------------------------------------------------
// One unchanging op that a Vigil records again and again is an iteration to
// the detector. The detector builds a region, and the Vigil is in COMPILED
// mode after about 128 calls. A record case that dispatches such an op times
// the compiled leaf after that. The cases here hold RECORD mode with a
// schema hash that never repeats, and abort if the mode changes under them.
// The record cases of bench_dispatch do the same, and this file adds the gate.
//
// The COMPILED leaf has no such problem: that path never touches the ring at
// all. One compiled case is repeated here so the gate covers both leaves from
// a single binary.
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
#include "../test/trace_rig.h"

#include <foundation/effects/Effect.h>
#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <fixy/os/Sched.h>

#include <cstdint>

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
    return mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>();
}
TraceRing::ValidatedEntryPtr certify_synthetic_entry(const TraceRing::Entry&&) =
    delete("the result points into the entry, and a temporary entry ends before the result");

// Certifies an Entry this file built, and pairs it with its records, so it
// can reach record_op and dispatch_op.  The process ends when the count of
// the records disagrees with the entry.
[[nodiscard]] ValidatedOp certify_synthetic_op(const TraceRing::Entry& entry CRUCIBLE_LIFETIMEBOUND,
                                               ::fixy::View<const TensorMeta> metas) noexcept {
    return crucible::mint_validated_op(certify_synthetic_entry(entry), metas)
        .expect("the count of the records agrees with the entry");
}
ValidatedOp certify_synthetic_op(const TraceRing::Entry&&, ::fixy::View<const TensorMeta>) =
    delete("the op points into the entry, and a temporary entry ends before the op");

// ── Shape of the traced op ─────────────────────────────────────────

constexpr uint32_t NUM_OPS = 8;
constexpr uint32_t K = Vigil::ALIGNMENT_K;

constexpr ::fixy::FixedArray<SchemaHash, NUM_OPS> SCHEMA{
    SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102}, SchemaHash{0x103},
    SchemaHash{0x104}, SchemaHash{0x105}, SchemaHash{0x106}, SchemaHash{0x107},
};
constexpr ::fixy::FixedArray<ShapeHash, NUM_OPS> SHAPE{
    ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
    ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207},
};

// The position of an op in the cycle of the NUM_OPS ops.
using OpSlot = ::fixy::Index<NUM_OPS>;

// The position after slot in the cycle of the ops.  A remainder is below its
// divisor, so the optimizer removes the check of the index.
[[nodiscard]] OpSlot next_in_cycle(OpSlot slot) noexcept {
    return OpSlot::check((slot.raw() + 1) % NUM_OPS).expect("a remainder is below its divisor");
}

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

// The cores of the threads of a Vigil row (the cores of a run of
// bench/bench_harness.h).  A Vigil runs five background threads: the thread
// of its pipeline and the four stages of the pipeline, which drain, detect,
// build and publish.  The bench states six threads with CORES 6 in
// bench/CMakeLists.txt, so loop bench gives it six isolated cores of one L3
// group.  The measuring thread, the producer, takes the core of slot 0.  Each
// Vigil starts in run_on_cores() with the cores of the slots 1 to 5 as the
// mask of its pipeline thread, and the pipeline pins each stage to its own
// core of that mask (fixy/concurrent/StageCpus.h).  The pipeline thread waits
// in the join of the stages, so it can share a core with a stage.  A stage
// that shares the core of the producer turns the producer into a thread that
// waits on the consumer, and that shows as outliers of milliseconds.
//
// The producer pins itself before it allocates the rings of a row, so the
// pages of the rings come from the NUMA node of its core.  A row with the
// buffers on another node measures the interconnect: 22.4 ns against 11.1 ns
// for the same binary.  The cores of a claim share one node.
//
// A run with no isolated core in its affinity, such as the run of ctest, has
// no core for a slot: the harness picks the core of the producer, and the
// threads of the Vigil keep the affinity of the process.
struct RowCores {
    bench::CpuId record{};
    crucible::warden::CpuSet vigil{};
};

// The background threads of a Vigil: the pipeline thread and its four stages.
constexpr std::size_t kVigilThreads = 5;

[[nodiscard]] RowCores row_cores() noexcept {
    const bench::Cores cores = bench::Cores::of_calling_thread();
    return RowCores{.record = cores.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none()),
                    .vigil = cores.from_slot(bench::ThreadSlot{1})};
}

// A Vigil whose background threads run on the cores of the slots 1 to 5.
[[nodiscard]] Vigil make_vigil(RowCores const& cores) {
    return bench::run_on_cores(cores.vigil, [] { return Vigil{}; });
}

// The core of the producer: the core of slot 0, or the core of
// CRUCIBLE_BENCH_CORE, or -1 for the pick of the harness.
[[nodiscard]] int record_core_of(RowCores const& cores) noexcept {
    return cores.record.is_valid() ? cores.record.raw() : bench::env_core();
}

// The iteration and the position of an op: the two parts of its synthetic
// data address.
enum class IterationNumber : std::uint32_t {
};
enum class OpPosition : std::uint32_t {
};

// The synthetic data address of the op at a position of an iteration.  No
// tensor lives there, and the bench reads only the bits of the address.
[[nodiscard]] SyntheticAddress fake_address(IterationNumber iter, OpPosition op) noexcept {
    const std::uint32_t low =
        (static_cast<std::uint32_t>(iter) + 1) * 0x100000 + (static_cast<std::uint32_t>(op) + 1) * 0x1000;
    return SyntheticAddress{low};
}

TensorMeta make_meta(SyntheticAddress address) noexcept {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = tensor_dim(1024);
    m.strides[0] = tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = mint_synthetic_data_ptr(address);
    return m;
}

struct OpData {
    TraceRing::Entry entry{};
    ::fixy::FixedArray<TensorMeta, 2> metas{};
    uint16_t n_metas = 0;
};

// The records of the op: the first n_metas records of its array.
[[nodiscard]] ::fixy::View<const TensorMeta> records_of(const OpData& op CRUCIBLE_LIFETIMEBOUND) noexcept {
    return ::fixy::View<const TensorMeta>{op.metas.view()}
        .window(0, op.n_metas)
        .expect("the count fits the records of the op");
}
::fixy::View<const TensorMeta>
records_of(const OpData&&) = delete("the result points into the op, and a temporary op ends before the result");

OpData make_op(uint32_t iter, uint32_t op_idx) noexcept {
    OpData d;
    const OpSlot slot = OpSlot::check(op_idx).expect("the position of an op is below NUM_OPS");
    d.entry.schema_hash = SCHEMA.at(slot);
    d.entry.shape_hash = SHAPE.at(slot);
    d.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    d.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0)
        d.metas.with_element(::fixy::Index<2>::check(idx++).expect("the position fits the array"),
                             [iter, op_idx](TensorMeta& meta) noexcept {
                                 meta = make_meta(fake_address(IterationNumber{iter}, OpPosition{op_idx - 1}));
                             });
    d.metas.with_element(::fixy::Index<2>::check(idx++).expect("the position fits the array"),
                         [iter, op_idx](TensorMeta& meta) noexcept {
                             meta = make_meta(fake_address(IterationNumber{iter}, OpPosition{op_idx}));
                         });
    d.n_metas = static_cast<uint16_t>(d.entry.num_inputs + d.entry.num_outputs);
    return d;
}

void feed_record(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(certify_synthetic_op(d.entry, records_of(d)));
    }
}

void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(certify_synthetic_op(d.entry, records_of(d)));
    }
}

void wait_region_published(Vigil& vigil) {
    uint64_t spins = 0;
    while (!vigil.has_pending_region()) {
        if (++spins > 100000000) ::fixy::fatal("bench_record_leaf: Vigil never published a region");
        CRUCIBLE_SPIN_PAUSE;
    }
}

void align_and_activate(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(iter, i);
        [[maybe_unused]] auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, records_of(d)));
    }
    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        [[maybe_unused]] auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, records_of(d)));
    }
}

void setup_compiled_vigil(Vigil& vigil) {
    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);
    const ::fixy::TestRunnerCtx waiter{::foundation::effects::testing::test()};
    vigil.flush(waiter).expect("the pipeline of the bench runs");
    wait_region_published(vigil);
    align_and_activate(vigil, 3);
}

// Records one op and waits until the background processed it, so the stage
// threads of the pipeline run on their cores before a row measures.  A stage
// thread starts on the core of the pipeline thread and moves to its own core
// when it runs, so a start inside the sampled loop waits behind the other
// threads of that core.  The pause check saw such a start: a stage waited
// 1.6 ms in 5 arrivals.  The schema hash of the op is not one of the hashes of
// the rows.
void start_pipeline(Vigil& vigil) {
    OpData d = make_op(0, 1);
    d.entry.schema_hash = SchemaHash{0x900};
    static_cast<void>(vigil.record_op(certify_synthetic_op(d.entry, records_of(d))));
    const ::fixy::TestRunnerCtx waiter{::foundation::effects::testing::test()};
    vigil.flush(waiter).expect("the pipeline of the bench runs");
}

// ── The gate ───────────────────────────────────────────────────────
//
// A ceiling per case, in nanoseconds, read from a committed file, plus a
// flag saying whether that case is allowed to fail the build. The numbers
// were measured on one machine, so a case only gates where the run still
// looks like the one that produced them.

struct Ceiling {
    bench::RunName name{};
    double p50_ns = 0.0;
    double p99_ns = 0.0;
    bool gate = false;  // false = report the case, never fail on it
};

// The rows of a baseline file, at most, and its bytes, at most.
constexpr std::size_t kCeilings = 16;
constexpr std::size_t kBaselineBytes = 8192;

// Whether the baseline file opened, so that a missing file and a file with
// no row differ.
enum class BaselineFile : std::uint8_t {
    Missing,
    Present,
};

// The rows of a baseline file, in the order of the file.
struct Baseline {
    BaselineFile file = BaselineFile::Missing;
    ::fixy::FixedList<Ceiling, kCeilings> rows{};
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

// Within-run cv is not the criterion, though it is the obvious one to reach
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

// The text after the first count characters of a text.
[[nodiscard]] ::fixy::TextView text_after(::fixy::TextView text, std::size_t count) noexcept {
    return crucible::warden::detail::text_suffix(text, count);
}

// The value of a decimal number of a baseline: digits, a point and digits,
// after the blanks that lead it, up to the first other character.  The
// number holds at most 15 digits and at most 15 after the point, so the
// integer of its digits and the power of ten are exact doubles, and their
// quotient is the nearest double to the decimal value, as strtod gives it.
// Each other form ends the process, because the bench reads its own
// committed file.  Complexity: linear in the digits.
[[nodiscard]] double decimal_value(::fixy::TextView text) noexcept {
    constexpr std::size_t kMostDigits = 15;
    std::uint64_t digits = 0;
    std::size_t count = 0;
    std::size_t after_point = 0;
    bool is_after_point = false;
    for (char const character : crucible::warden::detail::trim_blanks(text)) {
        if (character == '.' && !is_after_point) {
            is_after_point = true;
            continue;
        }
        if (character < '0' || character > '9') break;
        digits = digits * 10 + static_cast<std::uint64_t>(character - '0');
        ++count;
        after_point += is_after_point ? std::size_t{1} : std::size_t{0};
    }
    if (count == 0 || count > kMostDigits) {
        ::fixy::fatal("a number of the baseline is no decimal of 1 to {} digits: '{}'", kMostDigits, text);
    }
    double scale = 1.0;
    for (std::size_t place = 0; place < after_point; ++place)
        scale *= 10.0;
    return static_cast<double>(digits) / scale;
}

// A deliberately small reader: the file is a flat list of
// {"name":..,"p50_ns":..,"p99_ns":..} objects and nothing else, so a full
// JSON parser would be a dependency bought for no reason.  The file of the
// baseline tells if the file opens, so that a missing file and a file with
// no row differ.  A key is searched from the end of the name of its row
// onward.  Complexity: linear in the bytes of the file for each row.
[[nodiscard]] Baseline read_baseline(::fixy::CText path) noexcept {
    Baseline baseline{};
    ::fixy::FixedText<kBaselineBytes> file{};
    if (::foundation::os::read_file(path, file).is_err()) return baseline;
    baseline.file = BaselineFile::Present;
    if (file.size() == kBaselineBytes) {
        ::fixy::fatal("the baseline {} holds {} bytes or more, and the reader holds fewer", path.view(),
                      kBaselineBytes);
    }
    const ::fixy::TextView text = file.view();
    // The value of a key starts after "key":.  None when the key is absent.
    const auto value_after = [&text](::fixy::TextView needle,
                                     std::size_t from) noexcept -> ::fixy::Option<::fixy::TextView> {
        const ::fixy::TextView rest = text_after(text, from);
        ::fixy::Option<std::size_t> found = ::fixy::find(rest, needle);
        if (found.is_none()) return ::fixy::none;
        return ::fixy::some(text_after(rest, std::move(found).value_or(0) + needle.size()));
    };
    constexpr ::fixy::TextView kName{"\"name\":"};
    std::size_t pos = 0;
    for (;;) {
        ::fixy::Option<std::size_t> name_at = ::fixy::find(text_after(text, pos), kName);
        if (name_at.is_none()) break;
        const std::size_t key = pos + std::move(name_at).value_or(0);
        ::fixy::Option<std::size_t> open = ::fixy::find(text_after(text, key + kName.size()), '"');
        if (open.is_none()) break;
        const std::size_t q1 = key + kName.size() + std::move(open).value_or(0);
        ::fixy::Option<std::size_t> close = ::fixy::find(text_after(text, q1 + 1), '"');
        if (close.is_none()) break;
        const std::size_t q2 = q1 + 1 + std::move(close).value_or(0);
        ::fixy::Option<::fixy::TextView> p50 = value_after("\"p50_ns\":", q2);
        ::fixy::Option<::fixy::TextView> p99 = value_after("\"p99_ns\":", q2);
        if (p50.is_none() || p99.is_none()) break;
        Ceiling row{};
        row.name = bench::name_of(crucible::warden::detail::text_prefix(text_after(text, q1 + 1), q2 - q1 - 1));
        row.p50_ns = decimal_value(std::move(p50).value_or(::fixy::TextView{}));
        row.p99_ns = decimal_value(std::move(p99).value_or(::fixy::TextView{}));
        // The gate field is the integer 0 or 1.
        row.gate = value_after("\"gate\":", q2)
                       .match(
                           [](::fixy::TextView value) noexcept {
                               return bench::detail::leading_number(value).value_or(0) != 0;
                           },
                           []() noexcept { return false; });
        if (baseline.rows.append(row).is_err()) {
            ::fixy::fatal("the baseline {} holds more than {} rows", path.view(), kCeilings);
        }
        pos = q2 + 1;
    }
    return baseline;
}

}  // namespace

int main() {
    // Before anything allocates and before the first Vigil starts its drain
    // thread.  With no isolated core, each case falls back to the pin of the
    // harness, and the numbers are noisier.
    const RowCores cores = row_cores();
    bench::print_system_info();
    if (cores.record.is_valid()) {
        bench::pin_calling_thread_to(cores.record).expect("the pin of the producer to the core of slot 0");
    }
    if (!cores.record.is_valid()) {
        bench::print(
            "  NOTE: no isolated core in the affinity: the harness picks the core, and the numbers are noisy.\n");
    } else if (cores.vigil.size() < kVigilThreads) {
        bench::print("  NOTE: {} isolated cores for the {} background threads of each Vigil, so some of them share "
                     "a core.  Run the bench through loop bench, which claims its six cores.\n",
                     cores.vigil.size(), kVigilThreads);
    } else {
        bench::print("  producer on cpu{}, the background threads of each Vigil on {} cores\n", cores.record.raw(),
                     cores.vigil.size());
    }
    bench::elevate_priority();

    // The four recording cases below.
    ::fixy::FixedList<bench::Report, 4> reports{};

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
    //
    // The two rings live on the heap, as they do in Vigil.  The timed body
    // reads each ring through a Ref, so no test of a Box is in the timed loop.
    bench::add_report(reports, [&] {
        const ::foundation::effects::Alloc alloc = ::foundation::effects::testing::test().alloc;
        // Each door of the ring and of the log of the bench opens the side
        // of the door for one call (test/trace_rig.h), as record_op opens
        // the two producer sides of a Vigil.
        ::fixy::Box<crucible::test::OpenRing> ring_box = ::fixy::mint_box<crucible::test::OpenRing>(alloc);
        ::fixy::Box<crucible::test::OpenLog> log_box = ::fixy::mint_box<crucible::test::OpenLog>(alloc);
        auto const ring = ::fixy::to(*ring_box);
        auto const log = ::fixy::to(*log_box);

        auto d = make_op(0, 1);
        const ::fixy::View<const TensorMeta> records = records_of(d);
        uint64_t tick = 0;
        return bench::Run("record leaf [2 appends, no drainer]").core(record_core_of(cores)).measure([&] {
            d.entry.schema_hash = SchemaHash{0x1000000u + tick++};
            MetaIndex meta_start = log->try_append(records);
            if (!meta_start.is_valid()) [[unlikely]] {
                log->reset();
                meta_start = log->try_append(records);
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
    bench::add_report(reports, [&] {
        Vigil vigil = make_vigil(cores);
        start_pipeline(vigil);
        auto d = make_op(0, 1);
        const ::fixy::View<const TensorMeta> records = records_of(d);
        uint64_t refused = 0;
        uint64_t tick = 0;
        auto report = bench::Run("record_op [2 metas, ring has room]")
                          .warmup(RECORD_WARMUP)
                          .samples(RECORD_SAMPLES)
                          .batch(RECORD_BATCH)
                          .core(record_core_of(cores))
                          .measure([&] {
                              // Same non-repeating stream as the dispatch_op case below, so the
                              // difference between the two reports is that case's mode test and
                              // pending-region check and nothing else.
                              d.entry.schema_hash = SchemaHash{0x1000000u + tick++};
                              const bool took = vigil.record_op(certify_synthetic_op(d.entry, records));
                              refused += static_cast<uint64_t>(!took);
                              bench::do_not_optimize(took);
                          });
        if (refused != 0) {
            ::fixy::fatal("bench_record_leaf: {} of {} appends were refused. The ring filled, so "
                          "this case measured the reject path. Lower RECORD_SAMPLES.",
                          refused, RECORD_BODIES);
        }
        bench::print_thread_cpus("record_op [2 metas, ring has room]");
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
    bench::add_report(reports, [&] {
        Vigil vigil = make_vigil(cores);
        start_pipeline(vigil);
        auto d = make_op(0, 1);
        const ::fixy::View<const TensorMeta> records = records_of(d);
        uint64_t tick = 0;
        uint64_t recorded = 0;
        auto report = bench::Run("dispatch_op [RECORD leaf, ring has room]")
                          .warmup(RECORD_WARMUP)
                          .samples(RECORD_SAMPLES)
                          .batch(RECORD_BATCH)
                          .core(record_core_of(cores))
                          .measure([&] {
                              d.entry.schema_hash = SchemaHash{0x1000000u + tick++};
                              const auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, records));
                              recorded += static_cast<uint64_t>(r.is_record());
                              bench::do_not_optimize(r);
                          });
        // The count is the instrument, not the ring's head: the background
        // thread resets the ring, so head is a level rather than a total and
        // reads far below the number of ops that went through it.
        const uint64_t bodies = RECORD_BODIES;
        if (recorded < bodies) {
            ::fixy::fatal("bench_record_leaf: only {} of {} calls took the RECORD path ({}%). The "
                          "Vigil switched to COMPILED mid-run, so this case stopped measuring the "
                          "recording leaf.",
                          recorded, bodies,
                          ::fixy::Fixed<1>{100.0 * static_cast<double>(recorded) / static_cast<double>(bodies)});
        }
        bench::print_thread_cpus("dispatch_op [RECORD leaf, ring has room]");
        return report;
    }());

    // ── The shadow-dispatch leaf ────────────────────────────────────
    //
    // COMPILED mode returns a shadow handle without touching the ring, so
    // this one carries no capacity constraint and runs at the harness's
    // own defaults.
    bench::add_report(reports, [&] {
        Vigil vigil = make_vigil(cores);
        setup_compiled_vigil(vigil);
        ::fixy::FixedArray<OpData, NUM_OPS> ops{};
        for (OpSlot const slot : ::fixy::IndexRange<NUM_OPS>{})
            ops.with_element(slot,
                             [slot](OpData& op) noexcept { op = make_op(10, static_cast<uint32_t>(slot.raw())); });

        OpSlot slot = ::fixy::index<0>();
        bench::Report report =
            bench::Run("dispatch_op [COMPILED leaf, cyclic]").core(record_core_of(cores)).measure([&] {
                const OpData& d = ops.at(slot);
                bench::do_not_optimize(vigil.dispatch_op(certify_synthetic_op(d.entry, records_of(d))));
                slot = next_in_cycle(slot);
            });
        bench::print_thread_cpus("dispatch_op [COMPILED leaf, cyclic]");
        return report;
    }());

    bench::emit_reports_text(reports.view());
    // The JSON tail comes last on each path, because the reader reads each
    // line after its marker.
    const bench::JsonTail json = bench::env_json();

    // ── Gate ────────────────────────────────────────────────────────

    // The baseline of the hardware tier of this host (bench/CMakeLists.txt).  A tier with no baseline file measures,
    // prints its numbers above and passes.  A baseline file that gives no row fails, because it gates nothing.
    using ::fixy::Align;
    using ::fixy::Fixed;
    using ::fixy::Padded;
    using ::fixy::Sink;
    using ::fixy::TextView;
    const Baseline baseline = read_baseline(CRUCIBLE_RECORD_LEAF_BASELINE);
    const bool has_baseline_file = baseline.file == BaselineFile::Present;
    if (baseline.rows.size() == 0) {
        ::fixy::report(Sink::Err,
                       "\nbench_record_leaf: {} '{}', the baseline of the tier '{}' of this host.  The gate "
                       "judges nothing, and the run {}.\n",
                       has_baseline_file ? TextView{"no row in"} : TextView{"no file at"},
                       TextView{CRUCIBLE_RECORD_LEAF_BASELINE}, TextView{CRUCIBLE_RECORD_LEAF_TIER},
                       has_baseline_file ? TextView{"fails"} : TextView{"passes"});
        bench::emit_reports_json(reports.view(), json);
        return has_baseline_file ? 1 : 0;
    }

    bench::print("\n=== gate (ceiling = baseline x {}) ===\n", Fixed<2>{kTolerance});
    int failures = 0;
    int skipped = 0;
    for (const auto& report : reports) {
        ::fixy::Option<Ceiling> want{};
        for (const Ceiling& c : baseline.rows)
            if (c.name == report.name) want = ::fixy::some(c);
        if (want.is_none()) {
            bench::print("  {} NO-BASELINE\n", Padded<44, Align::Left>{report.name.view()});
            failures++;
            continue;
        }
        const Ceiling ceiling = std::move(want).value_or(Ceiling{});
        if (!ceiling.gate) {
            bench::print("  {} ADVISORY  p50 {} ns   p99 {} ns   (recorded {} / {})\n",
                         Padded<44, Align::Left>{report.name.view()}, Fixed<2, 8>{report.pct.p50},
                         Fixed<2, 8>{report.pct.p99}, Fixed<2, 6>{ceiling.p50_ns}, Fixed<2, 6>{ceiling.p99_ns});
            continue;
        }
        if (report.drift_flag) {
            bench::print("  {} SKIP      the run drifted {}% between its first and second half; "
                         "the machine changed under it\n",
                         Padded<44, Align::Left>{report.name.view()}, Fixed<1>{report.drift_pct});
            skipped++;
            continue;
        }
        const double p50_max = ceiling.p50_ns * kTolerance;
        const double p99_max = ceiling.p99_ns * kTolerance;
        const bool ok = report.pct.p50 <= p50_max && (!kGateOnP99 || report.pct.p99 <= p99_max);
        bench::print("  {} {}  p50 {} / {} ns   p99 {} (recorded {}, not gated)\n",
                     Padded<44, Align::Left>{report.name.view()}, ok ? TextView{"PASS"} : TextView{"FAIL"},
                     Fixed<2, 6>{report.pct.p50}, Fixed<2, 6>{p50_max}, Fixed<2, 6>{report.pct.p99},
                     Fixed<2, 6>{ceiling.p99_ns});
        if (!ok) failures++;
    }

    bench::print("=== verdict: {} ({} failed, {} skipped for drift) ===\n",
                 failures == 0 ? TextView{"PASS"} : TextView{"FAIL"}, failures, skipped);
    bench::emit_reports_json(reports.view(), json);
    return failures == 0 ? 0 : 1;
}
