# `PmuCounters` — counting-mode HW PMU + RDPMC fast read

**STATUS**: doc-only stub.  Tier-A keystone.  Eventual path:
`include/crucible/perf/PmuCounters.h` + `src/perf/PmuCounters.cpp`.
Userspace-only — NO custom BPF program.

## Problem

Existing `PmuSample` is sampling-mode only — every event triggers a
PMI + BPF.  Cumulative counters (cycles, instructions → IPC; cache
refs/misses → hit rate; branch misses / branches → mispredict rate;
frontend/backend stall ratios) need COUNTING mode where silicon
just increments the counter and userspace reads the cumulative value
on demand.  This is the keystone — every Augur ratio metric (IPC,
cache hit rate, mispredict rate, stall ratios) depends on it.

## Mechanism

```cpp
// One-time setup (per-CPU, one perf_event_attr per HW counter):
struct perf_event_attr attr{};
attr.size           = sizeof(attr);
attr.type           = PERF_TYPE_HARDWARE;       // or _SOFTWARE / _HW_CACHE
attr.config         = PERF_COUNT_HW_CPU_CYCLES; // etc.
attr.sample_period  = 0;                        // ← COUNTING MODE
attr.read_format    = PERF_FORMAT_GROUP
                    | PERF_FORMAT_TOTAL_TIME_ENABLED
                    | PERF_FORMAT_TOTAL_TIME_RUNNING;
attr.exclude_kernel = 1;
attr.exclude_hv     = 1;

int leader_fd = perf_event_open(&attr, /*pid=*/0, /*cpu=*/cpu_n,
                                /*group_fd=*/-1, /*flags=*/0);
// Subsequent counters in the SAME group:
int member_fd = perf_event_open(&attr2, 0, cpu_n,
                                /*group_fd=*/leader_fd, 0);
// ... up to ~6-8 HW counters per group depending on uarch.

// mmap the leader FD to get the kernel-published perf_event_mmap_page:
struct perf_event_mmap_page* page =
    mmap(nullptr, sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED,
         leader_fd, 0);
```

Then on the bench-end read path, RDPMC (~5-15 cycles each):

```cpp
// Read one counter via RDPMC.  page->index encodes which HW counter
// the kernel pinned this perf_event to (or 0 if not currently pinned;
// fall back to syscall read).
static inline uint64_t pmc_read(const perf_event_mmap_page* page) {
    uint32_t seq, idx;
    uint64_t offset, count;
    do {
        seq = page->lock;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        idx    = page->index;
        offset = page->offset;
        if (idx == 0) {
            count = 0;  // fall back to read(fd) at outer layer
            break;
        }
        count = __builtin_ia32_rdpmc(idx - 1) + offset;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
    } while (page->lock != seq);
    return count;
}
```

## API shape

```cpp
namespace crucible::perf {

// Bench-region snapshot: every counter we opened, in one shot.
struct PmuCountersSnapshot {
    uint64_t cycles;
    uint64_t instructions;
    uint64_t ref_cycles;          // freq-independent for IPC under DVFS
    uint64_t cache_references;    // LLC accesses
    uint64_t cache_misses;        // LLC misses
    uint64_t branch_instructions;
    uint64_t branch_misses;
    uint64_t stalled_cycles_frontend;
    uint64_t stalled_cycles_backend;
    uint64_t enabled_ns;          // for multiplex scaling
    uint64_t running_ns;          // for multiplex scaling

    [[nodiscard]] PmuCountersSnapshot operator-(const PmuCountersSnapshot&) const noexcept;
    // Derived ratios (post-subtraction snapshot):
    [[nodiscard]] double ipc() const noexcept;
    [[nodiscard]] double cache_hit_rate() const noexcept;
    [[nodiscard]] double branch_mispredict_rate() const noexcept;
    [[nodiscard]] double frontend_stall_ratio() const noexcept;
    [[nodiscard]] double backend_stall_ratio() const noexcept;
};

class PmuCounters {
public:
    [[nodiscard]] static std::optional<PmuCounters>
        load(::crucible::effects::Init) noexcept;

    // ~80 ns total: 9 RDPMCs across all CPUs × 16 cores ≈ 144
    // RDPMCs at ~5 ns each ≈ 720 ns ÷ batched + sum ≈ ~80 ns
    // amortized.  Bench-region read is the only call site.
    [[nodiscard]] PmuCountersSnapshot read() const noexcept;

    // For BPF integration: returns the PERF_EVENT_ARRAY map FD that
    // sched_switch.bpf.c et al. attach to via
    // bpf_perf_event_read_value().  Map shape: ARRAY[nr_cpus] of
    // perf_event FDs for the CYCLES counter.
    [[nodiscard]] int cycles_perf_array_fd() const noexcept;

    PmuCounters(const PmuCounters&) =
        delete("PmuCounters owns FDs + mmap'd PMU pages");
    PmuCounters(PmuCounters&&) noexcept;
    ~PmuCounters();

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
```

## Cost model

| Operation | Cost |
|---|---|
| `load(Init{})` — one-time setup | ~10-50 ms (16 cores × 9 events × `perf_event_open` syscall) |
| `read()` per call | ~80 ns total (RDPMC × 9 × N_CPUs ÷ batched) |
| Per-iteration on bench hot path | **0 ns** (silicon counts; we don't read until region end) |
| `enabled/running` scaling | Done in userspace post-RDPMC (FP math; 1 µs total) |

vs. PmuSample's 3-5 µs per overflow event: PmuCounters is ~400×
cheaper PER MEASUREMENT and gives ABSOLUTE totals rather than
sampled extrapolation.

## Known limits

- **PMU multiplexing**: Intel x86_64 typically has 4-8 GP counters
  + 3 fixed (instructions, cycles, ref_cycles).  AMD Zen has 6 GP +
  IBS.  ARM Neoverse has 6 GP + cycle counter.  More events than
  HW slots → kernel rotates events; `enabled`/`running` ratio gives
  the scaling factor.  We always open in groups so all members are
  scheduled together (consistent ratio across the group).
- **Per-CPU FDs**: 16 cores × 9 events = 144 FDs.  Plus mmap of leader
  FDs = 16 mmap'd pages.  Resource-light but bookkeeping-heavy.
  `inplace_vector<int, 256>` per the `PerfFd` newtype shape that
  `PmuSample.cpp` already uses.
- **CR4.PCE bit**: RDPMC requires `cr4.pce = 1` for unprivileged
  reads.  Kernel sets this when `perf_event_paranoid <= 1` AND the
  thread has at least one perf_event open in the relevant CPU's
  context.  Documented; falls back to `read(fd)` syscall when RDPMC
  unavailable.
- **`exclude_kernel = 1`** by default — measures only userspace.
  Toggle via env-var to capture kernel-side too (requires CAP_PERFMON).

## Reference implementations

- `tools/perf/util/evsel.c` and `tools/perf/util/mmap.c` in the kernel
  source tree
- librdpmc (Andi Kleen): https://github.com/andikleen/jevents (rdpmc.c)
- PAPI's `papi_internal.c` for the cross-vendor abstraction
- Intel PCM (`pcm.cpp`)

## Integration

Bench harness adds a new line below the existing SenseHub deltas:

```
└─ ipc=2.31 instr=12.4M cycles=5.4M cache_hit=98.7% br_mp=0.4% fe_bound=12% be_bound=18%
```

Augur consumes via `Senses::pmu_counters()->read()` for ratio metrics.

## Sibling refs

- `_ext_sched_switch.md` — depends on `cycles_perf_array_fd()`
- `PmuTopDown.md` — alternative microarch breakdown (uses same
  `perf_event_open` group_fd machinery)
- `PerfEventRing.md` — same `perf_event_mmap_page`, but for sampling
  ring-buffer mode rather than counting + RDPMC

Existing GAPS task: none yet.  Filing as `GAPS-004j` would cover this
when implementation begins.
