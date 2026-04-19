// reflect_hash runtime cost vs hand-rolled FNV over the same fields.
//
// P2996 reflection emits a per-field hash chain at compile time; the
// runtime code is identical in principle to a hand-written fmix64 loop.
// The bench confirms the abstraction has zero steady-state cost — the
// Mann-Whitney A/B at the end should come out "indistinguishable" on a
// clean machine.

#include <cstdint>
#include <cstdio>

#include <crucible/Expr.h>       // detail::fmix64
#include <crucible/Reflect.h>

#include "bench_harness.h"

namespace {

struct Small {
    uint32_t id;
    uint16_t kind;
    uint16_t flags;
    int64_t  payload;
};

struct Wide {
    uint64_t schema;
    uint64_t shape;
    uint64_t scope;
    uint64_t callsite;
    uint32_t num_inputs;
    uint32_t num_outputs;
    uint8_t  op_flags;
    uint8_t  pad[7];
    int64_t  scalars[5];
};

uint64_t manual_hash_small(const Small& s) noexcept {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    h = h * 0x9E3779B97F4A7C15ULL ^ crucible::detail::fmix64(s.id);
    h = h * 0x9E3779B97F4A7C15ULL ^ crucible::detail::fmix64(s.kind);
    h = h * 0x9E3779B97F4A7C15ULL ^ crucible::detail::fmix64(s.flags);
    h = h * 0x9E3779B97F4A7C15ULL ^ crucible::detail::fmix64(
        static_cast<uint64_t>(s.payload));
    return crucible::detail::fmix64(h);
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== reflect ===\n\n");

    // All three Runs use volatile-seeded inputs so the compiler can't
    // hoist the hash computation out of the body. The seeds live in the
    // outer scope so the IIFE-lambdas can capture by reference.
    volatile uint32_t vid       = 42;
    volatile uint16_t vkind     = 7;
    volatile uint16_t vflags    = 0x3F;
    volatile int64_t  vpayload  = 0x1234'5678'9ABC'DEF0LL;
    volatile uint64_t vschema   = 0xAABB'CCDD'0000'0000ULL;

    auto make_small = [&]() noexcept {
        Small s{};
        s.id      = vid;
        s.kind    = vkind;
        s.flags   = vflags;
        s.payload = vpayload;
        return s;
    };

    bench::Report reports[] = {
        bench::run("reflect_hash<Small>  (4 fields)", [&]{
            Small s = make_small();
            auto h = crucible::reflect_hash(s);
            bench::do_not_optimize(h);
        }),
        bench::run("manual fmix64 chain  (same 4)", [&]{
            Small s = make_small();
            auto h = manual_hash_small(s);
            bench::do_not_optimize(h);
        }),
        bench::run("reflect_hash<Wide>  (19 fields)", [&]{
            Wide w{};
            w.schema      = vschema;
            w.shape       = vschema ^ 1;
            w.scope       = vschema ^ 2;
            w.callsite    = vschema ^ 3;
            w.num_inputs  = 2;
            w.num_outputs = 1;
            w.op_flags    = 0x1;
            w.scalars[0]  = 1;
            w.scalars[1]  = 2;
            auto h = crucible::reflect_hash(w);
            bench::do_not_optimize(h);
        }),
    };

    bench::emit_reports_text(reports);

    // The core claim: reflect_hash<Small> and manual_hash_small produce
    // statistically indistinguishable timings on a clean machine. If
    // this goes [REGRESS], reflection's emitted code has drifted from
    // the hand-rolled form.
    std::printf("\n=== compare ===\n");
    bench::compare(reports[0], reports[1]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
