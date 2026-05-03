# `crucible::perf::Senses` — the aggregator

**STATUS**: doc-only stub.  Tier-Z.  Mostly a C++ facade (no new BPF
program); composes every per-program facade in this directory's
implemented siblings.

## Vision

Single entry point for "the organism's complete sensory nervous
system" — the original aspirational phrasing in `sense_hub.bpf.c`
line 3.  After GAPS-004{a,b,c} shipped 3 facades and this `_planned/`
tree planned 35 more, callers should NOT have to hand-load each
sub-program.  `Senses` does that.

## Design

```cpp
namespace crucible::perf {

class Senses {
public:
    // Load every available sub-program.  Each program loads
    // independently — failures (kernel too old, missing CAP, etc.)
    // are recorded in coverage().  load() never fails as a whole;
    // it returns Senses with whatever subset succeeded.
    [[nodiscard]] static Senses load_all(::crucible::effects::Init) noexcept;

    // Load only the named subset.  Useful for diagnostics where
    // PmuSample is wanted but not the whole 50-program battery.
    [[nodiscard]] static Senses load_subset(
        ::crucible::effects::Init,
        SensesMask which) noexcept;

    // Per-sub-program accessors return raw pointers (or nullopt) into
    // the loaded set.  Each is a `const SubProgram*` so callers can
    // pass to bench harness / Augur consumers without duplicating
    // ownership.
    [[nodiscard]] const SenseHub*       sense_hub()       const noexcept;
    [[nodiscard]] const SchedSwitch*    sched_switch()    const noexcept;
    [[nodiscard]] const PmuSample*      pmu_sample()      const noexcept;
    [[nodiscard]] const PmuCounters*    pmu_counters()    const noexcept;
    [[nodiscard]] const PmuTopDown*     pmu_topdown()     const noexcept;
    // ... 47 more accessors for the planned siblings ...

    // Coverage report — which subprograms attached, which failed,
    // why each failed.  For the bench harness banner.
    [[nodiscard]] CoverageReport coverage() const noexcept;

    // Snapshot helper — read every available sub-program's snapshot
    // in one batched API.  Returns a `SensesSnapshot` containing
    // optional sub-snapshots; consumer can pull what they need.
    [[nodiscard]] SensesSnapshot read() const noexcept;
};

struct SensesMask {
    bool sense_hub       : 1;
    bool sched_switch    : 1;
    bool pmu_sample      : 1;
    bool pmu_counters    : 1;
    bool pmu_topdown     : 1;
    // ... bits for all 50+ planned facades
    bool all() const noexcept { /* ... */ }
};

}
```

## Why a separate facade rather than direct sub-program use

1. **Coverage discoverability**: `Senses::coverage()` answers "what
   does this kernel give us?" in one place — vs. trying to load each
   sub-program individually and aggregating failure messages.
2. **Bench-harness simplification**: today's `bench_harness.h` has
   one alias `bench::bpf =` and per-facade hand-loading. After
   `Senses` lands, one `Senses::load_all(Init{})` replaces it all.
3. **Augur integration point**: Augur consumes telemetry across
   many programs.  A single `Senses` handle is a cleaner API than
   threading 30 sub-program pointers everywhere.
4. **Lifetime + dependency ordering**: PmuCounters must load BEFORE
   SchedSwitch's cycle-attribution extension.  `Senses` owns the
   ordering; consumers don't.

## Cost

Senses itself is structural — facade-of-facades, no BPF programs
of its own.  Loading all 50 facades takes ~5-15 seconds at startup
(verifier + JIT + map-create per program).  Acceptable for a
once-per-process load.  `Senses::read()` collates ~50 snapshots
in <1 ms total.

## Implementation TODO

1. Wait until at least 5 of the planned sub-program facades ship
   (need real call sites to validate the composition shape).
2. Author `include/crucible/perf/Senses.h` + `src/perf/Senses.cpp`.
3. Sentinel `test/test_perf_senses_smoke.cpp` verifying load_all +
   coverage + per-program accessor behavior.
4. Migrate `bench_harness.h` from per-facade hand-loading to
   `Senses::load_all(Init{})`.
5. Remove the legacy `bench::bpf =` namespace alias once Senses is
   the canonical name.

## Sibling refs

Composes: ALL 35 planned BPF programs + 11 userspace-only PMU
facades + the 3 existing GAPS-004{a,b,c} programs (SenseHub,
SchedSwitch, PmuSample) + the 3 extension PRs.

Existing reference: GAPS-004y task #1287.
