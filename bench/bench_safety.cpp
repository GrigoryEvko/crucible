// Zero-cost proof for the crucible::safety wrappers (§XVI).
//
// Each wrapper benched side-by-side with the bare primitive. Medians
// should be statistically indistinguishable — if a wrapper costs any
// measurable cycles, it invalidates the load-bearing claim that the
// safety layer is zero-cost after -O3. bench::compare() between every
// adjacent bare/wrapped pair makes this a one-line pass/fail per
// wrapper (look for [indistinguishable]; anything else is a regression).

#include <cstdint>
#include <cstdio>
#include <utility>

#include <crucible/safety/Safety.h>

#include "bench_harness.h"

using namespace crucible::safety;

// ── Linear<T> ─────────────────────────────────────────────────────

namespace {

struct Resource {
    int fd{0};
    int payload{0};
};

Resource consume_bare(Resource r) {
    r.payload = r.fd * 7 + 1;
    return r;
}

Linear<Resource> consume_linear(Linear<Resource>&& x) {
    Resource r = std::move(x).consume();
    r.payload = r.fd * 7 + 1;
    return Linear<Resource>{std::move(r)};
}

// ── Refined<Pred, T> ──────────────────────────────────────────────

using PosInt = Refined<positive, int>;

int square_bare(int n) { return n * n; }
int square_refined(PosInt n) { return n.value() * n.value(); }

// ── Secret<T> ─────────────────────────────────────────────────────

uint64_t leak_bare(uint64_t x) { return x ^ 0xDEADBEEFULL; }
uint64_t leak_secret(Secret<uint64_t> s) {
    uint64_t x = std::move(s).declassify<secret_policy::AuditedLogging>();
    return x ^ 0xDEADBEEFULL;
}

// ── Tagged<T, Tag> ────────────────────────────────────────────────

using UserInt = Tagged<int, source::FromUser>;
int id_bare(int x) { return x + 1; }
int id_tagged(UserInt x) { return x.value() + 1; }

// ── Machine<State> ────────────────────────────────────────────────

struct ConnState {
    int      sock_fd{-1};
    uint32_t attempts{0};
};

ConnState transition_bare(ConnState s) {
    ++s.attempts;
    return s;
}

Machine<ConnState> transition_machine(Machine<ConnState>&& m) {
    ConnState s = std::move(m).extract();
    ++s.attempts;
    return Machine<ConnState>{std::move(s)};
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    // All volatile seeds live in outer scope so lambdas capture the same
    // memory locations the compiler can't constant-fold through.
    const Resource  r0{.fd = 3, .payload = 0};
    volatile int    n0 = 7;
    volatile uint64_t v0 = 0xCAFEBABEULL;
    volatile int    t0 = 41;
    const ConnState c0{.sock_fd = 5, .attempts = 0};
    volatile uint32_t bit_ = 1;
    volatile uint32_t a    = 0xFFFFFFFFu;
    volatile uint32_t b    = 0x00000000u;

    std::printf("=== safety ===\n\n");

    // Layout: pairs of (bare, wrapped) at indices (0,1), (2,3), … for
    // trivial compare() at the end.
    bench::Report reports[] = {
        // Pair 0: Linear<Resource>
        bench::run("raw Resource round-trip", [&]{
            Resource r = consume_bare(r0);
            bench::do_not_optimize(r);
        }),
        bench::run("Linear<Resource> round-trip", [&]{
            Linear<Resource> l = consume_linear(Linear<Resource>{r0});
            Resource r = std::move(l).consume();
            bench::do_not_optimize(r);
        }),
        // Pair 1: Refined<positive, int>
        bench::run("raw int square", [&]{
            int r = square_bare(n0);
            bench::do_not_optimize(r);
        }),
        bench::run("Refined<positive,int> square", [&]{
            int r = square_refined(PosInt{n0});
            bench::do_not_optimize(r);
        }),
        // Pair 2: Secret<uint64_t>
        bench::run("raw uint64 xor", [&]{
            uint64_t r = leak_bare(v0);
            bench::do_not_optimize(r);
        }),
        bench::run("Secret<uint64> declassify+xor", [&]{
            uint64_t r = leak_secret(Secret<uint64_t>{v0});
            bench::do_not_optimize(r);
        }),
        // Pair 3: Tagged<int, source::FromUser>
        bench::run("raw int +1", [&]{
            int r = id_bare(t0);
            bench::do_not_optimize(r);
        }),
        bench::run("Tagged<int,source> +1", [&]{
            int r = id_tagged(UserInt{t0});
            bench::do_not_optimize(r);
        }),
        // Pair 4: Machine<ConnState>
        bench::run("raw ConnState transition", [&]{
            ConnState s = transition_bare(c0);
            bench::do_not_optimize(s);
        }),
        bench::run("Machine<ConnState> transition", [&]{
            Machine<ConnState> m = transition_machine(Machine<ConnState>{c0});
            ConnState s = std::move(m).extract();
            bench::do_not_optimize(s);
        }),

        // Standalone ct::* primitives — no bare comparator, they ARE
        // the primitive. Just confirm they stay ~1-2 ns.
        bench::run("ct::select<u32>", [&]{
            auto r = ct::select<uint32_t>(bit_, a, b);
            bench::do_not_optimize(r);
        }),
        bench::run("ct::less<u32>", [&]{
            auto r = ct::less<uint32_t>(a, b);
            bench::do_not_optimize(r);
        }),
        bench::run("ct::is_zero<u64>", [&]{
            auto r = ct::is_zero<uint64_t>(a);
            bench::do_not_optimize(r);
        }),

        // Monotonic fast path — try_advance with a strictly increasing
        // step counter. The state is inside the lambda so each auto-batch
        // invocation works from a fresh high-water mark (modular advance).
        [&]{
            Monotonic<uint32_t> mon{0};
            uint32_t step = 1;
            return bench::run("Monotonic::try_advance", [&]{
                bool ok = mon.try_advance(step++);
                bench::do_not_optimize(ok);
            });
        }(),
    };

    bench::emit_reports_text(reports);

    // One compare() per (bare, wrapped) pair. Zero-cost wrappers show
    // [indistinguishable]; any [REGRESS] flag is a real finding.
    std::printf("\n=== compare — zero-cost proof ===\n");
    const char* pair_labels[] = {
        "Linear<Resource>",
        "Refined<positive,int>",
        "Secret<uint64>",
        "Tagged<int,source>",
        "Machine<ConnState>",
    };
    for (size_t p = 0; p < std::size(pair_labels); ++p) {
        std::printf("  [%s]\n  ", pair_labels[p]);
        bench::compare(reports[2 * p], reports[2 * p + 1]).print_text(stdout);
    }

    bench::emit_reports_json(reports, json);
    return 0;
}
