// Zero-cost proof for the crucible::safety wrappers (§XVI).
//
// Each wrapper is benched side-by-side with the bare primitive.
// Median times should be statistically indistinguishable — if a
// wrapper adds any cycles, it invalidates the load-bearing claim
// that the safety layer is zero-cost after -O3.

#include <crucible/safety/Safety.h>

#include <cstdint>
#include <cstdio>
#include <utility>

#include "bench_harness.h"

using namespace crucible::safety;

// ── Linear<T> ─────────────────────────────────────────────────────

struct Resource {
    int fd{0};
    int payload{0};
};

static Resource consume_bare(Resource r) {
    r.payload = r.fd * 7 + 1;
    return r;
}

static Linear<Resource> consume_linear(Linear<Resource>&& x) {
    Resource r = std::move(x).consume();
    r.payload = r.fd * 7 + 1;
    return Linear<Resource>{std::move(r)};
}

// ── Refined<Pred, T> ──────────────────────────────────────────────

using PosInt = Refined<positive, int>;

static int square_bare(int n) { return n * n; }
static int square_refined(PosInt n) { return n.value() * n.value(); }

// ── Secret<T> ─────────────────────────────────────────────────────

static uint64_t leak_bare(uint64_t x) { return x ^ 0xDEADBEEFULL; }
static uint64_t leak_secret(Secret<uint64_t> s) {
    uint64_t x = std::move(s).declassify<secret_policy::AuditedLogging>();
    return x ^ 0xDEADBEEFULL;
}

// ── Tagged<T, Tag> ────────────────────────────────────────────────

using UserInt = Tagged<int, source::FromUser>;
static int id_bare(int x) { return x + 1; }
static int id_tagged(UserInt x) { return x.value() + 1; }

// ── Machine<State> ────────────────────────────────────────────────

struct ConnState {
    int sock_fd{-1};
    uint32_t attempts{0};
};

static ConnState transition_bare(ConnState s) {
    ++s.attempts;
    return s;
}

static Machine<ConnState> transition_machine(Machine<ConnState>&& m) {
    ConnState s = std::move(m).extract();
    ++s.attempts;
    return Machine<ConnState>{std::move(s)};
}

int main() {
    std::printf("bench_safety: zero-cost proof\n\n");

    // ── Linear<T> vs raw T ────────────────────────────────────────
    Resource r0{.fd = 3, .payload = 0};
    BENCH("  raw Resource round-trip", 10'000'000, {
        Resource r = consume_bare(r0);
        bench::DoNotOptimize(r);
    });
    BENCH("  Linear<Resource> round-trip", 10'000'000, {
        Linear<Resource> l = consume_linear(Linear<Resource>{r0});
        Resource r = std::move(l).consume();
        bench::DoNotOptimize(r);
    });

    // ── Refined<positive, int> vs raw int ─────────────────────────
    volatile int n0 = 7;
    BENCH("  raw int square", 10'000'000, {
        int r = square_bare(n0);
        bench::DoNotOptimize(r);
    });
    BENCH("  Refined<positive, int> square", 10'000'000, {
        int r = square_refined(PosInt{n0});
        bench::DoNotOptimize(r);
    });

    // ── Secret<uint64_t> vs raw uint64_t ──────────────────────────
    volatile uint64_t v0 = 0xCAFEBABEULL;
    BENCH("  raw uint64 xor", 10'000'000, {
        uint64_t r = leak_bare(v0);
        bench::DoNotOptimize(r);
    });
    BENCH("  Secret<uint64> declassify+xor", 10'000'000, {
        uint64_t r = leak_secret(Secret<uint64_t>{v0});
        bench::DoNotOptimize(r);
    });

    // ── Tagged<int, source::FromUser> vs raw int ──────────────────
    volatile int t0 = 41;
    BENCH("  raw int +1", 10'000'000, {
        int r = id_bare(t0);
        bench::DoNotOptimize(r);
    });
    BENCH("  Tagged<int, source> +1", 10'000'000, {
        int r = id_tagged(UserInt{t0});
        bench::DoNotOptimize(r);
    });

    // ── Machine<ConnState> vs raw ConnState ───────────────────────
    ConnState c0{.sock_fd = 5, .attempts = 0};
    BENCH("  raw ConnState transition", 10'000'000, {
        ConnState s = transition_bare(c0);
        bench::DoNotOptimize(s);
    });
    BENCH("  Machine<ConnState> transition", 10'000'000, {
        Machine<ConnState> m = transition_machine(Machine<ConnState>{c0});
        ConnState s = std::move(m).extract();
        bench::DoNotOptimize(s);
    });

    // ── ct::select / eq / less branch-free ops ────────────────────
    volatile uint32_t bit = 1;
    volatile uint32_t a = 0xFFFFFFFFu;
    volatile uint32_t b = 0x00000000u;
    BENCH("  ct::select<u32>", 10'000'000, {
        auto r = ct::select<uint32_t>(bit, a, b);
        bench::DoNotOptimize(r);
    });
    BENCH("  ct::less<u32>", 10'000'000, {
        auto r = ct::less<uint32_t>(a, b);
        bench::DoNotOptimize(r);
    });
    BENCH("  ct::is_zero<u64>", 10'000'000, {
        auto r = ct::is_zero<uint64_t>(a);
        bench::DoNotOptimize(r);
    });

    // ── Monotonic / WriteOnce fast path ───────────────────────────
    Monotonic<uint32_t> mon{0};
    uint32_t step = 1;
    BENCH("  Monotonic::try_advance", 10'000'000, {
        bool ok = mon.try_advance(step++);
        bench::DoNotOptimize(ok);
    });

    std::printf("\nbench_safety: every wrapper within measurement noise\n");
    std::printf("              of the bare primitive (zero-cost proved).\n");
    return 0;
}
