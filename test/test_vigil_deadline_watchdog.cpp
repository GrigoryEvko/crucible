#include <crucible/Vigil.h>
#include <crucible/warden/DeadlineWatchdog.h>
#include "test_harness.h"
#include "test_assert.h"

#include <cstdio>

using crucible::SchemaHash;
using crucible::ShapeHash;

// These two assertions consume constants the includes above pull in
// transitively, which would otherwise trip -Werror=unused-const-variable.
static_assert(crucible::perf::TIMELINE_MASK == crucible::perf::TIMELINE_CAPACITY - 1,
              "TIMELINE_MASK is the cyclic-mask companion to TIMELINE_CAPACITY");
static_assert(crucible::perf::PMU_SAMPLE_MASK == crucible::perf::PMU_SAMPLE_CAPACITY - 1,
              "PMU_SAMPLE_MASK is the cyclic-mask companion to PMU_SAMPLE_CAPACITY");

namespace {

crucible::TraceRing::Entry make_entry(SchemaHash schema_hash) {
    crucible::TraceRing::Entry e{};
    e.schema_hash = schema_hash;
    e.shape_hash = ShapeHash{0x1234};
    e.num_inputs = 1;
    e.num_outputs = 1;
    e.num_scalar_args = 0;
    e.op_flags = 0;
    return e;
}

crucible::TensorMeta make_meta() {
    crucible::TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(8);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx = -1;
    m.layout = crucible::Layout::Strided;
    m.data_ptr = crucible::external_data_ptr(nullptr);
    return m;
}

// Five schemas over three iterations is the smallest input that yields
// one region: the detector confirms the boundary on the second match
// and creates the region on the third.  The flush then waits until
// on_region_ready has run.
void drive_one_region(crucible::Vigil& vigil) {
    const SchemaHash schemas[5] = {SchemaHash{0xAA01}, SchemaHash{0xBB02}, SchemaHash{0xCC03}, SchemaHash{0xDD04},
                                   SchemaHash{0xEE05}};
    const crucible::TensorMeta meta = make_meta();
    const crucible::TensorMeta io_metas[2] = {meta, meta};

    for (int iter = 0; iter < 3; iter++) {
        for (int j = 0; j < 5; j++) {
            auto e = make_entry(schemas[j]);
            const bool ok = vigil.record_op(crucible::test::certify_synthetic_entry(e), io_metas, 2);
            assert(ok && "record_op must succeed (ring not full)");
        }
    }
    crucible::test::flush_and_wait_compiled(vigil);
}

void test_disabled_watchdog() {
    crucible::Vigil::Config cfg;
    // Spelled out although false is already the default.
    cfg.enable_deadline_watchdog = false;
    crucible::Vigil vigil(std::move(cfg));

    assert(!vigil.watchdog_enabled() && "default Config disables the watchdog");
    assert(vigil.last_watchdog_verdict() == ::crucible::warden::WatchdogVerdict::InsufficientData
           && "default verdict on disabled watchdog is InsufficientData");
    assert(vigil.watchdog_healthy_count() == 0);
    assert(vigil.watchdog_downgrade_count() == 0);
    assert(vigil.watchdog_insufficient_count() == 0);

    drive_one_region(vigil);

    // The region callback ran on the background thread and skipped the
    // watchdog, so every counter must still read zero.
    assert(!vigil.watchdog_enabled());
    assert(vigil.watchdog_healthy_count() == 0 && "disabled watchdog must not increment any counter");
    assert(vigil.watchdog_downgrade_count() == 0);
    assert(vigil.watchdog_insufficient_count() == 0);
    assert(vigil.last_watchdog_verdict() == ::crucible::warden::WatchdogVerdict::InsufficientData);

    // Reaching scope end without a crash is itself a claim: the
    // background thread joins before the members it borrows die.
}

void test_enabled_watchdog() {
    crucible::Vigil::Config cfg;
    cfg.enable_deadline_watchdog = true;
    // The default policy allows ten misses over a sixty-second window.
    crucible::Vigil vigil(std::move(cfg));

    assert(vigil.watchdog_enabled() && "enable_deadline_watchdog=true must construct senses_ + wd_");

    assert(vigil.watchdog_healthy_count() == 0);
    assert(vigil.watchdog_downgrade_count() == 0);
    assert(vigil.watchdog_insufficient_count() == 0);
    assert(vigil.last_watchdog_verdict() == ::crucible::warden::WatchdogVerdict::InsufficientData);

    drive_one_region(vigil);

    // Each observation increments exactly one of the three counters,
    // so their sum is the number of observations.
    const uint32_t healthy = vigil.watchdog_healthy_count();
    const uint32_t downgrade = vigil.watchdog_downgrade_count();
    const uint32_t insufficient = vigil.watchdog_insufficient_count();
    const uint32_t total = healthy + downgrade + insufficient;

    assert(total >= 1 && "at least one observe() must have run during flush");

    // Which verdict comes back is deliberately not asserted.  The
    // policy window is sixty seconds and this test runs in well under
    // a second, and without CAP_BPF the scheduler probe never attaches
    // at all.  Pinning a verdict would couple the test to wall-clock
    // timing and to the privileges of the machine running it.
    const auto v = vigil.last_watchdog_verdict();
    const bool legal_verdict = (v == ::crucible::warden::WatchdogVerdict::InsufficientData)
                            || (v == ::crucible::warden::WatchdogVerdict::Healthy)
                            || (v == ::crucible::warden::WatchdogVerdict::Downgrade);
    assert(legal_verdict && "verdict must be a legal enum value");

    // The first observation only captures a baseline, so it always
    // reports InsufficientData.  That makes a floor of one the safe
    // assertion, while leaving room for the other two counters to move
    // on a slower run.
    assert(insufficient >= 1
           && "first observation always returns InsufficientData "
              "(baseline-capture or window-not-elapsed)");

    // The watchdog borrows a pointer into a sibling member.  Reaching
    // scope end cleanly claims that the background thread joins, and
    // so the last observation finishes, before either member dies.
}

}  // anonymous namespace

int main() {
    test_disabled_watchdog();
    test_enabled_watchdog();
    std::printf("test_vigil_deadline_watchdog: all tests passed\n");
    return 0;
}
