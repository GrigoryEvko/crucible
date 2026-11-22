// MpscRing multi-producer contention bench.
//
// Validates the claim of the batch push of the ring:
//
//   "Real win for THIS API: multi-producer contention.
//    Single FAA(tail, N) replaces N × FAA(tail, 1)."
//
// A single-thread measurement showed a ~4.5× speedup from the batched
// API.  This bench shows the multi-thread story:
//   • Without batched API: every try_push contends on head_ via CAS.
//     Per-call cost grows with producer count.
//   • With batched API: each batch contends on head_ ONCE per N items
//     (single CAS claims N tickets).  Contention reduction = N×.
//
// Harness discipline (canonical bench/bench_harness.h):
//   • bench::Run{...}.samples(...).warmup(...).max_wall_ms(...).measure(body)
//     drives N iterations of a self-contained workload cycle.
//   • One iteration body = spawn P producers + drain consumer + join.
//   • Report.pct gives p50/p99/p99.9/cv of full-workload wall time.
//   • bench::emit_reports_text + bench::emit_reports_json publish.
//   • bench::compare(single, batched<64>) gives Mann-Whitney U for
//     statistical significance of the contention-reduction claim.
//
// The body is heavy (P thread starts + join), so we tune for low
// sample count and large per-sample work:
//   • samples(20)     — enough for p50/p99.9
//   • warmup(2)       — flush any first-call thread start overhead
//   • batch(1)        — never auto-batch a multi-thread cycle
//   • fan_out()       — the measuring thread takes the core of slot 0
//   • max_wall_ms(60_000) — generous wall budget; cap protects CI
//   • per_producer = 5'000 — keeps each iteration ≈ 1-50 ms
//
// THE CORES OF A CYCLE
//   The bench states 10 cores in bench/CMakeLists.txt: the measuring thread,
//   the eight producers of the largest row and the consumer.  The thread door
//   of fixy/os/ThreadTasks.h gives each thread of a cycle one core of the
//   claim apart from the core of the measuring thread, through the placement
//   OneCpuEach.  The line after each row names those cores.  A claim with
//   fewer cores than the threads of a row puts two threads on one core.  A
//   spin of one of them then holds the core until its time slice ends, and the
//   row measures the slices of the scheduler and not the contention.
//
// THE GATE
//   Each thread of a cycle arrives at the gate, and it waits until each
//   thread of the cycle has arrived.  So the producers and the consumer start
//   their work at the same time, also when the starts of the threads take
//   longer than the work.  The door holds the measuring thread from the first
//   start to the last join, so the threads open the gate themselves.  Each
//   thread has a core of its own, so the wait is a spin.

#include <cstdint>
#include <utility>

#include <fixy/Core.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/os/ThreadTasks.h>

#include "bench_harness.h"

namespace {

using ::fixy::concurrent::MpscRing;
using Item = std::uint64_t;

constexpr std::size_t kCap = 1U << 14;  // 16K cells; headroom
constexpr std::size_t kPerProducer = 5000;  // items per producer / iter
constexpr std::size_t kSamples = 32;  // ≥30 for Mann-Whitney
constexpr std::size_t kWarmup = 3;
constexpr std::size_t kMaxWallMs = 60000;

// The owner of the ring.  It makes the proof of the one consumer (THE WRITERS
// of fixy/concurrent/MpscRing.h), which the drain worker holds.
struct RingOwner final {
    [[nodiscard]] static constexpr ::fixy::SoleWriterProof<RingOwner, ::fixy::concurrent::MpscConsumerRole>
    consumer() noexcept {
        return {};
    }
};

using Ring = MpscRing<Item, kCap, RingOwner>;
// The count of the threads of a cycle: the producers and the consumer.  Each
// thread takes one core of the claim (THE CORES OF A CYCLE).
using ThreadCount = ::fixy::Quantity<::fixy::unit::Element, std::size_t>;

// The allocation capability of the ring of each cycle.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The gate of a cycle (THE GATE).  The gate holds the count of the threads
// that arrived, and the gate is open when the count is the count of the
// threads of the cycle.
struct alignas(64) StartGate {
    ::fixy::Atomic<std::uint32_t> arrived{0};
    std::uint32_t threads = 0;

    void arrive_and_wait() noexcept {
        static_cast<void>(arrived.fetch_add_acq_rel(1));
        while (arrived.load_acquire() < threads) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }
};

// Producer-side single-call write loop.  Each producer pushes K items
// via individual try_push calls.  Spin if full.
struct SinglePushWorker {
    Ring& ring;
    StartGate& gate;
    Item base = 0;

    void operator()() noexcept {
        gate.arrive_and_wait();
        for (std::size_t i = 0; i < kPerProducer; ++i) {
            const Item value = base + static_cast<Item>(i);
            while (!ring.try_push(value)) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    }
};

// Producer-side batched write loop.  Each producer pushes K items via
// try_push_batch<BATCH> calls — amortizes head_ CAS contention.
template <std::size_t BATCH>
struct BatchedPushWorker {
    Ring& ring;
    StartGate& gate;
    Item base = 0;

    void operator()() noexcept {
        gate.arrive_and_wait();
        ::fixy::FixedArray<Item, BATCH> buf{};
        std::size_t pushed = 0;
        while (pushed < kPerProducer) {
            const std::size_t remaining = kPerProducer - pushed;
            const std::size_t n = (remaining < BATCH) ? remaining : BATCH;
            buf.with_mut([this, pushed, n](auto run) noexcept {
                for (std::size_t i = 0; i < n; ++i) {
                    *run.get(i).expect("a position of a batch is below BATCH") = base + (pushed + i);
                }
            });
            std::size_t in_batch = 0;
            while (in_batch < n) {
                const std::size_t r = ring.try_push_batch(
                    buf.view().window(in_batch, n - in_batch).expect("the rest of a batch lies inside it"));
                if (r > 0) {
                    in_batch += r;
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            pushed += n;
        }
    }
};

// Consumer-side drain loop.  It pops until it has each item of the cycle.
struct DrainWorker {
    Ring& ring;
    StartGate& gate;
    std::size_t total = 0;

    void operator()() noexcept {
        gate.arrive_and_wait();
        ::fixy::FixedArray<Item, 64> buf{};
        std::size_t local = 0;
        // A pop of an MPSC ring is a merge, so it takes the tag of Nondet.
        const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
        while (local < total) {
            const std::size_t n = buf.with_mut([this, merge](auto run) noexcept {
                return ring.try_pop_batch(RingOwner::consumer(), merge, run.view());
            });
            if (n == 0) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            // The sink reads each byte of the buffer, so the copy of the pop
            // into it stays.
            bench::mark_changed(buf);
            local += n;
        }
    }
};

// The options of the threads of a row (THE CORES OF A CYCLE): one core of
// the claim for each thread, apart from the core of the measuring thread.
// With no claim, as under ctest, the threads keep the affinity of the process.
[[nodiscard]] ::fixy::thread::ThreadOptions cycle_options(crucible::warden::CpuSet const& cpus) noexcept {
    if (cpus.is_empty()) return ::fixy::thread::ThreadOptions{};
    return ::fixy::thread::ThreadOptions{.affinity = bench::detail::mask_of(cpus),
                                         .placement = ::fixy::thread::ThreadPlacement::OneCpuEach};
}

// One full multi-thread cycle: start Producers producers and one consumer
// through the thread door, and join them.  The rows time the bare ring,
// which holds no permission of its two sides, so the cycle starts its
// threads through the raw probe of the door.
template <typename Worker, std::size_t Producers, std::size_t... Producer>
void run_cycle(::fixy::thread::ThreadOptions const& options, std::index_sequence<Producer...> /*order*/) {
    ::fixy::Box<Ring> ring_box = ::fixy::mint_box<Ring>(kAlloc);
    StartGate gate{.threads = static_cast<std::uint32_t>(Producers + 1)};
    Ring& ring = *ring_box;
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    // Each thread runs only its own worker, which moves into the frame of the door, and the door joins each thread
    // before it returns.
    ::fixy::thread::run_joined_raw_probe(ctx, ::fixy::TestRunnerCtx{::foundation::effects::testing::test()},
                                         "the bare MPSC ring is the state under test", options,
                                         Worker{ring, gate, Producer * kPerProducer}...,
                                         DrainWorker{ring, gate, Producers * kPerProducer})
        .expect("the thread door starts the producers and the consumer of a cycle");
}

// Prints the cores of the threads of a row, or that the threads keep the
// affinity of the process.
void print_cycle_cores(crucible::warden::CpuSet const& cpus, ThreadCount threads) noexcept {
    if (cpus.is_empty()) {
        bench::print("     └─ {} threads of each cycle with the affinity of the process\n", threads.raw());
        return;
    }
    bench::print("     └─ {} threads of each cycle on the cores", threads.raw());
    for (int const cpu : cpus) {
        bench::print(" cpu{}", cpu);
    }
    bench::print("\n");
}

template <typename Worker, std::size_t Producers>
[[nodiscard]] bench::Report run_one(bench::Cores const& cores, ::fixy::TextView label) {
    const bench::CpuId measuring = cores.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none());
    const ThreadCount threads{Producers + 1};
    const crucible::warden::CpuSet cpus = cores.first_slots_apart(measuring, threads);
    const ::fixy::thread::ThreadOptions options = cycle_options(cpus);
    bench::Report report =
        bench::Run{bench::format_name("{} P={}", label, Producers)}
            .samples(kSamples)
            .warmup(kWarmup)
            .batch(1)
            .fan_out()
            .max_wall_ms(kMaxWallMs)
            .measure([&options] { run_cycle<Worker, Producers>(options, std::make_index_sequence<Producers>{}); });
    print_cycle_cores(cpus, threads);
    return report;
}

// The three rows of one producer count.
template <std::size_t Producers>
void add_rows(bench::Cores const& cores, ::fixy::FixedList<bench::Report, 12>& reports) {
    bench::add_report(reports, run_one<SinglePushWorker, Producers>(cores, "single"));
    bench::add_report(reports, run_one<BatchedPushWorker<16>, Producers>(cores, "batched<16>"));
    bench::add_report(reports, run_one<BatchedPushWorker<64>, Producers>(cores, "batched<64>"));
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();
    // The cores of the claim, read before the first Run pins the thread.
    const bench::Cores cores = bench::Cores::of_calling_thread();

    bench::print("=== mpsc_contention ===\n");
    bench::print("  Item:              uint64_t\n");
    bench::print("  Cap:               {} cells\n", kCap);
    bench::print("  Items per producer: {} / iteration\n", kPerProducer);
    bench::print("  Samples:           {} (warmup {})\n", kSamples, kWarmup);
    bench::print("  Body:              spawn P producers + drain consumer + join\n");
    bench::print("\n");

    // 4 producer counts × 3 API kinds = 12 cells.
    ::fixy::FixedList<bench::Report, 12> reports{};
    add_rows<1>(cores, reports);
    add_rows<2>(cores, reports);
    add_rows<4>(cores, reports);
    add_rows<8>(cores, reports);

    // Standard text + JSON emission.  Each Report's pct.p50 is the
    // wall-time of one workload cycle (P producers × kPerProducer items).
    bench::emit_reports_text(reports.view());

    // ── Headline analysis: per-producer-count items/sec ─────────────
    //
    // Items moved per cycle = P × kPerProducer; cycle wall = pct.p50.
    // Aggregate items/sec = items / wall.
    bench::print("\n=== contention reduction (single → batched<64>) ===\n");
    bench::print("  P    single (Mitems/s)    batched<64> (Mitems/s)   speedup\n");
    bench::print("  --   ------------------  ------------------------  -------\n");
    for (std::size_t pi = 0; pi < 4; ++pi) {
        const std::size_t P = std::size_t{1} << pi;  // 1, 2, 4, 8
        const bench::Report& s = *reports.get(pi * 3 + 0).expect("the single row of a producer count");
        const bench::Report& b64 = *reports.get(pi * 3 + 2).expect("the batched<64> row of a producer count");
        const double items = static_cast<double>(P * kPerProducer);
        const double s_rate = items / (s.pct.p50 * 1e-9);
        const double b_rate = items / (b64.pct.p50 * 1e-9);
        const double ratio = b_rate / s_rate;
        bench::print("  {}      {}      {}       {}×\n", ::fixy::Padded<2>{P}, ::fixy::Fixed<2, 14>{s_rate / 1e6},
                     ::fixy::Fixed<2, 18>{b_rate / 1e6}, ::fixy::Fixed<2, 5>{ratio});
    }

    // ── Statistical significance: Mann-Whitney U sweep across P ─────
    //
    // bench::compare gives Mann-Whitney U + tie-corrected z; bench
    // marks distinguishable iff |z| > 2.576 (p < 0.01).  We expect
    // batched<64> to IMPROVE over single at every contention level.
    bench::print("\n=== significance (single vs batched<64>) ===\n");
    for (std::size_t pi = 0; pi < 4; ++pi) {
        const bench::Report& s = *reports.get(pi * 3 + 0).expect("the single row of a producer count");
        const bench::Report& b64 = *reports.get(pi * 3 + 2).expect("the batched<64> row of a producer count");
        bench::compare(s, b64).print_text();
    }

    bench::print("\n  Interpretation:\n");
    bench::print("  • Each time is one full cycle: the starts of the threads, the\n");
    bench::print("    work and the joins.\n");
    bench::print("  • At P=1, the single row moves each item through the lines of\n");
    bench::print("    the ring one at a time, while the consumer reads the same lines.\n");
    bench::print("  • At P=2..8 (contention rises), the gap widens: batched API\n");
    bench::print("    does 1 CAS per N items, so head_ contention is reduced N×\n");
    bench::print("    vs single-call which CASes per item.\n");
    bench::print("  • A spread above 5% on a row means: run the bench again.\n");

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
