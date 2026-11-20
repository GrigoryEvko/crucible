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
//   • no_pin()        — multi-thread can't pin to one core
//   • max_wall_ms(60_000) — generous wall budget; cap protects CI
//   • per_producer = 5'000 — keeps each iteration ≈ 1-50 ms

#include <cstdint>
#include <thread>
#include <vector>

#include <fixy/Core.h>
#include <fixy/concurrent/MpscRing.h>

#include "bench_harness.h"

namespace {

using ::fixy::concurrent::MpscRing;
using Item = std::uint64_t;

constexpr std::size_t kCap = 1U << 14;  // 16K cells; headroom
constexpr std::size_t kPerProducer = 5000;  // items per producer / iter
constexpr std::size_t kSamples = 32;  // ≥30 for Mann-Whitney
constexpr std::size_t kWarmup = 3;
constexpr std::size_t kMaxWallMs = 60000;

using Ring = MpscRing<Item, kCap>;

// The allocation capability of the ring of each cycle.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The gate of a cycle.  The cycle opens it when each thread exists, so the
// producers and the consumer start their work together.
struct StartGate {
    ::fixy::Atomic<bool> is_open{false};

    void wait() const noexcept {
        while (!is_open.load_acquire()) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    void open() noexcept { is_open.store_release(true); }
};

// Producer-side single-call write loop.  Each producer pushes K items
// via individual try_push calls.  Spin if full.
struct SinglePushWorker {
    Ring& ring;
    StartGate& gate;
    Item base = 0;

    void operator()() noexcept {
        gate.wait();
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
        gate.wait();
        ::fixy::FixedArray<Item, BATCH> buf{};
        std::size_t pushed = 0;
        while (pushed < kPerProducer) {
            const std::size_t remaining = kPerProducer - pushed;
            const std::size_t n = (remaining < BATCH) ? remaining : BATCH;
            for (std::size_t i = 0; i < n; ++i) {
                *buf.get(i).expect("a position of a batch is below BATCH") = base + (pushed + i);
            }
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
        gate.wait();
        ::fixy::FixedArray<Item, 64> buf{};
        std::size_t local = 0;
        // A pop of an MPSC ring is a merge, so it takes the tag of Nondet.
        const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
        while (local < total) {
            const std::size_t n = ring.try_pop_batch(merge, buf.view());
            if (n == 0) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            // Anti-DCE: keep the read alive.
            bench::do_not_optimize(buf.at(::fixy::index<0>()));
            bench::do_not_optimize(*buf.get(n - 1).expect("a pop gives at most the count of the buffer"));
            local += n;
        }
    }
};

}  // namespace

// The rows time the bare ring, which holds no permission of its two sides.
// Each cycle times its thread starts.  On the one core of this bench, a
// cycle through the raw probe door of the base costs much more than a cycle
// of std::jthread.  So the thread starts stay on std::jthread, and the
// workers are outside the region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: each cycle of the bare MPSC ring starts its threads on std::jthread")
namespace {

// Starts the producers and the consumer of one cycle, opens the gate, and
// joins each thread.
template <typename Worker, std::size_t Producers>
void start_and_join(Ring& ring, StartGate& gate) {
    std::vector<std::jthread> producers(Producers);
    for (std::size_t p = 0; p < Producers; ++p) {
        producers[p] = std::jthread(Worker{ring, gate, static_cast<Item>(p) * kPerProducer});
    }
    std::jthread consumer(DrainWorker{ring, gate, Producers * kPerProducer});
    gate.open();
    producers.clear();
    consumer.join();
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// One full multi-thread cycle: start Producers producers and one consumer,
// and join them.
template <typename Worker, std::size_t Producers>
void run_cycle() {
    ::fixy::Box<Ring> ring_box = ::fixy::mint_box<Ring>(kAlloc);
    StartGate gate{};
    start_and_join<Worker, Producers>(*ring_box, gate);
}

template <typename Worker, std::size_t Producers>
[[nodiscard]] bench::Report run_one(::fixy::TextView label) {
    return bench::Run{bench::format_name("{} P={}", label, Producers)}
        .samples(kSamples)
        .warmup(kWarmup)
        .batch(1)
        .no_pin()
        .max_wall_ms(kMaxWallMs)
        .measure([] { run_cycle<Worker, Producers>(); });
}

// The three rows of one producer count.
template <std::size_t Producers>
void add_rows(::fixy::FixedList<bench::Report, 12>& reports) {
    bench::add_report(reports, run_one<SinglePushWorker, Producers>("single"));
    bench::add_report(reports, run_one<BatchedPushWorker<16>, Producers>("batched<16>"));
    bench::add_report(reports, run_one<BatchedPushWorker<64>, Producers>("batched<64>"));
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== mpsc_contention ===\n");
    bench::print("  Item:              uint64_t\n");
    bench::print("  Cap:               {} cells\n", kCap);
    bench::print("  Items per producer: {} / iteration\n", kPerProducer);
    bench::print("  Samples:           {} (warmup {})\n", kSamples, kWarmup);
    bench::print("  Body:              spawn P producers + drain consumer + join\n");
    bench::print("\n");

    // 4 producer counts × 3 API kinds = 12 cells.
    ::fixy::FixedList<bench::Report, 12> reports{};
    add_rows<1>(reports);
    add_rows<2>(reports);
    add_rows<4>(reports);
    add_rows<8>(reports);

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
    bench::print("  • At P=1 (no contention), batched API gives the single-thread\n");
    bench::print("    speedup (~4.5×).\n");
    bench::print("  • At P=2..8 (contention rises), the gap widens: batched API\n");
    bench::print("    does 1 CAS per N items, so head_ contention is reduced N×\n");
    bench::print("    vs single-call which CASes per item.\n");
    bench::print("  • CV >5% on any cell indicates throttling — re-run on a\n");
    bench::print("    quiet machine and check `dmesg | grep thermal`.\n");

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
