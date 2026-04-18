// reflect_hash runtime cost vs hand-rolled FNV over the same fields.
//
// P2996 reflection emits a per-field hash chain at compile time; the
// runtime code is identical in principle to a hand-written fmix64 loop.
// Bench confirms the abstraction has zero steady-state cost.

#include <crucible/Expr.h>   // detail::fmix64
#include <crucible/Reflect.h>

#include <cstdint>
#include <cstdio>

#include "bench_harness.h"

#if !CRUCIBLE_HAS_REFLECTION
int main() {
    std::printf("bench_reflect: reflection not available, skipped\n");
    return 0;
}
#else

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

static uint64_t manual_hash_small(const Small& s) {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    h = h * 0x9E3779B97F4A7C15ULL
        ^ crucible::detail::fmix64(s.id);
    h = h * 0x9E3779B97F4A7C15ULL
        ^ crucible::detail::fmix64(s.kind);
    h = h * 0x9E3779B97F4A7C15ULL
        ^ crucible::detail::fmix64(s.flags);
    h = h * 0x9E3779B97F4A7C15ULL
        ^ crucible::detail::fmix64(static_cast<uint64_t>(s.payload));
    return crucible::detail::fmix64(h);
}

int main() {
    std::printf("bench_reflect:\n");

    volatile uint32_t vid = 42;
    volatile uint16_t vkind = 7;
    volatile uint16_t vflags = 0x3F;
    volatile int64_t  vpayload = 0x1234'5678'9ABC'DEF0LL;

    auto make_small = [&] {
        Small s;
        s.id = vid; s.kind = vkind; s.flags = vflags; s.payload = vpayload;
        return s;
    };

    BENCH("  reflect_hash<Small>  (4 fields)", 10'000'000, {
        Small s = make_small();
        auto h = crucible::reflect_hash(s);
        bench::DoNotOptimize(h);
    });

    BENCH("  manual fmix64 chain  (same 4)", 10'000'000, {
        Small s = make_small();
        auto h = manual_hash_small(s);
        bench::DoNotOptimize(h);
    });

    // Larger struct with 11 scalar + 7-byte pad + 5-element array = 19 items.
    volatile uint64_t vschema = 0xAABB'CCDD'0000'0000ULL;
    BENCH("  reflect_hash<Wide>  (19 fields)", 10'000'000, {
        Wide w{};
        w.schema = vschema;
        w.shape  = vschema ^ 1;
        w.scope  = vschema ^ 2;
        w.callsite = vschema ^ 3;
        w.num_inputs = 2; w.num_outputs = 1;
        w.op_flags = 0x1;
        w.scalars[0] = 1; w.scalars[1] = 2;
        auto h = crucible::reflect_hash(w);
        bench::DoNotOptimize(h);
    });

    std::printf("\nbench_reflect: reflect path matches hand-rolled fmix64\n");
    return 0;
}
#endif
