# `PerfEventRing` — high-frequency sampling via mmap'd perf_event ring

**STATUS**: doc-only stub.  Tier-D.  Eventual path:
`include/crucible/perf/PerfEventRing.h`.  Userspace-only.

This is the **third** perf_event mode that PmuSample.h's comments
deferred to "future PerfEventRing path".  Now planned explicitly.

## Problem

PmuSample (BPF mode) is too costly per overflow for cycles + L1D
miss + branch_instructions — these fire MILLIONS per second on busy
CPUs.  BPF at 3-5 µs per sample means 10-50% CPU lost to BPF
overhead at those rates.  Unacceptable.

The kernel's classic perf_event ring buffer (mmap'd, kernel writes
PERF_RECORD_SAMPLE entries directly, userspace drains lock-free)
handles these high-rate events at ~50-100 ns per sample — what
`perf record` actually uses.

This is the missing third mode in the perf_event observability
trinity:

| Mode | Used for | Cost/event | Rate ceiling |
|---|---|---|---|
| BPF (PmuSample) | rare events with custom payload | 3-5 µs | ~10K/sec |
| Counting (PmuCounters) | cumulative totals via RDPMC | 0 ns/event | unlimited |
| **Ring (this one)** | **high-rate sampling with default payload** | **~50-100 ns** | **~10M/sec** |

## Mechanism

```cpp
struct perf_event_attr attr{};
attr.type           = PERF_TYPE_HARDWARE;
attr.config         = PERF_COUNT_HW_CPU_CYCLES;
attr.sample_period  = 100000;            // every 100K cycles
attr.sample_type    = PERF_SAMPLE_IP
                    | PERF_SAMPLE_TID
                    | PERF_SAMPLE_TIME
                    | PERF_SAMPLE_CPU
                    | PERF_SAMPLE_PERIOD;
attr.wakeup_events  = 1024;              // wake userspace every N samples
attr.disabled       = 0;
attr.exclude_kernel = 1;

int fd = perf_event_open(&attr, /*pid=*/0, /*cpu=*/cpu, -1, 0);

// mmap the data ring (kernel writes here):
size_t pages = 1 + (1u << 8);  // 1 metadata + 256 data pages = 1 MB
void* base = mmap(NULL, pages * page_size, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
struct perf_event_mmap_page* page = base;
char* data_ring = (char*)base + page_size;
size_t data_size = (pages - 1) * page_size;
```

Drain pattern (userspace):

```cpp
uint64_t head = __atomic_load_n(&page->data_head, __ATOMIC_ACQUIRE);
uint64_t tail = page->data_tail;  // we own this
while (tail < head) {
    struct perf_event_header* hdr =
        (perf_event_header*)(data_ring + (tail & (data_size - 1)));
    if (hdr->type == PERF_RECORD_SAMPLE) {
        // Decode {ip, tid, time, cpu, period} per sample_type bits
        // ...emit to bench-end aggregator...
    }
    tail += hdr->size;
}
__atomic_store_n(&page->data_tail, tail, __ATOMIC_RELEASE);
```

## API shape

```cpp
struct PerfEventSample {
    uint64_t ip;
    uint32_t tid;
    uint32_t cpu;
    uint64_t time;        // perf clock (similar to bpf_ktime_get_ns)
    uint64_t period;      // sample period AT TIME OF SAMPLE
};

class PerfEventRing {
public:
    enum class What : uint8_t {
        Cycles, Instructions, L1DMiss, BranchInstructions,
        Custom,  // user-provided perf_event_attr
    };

    [[nodiscard]] static std::optional<PerfEventRing>
        load(::crucible::effects::Init,
             What event,
             uint64_t sample_period = 100'000) noexcept;

    // Drain the ring on the bg thread; emit samples via callback.
    // Returns the count of samples drained.
    [[nodiscard]] uint32_t drain(
        std::span<PerfEventSample> out_buf) noexcept;

    // Borrowed view over the mmap'd ring (advanced — for SIMD-batched
    // userspace decoders).
    [[nodiscard]] safety::Borrowed<const std::byte, PerfEventRing>
        raw_ring_view() const noexcept;
};
```

## Cost

- Per-sample at kernel side: ~50-100 ns (PERF_RECORD_SAMPLE write).
- Per-sample at userspace side: ~10-30 ns (decode + memcpy to
  consumer buffer).
- `wakeup_events = 1024` keeps drain rate at ~1K wakeups/sec/CPU
  even at MHz sample rates.

## Known limits

- Ring overflow: if userspace drain falls behind, kernel sets
  `PERF_RECORD_LOST` records.  Drain code must monitor this.
- Per-CPU FDs (one per (event, cpu)).  16 cores × 4 events = 64 FDs.
- AUX ring (Intel PT) and data ring share the same `perf_event_attr`
  in some cases — be precise about which we open.
- Sample period adaptation (`SAMPLE_FREQ` mode where kernel adjusts
  period to hit a target rate) — useful for cross-microarch
  comparison; not included in v1.

## Sibling refs

- `PmuSample` (BPF mode) — for events too rare for ring overhead
  to amortize, where BPF gives custom payload
- `PmuCounters` — counting mode; pair with this ring for "total
  cycles + per-IP cycle distribution"
- `IntelPtOutlierReplay` — same AUX-area mmap pattern, but for
  Intel PT instead of generic perf samples

## Reference

`tools/perf/util/mmap.c` (kernel source) shows the canonical drain
loop.  `bcc/tools/profile.py` shows a Python-side equivalent.
