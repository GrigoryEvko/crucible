// ═══════════════════════════════════════════════════════════════════
// bench_mpmc_scq_vs_vyukov — head-to-head SCQ vs Vyukov MPMC bench
//
// Reproduces the central claim from Nikolaev DISC 2019: SCQ achieves
// ~2× throughput over Vyukov 2011 at high producer/consumer contention,
// with the gap shrinking to ~30% in the uncontended single-producer
// single-consumer case.
//
// Methodology:
//   * Sweep (P producers × C consumers) over {1, 2, 4, 8, 16}².
//   * Each thread runs ITERS pushes (producer) or pops (consumer).
//   * Wall-clock the whole run via steady_clock.
//   * Report items/sec (push throughput from producer side), and the
//     SCQ/Vyukov ratio per cell.
//
// Why throughput instead of latency:
//   * The latency narrative ("CAS retries cost X ns each") is what
//     SCQ's argument SOURCE is, but the load-bearing measure for an
//     MPMC queue is end-to-end items/sec.
//   * Latency under contention is meaningless without queue depth —
//     a queue can be "fast per-call" while losing total throughput
//     because of cache-line ping-pong.
//   * Items/sec captures both per-call latency AND scaling efficiency
//     in a single number.
//
// The harness pins each thread to its own core via sched_setaffinity,
// up to N_HARDWARE_CONCURRENCY.  Beyond that the OS scheduler decides.
// Variance > 5% means thermal/scheduling noise dominated; re-run on
// a quiet machine.
//
// References:
//   D. Vyukov, "Bounded MPMC queue", 1024cores.net, 2011.
//   R. Nikolaev, "A Scalable, Portable, and Memory-Efficient Lock-
//     Free FIFO Queue", DISC 2019, https://arxiv.org/abs/1908.04511
//   THREADING.md §5.5.1 — Crucible's design rationale for SCQ.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/MpmcRing.h>
#include <crucible/concurrent/VyukovMpmcRing.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#ifdef __linux__
  #include <sched.h>
  #include <pthread.h>
#endif

namespace {

using crucible::concurrent::MpmcRing;
using crucible::concurrent::VyukovMpmcRing;

constexpr std::size_t CAPACITY      = 1024;
constexpr std::uint64_t PER_THREAD  = 1'000'000ULL;
constexpr int          WARMUP_REPS  = 1;
constexpr int          MEASURE_REPS = 5;

void pin_to_core([[maybe_unused]] int core_id) {
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set);
#endif
}

// ── Run a single P×C sweep; return items/sec ─────────────────────

template <typename Ring>
double run_sweep(int n_producers, int n_consumers,
                 std::uint64_t iters_per_producer)
{
    Ring ring;
    std::atomic<std::uint64_t> total_popped{0};
    std::atomic<bool> producers_done{false};
    std::vector<std::thread> producers, consumers;

    const std::uint64_t expected =
        static_cast<std::uint64_t>(n_producers) * iters_per_producer;

    auto produce_loop = [&ring, iters_per_producer](int pin_core) {
        pin_to_core(pin_core);
        for (std::uint64_t i = 0; i < iters_per_producer; ++i) {
            while (!ring.try_push(i)) {
                std::this_thread::yield();
            }
        }
    };

    auto consume_loop = [&ring, &total_popped, &producers_done, expected](
                            int pin_core) {
        pin_to_core(pin_core);
        for (;;) {
            if (auto v = ring.try_pop()) {
                (void)v;
                total_popped.fetch_add(1, std::memory_order_relaxed);
            } else {
                if (producers_done.load(std::memory_order_acquire)
                    && total_popped.load() >= expected) {
                    return;
                }
                std::this_thread::yield();
            }
        }
    };

    const auto t0 = std::chrono::steady_clock::now();

    int core = 0;
    for (int p = 0; p < n_producers; ++p) {
        producers.emplace_back(produce_loop, core++);
    }
    for (int c = 0; c < n_consumers; ++c) {
        consumers.emplace_back(consume_loop, core++);
    }

    for (auto& t : producers) t.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& t : consumers) t.join();

    const auto t1 = std::chrono::steady_clock::now();

    if (total_popped.load() != expected) {
        std::fprintf(stderr, "ERROR: lost items (popped=%llu expected=%llu)\n",
                     static_cast<unsigned long long>(total_popped.load()),
                     static_cast<unsigned long long>(expected));
        std::abort();
    }

    const double sec =
        std::chrono::duration<double>(t1 - t0).count();
    return static_cast<double>(expected) / sec;
}

// Median of MEASURE_REPS for noise rejection.
template <typename Ring>
double measure_median(int n_producers, int n_consumers) {
    // Warmup — JIT cache lines, get clocks settled.
    for (int i = 0; i < WARMUP_REPS; ++i) {
        (void)run_sweep<Ring>(n_producers, n_consumers, PER_THREAD / 4);
    }
    std::vector<double> samples;
    samples.reserve(MEASURE_REPS);
    for (int i = 0; i < MEASURE_REPS; ++i) {
        samples.push_back(
            run_sweep<Ring>(n_producers, n_consumers, PER_THREAD));
    }
    std::sort(samples.begin(), samples.end());
    return samples[MEASURE_REPS / 2];
}

// ── Sweep grid ────────────────────────────────────────────────────

void run_grid() {
    constexpr int counts[] = {1, 2, 4, 8};
    const int n_cores = static_cast<int>(std::thread::hardware_concurrency());
    std::printf("# bench_mpmc_scq_vs_vyukov\n");
    std::printf("# capacity=%zu  per-thread-iters=%llu  warmup=%d  reps=%d\n",
                CAPACITY,
                static_cast<unsigned long long>(PER_THREAD),
                WARMUP_REPS, MEASURE_REPS);
    std::printf("# hardware_concurrency=%d\n", n_cores);
    std::printf("# items/sec (M = millions); SCQ_ratio = SCQ / Vyukov\n");
    std::printf("\n");
    std::printf("%4s %4s | %10s | %10s | %8s\n",
                "P", "C", "SCQ M/s", "Vyukov M/s", "ratio");
    std::printf("---------+-----------+-----------+--------\n");

    for (int p : counts) {
        for (int c : counts) {
            if (p + c > n_cores) continue;
            using SCQ = MpmcRing<std::uint64_t, CAPACITY>;
            using Vyk = VyukovMpmcRing<std::uint64_t, CAPACITY>;
            const double scq    = measure_median<SCQ>(p, c) / 1.0e6;
            const double vyk    = measure_median<Vyk>(p, c) / 1.0e6;
            const double ratio  = scq / vyk;
            std::printf("%4d %4d | %10.2f | %10.2f | %8.2f\n",
                        p, c, scq, vyk, ratio);
        }
    }
    std::printf("\n");
    std::printf("# Expected pattern (per Nikolaev DISC 2019):\n");
    std::printf("#   * (1, 1):  SCQ ~1.2-1.4× faster (CAS vs FAA per call)\n");
    std::printf("#   * (4, 4)+: SCQ ratio grows with contention\n");
    std::printf("#   * (8, 8)+: SCQ ~2× faster (Vyukov CAS-retry storm)\n");
}

}  // namespace

int main() {
    run_grid();
    return 0;
}
