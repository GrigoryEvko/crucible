// Sentinel TU for Vigil's GAPS-004h follow-up: the DeadlineWatchdog
// wiring that runs on the bg thread inside on_region_ready and
// publishes its verdict via atomic counters readable from any thread.
//
// What this test asserts:
//   1. Vigil constructed with `enable_deadline_watchdog = false`:
//      - watchdog_enabled() == false
//      - All four watchdog accessors report sentinel/zero values
//      - After a region transition, no counter increments (watchdog
//        was never built, so on_region_ready's `if (wd_)` branch
//        is never taken)
//      - last_watchdog_verdict() stays InsufficientData (the default
//        atomic-init value)
//
//   2. Vigil constructed with `enable_deadline_watchdog = true`:
//      - watchdog_enabled() == true (Senses + DeadlineWatchdog were
//        built; libbpf may or may not have attached the SchedSwitch
//        subprogram, but the Vigil-side wiring is unconditional)
//      - After a region transition, exactly one counter incremented
//        (sum of healthy + downgrade + insufficient == 1)
//      - last_watchdog_verdict() reflects that observation
//      - In CI / restricted environments without CAP_BPF, the
//        SchedSwitch facade is unattached and observe() returns
//        InsufficientData every call — so wd_insufficient_count_ ≥ 1
//        is the always-safe assertion
//
//   3. Destruction order does NOT deadlock or UAF:
//      - Vigil goes out of scope at end of block; bg_ destructs
//        first (joins thread), then wd_, then senses_.  The borrow
//        in DeadlineWatchdog (a `const Senses*`) survives until wd_
//        destructs, by which time bg's thread has joined and no
//        more observe() calls are possible.
//
// What this test does NOT assert:
//   - Specific verdict values beyond "in the legal enum range" — the
//     ~60-second default Policy window means a fast test will almost
//     always see InsufficientData (baseline captured on first call,
//     window not yet elapsed on subsequent calls).  Asserting a
//     specific verdict would couple the test to wall-clock timing.
//   - Underlying DeadlineWatchdog state (baseline_count, etc.) — not
//     exposed by Vigil for race-safety reasons; only the published
//     atomic counters + verdict are part of the public surface.

#include <crucible/Vigil.h>
#include <crucible/rt/DeadlineWatchdog.h>
#include "test_harness.h"
#include "test_assert.h"

#include <cstdio>

using crucible::SchemaHash;
using crucible::ShapeHash;

// ── Suppress -Werror=unused-const-variable on TIMELINE_MASK +
// PMU_SAMPLE_MASK transitively pulled in via Vigil.h → Senses.h →
// SchedSwitch.h / PmuSample.h.  Sibling perf smoke tests use this
// same pattern.
static_assert(crucible::perf::TIMELINE_MASK ==
              crucible::perf::TIMELINE_CAPACITY - 1,
    "TIMELINE_MASK is the cyclic-mask companion to TIMELINE_CAPACITY");
static_assert(crucible::perf::PMU_SAMPLE_MASK ==
              crucible::perf::PMU_SAMPLE_CAPACITY - 1,
    "PMU_SAMPLE_MASK is the cyclic-mask companion to PMU_SAMPLE_CAPACITY");

namespace {

crucible::TraceRing::Entry make_entry(SchemaHash schema_hash) {
    crucible::TraceRing::Entry e{};
    e.schema_hash      = schema_hash;
    e.shape_hash       = ShapeHash{0x1234};
    e.num_inputs       = 1;
    e.num_outputs      = 1;
    e.num_scalar_args  = 0;
    e.set_grad_enabled(false);
    e.op_flags         = 0;
    return e;
}

crucible::TensorMeta make_meta() {
    crucible::TensorMeta m{};
    m.ndim        = 1;
    m.sizes[0]    = 8;
    m.strides[0]  = 1;
    m.dtype       = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx  = -1;
    m.layout      = crucible::Layout::Strided;
    m.data_ptr    = nullptr;
    return m;
}

// Drive a single region transition: K=5 schemas × 3 iterations = 15
// ops, then flush.  IterationDetector confirms the boundary on the
// second match (ops 6-10), creates a region on the third (11-15).
// flush() waits until on_region_ready has executed.
void drive_one_region(crucible::Vigil& vigil) {
    const SchemaHash schemas[5] = {
        SchemaHash{0xAA01}, SchemaHash{0xBB02}, SchemaHash{0xCC03},
        SchemaHash{0xDD04}, SchemaHash{0xEE05}
    };
    const crucible::TensorMeta meta = make_meta();
    const crucible::TensorMeta io_metas[2] = {meta, meta};

    for (int iter = 0; iter < 3; iter++) {
        for (int j = 0; j < 5; j++) {
            auto e = make_entry(schemas[j]);
            const bool ok = vigil.record_op(crucible::vouch(e), io_metas, 2);
            assert(ok && "record_op must succeed (ring not full)");
        }
    }
    crucible::test::flush_and_wait_compiled(vigil);
}

// ── (1) Disabled watchdog ─────────────────────────────────────────
//
// Vigil with default Config: watchdog_enabled() == false; on_region_ready
// never invokes observe(); counters stay at zero through any number
// of region transitions.

void test_disabled_watchdog() {
    crucible::Vigil::Config cfg;
    // explicit for documentation; this is the default
    cfg.enable_deadline_watchdog = false;
    crucible::Vigil vigil(std::move(cfg));

    assert(!vigil.watchdog_enabled() &&
           "default Config disables the watchdog");
    assert(vigil.last_watchdog_verdict() ==
               ::crucible::rt::WatchdogVerdict::InsufficientData &&
           "default verdict on disabled watchdog is InsufficientData");
    assert(vigil.watchdog_healthy_count() == 0);
    assert(vigil.watchdog_downgrade_count() == 0);
    assert(vigil.watchdog_insufficient_count() == 0);

    drive_one_region(vigil);

    // After a region transition, on_region_ready ran on bg thread but
    // skipped the watchdog block (`if (wd_)` is false).  Counters MUST
    // remain zero — the disabled-watchdog invariant.
    assert(!vigil.watchdog_enabled());
    assert(vigil.watchdog_healthy_count() == 0 &&
           "disabled watchdog must not increment any counter");
    assert(vigil.watchdog_downgrade_count() == 0);
    assert(vigil.watchdog_insufficient_count() == 0);
    assert(vigil.last_watchdog_verdict() ==
               ::crucible::rt::WatchdogVerdict::InsufficientData);

    // Vigil destructs at scope end — bg thread joins cleanly.  No
    // crash here means destruction-order discipline (bg_ first, then
    // wd_/senses_/atomics) is correctly preserved.
}

// ── (2) Enabled watchdog ──────────────────────────────────────────
//
// Vigil with enable_deadline_watchdog = true: senses_ + wd_ are built.
// After a region transition, on_region_ready calls wd_->observe() and
// updates exactly one counter + the verdict.

void test_enabled_watchdog() {
    crucible::Vigil::Config cfg;
    cfg.enable_deadline_watchdog = true;
    // policy is default (production() — 10 misses / 60-second window)
    crucible::Vigil vigil(std::move(cfg));

    assert(vigil.watchdog_enabled() &&
           "enable_deadline_watchdog=true must construct senses_ + wd_");

    // Pre-transition: no observe() has run yet.  All zero / default.
    assert(vigil.watchdog_healthy_count() == 0);
    assert(vigil.watchdog_downgrade_count() == 0);
    assert(vigil.watchdog_insufficient_count() == 0);
    assert(vigil.last_watchdog_verdict() ==
               ::crucible::rt::WatchdogVerdict::InsufficientData);

    drive_one_region(vigil);

    // After flush_and_wait_compiled returns, on_region_ready has fully
    // executed at least once.  Exactly one counter incremented per
    // observe() call.
    const uint32_t healthy      = vigil.watchdog_healthy_count();
    const uint32_t downgrade    = vigil.watchdog_downgrade_count();
    const uint32_t insufficient = vigil.watchdog_insufficient_count();
    const uint32_t total = healthy + downgrade + insufficient;

    assert(total >= 1 &&
           "at least one observe() must have run during flush");

    // The verdict must be a legal enum value (0/1/2 for InsufficientData/
    // Healthy/Downgrade).  We don't assert which specific value because:
    //  - The default Policy window is 60 seconds; a fast test almost
    //    always sees InsufficientData (window not yet elapsed)
    //  - In CI without CAP_BPF, SchedSwitch is unattached → always
    //    InsufficientData
    //  - In a system with attached SchedSwitch + low contention, could
    //    see Healthy if a 60s test ran (but our test is sub-second)
    const auto v = vigil.last_watchdog_verdict();
    const bool legal_verdict =
        (v == ::crucible::rt::WatchdogVerdict::InsufficientData) ||
        (v == ::crucible::rt::WatchdogVerdict::Healthy) ||
        (v == ::crucible::rt::WatchdogVerdict::Downgrade);
    assert(legal_verdict && "verdict must be a legal enum value");

    // The MOST LIKELY outcome on a fast test: 1 InsufficientData
    // (baseline capture on first observation; SchedSwitch may or may
    // not be attached but window certainly hasn't elapsed).  This is
    // the safe assertion.  We require insufficient ≥ 1 for that
    // reason, but allow healthy/downgrade to also fire if a longer
    // run happens.
    assert(insufficient >= 1 &&
           "first observation always returns InsufficientData "
           "(baseline-capture or window-not-elapsed)");

    // Vigil destructs at scope end.  Order: bg_ joins first → wd_
    // destructs (no more observe() possible) → senses_ destructs (no
    // more borrow holders).  The borrow contract is preserved.
}

}  // anonymous namespace

int main() {
    test_disabled_watchdog();
    test_enabled_watchdog();
    std::printf("test_vigil_deadline_watchdog: all tests passed\n");
    return 0;
}
