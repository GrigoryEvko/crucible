# `PsiReader` — /proc/pressure/{cpu,memory,io} reader

**STATUS**: doc-only stub.  Tier-D.  Eventual path:
`include/crucible/perf/PsiReader.h`.  Userspace-only — no
perf_event, no BPF, just file reads.

## Problem

Pressure Stall Information (PSI, kernel 4.20+) is the cheapest and
most direct measure of "is this resource starving SOMEONE":

- `cpu.pressure` — % time tasks would have run if more CPU available
- `memory.pressure` — % time tasks stalled on memory (page fault,
  reclaim, swap)
- `io.pressure` — % time tasks stalled on I/O

Each line gives `some` (at least one task starving) and `full`
(all tasks starving) percentages over 10s / 60s / 300s windows
plus a cumulative `total=` counter.

Doesn't need perf_event_open at all.  Just open() + read().

## Mechanism

```cpp
int fd = open("/proc/pressure/cpu", O_RDONLY);
char buf[256];
read(fd, buf, sizeof(buf));
// Parse:
// some avg10=0.00 avg60=0.00 avg300=0.00 total=12345678
// full avg10=0.00 avg60=0.00 avg300=0.00 total=12345678
```

For per-cgroup PSI: `/sys/fs/cgroup/<path>/cpu.pressure` etc.
(kernel 5.2+).

## API shape

```cpp
struct PsiSnapshot {
    struct Resource {
        double some_avg10, some_avg60, some_avg300;
        uint64_t some_total_us;
        double full_avg10, full_avg60, full_avg300;
        uint64_t full_total_us;
    };
    Resource cpu, memory, io;

    [[nodiscard]] PsiSnapshot operator-(const PsiSnapshot& older) const noexcept;
};

class PsiReader {
public:
    [[nodiscard]] static std::optional<PsiReader>
        load(::crucible::effects::Init) noexcept;

    // ~3 syscall reads × ~5 µs each = ~15 µs per snapshot.
    // Acceptable for bench-end; not for per-iteration.
    [[nodiscard]] PsiSnapshot read() const noexcept;
};
```

## Cost

- Per `read()`: ~15 µs (3 file reads × ~5 µs).
- Per-iteration: 0 ns (we only call at bench boundaries).

## Known limits

- Kernel 4.20+ for system-wide; kernel 5.2+ for per-cgroup.
- Pre-4.20 kernels: load() returns nullopt (no /proc/pressure/).
- The `avg10/60/300` are kernel-maintained EMA windows; not bench-
  duration windows.  Use `total=` (cumulative microseconds) deltas
  for accurate bench-window pressure.
- Per-cgroup variant requires the bench process to be in a known
  cgroup (or pass cgroup path explicitly).

## Why include this in the perf observability suite

PSI gives a 30-second clear-signal answer to "is this node loaded"
without ANY perf_event setup overhead.  Pair with the deeper PMU
facades when PSI flags pressure: PSI says "memory is hurting", then
you drill into PmuCmtMbm + vmscan_ext + page_allocator for "WHO is
hurting and WHY".

## Sibling refs

- `cfs_bandwidth.bpf.c` — CFS throttle events drive cpu.pressure spikes
- `vmscan_ext.bpf.c` — kswapd activity drives memory.pressure
- All standalone facades pair with PsiReader as "screening signal +
  drill-down pair"
