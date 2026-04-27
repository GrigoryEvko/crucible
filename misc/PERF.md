# Perf — Crucible's Always-On Measurement Fabric

*The subsystem that makes Augur's recommendations engine actually have data, makes `crucible plan show` actually have measurements, and makes Crucible's 17-layer adaptive design an empirical feedback loop instead of a spec document. In-process. Always on. Sub-millisecond. Every binary. Every Relay. Every node in the Canopy mesh. Every user module loaded via dlopen. Zero syscalls on hot path. Zero vendor tooling. Zero external dependencies.*

Perf is the measurement layer that every other Crucible subsystem reads from. Forge Phase L VALIDATE (FORGE.md §17) reads drift signals. Augur §13 produces recommendations. `crucible plan bisect` (CRUCIBLE.md §17.6.3) replays measured counters. Mimic's calibration harness (MIMIC.md §22) feeds live residuals back into its simulator. The 96-counter BPF sense hub and six stack-traced BPF programs already in `include/crucible/perf/bpf/` are the producers. This document specifies the **consumer side** — the C++ code that lives inside every Crucible binary and turns the raw streams into coherent attribution — and the **federation plane** that stitches per-node data into one cluster-wide view via Canopy gossip.

Written in the voice of CLAUDE.md and CRUCIBLE.md: direct, opinionated, dense. Read alongside all three other design docs — this one assumes their vocabulary.

---

## Contents

1. Thesis and scope
2. The Linux syscall cost catastrophe
3. Architecture — ten layers, cost-budgeted
4. Primitives — `core/` (TSC, Vendor, Perf, Mmap, DynamicPmu)
5. User-space rdpmc — the killer feature
6. Counter classes — `counters/` (Pipeline, Cache, Bandwidth, Energy, Simd, MemLatency)
7. Region engine — `region/` (ScopeTable, ThreadLocal, RegionGuard)
8. BPF consumer — `bpf_consumer/` (Loader, SenseMmap, RingbufReader, PidFilter)
9. Zero-syscall BPF reading patterns
10. Symbol resolution — `symbols/` (Cipher-backed cache, embedded tables)
11. Multi-level code view — source↔LLVM IR↔ASM
12. Line profiler — `line/` (Sampler, IpHistogram, SymbiotTrace)
13. Augur engine integration — `augur/`
14. Broker — `broker/` (Local UDS, HTTP server, federation)
15. Cipher persistence — observability namespace
16. CNTP gossip for cluster-wide aggregation
17. User module integration — C++, Python, Rust, Vessel, dlopen
18. Bench harness integration
19. Dashboard data contract — JSON endpoints for Dioxus
20. Crucible-specific visualizations
21. Performance budget — the full arithmetic
22. Failure modes and graceful degradation
23. Replay determinism and the observability namespace
24. Security and multi-tenancy
25. Hardware-specific notes — Intel, AMD, ARM
26. Kernel version requirements
27. Build flags
28. Testing
29. Upgrade path
30. Build plan — milestones
31. Known limitations and what we get wrong
32. Open questions deferred
33. Glossary

---

## 1. Thesis and scope

Nine sentences:

1. **Crucible already produces the telemetry; we only need to correlate it.** TraceRing entries, CompiledKernel.predicted_cycles, Cipher plan records, BPF sense-hub counters, and CNTP event rings are all produced as side-effects of normal operation. Perf joins these streams into an attribution graph keyed by (file, line, function) × (kernel, region, scope) × (thread, node, cluster).
2. **Never syscall on the hot path.** `perf_event_open` at thread init: fine. `read(perf_fd)` on every scope exit: **unacceptable** — ~50–200 ns per syscall, dominates everything else. The entire hot path of Perf is: `rdtsc`, `rdpmc` via mmap metadata page, store to thread-local ring buffer, volatile load from mmap'd BPF array. Zero syscalls.
3. **Anything Linux provides synchronously is abysmally slow.** `clock_gettime` via vDSO is 15 ns — we bypass by using `rdtsc` and rebasing offline. `read(2)` on a BPF map fd is a syscall — we use `BPF_F_MMAPABLE` arrays and ringbufs that mmap directly. `bpf_map_lookup_batch` is a syscall costing ~100 µs for 10k entries — we restructure BPF programs to emit events into mmap'd ringbufs so userspace never issues `bpf()` syscalls on the drain path. `/proc/self/*` reads cost 10–50 µs — we read once at startup, cache in memory, never re-read.
4. **Raw hardware counters through `rdpmc` replace the `read(perf_fd)` path entirely.** `perf_event_open` still opens the counter and sets up the mmap — that's one-time cost at thread init. Every subsequent counter read is a single `rdpmc` instruction (~24 cycles, ~8 ns) with a seqlock-protected offset/index read from the mmap'd metadata page. No syscall. No context switch. No kernel entry. This is the single most impactful design choice — it reduces region-scope overhead from symbiotic's ~40 ns to ~20 ns.
5. **BPF kernel-side programs do aggregation in NMI/tracepoint context.** The CPU cycles burnt belong to the kernel's event-handling path, not the profiled thread. Our user-space consumer reads the resulting counters/events via mmap volatile loads — zero syscalls, zero thread wakeups, zero context switches. The sense hub's 96 counters are 768 bytes on 12 cache lines; reading all of them costs ~12 ns. Reading the 8 cache-line-grouped counters we care about per scope costs ~4–5 ns via AVX2 gather.
6. **Compile-time scope registration gives us zero-cost name interning.** Each `CRUCIBLE_PROFILE_SCOPE("name")` macro invocation registers at program init via `consteval + static initializer` into a fixed-size BSS table indexed by `u16`. No runtime hashmap. No mutex. No string comparison on hot path. First scope-enter cost is identical to 10,000th: pointer compare + table index = 1–2 cycles.
7. **Thread-local ring buffers are mmap-backed and drain offline.** Each thread has a 64 KB ring buffer allocated from an mmap'd region at thread creation. Scope events are 48-byte records written in sequential order. A reader thread drains at 10 Hz via lock-free SPSC protocol. Tree reconstruction, IP resolution, cross-thread interleaving — all happen in the reader, never on the producer's hot path.
8. **Cluster federation rides CNTP.** Every Keeper broadcasts a compressed `NodeSummary` flatbuffer (~2–5 KB) every 10 seconds via Canopy's existing gossip channel. Each Keeper maintains an atomic `shared_ptr<ClusterView>` with all peers' latest summaries — HTTP handlers serve cluster-wide queries with one atomic load + one JSON serialization (~1 µs). Deep-dive queries use targeted CNTP RPC to the specific node. No master, no designated aggregator, same topology-free design as the rest of Canopy (CRUCIBLE.md §7.5).
9. **Content-addressed everything, deduplicated across runs, threads, nodes, and organizations.** Symbol resolution is cached by `(build_id, ip_page)` in Cipher; the first Keeper in the cluster that resolves a binary's IPs populates the cache for all subsequent Keepers, forever. Cross-cluster sharing per CRUCIBLE.md §18.3 applies — resolved IPs are public artifacts with no privacy implication. Over time the installed base builds a content-addressed performance corpus with no industry analog: real instruction-level hardware behavior of real ML workloads at the scale of Crucible's deployment footprint.

Scope boundaries:

| Category | In scope | Out of scope |
|---|---|---|
| Counter sources | Hardware PMU via `perf_event_open` + `rdpmc`; BPF tracepoints; BPF perf_event overflow sampling; AMD IBS; Intel PEBS; RAPL via `power` PMU; uncore memory bandwidth | Vendor hardware probes (CUPTI, rocprof, neuron-profile) — those live in Mimic (MIMIC.md §23) and Perf consumes their output via the `probe_counters` contract |
| Attribution granularity | Per-(file, line, function) × event × region × thread × node × cluster | Per-cycle / per-instruction (that's Intel PT / AMD IBS — we'd use for Tier D on-demand only) |
| Data flows | In-process → per-thread ring → per-Keeper broker → Cipher + CNTP gossip → cluster-wide HTTP JSON | External observers (Prometheus, OpenTelemetry, vendor profilers) — we provide exporters but don't make them primary |
| User code | C++ (`CRUCIBLE_USER_SCOPE` macro), Python (`crucible.perf.scope` context manager), Rust (`#[profile]` attribute macro), user `.so` modules loaded via `dlopen` (LD_AUDIT auto-registration), Vessel-captured graph ops (IR001 ops get scope labels automatically) | Closed-source user binaries without debug info — we'd show raw IPs only |
| Dependencies | libbpf, libelf, libdw (elfutils), libcapstone, libzstd, nlohmann::json (header-only, vendored), cpp-httplib (header-only, vendored) | Rust crates, external profiler tools, vendor SDKs |
| Kernel | Linux ≥ 5.8 (BPF_F_MMAPABLE, BPF_MAP_TYPE_RINGBUF, bpf_map_lookup_batch, BTF/CO-RE); recommend ≥ 6.1 for MADV_COLLAPSE | Pre-5.8 kernels get a degraded /proc-only mode |

Everything out of scope is either (a) handled by a neighbor subsystem (Mimic for vendor-specific counter probes; Cipher for persistence; CNTP for transport; Augur for recommendation generation), or (b) an explicit non-goal (external tool interop beyond optional exporters).

---

## 2. The Linux syscall cost catastrophe

This is the document's central thesis. Everything Linux provides synchronously is abysmally slow relative to per-op dispatch on the Crucible hot path: a single syscall costs orders of magnitude more cycles than the entire user-visible work for one recorded op. Concrete kernel-side costs measured on Intel Sapphire Rapids / AMD Zen 4 with mitigations=off — these are hardware/kernel facts, not Crucible promises:

| Operation | Cost | Why it's a catastrophe |
|---|---|---|
| `write(fd, buf, n)` syscall entry/exit | 100–300 ns | Basic syscall roundtrip; ~60 dispatch-op budgets |
| `read(fd, buf, n)` | 100–300 ns | Same |
| `clock_gettime(CLOCK_MONOTONIC)` via vDSO | 15–25 ns | vDSO path; no syscall; still 3–5× our per-scope budget |
| `clock_gettime(CLOCK_MONOTONIC)` via fallback syscall | 500 ns+ | When vDSO is disabled (e.g., `vdso=0` kernel cmdline) |
| `getpid()` syscall | 70 ns | Almost always cached in TLS — but calling it is still expensive |
| `gettid()` syscall | 70 ns | No vDSO; every call hits the kernel |
| `futex(FUTEX_WAIT)` | 1–5 µs | Wake/wait adds scheduler interaction |
| `mmap(ANONYMOUS, size)` | 5–50 µs | Page-table setup; faults dominate |
| `munmap(ptr, size)` | 10–100 µs | TLB flush coordination across CPUs |
| `open(path, O_RDONLY)` | 5–50 µs | Path resolution + inode lookup |
| `ioctl(PERF_EVENT_IOC_ENABLE)` | 1–5 µs | Kernel acquires per-event mutex |
| `read(perf_event_fd, &group_val, N)` | 500 ns–1 µs | Kernel reads all counters in group; marshals to userspace |
| `perf_event_open(2)` | 10–100 µs | Full counter setup; allocates HW PMU slot |
| `bpf(BPF_MAP_LOOKUP_ELEM, …)` | 300 ns–1 µs | Syscall + map protection + per-bucket spinlock |
| `bpf(BPF_MAP_LOOKUP_BATCH, …)` with 10k entries | 100 µs | Better than per-entry, but still syscall-bound |
| `bpf(BPF_MAP_UPDATE_ELEM, …)` | 500 ns+ | Same as lookup + allocation |
| Reading `/proc/self/stat` | 10–50 µs | `open` + `read` + parse |
| Reading `/proc/self/status` | 20–100 µs | Larger file |
| Reading `/proc/self/task/<tid>/schedstat` | 20–50 µs per thread | Multiplied by thread count |
| `sched_setaffinity(pid, sizeof, mask)` | 1–10 µs | Syscall + load balancer wakeup |
| `pthread_self()` | 2–5 ns | Userspace, no syscall — good |
| TLS access via `__thread` | 1–3 ns | `fs:` base register — fast |
| Atomic compare-exchange | 3–10 ns | CPU coherency protocol |
| Cache-aligned volatile load | 1 ns | L1 hit |
| Cache-aligned volatile load + fence | 2–3 ns | L1 + LFENCE |
| `rdtsc` | 2–5 ns | Direct instruction, not serializing |
| `rdtscp` | 8–12 ns | Serializing |
| `rdpmc` (user-enabled) | 7–10 ns | Direct instruction; reads PMC register |
| `rdpmc` (unprivileged, traps) | 200+ ns | Fault to kernel, emulated or signaled — DISASTER |

**Design implications:**

- Our region-scope enter+exit must consume ≤ 20 ns total. That means **no syscalls**, **no vDSO**, **no atomics except relaxed ones**. Only `rdtsc`, `rdpmc`, and TLS-local memory writes.
- Every `read(fd)` or `bpf(…)` syscall on the drain path costs orders of magnitude more than the events themselves. Drain paths use `mmap` + volatile loads exclusively.
- The `/proc` filesystem is a **one-time-at-startup** data source for static process info (PID, UID, caps, cgroup). Never touched on hot path or even periodic polling.
- Linux provides `CLOCK_MONOTONIC_RAW` via vDSO but it's still 15 ns. We use `rdtsc` everywhere and rebase to ns at report generation time.

**The entire design is organized around the rule: if it's a syscall, it happens once at init or never.** Every hot path instruction is a direct CPU instruction (rdtsc, rdpmc, volatile load, store, arithmetic) or a cache-friendly memory access.

### 2.1 Why `perf stat` and `perf record` are the wrong answer

The standard Linux profiling tools (`perf stat`, `perf record`, `perf report`) are external processes that attach to the profiled workload via ptrace or perf_event_open + ring buffer reads, then do their work in a separate process. Reasons this is inadequate for Crucible:

- **External process**: `perf`'s work happens in a separate address space. Every counter read requires syscall from the perf process to read the mmap ringbuf. Perf's own CPU usage is budgeted separately and competes with the profiled workload for L3 cache, DRAM bandwidth, and CPU scheduling slots. For Crucible running training at 99% CPU utilization, there's no CPU left over for perf.
- **Tool overhead is 5–30%**: realistic workloads see measurable slowdown when `perf record` is attached. That's 50× over our 0.1% budget.
- **Delayed analysis**: `perf report` runs offline on the captured data. No real-time view. No live dashboard. No Augur-driven recompile.
- **No cross-run deduplication**: every `perf record` generates its own `perf.data` file. There's no content-addressed symbol cache across runs. Every invocation re-parses DWARF from scratch.
- **No cluster awareness**: `perf` is a single-node tool. For multi-node Crucible deployments, you'd need to correlate N `perf.data` files by hand. There's no equivalent of our Canopy gossip + federated view.
- **No Crucible semantic annotations**: `perf` knows about instructions and addresses. It doesn't know about KernelIds, PlanHashes, RegionScopes, or MFU breakdowns. Every correlation with Crucible concepts has to be done externally.

In-process profiling as designed here eliminates all of these — and at the cost of far less overhead because we avoid every syscall that `perf` makes per event.

### 2.2 What we use Linux for, minimally

- **`perf_event_open(2)`** at thread/profiler init only. Counter setup is a one-time cost.
- **`bpf(BPF_PROG_LOAD, …)`** at Keeper init only. BPF program loading.
- **`mmap(2)`** at init only. Set up mmap'd PMU metadata pages, BPF map mmap regions, thread ring buffers.
- **`ioctl(PERF_EVENT_IOC_*)`** at init only. Enable/disable counters, set period.
- **`clone(2)` / `pthread_create`** for creating worker threads. Part of Keeper's normal thread pool setup, not our addition.
- **`io_uring_*`** for Cipher disk writes (uses existing Cipher infrastructure from CRUCIBLE.md §9).

That's the entire syscall surface. Everything else is user-space CPU instructions plus volatile mmap loads.

---

## 3. Architecture — ten layers, cost-budgeted

Ten layers, top to bottom. Each layer has a strict cost budget; each uses only what's below it.

```
Layer 9  User integration (C++ macros, Python bindings, Rust FFI, dlopen hook, Vessel bridge)
Layer 8  Dashboard (HTTP server, JSON endpoints, SSE streams, flatbuffer gossip)
Layer 7  Augur engine (residuals, drift, regression, MFU, recommendations, collective learning)
Layer 6  Broker (per-Keeper aggregation, UDS listener, NodeBuckets ring, HTTP state cache)
Layer 5  Line profiler (per-CPU IP samplers, BPF sample drain, IP histograms, SymbiotTrace output)
Layer 4  Symbol resolution (libdw, Cipher-backed cache, embedded .symbiot_sym table, multi-level)
Layer 3  BPF consumer (libbpf loader, SenseMmap, RingbufReader, PidFilter)
Layer 2  Region engine (compile-time ScopeTable, thread-local ring, RegionGuard, Reconstruct)
Layer 1  Counter classes (Pipeline, Cache, Bandwidth, Energy, Simd, MemLatency)
Layer 0  Primitives (Tsc, Vendor, Perf, MmapSeqlock, DynamicPmu, CacheAtomic)
```

Cost budget per layer:

| Layer | Operation class | Budget | Achieves |
|---|---|---|---|
| 0 | `rdpmc` via mmap | 8 ns/counter | Zero syscalls |
| 1 | Counter group read | 16–30 ns for 6–8 counters | Opened once per thread, read via Layer 0 |
| 2 | Scope enter | 3 ns (TSC-only) or 20 ns (TSC + 2 rdpmc) | TLS-only, zero-alloc |
| 2 | Scope exit | 3 ns (TSC-only) or 23 ns (TSC + 2 rdpmc + deltas) | TLS-only, zero-alloc |
| 3 | Sense hub snapshot | 5 ns (AVX2 gather, 8 counters) | Volatile mmap loads |
| 3 | BPF ringbuf drain | 100 ns per event, run in reader thread at 10 Hz | Volatile mmap loads |
| 4 | Symbol resolution | 50 µs cold, 5 µs warm (Cipher-cached) | Reader thread only |
| 5 | IP histogram aggregation | 10–100 µs at report time | Reader thread only |
| 6 | HTTP response | 1–10 µs (atomic shared_ptr load + JSON serialize) | Separate server thread |
| 7 | Drift residual computation | 1 µs per kernel sample | Reader thread only |
| 8 | Gossip broadcast | 100 µs every 10s | CNTP channel, bg thread |
| 9 | User scope entry | Same as Layer 2 — the macro expands to that | User's choice, RAII |

Total per-iteration overhead on a Crucible training step doing 1000 dispatches:
- 1000 × 0.3 ns (TraceRing enter_tsc) = 300 ns
- 10 × 20 ns (region scopes around compile/Cipher paths) = 200 ns
- 0 × scope cost on dispatch path (not instrumented)
- Deferred: ~100 µs per 100 ms for reader thread work

Total: ~0.3 µs of profiler overhead in a ~15 ms iteration = **0.002% overhead**. Two orders of magnitude under budget. The budget headroom is consumed by: adaptive deep-dive when requested, cluster-wide gossip overhead, and cold-start compilation tracing.

---

## 4. Primitives — `include/crucible/perf/core/`

### 4.1 `Tsc.h`

TSC (Time Stamp Counter) is our primary clock. Modern x86 CPUs have **invariant TSC** — the counter ticks at a constant rate regardless of P-state or C-state, synchronized across all cores on the socket. We check at startup via `CPUID leaf 0x80000007, bit 8`.

```cpp
namespace crucible::perf::tsc {

// Set at calibrate() — ns per TSC tick, thread-safe static
inline double g_ns_per_tsc = 0.0;
inline uint64_t g_tsc_anchor = 0;  // TSC reading at calibration
inline uint64_t g_ns_anchor = 0;   // CLOCK_MONOTONIC at same instant

// Run once at Keeper init. ~10 ms calibration window.
void calibrate() noexcept {
    // Busy-wait 10 ms between two (rdtsc, clock_gettime) pairs.
    // Compute slope: ns/tsc = (ns_end - ns_start) / (tsc_end - tsc_start).
    // Store slope + first anchor for offset conversion.
    // ...
}

// Zero-syscall ns-from-TSC. ~1 ns cost.
[[gnu::always_inline]] inline uint64_t ns_from_tsc(uint64_t tsc) noexcept {
    return g_ns_anchor + static_cast<uint64_t>(
        static_cast<double>(tsc - g_tsc_anchor) * g_ns_per_tsc);
}

// Raw rdtsc — ~3 ns.
[[gnu::always_inline]] inline uint64_t rdtsc() noexcept {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// Serializing rdtscp — ~8 ns. Use for exit boundaries where we need ordering.
[[gnu::always_inline]] inline uint64_t rdtscp() noexcept {
    uint32_t lo, hi, aux;
    __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// LFENCE-sandwiched rdtsc — ~5 ns. Use when we need strong ordering without serialization cost of rdtscp.
[[gnu::always_inline]] inline uint64_t rdtsc_fenced() noexcept {
    uint64_t tsc;
    __asm__ volatile("lfence\n\trdtsc\n\tshl $32, %%rdx\n\tor %%rdx, %%rax"
                     : "=a"(tsc) :: "rdx");
    return tsc;
}

} // namespace
```

Why TSC not CLOCK_MONOTONIC? Even vDSO CLOCK_MONOTONIC costs ~15 ns. TSC costs 3 ns. At 1 M scopes/sec, that's 12 ms/sec saved — 1.2% recovered budget. At cluster scale with hundreds of threads per Keeper, it adds up.

Why not `std::chrono::steady_clock::now()`? Same reason — it calls clock_gettime under the hood. We're below std::chrono.

### 4.2 `Vendor.h`

CPU vendor detection at startup via `CPUID leaf 0` and `/proc/cpuinfo`:

```cpp
namespace crucible::perf {

enum class CpuVendor : uint8_t {
    Intel,
    Amd,
    Arm,
    Unknown,
};

struct CpuInfo {
    CpuVendor vendor;
    uint8_t family;
    uint8_t model;
    uint8_t stepping;
    bool has_invariant_tsc;
    bool has_ibs;           // AMD only
    bool has_pebs;          // Intel only
    bool has_intel_pt;      // Intel only
    uint8_t pmu_gp_counters;   // 4 (Zen2), 6 (Zen3), 8 (Zen5, Sapphire Rapids)
    uint8_t pmu_fixed_counters;  // 3 or 4
    uint64_t pmu_width;     // counter width in bits (40 or 48)
};

// Filled once at startup
extern const CpuInfo g_cpu_info;

// Per-vendor raw event codes for events not in the generic PERF_TYPE_HW_CACHE enum
namespace events {
    // Intel FP_ARITH variants (Skylake+)
    constexpr uint64_t INTEL_FP_ARITH_SCALAR_DOUBLE = 0x01C7;
    constexpr uint64_t INTEL_FP_ARITH_SCALAR_SINGLE = 0x02C7;
    constexpr uint64_t INTEL_FP_ARITH_128B_PACKED = 0x04C7;
    constexpr uint64_t INTEL_FP_ARITH_256B_PACKED = 0x08C7;
    constexpr uint64_t INTEL_FP_ARITH_512B_PACKED = 0x10C7;

    // Intel L2 (Skylake+)
    constexpr uint64_t INTEL_L2_RQSTS_DEMAND_DATA_RD = 0xE124;
    constexpr uint64_t INTEL_L2_RQSTS_DEMAND_DATA_RD_MISS = 0x2124;

    // AMD Zen2+ DC refill (L1D miss)
    constexpr uint64_t AMD_L1D_MISS = 0x0141;

    // AMD Zen2+ L2 cache from DC misses
    constexpr uint64_t AMD_L2_ACCESSES_FROM_DC_MISSES = 0x0064;
    constexpr uint64_t AMD_L2_MISSES_FROM_DC_MISSES = 0x0164;

    // AMD Zen2+ branch mispredicts (retired)
    constexpr uint64_t AMD_BR_MISP_RETIRED = 0x00C3;

    // AMD Zen2+ DTLB miss
    constexpr uint64_t AMD_DTLB_MISS = 0xFF45;

    // AMD Zen Data Fabric UMC read/write
    constexpr uint64_t AMD_DF_UMC_READ_UMASK = 0x38;
    constexpr uint64_t AMD_DF_UMC_WRITE_UMASK = 0x3F;
    constexpr uint64_t AMD_DF_UMC_BASE_EVENT = 0x07;

    // AMD FP ops (Zen)
    constexpr uint64_t AMD_FP_RETD_SSE_AVX_OPS = 0x0003;

    // ARM PMUv3 (Cortex-A76+, Neoverse N1+)
    constexpr uint64_t ARM_ASE_SPEC = 0x0074;
    constexpr uint64_t ARM_VFP_SPEC = 0x0075;
    constexpr uint64_t ARM_SVE_INST_SPEC = 0x8006;
    constexpr uint64_t ARM_L1D_CACHE_REFILL = 0x0003;
    constexpr uint64_t ARM_L2D_CACHE_REFILL = 0x0017;
    constexpr uint64_t ARM_BR_MIS_PRED = 0x0010;
    constexpr uint64_t ARM_L1D_TLB_REFILL = 0x0005;
}

} // namespace
```

### 4.3 `Perf.h`

Direct `perf_event_open(2)` wrapper — we don't depend on the `perf-event` crate equivalent:

```cpp
namespace crucible::perf {

// Linux perf_event_attr (C header wrapper for clarity)
struct PerfEventConfig {
    uint32_t type;       // PERF_TYPE_HARDWARE, PERF_TYPE_HW_CACHE, PERF_TYPE_RAW, dynamic
    uint64_t config;     // PERF_COUNT_HW_*, or raw event code
    uint64_t sample_period;   // 0 = counting only; >0 = sampling
    uint32_t sample_type;     // PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME
    uint32_t read_format;     // PERF_FORMAT_GROUP | PERF_FORMAT_ID
    bool disabled_at_init = true;
    bool inherit = false;     // inherit to child threads
    bool pinned = false;
    bool exclusive = false;
    bool exclude_user = false;
    bool exclude_kernel = false;
    bool exclude_hv = false;
    bool mmap = false;        // enable mmap metadata page + ringbuf (for sampling)
    bool precise_ip = false;  // PEBS/IBS precise sampling
    // ... more options
};

class PerfCounter {
public:
    // Open the counter. pid=0 → current thread; pid=-1 + cpu≥0 + CAP_PERFMON → per-CPU.
    // cpu=-1 → any CPU (counts migrate with thread).
    // group_fd=-1 → standalone; group_fd=leader → add to group.
    [[nodiscard]] static std::expected<PerfCounter, PerfError>
        open(PerfEventConfig cfg, pid_t pid, int cpu, int group_fd) noexcept;

    ~PerfCounter() noexcept;
    PerfCounter(PerfCounter&&) noexcept;
    PerfCounter& operator=(PerfCounter&&) noexcept;
    PerfCounter(const PerfCounter&) = delete;
    PerfCounter& operator=(const PerfCounter&) = delete;

    int fd() const noexcept { return fd_; }
    const perf_event_mmap_page* mmap_page() const noexcept { return mmap_page_; }
    uint32_t read_format() const noexcept { return read_format_; }

    // Control via ioctl. Each is one syscall — use at init, never hot path.
    void enable() noexcept;
    void disable() noexcept;
    void reset() noexcept;

    // Fallback read path: costs a syscall, ~500 ns. DO NOT USE ON HOT PATH.
    // Only used when mmap/rdpmc path is unavailable (e.g., old kernel).
    [[nodiscard]] uint64_t read_via_syscall() noexcept;

    // Preferred read path: user-space via rdpmc + mmap seqlock. ~8 ns.
    // Requires CAP_PERFMON + perf_event_paranoid ≤ 2.
    [[nodiscard, gnu::always_inline]]
    uint64_t read_via_mmap() const noexcept {
        return perf_read_via_mmap(mmap_page_);
    }

private:
    int fd_ = -1;
    perf_event_mmap_page* mmap_page_ = nullptr;
    size_t mmap_size_ = 0;
    uint32_t read_format_ = 0;
};

// Group of counters — one leader + siblings, read atomically.
// On hot path: read each via rdpmc (no syscall) and combine in userspace.
// At init / shutdown: read via syscall (PERF_FORMAT_GROUP).
class PerfGroup {
public:
    [[nodiscard]] static std::expected<PerfGroup, PerfError>
        open(std::span<const PerfEventConfig> configs, pid_t pid, int cpu) noexcept;

    // Returns the leader's fd (for group_fd parameter when adding to another group).
    int leader_fd() const noexcept { return leader_fd_; }

    // Enable/disable all at once via ioctl(PERF_IOC_FLAG_GROUP).
    void enable() noexcept;
    void disable() noexcept;
    void reset() noexcept;

    // Hot-path read: iterate members, rdpmc each.
    template <size_t N>
    [[gnu::always_inline]] std::array<uint64_t, N> read_all() const noexcept {
        std::array<uint64_t, N> out;
        for (size_t i = 0; i < N && i < members_.size(); ++i) {
            out[i] = perf_read_via_mmap(members_[i].mmap_page());
        }
        return out;
    }

private:
    int leader_fd_ = -1;
    std::vector<PerfCounter> members_;
};

} // namespace
```

The `PerfGroup::open` is a one-time cost at thread init (~100 µs). After that, all reads go through `read_via_mmap` — zero syscalls.

### 4.4 `MmapSeqlock.h` — the load-bearing inline

The core primitive. Implements the user-space rdpmc protocol documented in the Linux kernel's `tools/perf/design.txt`:

```cpp
namespace crucible::perf {

// One raw read + the multiplexing timing that must be read atomically with it.
// Callers that need scaling compose raw + scale(time_enabled, time_running).
struct RawReading {
    uint64_t raw;           // offset + sign-extended PMC
    uint64_t time_enabled;  // ns the kernel was ever running the counter
    uint64_t time_running;  // ns the kernel actually had HW slot allocated
};

[[nodiscard, gnu::always_inline, gnu::flatten]]
inline RawReading perf_read_raw_via_mmap(const perf_event_mmap_page* pc) noexcept {
    // Seqlock retry — the kernel updates offset/index/time_* atomically w.r.t. lock.
    // In practice, one iteration; the do-while handles the rare mid-update race.
    uint32_t seq;
    uint64_t offset, te, tr;
    int64_t value;
    int32_t idx;
    uint16_t width;
    do {
        seq = __atomic_load_n(&pc->lock, __ATOMIC_ACQUIRE);
        __asm__ volatile("" ::: "memory");

        idx = pc->index;
        offset = pc->offset;
        te = pc->time_enabled;
        tr = pc->time_running;
        width = pc->pmc_width;

        // rdpmc only if the kernel says the counter is running on THIS cpu.
        // idx==0 → counter stopped or migrated; use offset only.
        if (idx != 0) {
            value = __builtin_ia32_rdpmc(idx - 1);
            // rdpmc returns truncated to pmc_width (48 on x86, 64 on ARM).
            // Sign-extend so negative deltas after last kernel store work out.
            value <<= (64 - width);
            value >>= (64 - width);
        } else {
            value = 0;
        }

        __asm__ volatile("" ::: "memory");
    } while (__atomic_load_n(&pc->lock, __ATOMIC_ACQUIRE) != seq);

    return {static_cast<uint64_t>(offset + value), te, tr};
}

// Scale raw reading for multiplexing. When the kernel time-shares the HW
// counter with another group (too many groups active for PMU slots), the
// counter only accumulates for a fraction of the window. Linearly extrapolate
// to the full window.
//
// Caller computes (delta_raw, delta_te, delta_tr) between two readings;
// we scale delta_raw by (delta_te / delta_tr). When tr == te (no multiplexing
// on this window), scale factor is 1.0.
[[nodiscard, gnu::always_inline]]
inline uint64_t scale_for_multiplexing(uint64_t delta_raw,
                                        uint64_t delta_te,
                                        uint64_t delta_tr) noexcept {
    if (delta_tr == 0) return 0;                // never ran — degenerate
    if (delta_tr >= delta_te) return delta_raw; // no multiplexing
    return static_cast<uint64_t>(
        static_cast<double>(delta_raw) * delta_te / delta_tr);
}

// Convenience for callers that don't care about exposing the raw timing:
[[nodiscard, gnu::always_inline, gnu::flatten]]
inline uint64_t perf_read_via_mmap(const perf_event_mmap_page* pc) noexcept {
    return perf_read_raw_via_mmap(pc).raw;
}

} // namespace
```

Region scopes store the `RawReading` at enter+exit, compute `delta_raw` and `(delta_te, delta_tr)` at report-time reconstruction, then `scale_for_multiplexing` to recover true counts. This matters: at full PMU saturation (6 groups on Zen3's 6 slots, overlapping via multiplexing), raw reads are ~50% of actual values. Scaling is the difference between "IPC = 1.2" and "IPC = 2.4" — an order-of-magnitude decision.

Key correctness points:

- The `lock` field in `perf_event_mmap_page` is an odd/even seqlock. Kernel increments twice (to odd, do update, to even) when modifying offset/index. Reader retries if it sees an odd value or if the value changed during the read.
- `rdpmc` only works if `index != 0`. On CPU migration, kernel sets `index = 0` and reschedules the counter; next `rdpmc` sees `index = 0` and uses only `offset`.
- Width is typically 48 bits on Intel, 48 on AMD Zen, 64 on some ARM. Sign-extension preserves the correct running total.
- `offset` is the kernel-tracked running total; `value` is the current PMC hardware register value. Sum = total count since counter enabled.

Cost: 2 atomic loads (3 ns each), 2 compiler barriers (free), 1 rdpmc (7 ns), 2 shifts + add = **~15 ns for one counter**. For a group of 6 counters, ~90 ns per full group read; but we only read 2 (cycles + instructions) per scope, so **~30 ns per scope**.

### 4.5 `DynamicPmu.h`

For PMUs that aren't in the fixed `PERF_TYPE_*` enum — AMD IBS (`ibs_op`), AMD Data Fabric (`amd_df`), RAPL (`power`), Intel uncore IMC (`uncore_imc_0`).

Each dynamic PMU exposes its type ID at `/sys/bus/event_source/devices/<name>/type` and its event encoding format at `/sys/bus/event_source/devices/<name>/format/*`. We parse these at startup:

```cpp
namespace crucible::perf::dynamic_pmu {

struct FieldSpec {
    std::string name;       // e.g., "event"
    uint8_t low_bit;        // e.g., 0
    uint8_t high_bit;       // e.g., 7
    uint8_t config_idx;     // 0 = config, 1 = config1, 2 = config2
};

struct PmuSpec {
    uint32_t type_id;
    std::unordered_map<std::string, FieldSpec> fields;
    std::unordered_map<std::string, uint64_t> named_events;  // from events/ dir
};

[[nodiscard]] std::expected<PmuSpec, std::string>
    discover(const char* pmu_name) noexcept;
// Reads /sys/bus/event_source/devices/<name>/{type, format/*, events/*}.
// Returns a spec usable to build perf_event_attr.

// Build an attr from (name, values) pairs.
[[nodiscard]] std::expected<perf_event_attr, std::string>
    build_attr(const PmuSpec& spec,
               std::span<const std::pair<std::string_view, uint64_t>> fields) noexcept;

} // namespace
```

Example:

```cpp
auto ibs = dynamic_pmu::discover("ibs_op").value();
auto attr = dynamic_pmu::build_attr(ibs, {{"cnt_ctl", 1}}).value();
// attr.type = ibs.type_id, attr.config encodes cnt_ctl=1
```

Discovery is one-time at startup, ~100 µs per PMU. After that, no `/sys` access.

### 4.6 `CacheAtomic.h`

Atomic `shared_ptr<const State>` pattern for hot-read data (used by HTTP server, cluster view, etc.):

```cpp
template <typename T>
class AtomicState {
public:
    AtomicState() : ptr_(nullptr) {}

    // Writer — expensive, rare (50 ms cadence)
    void store(std::shared_ptr<const T> new_state) noexcept {
        std::atomic_store_explicit(&ptr_, std::move(new_state), std::memory_order_release);
    }

    // Reader — fast, hot (~5 ns)
    std::shared_ptr<const T> load() const noexcept {
        return std::atomic_load_explicit(&ptr_, std::memory_order_acquire);
    }

private:
    std::shared_ptr<const T> ptr_;
};
```

---

## 5. User-space rdpmc — the killer feature

Enabling user-space rdpmc is the single biggest perf win in the entire design. Without it, every group-counter read is a syscall at 500–1000 ns. With it, every read is an instruction at 8 ns. **60–120× speedup on the hottest path.**

### 5.1 Prerequisites

Three settings, all controllable at Keeper startup:

1. **`/proc/sys/kernel/perf_event_paranoid ≤ 2`** (default on most distros). Allows CAP_PERFMON users to open hardware counters.
2. **`CAP_PERFMON`** capability on the Crucible binary. We set this via `setcap` in the build, same mechanism already used for CAP_BPF in `bench-caps` target.
3. **`attr.exclude_kernel = 0`** AND userspace has `CAP_PERFMON` — the kernel refuses `rdpmc` on kernel-space counters unprivileged.

If any of these fails, graceful fallback to `read(fd)` syscall path (~500 ns per group read). Fallback is logged clearly at startup; users will see it.

### 5.2 How the kernel enables user rdpmc

In `perf_event_open(2)`, the kernel sets `mmap_page->cap_user_rdpmc = 1` when:
- CAP_PERFMON is held
- `/proc/sys/kernel/perf_event_paranoid ≤ 2`
- The counter is not exclude_user (must include user-mode counting)

Check this at init:

```cpp
if (!mmap_page->cap_user_rdpmc) {
    // Not allowed — fall back to syscall path
    enable_fallback_mode();
}
```

### 5.3 CPU migration handling

Per-thread counters migrate with the thread across CPUs. When migration happens:

1. Kernel stops the counter on the old CPU
2. Writes `mmap_page->offset += last_pmc_value` (so the running total is preserved)
3. Sets `mmap_page->index = 0` (so rdpmc is skipped)
4. Reschedules counter on the new CPU
5. Sets `mmap_page->index` to the new CPU's PMC index

During migration (step 1–4), our seqlock-protected read sees `index = 0` and returns just the offset. After step 5, subsequent reads return `offset + new_pmc_value`. **No information loss; no need for explicit migration detection on our side.**

### 5.4 What if thread is suspended (off-CPU)?

When the thread is descheduled:
1. Kernel stops counter, adds last PMC value to offset, sets index=0
2. When rescheduled, kernel resets PMC and sets new index

Reading during off-CPU window returns just offset — correct (last known value, paused).

### 5.5 Group counter reads via rdpmc

Each counter in a group has its own `mmap_page` (one mmap per fd). To read the whole group we iterate:

```cpp
[[gnu::always_inline]]
std::array<uint64_t, 6> read_group_6() const noexcept {
    return {
        perf_read_via_mmap(cycles_mmap_),
        perf_read_via_mmap(insns_mmap_),
        perf_read_via_mmap(branches_mmap_),
        perf_read_via_mmap(br_miss_mmap_),
        perf_read_via_mmap(l1d_miss_mmap_),
        perf_read_via_mmap(llc_miss_mmap_),
    };
}
```

6 × ~15 ns = ~90 ns per group. Critically: all 6 reads happen on the same CPU (since the thread doesn't migrate between them), so the counters are consistent. The 6 reads aren't atomic with each other, but the kernel's offsets are — if migration happens between reads, subsequent reads pick up where the previous ones left off. Slight skew of ~100 ns between first and last counter in the group, but that's below our measurement noise floor.

For scopes that only need cycles + instructions (Tier C-lite), we read 2 counters × 15 ns = 30 ns — the full scope enter/exit cost including rdtsc and stores comes out to ~35 ns round-trip.

---

## 6. Counter classes — `include/crucible/perf/counters/`

Each header provides a typed wrapper around a `PerfGroup` configured for a specific measurement domain.

### 6.1 `Pipeline.h`

Generic CPU pipeline counters — works on Intel, AMD, ARM without modification:

```cpp
struct PipelineCounters {
    uint64_t cycles;
    uint64_t instructions;
    uint64_t branches;
    uint64_t branch_misses;
    uint64_t stalled_frontend;
    uint64_t stalled_backend;
    double ipc() const { return cycles > 0 ? double(instructions) / cycles : 0; }
    double branch_miss_rate() const {
        return branches > 0 ? double(branch_misses) / branches : 0;
    }
    double frontend_stall_pct() const {
        return cycles > 0 ? 100.0 * stalled_frontend / cycles : 0;
    }
    double backend_stall_pct() const {
        return cycles > 0 ? 100.0 * stalled_backend / cycles : 0;
    }
};

class PipelineGroup {
public:
    [[nodiscard]] static std::expected<PipelineGroup, std::string>
        open(pid_t pid = 0, int cpu = -1) noexcept;
    [[gnu::always_inline]]
    PipelineCounters read() const noexcept { return read_group_6(); }
private:
    PerfGroup group_;
};
```

Opens 6 PERF_TYPE_HARDWARE counters in one group. Read via `read_via_mmap` on each. Graceful fallback: stalled_frontend/backend may not be available on all cores (e.g., some Xeon D models); if so, that field reads 0 and we mark it as unavailable in metadata.

### 6.2 `Cache.h`

Cache hierarchy. Uses `PERF_TYPE_HW_CACHE` for L1D, LLC, DTLB, ITLB; raw events for L2:

```cpp
struct CacheCounters {
    uint64_t l1d_refs, l1d_misses;
    uint64_t l2_refs, l2_misses;      // vendor-specific raw
    uint64_t llc_refs, llc_misses;    // PERF_COUNT_HW_CACHE_LL
    uint64_t dtlb_refs, dtlb_misses;
    uint64_t itlb_refs, itlb_misses;
    double l1d_miss_rate() const { /* ... */ }
    double llc_miss_rate() const { /* ... */ }
    double dtlb_miss_rate() const { /* ... */ }
    // ...
};
```

Multiplexing consideration: 10 counters exceed Zen3's 6 GP PMU slots. Kernel time-shares them. We scale by `time_enabled/time_running` at report time. Alternative: split into two sub-groups (L1D+LLC in one group, DTLB+ITLB in another), opened on the same thread. Kernel schedules them alternately. Net result: 50% resolution on each but less multiplexing chaos.

Decision: split into two groups. L2 raw is added to the appropriate group based on vendor. This fits in two group-reads per scope = ~150 ns, too expensive for every scope. **Cache counters are only read at explicit `CRUCIBLE_PROFILE_SCOPE_CACHE("name")` — a separate, heavier macro.**

### 6.3 `Bandwidth.h`

Memory bandwidth via uncore PMUs. Dispatches by vendor:

```cpp
class BandwidthGroup {
public:
    struct Measurement {
        uint64_t read_bytes;
        uint64_t write_bytes;
        // Compute GB/s from (end - start) / duration_s
    };

    [[nodiscard]] static std::expected<BandwidthGroup, std::string>
        open_amd_df() noexcept;           // AMD amd_df dynamic PMU
    [[nodiscard]] static std::expected<BandwidthGroup, std::string>
        open_intel_imc() noexcept;        // Intel uncore_imc_0

    // ...
};
```

Uncore counters count for the whole socket, not per-thread. Only one Keeper on each socket should open them; gossip for cross-socket aggregation.

### 6.4 `Energy.h`

RAPL energy via `power` PMU. Read at periodic cadence only (once per second is enough); rates are computed as deltas:

```cpp
struct EnergyMeasurement {
    double pkg_joules;
    double dram_joules;
    double watts_pkg() const { /* ... */ }
    double watts_dram() const { /* ... */ }
};
```

### 6.5 `Simd.h`

FP/vector utilization — Intel uses 5 FP_ARITH variants; AMD uses FP_RETD_SSE_AVX_OPS (single); ARM uses ASE_SPEC/VFP_SPEC/SVE_INST_SPEC. Graceful: unknown vendors get empty counters.

### 6.6 `MemLatency.h`

This is the crown jewel for cache-hierarchy attribution. On AMD (user's Ryzen 5950x and later Zen) uses `ibs_op` with `SampleFlag::WEIGHT | SampleFlag::DATA_SRC`. On Intel (Skylake+) uses `MEM_LOAD_RETIRED` with PEBS:

```cpp
struct LoadLatencySample {
    uint64_t ip;
    uint32_t tid;
    uint64_t latency_cycles;     // WEIGHT — exact cycles the load stalled
    enum class CacheLevel : uint8_t { L1 = 0, LFB, L2, L3, RAM, Other } level;
    uint64_t ts_ns;
};

class LatencySampler {
public:
    [[nodiscard]] static std::expected<LatencySampler, std::string>
        open(uint64_t sample_period = 100000) noexcept;

    // Drain mmap ringbuf — zero syscalls
    [[nodiscard]] std::vector<LoadLatencySample> drain() noexcept;
};
```

Per-load latency distribution + cache-level attribution is structurally different from counter-based cache miss rates. With this: "37% of loads hit L1 at 4-cycle latency; 42% hit L2 at 14-cycle; 15% hit L3 at 40-cycle; 6% went to DRAM at 250-cycle; and here are the exact IPs responsible for the DRAM loads." This is the answer to "why is my memory hierarchy slow?" that generic counters never give.

On AMD IBS the skid is literally zero; the IP is precisely the faulting load. On Intel PEBS, skid is typically 1–4 instructions. Both are dramatically better than non-precise sampling.

---

## 7. Region engine — `include/crucible/perf/region/`

This is the user-facing API for Crucible developers. Wraps scopes with RAII, produces the per-thread event stream that gets reassembled offline into call trees and flamegraphs.

### 7.1 `ScopeTable.h`

Compile-time scope registration into a fixed BSS table. Zero runtime allocation. Zero mutex. Zero string comparison on hot path.

```cpp
namespace crucible::perf::region {

struct ScopeInfo {
    const char* name;           // static literal ptr, 8 B
    const char* file;            // __FILE__, 8 B
    const char* function;        // __PRETTY_FUNCTION__, 8 B
    uint32_t line;              // 4 B
    uint32_t name_hash;          // 4 B — for aggregation
    std::atomic<uint32_t> sample_mask;   // 4 B — adaptive throttle
    std::atomic<uint64_t> call_count;    // 8 B
    // Total: 40 B (aligned to 48 for cache-line-friendly iteration)
};

// Fixed-size BSS table. 64K entries × 48 B = 3 MB. Negligible footprint.
constexpr size_t kMaxScopes = 65536;
extern std::array<ScopeInfo, kMaxScopes> g_scope_table;
extern std::atomic<uint16_t> g_next_scope_id;

// Register a scope at program init (invoked via static initializer).
// Returns the u16 ID. Never fails until table full (after which returns 0xFFFF).
uint16_t register_scope(const char* name, const char* file, uint32_t line,
                        const char* function) noexcept;

} // namespace
```

The macro — with lazy registration to dodge static initialization order fiasco:

```cpp
// Compile-time FNV-1a hash of __FILE__, combined with per-file __COUNTER__,
// produces a globally-unique 32-bit key without any runtime string work.
constexpr uint32_t CRUCIBLE_PP_FNV1A(const char* s) {
    uint32_t h = 0x811c9dc5u;
    while (*s) h = (h ^ uint8_t(*s++)) * 0x01000193u;
    return h;
}

#define CRUCIBLE_PROFILE_SCOPE(name_literal)                                      \
    static constexpr uint32_t CRUCIBLE_PP_CAT(_csp_key_, __COUNTER__) =            \
        (::crucible::perf::region::CRUCIBLE_PP_FNV1A(__FILE__) & 0xFFFF0000u)      \
        | (__COUNTER__ & 0xFFFFu);                                                 \
    ::crucible::perf::region::RegionGuard CRUCIBLE_PP_CAT(_csp_g_, __COUNTER__){   \
        ::crucible::perf::region::lazy_register(                                   \
            CRUCIBLE_PP_CAT(_csp_key_, __COUNTER__),                               \
            name_literal, __FILE__, __LINE__, __PRETTY_FUNCTION__)                 \
    }

// In the region engine:
//
// Atomic registry keyed by the compile-time key. First call from any
// thread CASes the slot; others read the cached id. Amortized ~5 ns
// cold, ~1 ns warm (one atomic load + one array index).
//
// No static-initialization-order dependency: the registry is a POD
// zero-initialized in BSS. A static ctor somewhere calling a profiled
// function pre-main works correctly — first call into lazy_register
// stakes the slot.
[[gnu::hot]]
inline uint16_t lazy_register(uint32_t key, const char* name,
                               const char* file, uint32_t line,
                               const char* function) noexcept {
    // Hash table indexed by key, open-addressed, fixed capacity.
    // Capacity 2× the expected scope count so load stays < 0.5.
    size_t slot = key & (kScopeCap - 1);
    for (size_t probe = 0; probe < kScopeCap; ++probe, slot = (slot+1) & (kScopeCap-1)) {
        uint32_t expect = g_scope_key[slot].load(std::memory_order_acquire);
        if (expect == key) {                           // cache hit
            return g_scope_id[slot];
        }
        if (expect == 0) {                             // empty slot
            uint32_t zero = 0;
            if (g_scope_key[slot].compare_exchange_strong(
                    zero, key, std::memory_order_acq_rel)) {
                // We won the CAS; populate metadata and assign ID.
                uint16_t id = g_next_scope_id.fetch_add(1, std::memory_order_acq_rel);
                g_scope_table[id] = {name, file, function, line,
                                      CRUCIBLE_PP_FNV1A(name), 0, 0};
                g_scope_id[slot] = id;
                // Release-store so readers that saw key via acquire see id.
                std::atomic_thread_fence(std::memory_order_release);
                return id;
            }
            // Lost CAS → re-read and continue.
        }
    }
    return 0;  // table exhausted; scope logged but IDs collide (rare)
}
```

Why the old design was broken: `static constinit uint16_t _csp_id_ = 0xFFFF;` plus a struct-constructor initialization depends on C++ static initialization order — which is **unspecified across translation units**. A static constructor in `tu_a.cc` calling a profiled function in `tu_b.cc` before `tu_b.cc`'s scope initializers run would see `_csp_id_ == 0xFFFF` and emit junk. Lazy registration makes ordering irrelevant: the first thread that enters any scope races through CAS to stake its slot, all subsequent callers (including from the same scope) hit the fast path.

### 7.2 `ThreadLocal.h`

Per-thread state. All reads and writes on the hot path are to this TLS struct — no cross-thread sharing, no atomics needed on hot path.

```cpp
namespace crucible::perf::region {

// Ring buffer event — 48 B, two per cache line.
struct alignas(48) RingEvent {
    uint64_t enter_tsc;   // 8 B
    uint64_t exit_tsc;    // 8 B (written on scope exit)
    uint64_t exit_cycles; // 8 B — cumulative cycles at exit
    uint64_t exit_insns;  // 8 B — cumulative instructions at exit
    uint16_t name_id;      // 2 B
    uint8_t depth;         // 1 B
    uint8_t flags;         // 1 B
    uint32_t sense_delta_id; // 4 B — ID into sense-delta pool (optional)
    // total: 40 B; 8 B padding for 48 B alignment
};
static_assert(sizeof(RingEvent) == 48);

// Per-thread profile state. Cache-line-aligned; hot fields in first line.
struct alignas(64) ThreadProfile {
    // Cache line 0: hot state
    uint64_t next_idx;                          // 8 B — ring buffer head
    uint64_t ring_mask;                         // 8 B — capacity - 1
    RingEvent* ring;                             // 8 B — ring buffer base
    const perf_event_mmap_page* cycles_mmap;    // 8 B
    const perf_event_mmap_page* insns_mmap;     // 8 B
    const uint64_t* sense_hub;                   // 8 B — mmap'd BPF counter array
    uint32_t tid;                                // 4 B
    uint32_t depth;                              // 4 B — current scope depth
    uint64_t pcg32_state;                        // 8 B — sampling RNG

    // Cache line 1: cold state (touched only by reader thread)
    uint32_t cpu;                                // 4 B — last seen CPU
    uint32_t ring_capacity;                      // 4 B
    std::atomic<uint64_t> drain_cursor;         // 8 B — reader's read pointer
    uint64_t virtual_cycles_high;                // 8 B — 48 → 64-bit virtual hi bits
    uint64_t virtual_insns_high;                 // 8 B
    uint64_t last_raw_cycles;                    // 8 B — last reader-observed raw
    uint64_t last_raw_insns;                     // 8 B
    uint32_t wrap_count_cycles;                  // 4 B — instrumentation
    uint32_t wrap_count_insns;                   // 4 B
    uint8_t  pad_[16];                           // padding to 64 B
};
static_assert(sizeof(ThreadProfile) == 192);

// TLS pointer — lazy-initialized on first scope enter
extern thread_local ThreadProfile* g_tls;

// Initialize TLS for the calling thread. Called on thread creation.
void init_thread(uint32_t tid) noexcept;

// Shut down TLS for the calling thread. Called on thread exit.
// Flushes remaining events to the global reader queue.
void exit_thread() noexcept;

} // namespace
```

Ring buffer allocation: `mmap(NULL, ring_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)` with `MADV_HUGEPAGE` for 2 MB pages and `mbind(MPOL_PREFERRED, numa_node_of_cpu())` for NUMA locality. Typical size 64 KB (1365 events), or 2 MB (43690 events) for heavy-use threads.

### 7.2.1 Thread registry — how the reader finds producer rings

TLS is per-thread-only-visible by definition. The reader thread needs a way to enumerate all producer threads' ring buffers without taking a mutex on the hot path.

**Solution: a tombstoned atomic-slot registry.**

```cpp
struct ThreadRegistry {
    // Fixed-capacity array. 4096 slots is enough for any realistic Keeper
    // (16 cores × ~20 thread families × safety margin). Entries transition:
    //   nullptr → live ThreadProfile* → TOMBSTONE.
    // Reader iterates skipping both nullptr and TOMBSTONE entries.
    static constexpr size_t kCap = 4096;
    static constexpr ThreadProfile* TOMBSTONE =
        reinterpret_cast<ThreadProfile*>(uintptr_t(~0));

    alignas(64) std::array<std::atomic<ThreadProfile*>, kCap> slots;
    alignas(64) std::atomic<uint32_t> hint_next_free{0};

    // Called once at thread start. Stakes a slot via CAS.
    size_t register_thread(ThreadProfile* tp) noexcept {
        uint32_t start = hint_next_free.load(std::memory_order_relaxed);
        for (size_t i = 0; i < kCap; ++i) {
            size_t idx = (start + i) & (kCap - 1);
            ThreadProfile* expect = nullptr;
            if (slots[idx].compare_exchange_strong(
                    expect, tp, std::memory_order_acq_rel)) {
                hint_next_free.store(idx + 1, std::memory_order_relaxed);
                return idx;
            }
        }
        std::abort();  // registry full (shouldn't happen in practice)
    }

    // Called once at thread exit. Mark tombstone; reader GCs later.
    void retire_thread(size_t idx) noexcept {
        slots[idx].store(TOMBSTONE, std::memory_order_release);
    }

    // Reader walks the whole array, skipping nullptr and TOMBSTONE.
    // Occasional full sweep (every 60 s) compacts tombstones by
    // CAS-replacing them with nullptr and updating hint_next_free.
    template <typename F>
    void for_each_live(F&& fn) const noexcept {
        for (size_t i = 0; i < kCap; ++i) {
            ThreadProfile* tp = slots[i].load(std::memory_order_acquire);
            if (tp && tp != TOMBSTONE) fn(tp);
        }
    }
};

static ThreadRegistry g_thread_registry;
```

**Retirement protocol.** On `exit_thread`:
1. Producer drains its own remaining ring events into a reader-visible queue (one-shot SPSC with atomic producer head, read by reader thread).
2. Producer marks slot as TOMBSTONE (release store).
3. Reader, on next iteration, sees TOMBSTONE, flushes any residual data, zeros out the TLS struct.
4. Reader's 60-second sweep compacts tombstones back to nullptr so the registry doesn't grow unboundedly for pool-churn workloads.

**Cost.** Registration: ~50 ns CAS + cache-line acquire. Retirement: ~10 ns store. Reader iteration: O(kCap) worst case with branch-per-slot → ~4 µs for the full 4096-slot scan. At 10 Hz drain rate → 40 µs/sec reader overhead for iteration alone. Negligible.

**Race during drain.** While the reader is iterating, a producer may retire. The reader either sees the live pointer (and drains live data) or TOMBSTONE (and flushes the one-shot queue). Correct either way. The ThreadProfile memory is never freed until the reader explicitly releases it at compaction time, so loaded pointers remain valid for the duration of one reader iteration.

**Why not a lock-free linked list?** Simpler conceptually (no size cap) but has retire-safe-reclamation issues: a dying producer can't free its node while the reader might still hold a pointer to it. Deferred reclamation via hazard pointers or epoch-based reclamation adds complexity we don't need. The fixed-array tombstone design avoids this entirely — memory lifetime is tied to the registry's lifetime, and compaction is an explicit, non-racing reader-only operation.

### 7.3 `Region.h` — the macro + guard

```cpp
namespace crucible::perf::region {

struct RegionGuard {
    RingEvent* slot;
    uint64_t enter_tsc;
    uint16_t name_id;
    uint8_t depth;
    uint8_t flags;

    [[gnu::always_inline]] explicit RegionGuard(uint16_t id) noexcept {
        auto* tls = g_tls;
        if (__builtin_expect(!tls, 0)) {
            init_thread_lazy();
            tls = g_tls;
        }
        name_id = id;
        depth = static_cast<uint8_t>(tls->depth++);
        enter_tsc = crucible::perf::tsc::rdtsc();

        uint64_t idx = tls->next_idx++;
        slot = &tls->ring[idx & tls->ring_mask];
        slot->enter_tsc = enter_tsc;
        slot->name_id = id;
        slot->depth = depth;
        slot->flags = 0;  // filled at exit
    }

    [[gnu::always_inline]] ~RegionGuard() noexcept {
        auto* tls = g_tls;
        // Adaptive sampling decision — inline branchless
        uint32_t mask = crucible::perf::region::g_scope_table[name_id]
                            .sample_mask.load(std::memory_order_relaxed);
        uint32_t r = pcg32_next(tls);
        bool read_pmu = (r & mask) == 0;

        slot->exit_tsc = crucible::perf::tsc::rdtsc();
        if (__builtin_expect(read_pmu, 1)) {
            slot->exit_cycles = perf_read_via_mmap(tls->cycles_mmap);
            slot->exit_insns  = perf_read_via_mmap(tls->insns_mmap);
            slot->flags |= RING_FLAG_HAS_PMU;
        }
        tls->depth = depth;
    }

    RegionGuard(const RegionGuard&) = delete;
    RegionGuard(RegionGuard&&) = delete;
};

} // namespace
```

Cost breakdown:
- Enter: rdtsc (3 ns) + TLS-ptr deref (1 ns) + 3 stores (3 ns) = **~7 ns**
- Exit with PMU: rdtsc (3 ns) + 2 rdpmc (16 ns) + 4 stores (4 ns) + PCG32 (2 ns) = **~25 ns**
- Exit without PMU (sampled out): rdtsc + 1 store + PCG32 = **~6 ns**

For 1-in-4 sampling (default for hot scopes): amortized exit = (1/4)(25) + (3/4)(6) = ~11 ns average. Total enter+exit ≈ 18 ns amortized. Under our budget.

Adaptive throttle (§7.4) lowers the sample_mask when a scope fires > 1M/sec, so hot inner scopes automatically downgrade to TSC-only mode.

**`noexcept` is load-bearing.** `RegionGuard::~RegionGuard` is explicitly `noexcept`. Violating this would cause `std::terminate` on the rare path where a scope is destroyed during exception unwinding — which happens legitimately if user code throws through a profiled region. More importantly: `noexcept` lets the compiler skip emitting unwind tables for the destructor body, reducing `.eh_frame` bloat and keeping the destructor fully inlineable. No path in the destructor may throw — `perf_read_via_mmap` is `noexcept`, TLS pointer deref is `noexcept`, atomic loads are `noexcept`. Verified by `static_assert(noexcept(std::declval<RegionGuard>().~RegionGuard()))` in the test suite.

### 7.3.1 48-bit counter rollover handling

`rdpmc` returns the PMC register truncated to `pmc_width` bits — **48 bits on x86** (Intel and AMD), 64 bits on ARMv8.2+. At 5 GHz the cycles counter wraps every `2^48 / 5e9 ≈ 15.6 hours`. Over a long-running Keeper (days, weeks), this happens regularly.

The seqlock inline sign-extends a single reading correctly, but cross-reading arithmetic (`exit_raw - enter_raw`) breaks silently on wrap. Two approaches:

**Approach A: reader-side virtual accumulation.** The reader thread samples each thread's raw counters every 1 second. On monotonic decrease (wrap detected), bumps the per-thread `virtual_cycles_high` by `2^width`:

```cpp
void reader_sync_virtual_counters(ThreadProfile& tp) noexcept {
    RawReading r_cy = perf_read_raw_via_mmap(tp.cycles_mmap);
    RawReading r_in = perf_read_raw_via_mmap(tp.insns_mmap);

    if (r_cy.raw < tp.last_raw_cycles) {
        tp.virtual_cycles_high += (1ULL << 48);
        tp.wrap_count_cycles++;
    }
    if (r_in.raw < tp.last_raw_insns) {
        tp.virtual_insns_high += (1ULL << 48);
        tp.wrap_count_insns++;
    }

    tp.last_raw_cycles = r_cy.raw;
    tp.last_raw_insns  = r_in.raw;
}

uint64_t virtual_cycles(const ThreadProfile& tp, uint64_t raw) noexcept {
    // raw is sign-extended at read time; for positive values (typical)
    // we just add the virtual high.
    return tp.virtual_cycles_high + raw;
}
```

At 1 Hz sampling, wrap is guaranteed to be detected within one second of occurring, and a second-long window is vastly larger than any scope's measurement window. No information lost.

**Approach B: scope-local delta computation.** For scopes < 15 hours (all of them), enter_raw and exit_raw are within one wrap window. Compute `delta_raw = (exit_raw - enter_raw) & 0xFFFFFFFFFFFF` (48-bit mask, with wrap handled by two's-complement subtraction). Works without reader cooperation.

We use both: Approach B at scope exit for per-scope deltas (no cross-thread coordination needed), Approach A at report-time aggregation for absolute running totals.

**Invariant TSC check at startup.** `CPUID leaf 0x80000007 bit 8` must be set (invariant TSC). If not, TSC ticks at variable rate with P-state and our calibration is wrong. Log warning and fall back to `CLOCK_MONOTONIC` via vDSO (~15 ns per scope instead of 3 ns).

### 7.4 `Sample.h` — adaptive throttling, empirically tuned

**The thresholds below are starting estimates, not measured values.** The exact break-points where sampling must kick in depend on microarchitecture, cache behavior, and scope body cost. Real values must be derived from measured overhead on real Crucible workloads.

Starting estimates (to be revised):

| Call rate | `sample_mask` | Sampled fraction | Rationale |
|---|---|---|---|
| > 10 M/sec | 0xFFFF | 1 in 65 536 | Amortized overhead ~0.0004 ns/call |
| > 1 M/sec | 0x0FFF | 1 in 4 096 | Amortized overhead ~0.006 ns/call |
| > 100 K/sec | 0x00FF | 1 in 256 | Amortized overhead ~0.1 ns/call |
| > 10 K/sec | 0x001F | 1 in 32 | Amortized overhead ~0.8 ns/call |
| > 1 K/sec | 0x0007 | 1 in 8 | Amortized overhead ~3 ns/call |
| ≤ 1 K/sec | 0x0000 | every call | Full 23 ns (negligible aggregate) |

Implementation — a bg thread every 1 s walks `g_scope_table`, reads `call_count` deltas, and adjusts `sample_mask`:

```cpp
void adaptive_sampling_tick() noexcept {
    uint16_t nscopes = g_next_scope_id.load(std::memory_order_acquire);
    for (uint16_t id = 0; id < nscopes; ++id) {
        auto& info = g_scope_table[id];
        uint64_t count = info.call_count.exchange(0, std::memory_order_acq_rel);
        uint32_t target_mask = compute_mask(count, g_tuning_table);
        info.sample_mask.store(target_mask, std::memory_order_relaxed);
    }
}
```

**Tuning protocol — how the real thresholds get set:**

1. Build with `CRUCIBLE_PERF_MEASURE_OVERHEAD=1`. This enables per-scope dual timing: wall time with instrumentation vs without (measured by bracketing the scope body with additional rdtsc pairs outside the guard).
2. Run the Crucible bench suite + representative training workload (e.g., a ResNet step).
3. Log per-scope `(call_count, total_ns_with_guard, total_ns_without_guard)`.
4. Compute overhead ratio: `(total_with - total_without) / total_without`.
5. Pick thresholds so that no single scope exceeds 0.05% overhead; aggregate per-thread overhead stays under 0.5%.
6. Commit per-microarchitecture tuning tables to `include/crucible/perf/region/sample_tuning.h`:

```cpp
struct TuningTable {
    std::array<std::pair<uint64_t, uint32_t>, 8> thresholds;
    // Pairs of (call_rate_threshold, sample_mask)
};

constexpr TuningTable kTuning_Zen3 = {{
    { 10'000'000, 0xFFFF }, { 1'000'000, 0x0FFF }, { 100'000, 0x00FF },
    { 10'000, 0x001F }, { 1'000, 0x0007 }, { 0, 0x0000 },
}};
constexpr TuningTable kTuning_Zen5 = {{ /* different scope costs */ }};
constexpr TuningTable kTuning_SapphireRapids = {{ /* ... */ }};

const TuningTable& select_tuning_table(const CpuInfo& info) noexcept;
```

New microarchitectures added as they enter the fleet. The initial table is the "best guess" from benchmark data; we refresh every few releases as new data arrives.

**Safety valve: watchdog throttle.** If per-scope overhead exceeds 2% for > 10 seconds despite the adaptive mask, the watchdog unilaterally sets the mask to `0xFFFFFFFF` (sample effectively nothing), logs a warning with the scope's file:line, and emits a health signal. Prevents a misconfigured scope (e.g., user accidentally adds a scope inside an inner loop) from tanking throughput.

### 7.5 `Reconstruct.h` — offline tree building

Reader thread drains each thread's ring buffer every 100 ms, runs the reconstruction algorithm:

```cpp
struct RegionNode {
    uint16_t name_id;
    uint64_t call_count;
    uint64_t total_wall_ns;   // sum of exit-enter TSC deltas (scaled)
    uint64_t total_cycles;     // sum of delta_cycles after multiplex scaling
    uint64_t total_insns;
    uint64_t max_wall_ns;
    std::vector<std::unique_ptr<RegionNode>> children;
};

// Walk events in enter_tsc order. Events with depth N are children of the
// currently-active scope at depth N-1. Maintain a stack; on each event:
//   - If depth increased: push onto stack
//   - If depth decreased: pop until we match
//   - Accumulate wall/cycles/insns into the node
std::unique_ptr<RegionNode> reconstruct_tree(
    std::span<const RingEvent> events) noexcept;
```

Concrete pseudocode:

```cpp
std::unique_ptr<RegionNode> reconstruct_tree(
        std::span<const RingEvent> events) noexcept {
    auto root = std::make_unique<RegionNode>();
    std::vector<RegionNode*> active;  // stack of currently-open scopes
    active.reserve(32);                // reasonable depth limit
    active.push_back(root.get());

    // Events must be in enter_tsc order; if producer ring is FIFO this holds.
    // Sort defensively in case events were interleaved across drain windows.
    for (const auto& ev : events) {
        // Pop until stack depth matches ev.depth + 1 (ev is child of top).
        while (active.size() > ev.depth + 1u) {
            active.pop_back();
        }

        RegionNode* parent = active.back();
        RegionNode* child = nullptr;
        for (auto& c : parent->children) {
            if (c->name_id == ev.name_id) { child = c.get(); break; }
        }
        if (!child) {
            parent->children.push_back(std::make_unique<RegionNode>());
            child = parent->children.back().get();
            child->name_id = ev.name_id;
        }
        active.push_back(child);

        uint64_t wall_ns = tsc_to_ns(ev.exit_tsc - ev.enter_tsc);
        uint64_t cycles_delta = 0, insns_delta = 0;
        if (ev.flags & RING_FLAG_HAS_PMU) {
            // 48-bit wrap-safe subtraction (Approach B from §7.3.1).
            constexpr uint64_t MASK48 = (1ULL << 48) - 1;
            cycles_delta = (ev.exit_cycles - ev.enter_cycles) & MASK48;
            insns_delta  = (ev.exit_insns  - ev.enter_insns)  & MASK48;
            // Multiplex-scale if time_enabled/time_running differ.
            cycles_delta = scale_for_multiplexing(cycles_delta,
                               ev.delta_time_enabled, ev.delta_time_running);
            insns_delta = scale_for_multiplexing(insns_delta,
                               ev.delta_time_enabled, ev.delta_time_running);
        }
        child->call_count++;
        child->total_wall_ns += wall_ns;
        child->total_cycles  += cycles_delta;
        child->total_insns   += insns_delta;
        if (wall_ns > child->max_wall_ns) child->max_wall_ns = wall_ns;
    }
    // `active` still holds the final path; the guards that haven't exited
    // yet will show up in a later drain window.
    return root;
}
```

**Merging across threads.** Each thread produces its own tree. The reader builds a cluster-level tree by merging: same `name_id` at the same depth → merge counts and child lists. Identical `(name_id, path_of_parent_name_ids)` tuples are treated as the same logical scope regardless of which thread produced them.

Cost: O(N) over ring events, ~100 ns per event. At 10 Hz × 10 K events/drain = 1 ms/sec = 0.1% overhead on the reader thread.

### 7.6 Three scope tiers

The macro expands to one of three variants based on user need:

```cpp
// Full attribution: TSC + 2 rdpmc + adaptive sample. 18 ns amortized.
CRUCIBLE_PROFILE_SCOPE("gemm_m256n256k64")

// Wall-time only: TSC, no PMU. 6 ns enter+exit. For very hot loops where
// IPC isn't needed (e.g., per-op TraceRing drain).
CRUCIBLE_PROFILE_SCOPE_WALL("trace_drain_iter")

// Full tier including cache counters (8 more counters, separate group): 60 ns.
// Only for cold paths — compile phases, Cipher tier transitions.
CRUCIBLE_PROFILE_SCOPE_CACHE("forge_phase_E_lower")

// Batch variant — one enter/exit pair for a whole iteration count. The
// measurement is amortized over `count` iterations; per-iter IPC is
// reported as (total_cycles / count) / (total_insns / count). For very
// hot inner loops: a 23 ns pair over N=10,000 iterations is 2 ps/iter.
CRUCIBLE_PROFILE_BATCH("hot_inner_loop", count)
```

A compile-time define `CRUCIBLE_PERF_MODE=minimal|standard|rich` controls the default tier when an unqualified `CRUCIBLE_PROFILE_SCOPE` is used. In production, most Keepers run `standard` (PMU only) for line-level attribution; profiling runs use `rich` (cache too).

### 7.7 Decision table — which scope tier to use

| Scope fires at... | Use | Rationale |
|---|---|---|
| > 1 M/sec (hot inner loop) | `CRUCIBLE_PERF_SAMPLE_REGION` (IBS/PEBS) | Zero instrumentation per iter; hardware sampling attributes |
| 100 K – 1 M/sec with many iters | `CRUCIBLE_PROFILE_BATCH` around the outer loop | Amortize one enter/exit across N iters |
| 100 K – 1 M/sec | `CRUCIBLE_PROFILE_SCOPE_WALL` (6 ns) | Wall time only; BPF captures PMU via sampling |
| 10 K – 100 K/sec | default `CRUCIBLE_PROFILE_SCOPE` (23 ns) | Full IPC + per-scope attribution; within budget |
| < 10 K/sec | default or `_CACHE` (60 ns) | Can afford richer measurement |
| Once per iteration or less | `CRUCIBLE_PROFILE_SCOPE_CACHE` | Cache counters attribute well at this granularity |
| Compile-time code (Forge phases, Mimic backends) | default | Compile runs in ms; any overhead negligible |
| User Python scope via pybind11 | `CRUCIBLE_USER_SCOPE` | ~100 ns pybind11 crossing dominates anyway |
| User custom data loader inner loop | `_WALL` or `_BATCH` | Match the dispatch-path guideline |
| Cipher tier transition (promote/evict) | `_CACHE` | Rare (per-MB of data), cache behavior is the point |

Rule of thumb: **if the scope fires more often than once per 100 µs of real work, pick `_WALL`, `_BATCH`, or IBS-SAMPLE.** Hot inner loops are observed via BPF sampling; call-tree attribution comes from the parent scope.

### 7.8 Overhead characterization and compensation — accurate IPC in hot loops

The instrumentation has cost. For scopes with ~100 µs bodies that cost is negligible; for ~100 ns bodies it's 20% of the measurement; for ~10 ns bodies it dominates. The challenge: *make hottest-loop IPC/L1 data accurate by measuring and subtracting the measurement's own overhead.*

You can't measure your own measurement cost directly (the measurement is the variable). But you can stack four complementary techniques that together give α (per-scope overhead) with ~2% accuracy in real workloads.

#### 7.8.1 CI null-scope calibration — establishes α per microarchitecture

A dedicated micro-benchmark (`bench/bench_scope_overhead.cpp`) measures **empty-body** scopes on every supported CPU microarch. Committed to the repo as a lookup table; refreshed when hardware changes.

```cpp
// Simplified structure — full code in bench_scope_overhead.cpp

template <auto ScopeMacro>
double measure_empty_scope_ns(size_t iterations) {
    auto t0 = rdtsc();
    for (size_t i = 0; i < iterations; i++) {
        ScopeMacro("__null__");  // expands to whichever tier we're measuring
    }
    auto t1 = rdtsc();
    return tsc_to_ns(t1 - t0) / iterations;
}

int main() {
    constexpr size_t N = 100'000'000;  // amortize rdtsc overhead itself
    double wall  = measure_empty_scope_ns<CRUCIBLE_PROFILE_SCOPE_WALL >(N);
    double full  = measure_empty_scope_ns<CRUCIBLE_PROFILE_SCOPE      >(N);
    double cache = measure_empty_scope_ns<CRUCIBLE_PROFILE_SCOPE_CACHE>(N);
    double rdpmc = measure_rdpmc_bare(N);

    // Write overhead_tuning.h for this microarch
    emit_tuning_header(detect_microarch(), {wall, full, cache, rdpmc});
}
```

Result committed as:

```cpp
// include/crucible/perf/region/overhead_tuning.h — generated, per microarch
struct ScopeOverhead {
    double wall_ns;           // empty _WALL enter+exit
    double full_ns;           // empty full scope (rdtsc + 2 rdpmc)
    double cache_ns;          // empty _CACHE (8 more counters)
    double rdpmc_bare_ns;     // single rdpmc in isolation
    uint64_t full_cycles;     // cycles consumed by the measurement
    uint64_t full_insns;      // instructions retired by the measurement
};

constexpr ScopeOverhead kOverhead_Zen3 = { 5.8, 22.4, 61.1, 7.9, 104, 48 };
constexpr ScopeOverhead kOverhead_Zen4 = { 5.2, 19.8, 54.3, 6.8, 92,  44 };
constexpr ScopeOverhead kOverhead_Zen5 = { 4.8, 17.6, 49.2, 6.1, 81,  42 };
constexpr ScopeOverhead kOverhead_SapphireRapids = { 5.0, 20.1, 55.7, 7.2, 94, 46 };
constexpr ScopeOverhead kOverhead_AlderLake_P = { 5.3, 20.8, 57.0, 7.4, 96, 47 };
constexpr ScopeOverhead kOverhead_AlderLake_E = { 6.8, 26.1, 68.9, 9.1, 120, 58 };
// ... etc. for every microarch in the fleet ...

const ScopeOverhead& select_overhead(const CpuInfo& info) noexcept;
```

A CI job `perf_overhead_calibrate.yml` runs across the fleet's microarchs (self-hosted runners on real hardware) and commits the table. **Baseline accuracy: ~90% of actual** — limited by cache state differences between calibration and real use, and by branch predictor state.

#### 7.8.2 Runtime p1/p2 differencing — self-compensating online

CI α is good but not perfect. The actual per-scope cost depends on cache state, neighboring code, thermal conditions. The cleanest runtime correction uses **period-1 vs period-2 alternation**.

The key insight: if half the calls pay the overhead and half don't, the difference is exactly the overhead.

```
Window A: sample_mask = 0 (period=1 — every call reads PMU)
    measured_cycles_A = real_cycles + N × α

Window B: sample_mask = 0x1 (period=2 — half calls read PMU)
    measured_cycles_B = real_cycles + (N/2) × α

Solving:
    α          = 2 × (measured_A - measured_B) / N
    real_cycles = 2 × measured_B - measured_A
```

Alternate windows every ~1 second. At each bucket boundary, solve the linear system and emit:
- `alpha_measured_ns` — current α from live measurement
- `real_cycles` — measurement with α subtracted
- `alpha_uncertainty_ns` — standard error from Mann-Whitney over multiple windows

Report format in JSON:

```json
{
    "scope": "build_trace/phase2_main_loop",
    "call_count": 120000,
    "measured_cycles_per_call": 8450.3,
    "measured_ipc": 2.41,
    "alpha_ns": 22.6,
    "real_cycles_per_call": 8380.1,
    "real_ipc": 2.43,
    "uncertainty_alpha_ns": 0.4,
    "uncertainty_ipc": 0.01
}
```

Dashboard shows real values prominently, measurement + uncertainty below in smaller type.

**Implementation:** the adaptive_sampling_tick bg thread, in addition to its rate-based throttling logic, rotates the calibration schedule:

```cpp
void adaptive_sampling_tick() noexcept {
    uint32_t epoch = g_epoch_sec.fetch_add(1);
    for (uint16_t id = 0; id < g_next_scope_id; ++id) {
        auto& info = g_scope_table[id];
        if (!overhead_calibration_enabled(info)) continue;
        // Alternate scope's sample_mask between 0 and 0x1 each second.
        // Over a 10-second window: 5 seconds of p1 data + 5 seconds of p2.
        info.sample_mask.store(epoch & 1 ? 0x1 : 0, std::memory_order_relaxed);
    }
}
```

Cost: scope runs at half-instrumentation half the time. If α = 23 ns and scope body is 200 ns, measured overhead drops from 10% to 7.5%. Acceptable. Big wins for scopes where overhead was marginal to begin with.

**Stability gate:** p1/p2 differencing only produces reliable α when the scope body is stable across windows (same cache behavior, same branch pattern). For noisy scopes (p99/p50 > 3), fall back to CI α and display an `unstable` tag.

#### 7.8.3 Batch macro for the hottest loops

For loops whose body is < 5× α, individual instrumentation is pointless (noise dominates). Instead, instrument the batch:

```cpp
// include/crucible/perf/region/Region.h

class BatchGuard {
public:
    [[gnu::always_inline]] BatchGuard(uint16_t id, size_t count) noexcept
        : id_(id), count_(count) {
        enter_tsc_    = rdtsc();
        enter_cycles_ = perf_read_via_mmap(tls_cycles_mmap());
        enter_insns_  = perf_read_via_mmap(tls_insns_mmap());
    }

    [[gnu::always_inline]] ~BatchGuard() noexcept {
        uint64_t exit_tsc    = rdtsc();
        uint64_t exit_cycles = perf_read_via_mmap(tls_cycles_mmap());
        uint64_t exit_insns  = perf_read_via_mmap(tls_insns_mmap());

        uint64_t wall_ns     = tsc_to_ns(exit_tsc - enter_tsc_);
        uint64_t delta_cyc   = (exit_cycles - enter_cycles_) & MASK48;
        uint64_t delta_ins   = (exit_insns - enter_insns_) & MASK48;

        // Divide by count for per-iter stats; keep totals for attribution.
        auto* tls = g_tls;
        auto* slot = &tls->ring[tls->next_idx++ & tls->ring_mask];
        slot->name_id = id_;
        slot->enter_tsc = enter_tsc_;
        slot->exit_tsc  = exit_tsc;
        slot->exit_cycles = exit_cycles;
        slot->exit_insns  = exit_insns;
        slot->flags = RING_FLAG_HAS_PMU | RING_FLAG_BATCH;
        slot->batch_count = count_;   // new field; total = 48B ring event still
    }

private:
    uint16_t id_;
    size_t   count_;
    uint64_t enter_tsc_, enter_cycles_, enter_insns_;
};

#define CRUCIBLE_PROFILE_BATCH(name, count_expr) \
    ::crucible::perf::region::BatchGuard \
        CRUCIBLE_PP_CAT(_bg_, __COUNTER__){ \
            ::crucible::perf::region::lazy_register( \
                compile_time_key, name, __FILE__, __LINE__, __PRETTY_FUNCTION__), \
            static_cast<size_t>(count_expr) \
        }
```

Usage:

```cpp
void hot_kernel(float* a, float* b, size_t N) {
    CRUCIBLE_PROFILE_BATCH("hot_kernel/inner", N);
    for (size_t i = 0; i < N; i++) {
        a[i] = a[i] * b[i];  // zero instrumentation per iter
    }
}
```

Per-iter overhead: 23 ns / N. For N=1000, that's 23 ps — far below DRAM access latency. Total distortion: `overhead / real_per_iter`. For 10 ns per-iter body and N=1000: 23 ps / 10 ns = 0.23% — well within measurement noise.

Reconstruct.h knows about the `RING_FLAG_BATCH` flag: when set, the node's `call_count` is bumped by `batch_count` not 1; per-call metrics are derived as `total / batch_count`.

Caveat: you lose per-iteration variance. You see mean IPC and mean cycles over the batch. For "why is this loop slow" this is the right level anyway.

#### 7.8.4 IBS / PEBS region markers — hardware attribution, zero instrumentation

For the very hottest code — inner GEMM loops, dispatch-path tail bodies — even batch overhead (≥ 23 ns total per loop) matters. Use hardware-assisted sampling that doesn't touch the retirement pipeline at all.

AMD IBS-Op: every N retired micro-ops, hardware captures `(IP, latency, cache_level, operation_type)` and delivers via NMI to a BPF perf_event program, which writes to our mmap'd ringbuf. The profiled code pays **zero per-iteration cost**. IBS reduces IPC by ~2% globally (sampling overhead in the microcode) but doesn't distort any single loop.

```cpp
// include/crucible/perf/region/HwSample.h

class HwSampleRegion {
public:
    [[gnu::always_inline]] HwSampleRegion(uint16_t id, IbsPeriod period = 10'000) noexcept
        : id_(id) {
        enter_tsc_     = rdtsc();
        enter_ringbuf_ = tls_pmu_ringbuf_head();
        enter_period_  = ibs_set_period(period);  // ~10 ns via mmap'd PMU config page
    }

    [[gnu::always_inline]] ~HwSampleRegion() noexcept {
        ibs_set_period(enter_period_);  // restore prior period
        uint64_t exit_tsc = rdtsc();
        uint64_t exit_ringbuf = tls_pmu_ringbuf_head();

        auto* tls = g_tls;
        auto* slot = &tls->ring[tls->next_idx++ & tls->ring_mask];
        slot->name_id = id_;
        slot->enter_tsc = enter_tsc_;
        slot->exit_tsc  = exit_tsc;
        slot->ringbuf_range_begin = enter_ringbuf_;
        slot->ringbuf_range_end = exit_ringbuf;
        slot->flags = RING_FLAG_HW_SAMPLE;
    }

private:
    uint16_t id_;
    uint64_t enter_tsc_;
    uint64_t enter_ringbuf_;
    IbsPeriod enter_period_;
};

#define CRUCIBLE_PERF_SAMPLE_REGION(name, period) \
    ::crucible::perf::region::HwSampleRegion \
        CRUCIBLE_PP_CAT(_hs_, __COUNTER__){ \
            ::crucible::perf::region::lazy_register(/*...*/), period \
        }
```

At report time, reconstruction walks the BPF ringbuf range [enter_ringbuf_, exit_ringbuf_) — all IBS samples captured during that region. Aggregates by source line, produces per-line attribution.

**Statistics:** at period=10K and a hot loop doing 10⁹ µops/sec, we get 100K samples/sec. Over a 1 ms region: ~100 samples. Standard error on IPC estimate: ~3%. Good enough for "which line is hot."

**Intel equivalent:** `MEM_LOAD_RETIRED.L1_MISS` with `precise_ip=2` (PEBS). Similar mechanism, 1-instruction skid (worse than IBS's zero skid but still fine for line attribution).

**Cost of enter/exit:** ~10 ns total (2 rdtsc + 2 ringbuf head reads + 2 period-change writes to PMU config page). No per-iteration cost inside the region.

#### 7.8.5 Protocol: which technique, in what order

For any hot-loop Crucible scope, the decision tree:

1. **If body > 250 ns (> 10× α):** plain `CRUCIBLE_PROFILE_SCOPE`. Runtime p1/p2 differencing applies automatically. Overhead < 10% of measurement, compensation brings it to < 1%.

2. **If body 50–250 ns (2–10× α):** plain scope + runtime p1/p2 differencing is required. The reported `real_cycles` column is the number to trust, not `measured_cycles`. Uncertainty visible.

3. **If body 20–50 ns (< 2× α) AND iteration count is known:** use `CRUCIBLE_PROFILE_BATCH`. Single measurement, divided by batch count. Accuracy ~1% regardless of body size.

4. **If body < 20 ns OR iteration count is dynamic OR you need per-iteration variance:** `CRUCIBLE_PERF_SAMPLE_REGION` with IBS (AMD) or PEBS (Intel). Hardware attribution, statistical IPC from sample density.

5. **If body is < 5 ns AND you need absolutely accurate IPC:** you can't. Physical limit is rdpmc latency (~8 ns). Use IBS globally for the whole bench run and read aggregate.

#### 7.8.6 Dashboard display of overhead — always visible

Every scope entry in the dashboard shows three columns by default:

```
┌──────────────────────────────────┬────────────┬──────────┬─────────┐
│ scope                             │ measured   │ real±σ   │ α±σ      │
├──────────────────────────────────┼────────────┼──────────┼─────────┤
│ Vigil::dispatch_op                │ 5.2 ns/call│ 4.8±0.3  │ 0.4±0.1 │
│ phase2_main_loop                  │ 1305 µs    │ 1299±4   │ 6±2 µs  │
│ Forge::phase_E_lower              │ 2.1 ms     │ 2.05±0.01│ 50±10µs │
│ Mimic::nv::compile_kernel (cold)  │ 847 ms     │ 847±0    │ 0       │
└──────────────────────────────────┴────────────┴──────────┴─────────┘
```

For scopes where measured − real > 3σ of real, the row is highlighted as "compensation significant" — user sees at a glance where the uncorrected numbers would mislead.

For batch-instrumented scopes, the display shows per-iter column:

```
┌──────────────────────────────────┬────────────┬────────────┬─────────┐
│ scope                             │ per-batch  │ per-iter   │ IPC     │
├──────────────────────────────────┼────────────┼────────────┼─────────┤
│ hot_kernel/inner (N=10000)        │ 123 µs     │ 12.3 ns    │ 3.21    │
└──────────────────────────────────┴────────────┴────────────┴─────────┘
```

IPC from batch is computed from the total delta_cycles / delta_insns — the batch macro preserves these totals.

#### 7.8.7 CI gate — α drift protection

CI runs the null-scope benchmark on every PR as part of `bench-ci.yml`. If α regresses by > 10% vs the committed tuning table:

- PR marked with `perf-regression` label
- CI job fails with clear diff showing which tier's α increased
- Forces the author to investigate (did they add work to the scope implementation?) or explicitly re-calibrate (expected change from a compiler upgrade)

Prevents silent measurement drift. Every committed α value is defensible against hardware-level measurement on committed code.

#### 7.8.8 Summary of accuracy bounds

| Scope body size | Technique | Accuracy of reported per-iter IPC |
|---|---|---|
| > 250 ns | plain scope | 99%+ |
| 50–250 ns | plain scope + p1/p2 | 98% |
| 20–50 ns | `_BATCH` (N ≥ 100) | 99% |
| 5–20 ns | `_BATCH` (N ≥ 10K) | 98% |
| < 5 ns | IBS/PEBS region | Statistical; ±3% with 100+ samples |

In all cases, the dashboard reports both `measured` and `real` (after α subtraction) with uncertainty bars. Users see the gap between raw and corrected numbers and can reason about whether compensation affects their conclusion.

---

## 8. BPF consumer — `include/crucible/perf/bpf_consumer/`

The BPF programs are already in `include/crucible/perf/bpf/`. This layer is the C++ userspace consumer.

### 8.1 `Loader.h`

libbpf wrapper. Loads each BPF program independently with graceful fallback:

```cpp
class BpfProfiler {
public:
    // Loads all 7 programs (sense_hub + 6 stack-traced) with PID filter.
    // Returns the number successfully loaded (0 = total failure).
    static BpfProfiler& instance();

    int active_programs() const { return active_count_; }

    // Accessor for each loaded program's maps
    const uint64_t* sense_hub_counters() const { return sense_hub_ptr_; }
    ringbuf::Reader* sched_ringbuf();
    ringbuf::Reader* syscall_ringbuf();
    ringbuf::Reader* lock_ringbuf();
    ringbuf::Reader* pmu_ringbuf();

    // Drain all ringbufs via mmap volatile reads (zero syscalls)
    void drain_all() noexcept;
    //  ...
};
```

BPF programs are compiled at build time via `clang -target bpf -O2 -g` and embedded as `.bpf.o` bytes via `xxd -i` (same pipeline already in `bench/CMakeLists.txt`). libbpf loads from memory — no file I/O at runtime.

### 8.2 Move from hash maps to mmap'd ringbufs

**Critical design change from symbiotic.** Symbiotic's sched_switch / syscall_latency / lock_contention programs use BPF_MAP_TYPE_HASH maps aggregated in kernel, drained via `bpf_map_lookup_batch` — costs ~100 µs per 10 K entries, which is a syscall.

We change the BPF side to use **BPF_MAP_TYPE_RINGBUF** (Linux 5.8+) for all event emission. Each event is a 24–32 B record written via `bpf_ringbuf_reserve` + `bpf_ringbuf_submit`. Userspace mmaps the ringbuf via `bpf_map__mmap` and reads via volatile loads:

```
[sched_switch event]  → ringbuf → mmap → userspace aggregates in RAM
[syscall_exit event]  → ringbuf → mmap → userspace aggregates in RAM
[lock_exit event]     → ringbuf → mmap → userspace aggregates in RAM
[pmu_overflow event]  → ringbuf (or sense_hub array) → mmap → userspace
```

Userspace-side aggregation is cheap (hashmap insert = ~20 ns). Total drain cost: O(N_events) × 20 ns. At 100 K events/sec × 20 ns = 2 ms/sec = 0.2% overhead on reader thread. **No syscalls.**

Ringbuf size: 16 MB per program × 4 programs = 64 MB total. Fits in RAM. Drain at 100 Hz = every 10 ms — events buffered for ~100 ms average, plenty of headroom before overflow.

### 8.3 `SenseMmap.h`

Consumer of the 96-counter sense_hub. Uses AVX2 gather for hot subset:

```cpp
class SenseMmap {
public:
    // Initialize by mmapping the sense_hub BPF array
    static std::expected<SenseMmap, std::string> attach() noexcept;

    // Read all 96 counters via 24 × vmovdqa ymm (24 × 1 cycle = ~8 ns)
    [[gnu::always_inline, gnu::target("avx2")]]
    std::array<uint64_t, 96> read_all() const noexcept;

    // Read the 8 counters most-used for region snapshots via one vgatherqpd
    [[gnu::always_inline, gnu::target("avx2")]]
    std::array<uint64_t, 8> read_region_subset() const noexcept {
        // Indices 17, 18, 20, 21, 26, 11, 12, 54
        // (minor pgflt, major pgflt, ctx_vol, ctx_invol, futex_wait_ns,
        //  io_read_bytes, io_write_bytes, disk_io_latency_ns)
        __m256i idx = _mm256_set_epi32(54, 12, 11, 26, 21, 20, 18, 17);
        __m256i lo = _mm256_i32gather_epi64(
            reinterpret_cast<const long long*>(ptr_),
            _mm256_castsi256_si128(idx), 8);
        __m256i hi = _mm256_i32gather_epi64(
            reinterpret_cast<const long long*>(ptr_),
            _mm256_extracti128_si256(idx, 1), 8);
        // Store to output array...
    }

private:
    const uint64_t* ptr_;
    size_t len_;
};
```

Read cost for the 8-counter subset: **~5 ns**. Region guards invoke `read_region_subset()` at enter and exit, compute delta, store to event. Adds ~10 ns to scope overhead; only for scopes that opt in via `CRUCIBLE_PROFILE_SCOPE_WITH_SENSES`.

### 8.4 `RingbufReader.h`

Generic zero-syscall ringbuf consumer:

```cpp
class RingbufReader {
public:
    explicit RingbufReader(const bpf_ringbuf* rb);

    // Drain all available events. Zero syscalls — reads via mmap'd producer/consumer
    // cursors and direct volatile loads from the data region.
    template <typename F>
    size_t drain(F&& handler) noexcept {
        uint64_t prod_pos = __atomic_load_n(producer_pos_, __ATOMIC_ACQUIRE);
        uint64_t cons_pos = __atomic_load_n(consumer_pos_, __ATOMIC_RELAXED);
        size_t count = 0;
        while (cons_pos < prod_pos) {
            uint64_t hdr_offset = cons_pos & mask_;
            volatile uint32_t len_flags = *reinterpret_cast<volatile uint32_t*>(
                data_ + hdr_offset);
            if (len_flags & BPF_RINGBUF_BUSY_BIT) break;  // producer still writing
            uint32_t record_len = (len_flags & BPF_RINGBUF_LEN_MASK);
            if (!(len_flags & BPF_RINGBUF_DISCARD_BIT)) {
                handler(data_ + hdr_offset + 8, record_len);
                ++count;
            }
            cons_pos += round_up_8(record_len + 8);
        }
        __atomic_store_n(consumer_pos_, cons_pos, __ATOMIC_RELEASE);
        return count;
    }

private:
    uint8_t* data_;
    uint64_t mask_;
    uint64_t* producer_pos_;
    uint64_t* consumer_pos_;
};
```

The `drain` method:
1. Reads producer position via atomic acquire (no syscall — it's mmap'd)
2. Reads consumer position (our own cursor)
3. For each record between them: checks the BUSY bit; if clear, invokes handler; advances consumer
4. Publishes new consumer position via atomic release

No syscalls. No kernel interaction beyond the BPF program writing to the buffer. Drain cost: ~20 ns per event handled + minimal per-drain overhead.

### 8.5 `PidFilter.h`

Populates the BPF `our_tids` map with our process's thread IDs, so sched_switch filters correctly. This is a write-path — we pay ~500 ns syscall per tid update. Done once at thread creation, never hot path.

```cpp
class PidFilter {
public:
    // Called at Keeper start + every time a new thread is created
    void add_tid(pid_t tid) noexcept;
    void remove_tid(pid_t tid) noexcept;

    // Periodically walk /proc/self/task/ and sync with `our_tids` map
    void resync() noexcept;
};
```

---

## 9. Zero-syscall BPF reading patterns

This section is the "how we do aggregation without calling `bpf(2)`" playbook. Four patterns, in order of preference:

### 9.1 Pattern A: BPF_F_MMAPABLE array (fixed key space)

Best for: counters keyed by a small, known enum (like the 96 sense_hub indices).

- BPF side: `counter_add(&counters, idx, delta)` — atomic add on mmap'd memory
- Userspace: `volatile uint64_t val = counters[idx]` — single load, 1 ns

Used by sense_hub (96 counters, already in place). Could be extended to per-syscall counters if we cap at top-N syscalls.

### 9.2 Pattern B: BPF_MAP_TYPE_RINGBUF (streaming events)

Best for: per-event records with stack traces, timestamps, IPs. Unbounded or bursty.

- BPF side: `bpf_ringbuf_reserve` + write + `bpf_ringbuf_submit`
- Userspace: volatile reads of producer/consumer pos, direct reads of data region

Used by: sched_switch events, syscall_latency events, lock_contention events, pmu_sample events. Ring size 16 MB per program on Linux 5.8+.

### 9.3 Pattern C: BPF_MAP_TYPE_PERCPU_ARRAY + atomic accumulation in BPF

Best for: per-CPU aggregation of small counter sets (e.g., running totals of syscall ns per CPU).

- BPF side: lookup percpu array slot, atomic add
- Userspace: walk N_CPUs via mmap, sum in userspace

16 CPUs × 8 B = 128 B per counter — 2 cache lines. AVX2 gather sums 4 cache lines in one instruction (~3 ns).

### 9.4 Pattern D: BPF_MAP_TYPE_HASH + lazy drain (last resort)

Best for: unbounded key space where rekeying isn't feasible (e.g., stack_id → off-cpu time aggregated in BPF).

- BPF side: `bpf_map_update_elem` with atomic accumulation
- Userspace: `bpf_map_lookup_batch` — one syscall, returns batch

Used only when Patterns A-C don't fit. For sched_switch off-CPU aggregation, we use Pattern B (emit event per switch) and aggregate in userspace — avoids per-stack-id hash map entirely.

### 9.5 What symbiotic does that we change

Symbiotic's `bpf/sched_switch.bpf.c` uses Pattern D (BPF_MAP_TYPE_HASH keyed on offcpu_key). Userspace drains via bpf_map_lookup_batch — ~100 µs per drain cycle.

Our rewrite uses Pattern B: emit a `sched_switch_event { tid, stack_id, off_cpu_ns, on_cpu }` per switch into the ringbuf. Userspace aggregates into an in-RAM map<stack_id, OffCpuVal>. No syscalls; all the cost is in-userspace hashmap inserts (~20 ns each). At 10K context switches/sec/core × 16 cores = 160K events/sec × 20 ns = 3.2 ms/sec = 0.3% overhead. Acceptable.

### 9.6 Stack resolution without syscall

BPF `bpf_get_stackid()` writes to a BPF_MAP_TYPE_STACK_TRACE map — userspace retrieves via `bpf_map_lookup_elem(stack_id)`. That's a syscall per stack.

Alternative: use `bpf_get_stack()` which writes the raw IP array directly into the event (ringbuf record). Each record becomes ~1 KB (127 × 8 B) but no separate syscall needed.

For stack-heavy workloads (millions of switches/sec), this bloats ringbuf usage. Compromise: use stack_id in the event, but batch-resolve all unique stack_ids once at report time via a single large `bpf_map_lookup_batch` — one syscall per report, not per event.

---

## 10. Symbol resolution — `include/crucible/perf/symbols/`

This is where we turn IP values into `(file, line, function)` tuples for the dashboard.

### 10.1 Why this matters for overhead

Naive: resolve each IP on sample with libdw. Cost ~50 µs cold per IP. At 10K samples/sec, 500 ms/sec = 50% overhead. Disaster.

Better: batch-resolve at report time. Aggregate raw IPs; when we flush a report (say, every 60 s), resolve the unique IPs in one libdw pass. Reduces to ~10 K IPs × 50 µs = 500 ms per report, amortized over 60 s = 0.8% overhead.

Best (our design): **Cipher-backed cache keyed by `(build_id, ip_page)`.** First resolution populates Cipher; subsequent runs read from Cipher. Hit rate 95%+ after first run. Cold + warm path: 500 ms cold / run, ~10 ms warm / run.

Best-best: **compile-time embedded symbol table in `.symbiot_sym` ELF section.** Every Crucible release build includes the symbol table in the binary. Runtime consumer just mmaps the binary and reads the section. Zero DWARF parsing at runtime. Zero libdw overhead.

### 10.2 `.symbiot_sym` section layout

A compact binary format embedded in every release binary via `objcopy --add-section`. Schema version 2 preserves full inline chains per IP range — this is what the dashboard needs for "file A:10 inlined into file B:20 inlined into file C:30" correctness at any IP.

```
[Section Header]
  magic:          0x53594D5379        // "SYMs"
  version:        u32  = 2            // v1 was flat, v2 has inline chains
  build_id:       u8[20]              // ELF NT_GNU_BUILD_ID
  num_ranges:     u32
  num_frames:     u32
  num_strings:    u32
  ranges_offset:  u32                  // bytes from section start
  frames_offset:  u32
  strings_offset: u32                  // zstd-compressed
  strings_csize:  u32                  // compressed size of string table
  strings_dsize:  u32                  // uncompressed size for allocation

[Range Table] — num_ranges entries, sorted by ip_start for O(log N) binary search
  Each range covers a contiguous run of IPs sharing the same inline chain.
  struct RangeEntry {              // 20 B
      uint64_t ip_start;
      uint64_t ip_end;              // exclusive
      uint32_t frames_idx;          // index into Frame Table
      uint16_t frames_count;        // how many inline frames for this range
      uint16_t _pad;
  };

[Frame Table] — packed, one per inline frame per range
  Each frame names a (function, file, line) at a specific inlining depth.
  struct FrameEntry {              // 20 B
      uint32_t function_id;         // index into string table
      uint32_t file_id;
      uint32_t line;
      uint16_t column;
      uint8_t  depth;               // 0 = innermost, increasing outward
      uint8_t  flags;               // reserved
  };

[String Table]
  zstd-compressed, null-terminated strings indexed by byte-offset into
  the decompressed blob. Deduped at build time — function names and file
  paths share storage across ranges.
```

**Why inline chains matter.** Consider `std::vector<T>::push_back` inlined into `transform_batch` inlined into `train_step`. An IP inside the inlined `push_back` body needs to resolve to three frames, not one. A flat `ip → (function, file, line)` format would mis-attribute the sample — user sees "hot line in vector.h" when the real hot spot is `train_step`. The inline chain preserves this correctly.

**Runtime lookup** — O(log N) binary search + O(k) frame walk where k is typical inline depth (≤ 5):

```cpp
std::span<const FrameEntry> resolve(uint64_t ip, const SymSection& sym) noexcept {
    auto* r = std::ranges::lower_bound(sym.ranges, ip,
        std::less{}, [](const RangeEntry& x) { return x.ip_end; });
    if (r == sym.ranges.end() || ip < r->ip_start) return {};
    return std::span(&sym.frames[r->frames_idx], r->frames_count);
}
```

Typical cost: ~50 ns for binary search + ~5 ns per returned frame = ~75 ns for a 5-deep chain.

**Built by a post-link tool `crucible-embed-symtab`:**
1. Reads the freshly-linked binary via libelf
2. Runs DWARF parse via libdw — walks each CU's `.debug_info` + `.debug_line` + `.debug_inlined_subroutines`
3. For each distinct `(ip_start, ip_end)` range: emits one RangeEntry + N FrameEntries for the inline chain at that range
4. Deduplicates strings across ranges, packs into string table
5. zstd-compresses strings (level 3)
6. `objcopy --add-section .symbiot_sym=<tmp> --set-section-flags .symbiot_sym=noload,readonly` back into the binary

Size estimate for Crucible: ~500 K – 2 MB per binary after compression. Added to binary size; no runtime cost to extract beyond the one-time zstd-decompress of the string table (~5 ms at Keeper startup).

**Version-compat.** Readers check `version` field; v1 readers can fail fast; v2 readers tolerate v1 sections by treating them as single-frame ranges. Forward-compatibility for v3+ via feature flags.

### 10.3 Runtime resolver

At startup, each Keeper:
1. Reads `/proc/self/exe`, parses ELF
2. Locates `.symbiot_sym` section
3. zstd-decompresses into RAM
4. Builds in-memory lookup: `ip → (function, file, line)` via binary search on function table

Per-IP lookup cost: ~50 ns (binary search + string offset deref). For 100 K IPs: 5 ms. Negligible at report time.

For user modules loaded via dlopen: the LD_AUDIT handler (§17.5) reads the module's `.symbiot_sym` section the same way. No libdw involved.

### 10.4 Fallback: libdw for binaries without `.symbiot_sym`

If a binary wasn't built with the post-link tool (e.g., system libraries, vendor-provided `.so` files), we fall back to libdw:

```cpp
class DwarfResolver {
public:
    struct Resolution {
        std::string function;
        std::string file;
        uint32_t line;
    };

    [[nodiscard]] std::vector<Resolution> resolve_batch(
        std::span<const uint64_t> ips) noexcept;

private:
    Dwfl* dwfl_;  // libdw handle
};
```

Cached in RAM via `ip_page_cache_` mapped by `(build_id, ip_page)`. Hot path stays ~50 ns/lookup via page-level indirection.

### 10.5 Cipher cache

Cross-run / cross-node symbol sharing via Cipher's observability namespace:

```
cipher://observability/symbols/<build_id>/<ip_page>/
  → compressed blob of { ip_offset_in_page: u16, function_id: u32, file_id: u32, line: u32 }[]
```

First Keeper in a cluster that encounters a new `(build_id, ip_page)` resolves via DWARF + writes to Cipher. Subsequent Keepers pull from Cipher — content-addressed, no invalidation ever (symbols are immutable given build_id).

Federation: per CRUCIBLE.md §18.3, symbol cache is publicly shareable. Organizations can opt into a global symbol corpus.

---

## 11. Multi-level code view — source↔LLVM IR↔ASM

For any hot function, the dashboard shows all three representations side-by-side with sample attribution at each level. Implementation:

### 11.1 Build-time artifact generation

CMake option `CRUCIBLE_PERF_MULTI_LEVEL=ON` adds per `.cpp`:

```cmake
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/perf-ll/${src_base}.ll
    COMMAND ${CMAKE_CXX_COMPILER}
        -S -emit-llvm -O3 -g -gdwarf-5 -fdebug-info-for-profiling
        ${CMAKE_CXX_FLAGS} ${src} -o ${CMAKE_BINARY_DIR}/perf-ll/${src_base}.ll
    DEPENDS ${src}
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/perf-asm/${src_base}.s
    COMMAND ${CMAKE_CXX_COMPILER}
        -S -O3 -g -gdwarf-5
        ${CMAKE_CXX_FLAGS} ${src} -o ${CMAKE_BINARY_DIR}/perf-asm/${src_base}.s
    DEPENDS ${src}
)
```

These run in parallel with the main build. Total extra time: ~30% of build time (clang does two extra compiles per file).

### 11.2 Post-link bundle assembly

Tool `crucible-embed-bundle`:
1. Parses all `.ll` files (extract `!DILocation` + `inlinedAt` chains per instruction)
2. Parses all `.s` files (extract `.loc` directives + discriminators per instruction)
3. Matches functions by mangled name across IR, ASM, and binary symbol table
4. Builds `MultiLevelBundle` with bidirectional `(source, IR, ASM)` index maps per function
5. Serializes to JSON, zstd-compresses
6. `objcopy --add-section .symbiot_ml=<tmp>` into binary

Same pipeline as symbiotic's `embed.rs`, just in C++.

### 11.3 Runtime rendering

When Dioxus requests `/api/v1/source/multilevel?function=XYZ`:
1. Find function by name in `.symbiot_ml` index
2. Return JSON with `{source_lines: [...], ir_insns: [...], asm_insns: [...], links: {source_to_ir: {...}, ir_to_asm: {...}}}`
3. Include attribution: for each line/instruction, the `EventCounts` accumulated during the profile window

Dioxus renders three panels side-by-side; hover on a source line highlights the corresponding IR and ASM instructions; samples overlay as heatmap.

### 11.4 What we drop vs symbiotic

Symbiotic has four levels (source, Rust MIR, LLVM IR, ASM). Rust MIR is Rust-specific. C++ has no MIR equivalent.

We keep three levels: **source, LLVM IR, ASM**. The correlation engine is otherwise identical to symbiotic's `multi_level.rs` — same three-tier fallback `(line, col, discriminator) → (line, col) → line`.

---

## 12. Line profiler — `include/crucible/perf/line/`

Puts counters + BPF samples + symbols together to produce `.symbiot` traces.

### 12.1 Architecture

```
[perf_event samplers: cycles + L1D]  ─┐
                                       ├─→ [merged IP histograms]
[BPF pmu_sample ringbuf: LLC,           │       │
 branch, DTLB, IBS, page-fault, etc]  ─┘       ▼
                                        [batch DWARF resolve via .symbiot_sym + Cipher cache]
                                                │
                                                ▼
                                        [aggregate by (file, line) → EventCounts]
                                                │
                                                ▼
                                        [SymbiotTrace {...} → zstd JSON]
```

### 12.2 Per-CPU sampler

Open per-CPU `perf_event_open` for cycles + L1D miss with `any_pid + one_cpu(N) + sample_type = IP|TID|TIME`. Requires CAP_PERFMON. Mmap ringbuf per sampler; drain via volatile reads.

At Keeper init: `num_online_cpus` samplers × 2 events = 32 samplers on a 16-core machine. Each has 128 KB ring = 4 MB total RAM.

Cost: same pattern as BPF ringbuf reads. Drained by the perf reader thread at 100 Hz. Per drain: volatile reads of ring head, iterate events, handler appends IP to histogram.

### 12.3 Merge with BPF

BPF `pmu_sample.bpf.c` program already writes all 10 event types to its ringbuf. Drain alongside perf ringbufs:

```cpp
void drain_line_samples() {
    perf_sampler_cpu_0.drain([&](Sample& s) { hist[EventKind::Cycles][s.ip]++; });
    // ... same for L1D ...
    bpf_pmu_ringbuf.drain([&](BpfSample& s) {
        EventKind k = EventKind::from_bpf_type(s.event_type);
        hist[k][s.ip]++;
    });
}
```

### 12.4 Resolution + aggregation

At report time:
```cpp
auto unique_ips = collect_unique(hist);  // HashSet<u64>
auto resolved = symbol_resolver.resolve_batch(unique_ips);  // ~5ms warm
auto per_line_counts = aggregate_by_line(hist, resolved);
SymbiotTrace trace = build_trace(name, duration, per_line_counts, resolved);
trace.write_to("run.symbiot");  // zstd JSON
```

Same schema as symbiotic's `.symbiot` — portable across the two ecosystems.

---

## 13. Augur engine integration — `include/crucible/augur/`

CRUCIBLE.md §13 specifies Augur's behavior. Perf provides the data; Augur is the consumer.

### 13.1 Residuals

For each kernel sample (1% of launches per CRUCIBLE.md §13.1), compute residual vs `CompiledKernel.predicted_cycles`:

```cpp
struct KernelSample {
    ContentHash kernel_hash;
    ChipId chip;
    uint64_t measured_cycles;
    uint64_t measured_memory_bytes;
    uint64_t predicted_cycles;
    uint64_t predicted_memory_bytes;
    double cycle_residual_pct;
    double memory_residual_pct;
    uint64_t ts_ns;
};

class ResidualTracker {
public:
    void record(const KernelSample& s) noexcept;
    // Maintains per-(kernel_hash, chip) rolling windows for P50/P95/P99
};
```

Residuals feed into Cipher as time series (§15).

### 13.2 Drift detection

Per §13.2: P95 residual > 10% for 100+ consecutive samples → trigger recalibration. Augur's drift logic watches the residual stream and emits events when thresholds are crossed:

```cpp
class DriftDetector {
public:
    using Event = std::variant<CycleDrift, MemoryBwDrift, StallAttributionDrift>;
    std::optional<Event> tick(const KernelSample& s) noexcept;
};
```

### 13.3 MFU breakdown (§13.7)

Attribute lost MFU to specific loss categories:

```cpp
struct MfuBreakdown {
    double peak_tflops;
    double achieved_tflops;
    double lost_to_comm;          // from CNTP event stream + kernel idle
    double lost_to_pipeline;       // from region attribution
    double lost_to_memory_bw;      // from cache miss counters + memory BW
    double lost_to_kernel_launch;  // from launch overhead tracking
    double lost_to_suboptimal_kernels;  // from predicted-vs-measured per kernel
    double lost_to_tile_shape;     // from MAP-Elites comparison
};
```

Each component is derived from a specific perf data source; aggregation happens in the Augur worker.

### 13.4 Recommendations

Augur generates recommendations; perf is a producer of the data backing them. Output:

```cpp
struct Recommendation {
    std::string title;
    std::string description;
    double expected_speedup_pct;
    double confidence_0_to_1;
    enum class Tag : uint8_t { AutoHot, AutoCold, Manual } tag;
    std::vector<std::string> actions;  // e.g., "recompile kernel=X", "expand bucket Y"
};
```

Stored in Cipher; readable via `crucible recommendations list`.

---

## 14. Broker — `include/crucible/broker/`

Per-Keeper thread that owns the HTTP server, local UDS listener, and gossip state.

### 14.1 `Server.h` — HTTP server

cpp-httplib based. Single dedicated thread; all handlers non-blocking since everything's served from cached atomic state:

```cpp
class Server {
public:
    explicit Server(uint16_t port);
    void start();  // spawns thread
    void stop();

    // Called by reader thread every 50 ms
    void update_state(std::shared_ptr<const CachedState> new_state);

private:
    httplib::Server server_;
    AtomicState<CachedState> state_;
    std::thread server_thread_;
};
```

Handler example:
```cpp
server_.Get("/api/v1/cluster/overview", [&](const Request& req, Response& res) {
    auto s = state_.load();  // ~5 ns
    nlohmann::json j;
    j["timestamp_ns"] = s->last_updated_ns;
    j["cluster_mfu"] = s->cluster_mfu;
    j["nodes"] = s->node_summaries;  // serialize via nlohmann::adl_serializer
    res.set_content(j.dump(), "application/json");
});
```

### 14.2 `Local.h` — Unix domain socket for local processes

User programs running on the same machine (e.g., dlopen'd modules, child processes) emit per-scope events via UDS. Broker aggregates into the same NodeBuckets ring as native Crucible events.

Protocol: flatbuffer messages over SOCK_SEQPACKET. Each message is one `ScopeReport { scope_name, wall_ns, cycles, insns, sense_delta, event_count }`. Broker acknowledges with acks only (no reply data). ~10 µs per message round-trip.

### 14.3 `NodeBuckets.h` — 3600-slot ring

```cpp
struct NodeBucket {
    uint64_t start_ts_ns;
    uint64_t end_ts_ns;
    // Aggregates
    PipelineCounters pipeline;
    CacheCounters cache;
    BandwidthMeasurement bandwidth;
    EnergyMeasurement energy;
    uint64_t total_ops;
    std::array<HotFunctionEntry, 20> top_hot_functions;
    std::array<HotLineEntry, 20> top_hot_lines;
    std::array<StackEntry, 10> top_off_cpu;
    std::array<LockEntry, 10> top_locks;
};

class NodeBuckets {
public:
    // 3600 × 1-second buckets = 1 hour history
    // Ring index: (now_sec % 3600)
    void record_second(const Aggregates& agg) noexcept;
    const NodeBucket& at(size_t n_seconds_ago) const noexcept;
    std::span<const NodeBucket> last_window(size_t seconds) const noexcept;
};
```

### 14.4 `Gossip.h` — CNTP broadcast

Every 10 s, summarize the last 10 s of data into a flatbuffer, broadcast via CNTP (piggyback on SWIM or separate channel):

```fbs
// NodeSummary.fbs
table NodeSummary {
    node_uuid: [uint8:16];
    epoch: uint32;
    wall_ns: uint64;
    mfu: float;
    mfu_breakdown: MfuBreakdown;
    pipeline: PipelineCounters;
    top_hot_functions: [HotFunctionEntry:20];
    top_hot_lines: [HotLineEntry:20];
    top_off_cpu: [StackEntry:10];
    drift_events: [DriftEvent];
    health: HealthSignals;
}
```

Typical size: 1–5 KB compressed. 100-node cluster × 5 KB × 0.1 Hz = 50 KB/s cluster-wide gossip. Fits in any network.

### 14.5 Federation

Each Keeper maintains `map<node_uuid, NodeSummary>` updated from incoming gossip. HTTP endpoints aggregate from local state + peer summaries for cluster-wide queries. No RPC for common queries.

For deep-dive queries (per-second data from a specific node N), issue a targeted CNTP message to N. N responds with raw ring data.

---

## 15. Cipher persistence — observability namespace

Per CRUCIBLE.md §9, Cipher has three tiers (hot RAM, warm NVMe, cold S3). Perf uses the **observability namespace**:

```
cipher://observability/
  symbols/<build_id>/<ip_page>/       ← symbol cache
  plans/<plan_hash>/measurements/     ← per-plan measurement records
  residuals/<kernel_hash>/<chip>/     ← time series, 7-day rolling
  snapshots/<node_uuid>/<epoch>/      ← periodic perf dumps
  flamegraphs/<node_uuid>/<hour>/     ← cold-tier folded stacks
  traces/<node_uuid>/<run_id>.symbiot ← per-run .symbiot files
```

This namespace is **separate from the replay state** (CRUCIBLE.md §10). Perf data never affects training state deterministically; perf-driven recommendations go through Augur's explicit queue.

### 15.1 Retention

- Hot (RAM): last 1 hour of aggregated bucket data per node
- Warm (NVMe): last 24 hours of per-second buckets, last 7 days of per-minute buckets
- Cold (S3): 90-day archive of hourly rollups per node

Total storage: ~1 GB per Keeper per day at per-second granularity. Tapers to ~50 MB/day at hourly rollup.

### 15.2 Federation rules

Per CRUCIBLE.md §18.3, symbol cache and flamegraphs are safe to share across clusters (content-addressed, no user data). Plan measurements and node snapshots default to cluster-private; opt-in federation via Cipher ACL.

### 15.3 Crucible planning integration

Plans (CRUCIBLE.md §17.6) carry a `measurements: [MeasurementRecord]` list that's populated from perf data. `crucible plan show` reads from this list to produce the "Predicted 14.2 ms / Measured 14.4 ms" line.

---

## 16. CNTP gossip for cluster-wide aggregation

Already summarized in §14.4. Detailed wire format and cadence here:

### 16.1 Wire format

FlatBuffers schema (`NodeSummary.fbs`). zstd-compressed at level 3. Typical size after compression: 1–5 KB.

### 16.2 Cadence

- Default: 10-second cadence per node
- Under load (CPU > 80%): back off to 30-second cadence
- During incidents (drift event fired): increase to 1-second cadence for affected metrics

### 16.3 Delivery guarantees

Gossip is eventually consistent. No ACKs, no retransmission. Missed gossip = 10 seconds of missing data for that node, invisible in the dashboard. Acceptable for observability.

### 16.4 Network budget

At 100 nodes: 100 × 5 KB × 0.1 Hz = 50 KB/s, both directions (each node sees everyone else's). 1 GB/s data center 10GbE has ~0.005% utilization. Trivial.

At 1000 nodes: 1000 × 5 KB × 0.1 Hz = 500 KB/s. Still under 0.05%.

At 10000 nodes: start thinking about hierarchical aggregation (rack-level proxies). Probably doesn't apply for years.

---

## 17. User module integration

Per user request: user-written modules (custom data loaders, custom networking layers, user Python code, user C++ libraries) get identical profiling treatment.

### 17.1 C++ modules — `CRUCIBLE_USER_SCOPE`

Same macro as Crucible's internal scopes. User code:

```cpp
#include <crucible/perf/user/CppScope.h>

void my_loss_function(const Tensor& pred, const Tensor& target) {
    CRUCIBLE_USER_SCOPE("my_loss_function");
    // ... work ...
    {
        CRUCIBLE_USER_SCOPE("my_loss/reduction");
        // ... hot part ...
    }
}
```

Scope IDs allocated from the same ScopeTable. User's function shows up in the same flamegraph as Crucible's internal functions.

### 17.2 Python — `crucible.perf.scope`

```python
import crucible.perf as cp

with cp.scope("my_data_transform"):
    batch = preprocess(raw)

@cp.profile_fn
def my_collate(batch):
    ...
```

The Python binding is a pybind11 wrapper that calls into the region engine. Each Python scope records a RingEvent. At Python tracing cost (~100 ns per scope enter via pybind11), this is workable for anything not in a tight inner loop.

For inner loops in Python: use `cp.batch_record(scope_id, duration_ns, count)` — amortizes the pybind11 overhead over N iterations.

### 17.3 Rust FFI — `#[profile]` macro

```rust
use crucible_perf::scope;

#[profile]
fn my_fn(x: &[f32]) -> f32 {
    // Body wrapped in CRUCIBLE_USER_SCOPE equivalent
}
```

Proc-macro generates calls to the C ABI exported by our region engine.

### 17.4 Vessel bridge — IR001 op auto-labeling

Every `CrucibleTensorImpl` carries a `source_line` when captured (CRUCIBLE.md §3.2). Perf joins with TraceRing's `enter_tsc`: the dispatch timeline becomes source-line-annotated for free. User code like:

```python
# train.py line 154
logits = model(input)
```

shows up in the dashboard as "line 154 dispatched 47 ops, total wall 12ms."

### 17.5 dlopen hook — LD_AUDIT

Crucible loads user custom modules via `dlopen(user_lib.so)` or uses LD_PRELOAD. Our LD_AUDIT handler (`libcrucible_audit.so`) intercepts:

```cpp
extern "C" unsigned int la_objopen(struct link_map* map, Lmid_t, uintptr_t*) {
    // Extract build_id from the newly-loaded module's ELF NT_GNU_BUILD_ID
    uint8_t build_id[20];
    if (read_build_id_from_map(map, build_id)) {
        register_module(build_id, map->l_name, map->l_addr);
        // If module has .symbiot_sym section: load symbol table
        // If not: fall back to libdw parse (lazy, on first IP encounter)
    }
    return LA_FLG_BINDFROM | LA_FLG_BINDTO;
}
```

Result: any user `.so` compiled with `-g` (and optionally with `.symbiot_sym` section added) automatically gets its IPs resolved to user source. User custom data loader's hot loop shows in the same dashboard as Crucible's dispatch path.

---

## 18. Bench harness integration

Every `bench::Run::measure()` opens a PipelineGroup + CacheGroup + BandwidthGroup + EnergyGroup on thread init. Reads at measurement boundaries. Attaches to Report.

```cpp
// bench/bench_harness_perf.h
struct PerfReport {
    PipelineCounters pipeline;
    CacheCounters cache;
    BandwidthMeasurement bandwidth;
    EnergyMeasurement energy;
    SimdCounters simd;
    std::vector<LoadLatencySample> latency_histogram;  // IBS/PEBS samples
};

// Added to existing bench::Report
struct Report {
    // ... existing fields ...
    std::optional<PerfReport> perf;
};
```

Every bench footer now includes:

```
build_trace (full pipeline, 36499 ops)  p50=2.89ms ...
   └─ preempt=6 · runtime=1.4s · softirq=90ms · ...     ← existing BPF sense line
   └─ IPC=2.41 · L1d=3.2% · L2=18% · LLC=0.18% · br=1.4% · FE=8% · BE=12%   ← NEW
   └─ BW=4.7GB/s(r)+0.3(w) · pkg=3.2W · mem=0.8W · J/op=45nJ              ← NEW
   └─ vec=42% (SSE=12%+AVX2=28%+AVX512=2%)                                 ← NEW
   └─ mem: p50=14cy p99=230cy | L1 38% · L2 42% · L3 15% · DRAM 5% · n=128k ← NEW
```

Three lines of hardware context per bench Report, zero user action. Automatic.

---

## 19. Dashboard data contract — JSON endpoints for Dioxus

All JSON, all stateless, all served from atomic shared_ptr cache. Polling at 500 ms is the default; WebSocket or SSE for live event streams.

### 19.1 Cluster-wide endpoints

```
GET  /api/v1/cluster/overview
     → { timestamp_ns, cluster_mfu, mfu_breakdown, active_nodes, degraded_nodes,
         total_ops_per_sec, training_steps, epoch, current_recipe, ... }

GET  /api/v1/cluster/nodes
     → [ { uuid, chip, mfu, degraded, active_scopes, hot_funcs[:5], ... } × N_nodes ]

GET  /api/v1/cluster/top?metric=cycles&count=20
     → [ { function, file, line, total_cycles, total_samples, node_distribution } × 20 ]

GET  /api/v1/cluster/heatmap
     → { nodes: [...], per_node_load_pct: [...], hotspots: [...] }

GET  /api/v1/cluster/drift
     → { recent_events: [...], affected_kernels: [...], recalibration_queue: [...] }
```

### 19.2 Per-node endpoints

```
GET  /api/v1/node/<uuid>/detail
     → { ... all node-level perf data for this node ... }

GET  /api/v1/node/<uuid>/flamegraph.folded
     → Brendan Gregg folded format, text/plain
     → Dioxus renders with flamegraph.js or our own renderer

GET  /api/v1/node/<uuid>/chrome-trace.json
     → Chrome trace format for chrome://tracing / Perfetto consumption

GET  /api/v1/node/<uuid>/symbiot.gz
     → zstd-compressed .symbiot trace file (direct download)
```

### 19.3 Source-level endpoints

```
GET  /api/v1/source?file=<path>&line=<n>
     → { attribution: { cycles, l1d_misses, llc_misses, branch_misses,
                        page_faults, ctx_switches, futex_wait_ns, ... },
         function: "...", nearby_hot_lines: [...] }

GET  /api/v1/source?function=<name>
     → { lines: [ { line_no, source_text, counts } × N ], ... }

GET  /api/v1/source/multilevel?function=<name>
     → { source_lines: [...], ir_insns: [...], asm_insns: [...],
         links: { source_to_ir: [...], ir_to_asm: [...] } }
```

### 19.4 Region / scope endpoints

```
GET  /api/v1/regions/tree?tid=<t>
     → recursive JSON tree of per-scope call data for thread t

GET  /api/v1/regions/aggregate
     → cluster-wide scope statistics (self_time, incl_time, call_count)

GET  /api/v1/regions/top?scope=<id>
     → hottest instances of this scope across cluster
```

### 19.5 BPF event endpoints

```
GET  /api/v1/ebpf/offcpu?top=20
     → top 20 off-CPU stacks cluster-wide with folded representation

GET  /api/v1/ebpf/syscalls
     → per-syscall histograms: { name, count, total_ns, p50, p99 }

GET  /api/v1/ebpf/locks?top=20
     → top 20 lock-contention sites with stack + wait_ns

GET  /api/v1/ebpf/events?kind=<k>&from=<ts>&to=<ts>
     → raw event stream for deep-dive
```

### 19.6 Crucible-specific endpoints

```
GET  /api/v1/plans/current
GET  /api/v1/plans/<hash>
GET  /api/v1/plans/<hash>/diff/<hash2>
POST /api/v1/plans/bisect
POST /api/v1/recipes/materialize
GET  /api/v1/forge/phases?kernel=<hash>
GET  /api/v1/mimic/archive?kernel=<hash>
GET  /api/v1/augur/recommendations
GET  /api/v1/augur/mfu
GET  /api/v1/augur/drift
GET  /api/v1/cntp/topology
GET  /api/v1/cipher/tiers
```

### 19.7 Live streams

```
GET  /ws/events
     → Server-Sent Events (or WebSocket) streaming per-second event summaries

GET  /ws/samples
     → live PMU sample stream, rate-limited to 1000/s
```

### 19.8 Deep-dive control

```
POST /api/v1/deepdive?duration=60s&kind=llc_miss
     → activates period=1 sampling for 60 seconds
     → returns job_id
GET  /api/v1/deepdive/<job_id>/status
     → pending / running / complete
GET  /api/v1/deepdive/<job_id>/result
     → full folded stacks + line annotations
```

---

## 20. Crucible-specific visualizations

Beyond generic flamegraphs and timelines, Crucible's unique data gets dedicated Dioxus components. Each backed by a dedicated JSON endpoint.

### 20.1 Plan timeline

Per-iteration gantt chart. Each kernel box colored by IPC, width by cycles, with predicted-vs-measured overlaid. Click a kernel → drill into source↔IR↔ASM with sampled events painted on each line.

### 20.2 MFU waterfall

Peak TFLOPs → list of losses (comm, pipeline, memory BW, launch, suboptimal kernels, tile shape) → achieved TFLOPs. Each loss category clickable with drill-down to specific kernels.

### 20.3 Recipe flow diagram

NumericalRecipe graph: BF16 input → FP32 accum → BF16 output with specific rounding modes. Nodes = kernels realizing each operation; edges = precision-preserving data flow. Hover an edge to see ULP drift contribution.

### 20.4 Reduction topology map

Cluster allreduce routing visualized: nodes in ring/tree/HD layout, edges showing per-link bandwidth + latency. Degraded links highlighted in red. Historical traffic heatmap animated over time.

### 20.5 Forge phase timeline

For a selected compile event: A INGEST → B ANALYZE → ... → L VALIDATE as horizontal bars. Each phase clickable to show sub-passes, time spent, cache hits/misses.

### 20.6 MAP-Elites archive browser

For each kernel compiled: archive of explored configurations (tile × register pressure × predicted cycles) as 2D colormap. Pareto frontier highlighted. Currently-selected point, neighbors that lost.

### 20.7 Kernel drift heatmap

2D grid of (kernel × chip) with color = P95 residual from prediction. Hot cells trigger Augur recalibration; dashboard shows live which cells are drifting and by how much.

### 20.8 Per-SM / per-CU timeline

For any selected time window on a given chip: which SM (NV) / CU (AMD) / core (CPU) ran which kernel when. Utilization colors. Helps identify scheduling inefficiencies.

### 20.9 CNTP link flow map

All NIC-to-NIC flows as Sankey diagram. Per-flow: RDMA op, message size, rate, latency distribution. Click a flow → see the collective operation producing it.

### 20.10 Cipher tier heatmap

Hot/warm/cold tier utilization. Promotion/eviction rates. Per-namespace L1/L2/L3 hit ratios.

---

## 21. Performance budget — the full arithmetic

Steady-state overhead for a Crucible Keeper running training at 1 M dispatch ops/sec/core:

**Producer side (hot path):**

| Operation | Frequency | Cost | Total |
|---|---|---|---|
| TraceRing `enter_tsc` store | 1 M/s | 0.3 ns | 0.3 ms/s = 0.03% |
| CRUCIBLE_PROFILE_SCOPE (compile paths, bg threads) | 1 K/s | 23 ns | 0.023 ms/s = 0.002% |
| CRUCIBLE_USER_SCOPE (user code) | 10 K/s typical | 23 ns | 0.23 ms/s = 0.023% |
| BPF tracepoint events (in kernel, free) | unlimited | 0 ns (NMI context) | 0% |
| Sense hub counter ticks (in kernel, free) | unlimited | 0 ns | 0% |

**Consumer side (reader thread):**

| Operation | Frequency | Cost | Total |
|---|---|---|---|
| Ring buffer drain (per thread) | 10 Hz | 100 µs | 1 ms/s = 0.1% (on reader thread) |
| BPF ringbuf drain | 100 Hz | 50 µs | 5 ms/s = 0.5% (on reader thread) |
| Sense hub snapshot | 1 Hz | 5 ns | negligible |
| Symbol resolution (batch, warm) | 0.017 Hz (60s) | 5 ms | 0.085 ms/s = 0.008% |

**Reader thread consumes ~0.6% of a core**, dedicated. On a 16-core machine: 0.6 / 16 = **0.04% of total CPU**.

**Broker thread:**

| Operation | Frequency | Cost | Total |
|---|---|---|---|
| HTTP request serve | 10 Hz | 10 µs | 0.1 ms/s = 0.01% |
| Gossip broadcast | 0.1 Hz | 100 µs | 0.01 ms/s = 0.001% |
| NodeBucket aggregation | 1 Hz | 50 µs | 0.05 ms/s = 0.005% |

**Broker thread consumes ~0.02% of a core.**

**Cipher writer thread:**

| Operation | Frequency | Cost | Total |
|---|---|---|---|
| Snapshot write to L2 | 0.017 Hz | 10 ms | 0.17 ms/s = 0.017% |
| Hourly rollup to L3 | 0.0003 Hz | 100 ms | 0.003 ms/s = 0.0003% |

**Cipher writer thread consumes ~0.02% of a core.**

**Total steady-state overhead per 16-core Keeper:**
- Hot path (all threads): ~0.05%
- Reader thread: ~0.04%
- Broker thread: ~0.02%
- Cipher writer: ~0.02%
- **Total: ~0.13% of Keeper CPU**

Well under 1% target. Headroom absorbs deep-dive bursts (60-second period=1 windows) without exceeding 1% sustained.

**Memory:**
- TLS ring buffers: 16 threads × 64 KB = 1 MB
- BPF ringbufs: 4 × 16 MB = 64 MB
- BPF sense hub: 768 B (rounded to 4 KB page)
- NodeBuckets history: 3600 × 2 KB = 7.2 MB
- Cluster view (peer summaries): 100 nodes × 5 KB = 500 KB
- Cipher hot tier: up to 500 MB per Keeper
- Scope table: 3 MB BSS
- Symbol cache in RAM: 50 MB typical
- **Total RAM: ~630 MB per Keeper**

Negligible on a machine with 256 GB+ DRAM.

**Network:**
- Gossip: 100 nodes × 5 KB × 0.1 Hz × both-directions = 50 KB/s
- **Bandwidth usage: ~0.001% of 10GbE**

---

## 22. Failure modes and graceful degradation

Every component fails independently. Each failure is logged with a specific message; the profiler continues operating in reduced mode.

### 22.1 BPF load failure

Causes: kernel < 5.8, no CAP_BPF, `kernel.unprivileged_bpf_disabled=1` without CAP_BPF, BTF not present.

Response:
- Log `[perf] BPF unavailable: <reason> — degrading to perf_event + /proc mode`
- `sense_hub` mmap unavailable → per-region sense delta is zero
- `pmu_sample` not loaded → line profiler only has perf_event ring (cycles + L1D); skip LLC/branch/DTLB attribution
- `sched_switch` not loaded → no off-CPU flamegraph
- `syscall_latency` not loaded → no syscall histogram
- `lock_contention` not loaded → no futex stacks

All other layers continue.

### 22.2 perf_event_open failure

Causes: `kernel.perf_event_paranoid ≥ 3`, no CAP_PERFMON (for per-CPU samplers), PMU hardware exhausted.

Response:
- Log `[perf] perf_event_open denied: check CAP_PERFMON + perf_event_paranoid`
- No PMU counters in scopes → TSC-only wall time
- No line profiler IP sampling
- No IBS/PEBS latency sampling

Scope engine + BPF subsystem continue.

### 22.3 rdpmc unavailable

Causes: `cap_user_rdpmc = 0` (paranoid too high, kernel disabled userspace counters), CPU doesn't support rdpmc.

Response:
- Log `[perf] userspace rdpmc unavailable: falling back to read(fd) syscall path`
- Scope PMU reads go through syscall (~500 ns each)
- Total scope cost jumps from ~23 ns to ~1 µs
- Warning in dashboard: "Perf overhead elevated"

### 22.4 Symbol resolution failure

Causes: no `.symbiot_sym` section, no debug info, stripped binary.

Response:
- Log `[perf] no DWARF for <module>: raw IPs only`
- Dashboard shows IPs instead of function names for unresolved modules
- Cipher cache doesn't populate for unresolved IPs

### 22.5 Cipher unavailable

Causes: disk full, S3 credentials missing, network partition.

Response:
- Log `[perf] Cipher write failed: <reason>`
- In-memory data kept; cluster gossip still works
- No persistence of historical data until Cipher restored

### 22.6 HTTP server bind failure

Causes: port in use.

Response:
- Log `[perf] port <n> in use; trying <n+1>`
- Retry up to 10 ports; if all fail, continue without HTTP server
- Cluster gossip + local UDS still work
- Dashboard disabled

### 22.7 CNTP gossip failure

Causes: network partition, peer down.

Response:
- Log at CNTP layer
- Local observability continues
- Cluster-wide view degraded to last-known-good peer summaries
- Recovery automatic when CNTP heals

### 22.8 Ringbuf overflow

Causes: reader thread stalled, events producing faster than drain rate.

Response:
- BPF side drops events (ringbuf reservation fails)
- Log every 1 s with dropped count: `[perf] pmu_sample ringbuf dropped N events in last 1s`
- Adaptive throttle: reduce sampling period on over-producing event kinds

### 22.9 Watchdog: runaway profiling overhead

If perf threads exceed 5% CPU for 10 seconds continuously:
- Log `[perf] self-throttling: profiling overhead too high`
- Double all sampling periods
- Reduce gossip cadence to 60 s
- Disable deep-dive endpoint for 60 s
- Page operator via CNTP health signal

---

## 23. Replay determinism and the observability namespace

Per CRUCIBLE.md §10, training state at step T = `(weights_T, optimizer_T, cursor_T, seed)`. Perf data must not be input to this state.

### 23.1 Sampling decisions are deterministic

Sampling RNG is seeded from `(Keeper UUID, step_number, scope_id)` — not wall-clock. The same sample set is chosen on replay.

### 23.2 Recommendation queue is deterministic

Augur's drift detection thresholds, residual computations, and recommendation scores are all deterministic functions of measured data. Replays produce the same recommendations.

### 23.3 Recommendation consumption is explicit

Recompile/reshard actions triggered by recommendations happen at checkpoint boundaries (iteration ends). They're logged in the replay journal. Replay applies the same actions.

### 23.4 Perf data doesn't enter Cipher replay namespace

Cipher has two root namespaces:
- `cipher://replay/*` — state inputs (weights, optimizer, data cursor)
- `cipher://observability/*` — perf data

They never cross. Replay reads only from `cipher://replay/*`.

### 23.5 Consequence: two replays produce identical model state

Observability differs (different real-time samples, different wall-clock) but model state is bit-identical. Bit-exact CI tests pass because they compare `cipher://replay/*` content, not observability data.

---

## 24. Security and multi-tenancy

Per CRUCIBLE.md §18.

### 24.1 Perf namespace access

- `cipher://observability/symbols/*` — shareable by default (content-addressed, public-compatible per §18.3)
- `cipher://observability/plans/*` — cluster-private default, namespace ACL
- `cipher://observability/residuals/*` — cluster-private default
- `cipher://observability/snapshots/*` — cluster-private default
- `cipher://observability/traces/*` — cluster-private default

### 24.2 HTTP endpoint authn/authz

- Localhost only by default (`bind_addr = 127.0.0.1`)
- Optional TLS + client certs for cross-network access
- Optional JWT auth via CNTP-issued tokens
- Per-endpoint ACL: read-only endpoints public within cluster; deep-dive endpoints require admin role

### 24.3 User code scopes are untrusted

User scopes can fire uncontrolled. Watchdog throttles per-scope sample rate; runaway user scopes are throttled to 0 (logged; no crash).

### 24.4 LD_AUDIT attack surface

Our audit handler parses ELF notes. Use `libelf` parsers (well-fuzzed). No user-controlled string parsing in the handler.

---

## 25. Hardware-specific notes

### 25.1 Intel

- PMU: 4–8 GP counters + 3–4 fixed. Multiplexing when exceeded.
- `PEBS` for precise sampling (MEM_LOAD_RETIRED.* events).
- `Intel PT` available if enabled in BIOS. Out-of-scope; use only for on-demand deep dive.
- Uncore IMC for memory BW: `uncore_imc_0` through `uncore_imc_N`.
- `power` PMU: energy-pkg + energy-ram + energy-cores.
- Hybrid cores (Alder Lake+): P-core vs E-core events differ. Track core type in samples.

### 25.2 AMD Zen3+

- 6 GP counters + 6 fixed (Zen5 has 8 + 8).
- IBS (Instruction-Based Sampling): precise IP, cache level, load latency. Works on all cores Zen3+.
- `amd_df` (Data Fabric): UMC read/write counters for memory BW.
- `power` PMU: energy-pkg (package) + energy-ram (DRAM, since Zen2).
- No Intel PT equivalent; IBS is the substitute.
- Zen has native `rdpmc` support with no skid.

### 25.3 ARM Cortex-A76+ / Neoverse N1+

- PMUv3 architectural events (ARMv8.0+).
- SPE (Statistical Profiling Extension, ARMv8.2+): similar to Intel PEBS.
- ASE_SPEC / VFP_SPEC / SVE_INST_SPEC for vector utilization.
- No RAPL equivalent; use `arm_scmi` or BMC-provided energy counters.
- `rdpmc` equivalent: `mrs` instruction reading `PMCCNTR_EL0`.

### 25.4 CPU-only (development / fallback)

Full functionality except device-specific events. Cycles, insns, cache, branches all work. No IBS/PEBS/SPE without vendor support. Used as Crucible's reference tier (MIMIC.md `cpu/` backend).

---

## 26. Kernel version requirements

| Feature | Min kernel |
|---|---|
| `perf_event_open(2)` | 2.6.31 |
| mmap'd PMU metadata page (`cap_user_rdpmc`) | 3.12 |
| `BPF_F_MMAPABLE` | 5.5 |
| BTF / CO-RE | 5.3 |
| `BPF_MAP_TYPE_RINGBUF` | 5.8 |
| `bpf_map_lookup_batch` | 5.6 |
| `MADV_HUGEPAGE` strict alignment | 5.8 (prior kernels silently round) |
| `MADV_COLLAPSE` | 6.1 |
| `io_uring` | 5.1 |

**Minimum supported: Linux 5.8.** Earlier kernels get the /proc fallback mode (per §22.1). Fedora 43 (6.x) and RHEL 9 (5.14+) both supported.

Recommend Linux 6.1+ for MADV_COLLAPSE and improved ringbuf performance.

---

## 27. Build flags

```cmake
option(CRUCIBLE_PERF              "Enable perf counter groups + sense hub consumer"  ON)
option(CRUCIBLE_PERF_BPF          "Enable 6 stack-traced BPF programs"                ON)
option(CRUCIBLE_PERF_REGIONS      "Enable CRUCIBLE_PROFILE_SCOPE macros"              ON)
option(CRUCIBLE_PERF_LINE         "Enable per-line IP sampling + .symbiot output"     ON)
option(CRUCIBLE_PERF_MULTI_LEVEL  "Enable source↔IR↔ASM multi-level view (longer build)"  OFF)
option(CRUCIBLE_PERF_SERVER       "Enable HTTP server for Dioxus dashboard"           ON)
option(CRUCIBLE_PERF_FEDERATION   "Enable cluster-wide gossip aggregation"            ON)
option(CRUCIBLE_PERF_USER         "Enable user-code scopes (Python, C++, dlopen)"     ON)
option(CRUCIBLE_PERF_MODE         "Scope default tier: minimal|standard|rich"         "standard")
```

All flags OFF = zero runtime cost (macros compile to no-op pass-throughs). Hardening discipline: any code under a flag must `#if defined(CRUCIBLE_PERF_xxx)` its symbols so disabling truly removes them from the binary.

Required dependencies (find_package or vendored):
- libbpf ≥ 1.4
- libelf (from elfutils)
- libdw (from elfutils)
- libcapstone ≥ 5.0 (for Line.h disassembly)
- libzstd ≥ 1.5
- nlohmann::json (header-only, vendored under `third_party/`)
- cpp-httplib (header-only, vendored under `third_party/`)

---

## 28. Testing

### 28.1 Unit tests

- `test/perf_tsc_test.cpp` — calibrate; verify rdtsc monotonic, ns rebase accurate
- `test/perf_mmap_seqlock_test.cpp` — read a counter under artificial seq churn
- `test/perf_region_test.cpp` — macro expansion, scope ID registration, ring buffer write
- `test/perf_reconstruct_test.cpp` — tree build from synthetic events
- `test/perf_symbiot_trace_test.cpp` — round-trip serialization
- `test/perf_bpf_consumer_test.cpp` — mock BPF program, verify ringbuf drain
- `test/perf_augur_test.cpp` — drift detection threshold crossings
- `test/perf_gossip_test.cpp` — flatbuffer encode/decode
- `test/perf_http_test.cpp` — endpoint JSON schema conformance

### 28.2 Integration tests

- `test/integration/perf_e2e_bench.cpp` — bench_harness with full profiler attached; verify Report has perf data
- `test/integration/perf_replay_determinism.cpp` — run same workload twice; verify replay state identical while perf data differs in expected ways
- `test/integration/perf_cluster_gossip.cpp` — 3-Keeper virtual cluster; verify cluster-wide aggregation

### 28.3 Performance tests

- `test/perf_bench_scope_enter_exit.cpp` — measure scope cost and gate against the previously-recorded baseline on the dev hardware
- `test/perf_bench_rdpmc_roundtrip.cpp` — measure rdpmc via mmap and gate against baseline
- `test/perf_bench_sense_snap.cpp` — measure AVX2 gather and gate against baseline
- `test/perf_bench_ringbuf_drain_throughput.cpp` — measure ringbuffer drain throughput and gate against baseline

### 28.4 Hardware-specific CI

- Runner with AMD Zen3 for IBS tests
- Runner with Intel Sapphire Rapids for PEBS tests
- Runner with ARM Neoverse N1 for SPE tests
- Runner in KVM with software PMU (correctness only, not perf)

---

## 29. Upgrade path

### 29.1 Schema versioning

- `SymbiotTrace.version` bumped on breaking changes; old files still read
- Gossip flatbuffer: all fields optional with defaults; forward-compatible
- Cipher namespace versioned via sub-path: `cipher://observability/v1/...`

### 29.2 Adding counter classes

New counter class goes in `include/crucible/perf/counters/NewClass.h`. Wire into bench_harness.h via `optional<NewClassCounters>` field. Dashboard endpoints unchanged (handlers auto-serialize new fields via nlohmann::json).

### 29.3 Adding BPF programs

New `.bpf.c` in `include/crucible/perf/bpf/`. Register loader in `BpfProfiler`. Add consumer in `bpf_consumer/`. Gossip schema gets new optional field.

### 29.4 Kernel feature detection

At startup, probe for:
- CAP_PERFMON via `prctl(PR_CAPBSET_READ, CAP_PERFMON)`
- `cap_user_rdpmc` via mmap'd PMU metadata page
- BPF feature support via libbpf feature probes
- Kernel version from `uname(2)` one-time

Cache results in `PerfCapabilities` struct; all downstream layers check this. Missing features → log + graceful degradation.

### 29.5 Migration from old perf data

When binary is rebuilt with new feature flags, `.symbiot_sym` section rebuilt. Old Cipher cache entries for this `build_id` become stale but not invalid (content-addressed, still correct for their schema version). New resolutions use new schema.

---

## 30. Build plan — milestones

Twelve weeks to full system. Each milestone independently useful.

### M1: Core + first counter classes (Week 1)

- `core/Tsc.h`, `core/Vendor.h`, `core/Perf.h`, `core/MmapSeqlock.h`
- `counters/Pipeline.h`, `counters/Cache.h`
- `bench/bench_harness_perf.h` integration
- **Payoff**: every bench Report footer shows IPC, L1/L2/L3 miss rates

### M2: Remaining counters (Week 2)

- `counters/Bandwidth.h`, `counters/Energy.h`, `counters/Simd.h`, `counters/MemLatency.h`
- `core/DynamicPmu.h`
- **Payoff**: bench shows GB/s, watts, vector %, per-load cache-level

### M3: BPF consumer (Week 3)

- `bpf_consumer/Loader.h`, `bpf_consumer/SenseMmap.h`, `bpf_consumer/RingbufReader.h`
- `bpf_consumer/PidFilter.h`
- Rewrite the 4 BPF programs (sched_switch, syscall_latency, lock_contention, pmu_sample) to use ringbuf instead of hash map
- **Payoff**: bench shows off-CPU, syscall latency, lock contention, PMU samples — all zero-syscall drain

### M4: Region engine (Week 4)

- `region/ScopeTable.h`, `region/ThreadLocal.h`, `region/Region.h`, `region/Sample.h`, `region/Reconstruct.h`
- CRUCIBLE_PROFILE_SCOPE deployment on Forge/Mimic/Cipher background threads
- **Payoff**: per-function attribution in Crucible internals

### M5: Symbol resolution + line profiler (Weeks 5-6)

- `symbols/Embed.h`, `symbols/Dwarf.h`, `symbols/Cache.h` (Cipher-backed)
- `symbols/BuildId.h`, `symbols/Demangle.h`
- `line/Sampler.h`, `line/BpfPmu.h`, `line/Aggregator.h`, `line/SymbiotTrace.h`
- Post-link tool `crucible-embed-symtab` for `.symbiot_sym` generation
- **Payoff**: per-line attribution for any function, `.symbiot` trace per bench

### M6: Augur engine (Week 7)

- `augur/Residual.h`, `augur/Drift.h`, `augur/Regression.h`
- `augur/MFU.h`, `augur/Collective.h`, `augur/Recommend.h`
- Forge Phase L VALIDATE integration
- **Payoff**: CRUCIBLE.md §13 Augur becomes runnable; drift-triggered recompile works

### M7: Broker + HTTP server (Week 8)

- `broker/Server.h`, `broker/Endpoints.h`, `broker/Local.h`
- `broker/NodeBuckets.h`
- cpp-httplib integration, JSON endpoints
- **Payoff**: Dioxus can connect to a Keeper and see full local data

### M8: Federation (Week 9)

- `broker/Gossip.h`, `broker/Cache.h`, `broker/Federation.h`
- FlatBuffers schema + CNTP channel integration
- `/api/v1/cluster/*` endpoints
- **Payoff**: one Dioxus dashboard sees entire Canopy live

### M9: Multi-level view (Weeks 10-11)

- `symbols/MultiLevel.h`, `symbols/IrParser.h`, `symbols/AsmParser.h`
- Post-link tool `crucible-embed-bundle`
- CMake build pipeline for .ll + .s artifacts
- Source↔IR↔ASM dashboard component
- **Payoff**: any hot function drills down to machine-code attribution

### M10: User integration (Week 12)

- `user/CppScope.h`, `user/Python.h` (pybind11), `user/Rust.h` (FFI)
- LD_AUDIT handler for dlopen'd modules
- Vessel-IR001 scope annotations
- **Payoff**: user custom data loaders / networking / losses appear in dashboard identically

### M11: Crucible-specific visualizations (ongoing)

- Plan timeline, MFU waterfall, Recipe flow, Reduction topology, Forge phase timeline, MAP-Elites browser, Kernel drift heatmap, per-SM timeline, CNTP link flow, Cipher tier heatmap
- Each a dedicated endpoint + Dioxus component

### M12: Hardening (ongoing)

- Adaptive throttling tuning on real workloads
- Cipher symbol cache population at CI time (pre-seed for common binaries)
- Cross-org federation policy
- Dashboard UX refinement with real user feedback

---

## 31. Known limitations and what we get wrong

Honest accounting of what this design doesn't solve perfectly.

### 31.1 Scope enter+exit is still ~20 ns

For the very hottest paths (sub-100 ns code regions), a 20 ns scope guard is 20% overhead. We recommend: don't annotate those regions. Wrap their parents instead.

The only way to get below 20 ns is to drop rdpmc — then we only get wall time, not IPC. The `_WALL` tier does this: ~6 ns, but no IPC.

Fundamental limit: we need at least 3 cycles (rdtsc) + 1 store = ~1 ns minimum per scope. Everything else is kernel/bus interaction.

### 31.2 TSC skew on non-invariant-TSC systems

On old CPUs (pre-2010 Intel, pre-Zen AMD) TSC ticks at variable rate with P-state. Our calibration is wrong under frequency scaling.

Mitigation: detect at startup via `CPUID leaf 0x80000007 bit 8`; fall back to `clock_gettime(CLOCK_MONOTONIC)` (15 ns instead of 3 ns).

### 31.3 rdpmc + CPU migration consistency

Between reading cycles and reading instructions in a group, the thread can migrate. If it does, cycles is from CPU A and insns is from CPU B. IPC computation is wrong for that one scope.

Mitigation: thread pinning for Keeper's dispatch thread (CRUCIBLE.md §14.3). For other threads: rare migrations at typical ~10 Hz; effect on IPC averaging is negligible.

### 31.4 BPF map key space for high-cardinality events

`syscall_latency` keyed on syscall_nr has ~500 possible values — fits in fixed array, covered by Pattern A. But futex address space is huge. Falling back to Pattern B (events in ringbuf) bloats ringbuf at high lock-contention workloads.

Mitigation: ringbuf size 16 MB per program. At 100 B per event, 160 K events buffered. At 10 K lock events/sec, 16 seconds of buffering — plenty.

Still: an adversarial workload with 1 M lock events/sec would overflow. Watchdog drops oldest events; logs.

### 31.5 Symbol resolution staleness for user modules

User dlopen's a new module at runtime. LD_AUDIT handler registers it, but the first IPs encountered still take cold resolution (~500 ms). During the resolution window, those IPs show as raw hex.

Mitigation: async resolution on a worker thread; UI shows "(resolving...)" placeholders; updates when ready.

### 31.6 Multi-level view requires full rebuild

`.symbiot_ml` is baked at link time. Can't be added post-deployment without relinking.

Mitigation: we recommend `CRUCIBLE_PERF_MULTI_LEVEL=ON` in release builds. Dev builds skip it (faster iteration). For external `.so` modules the user controls, user adds the post-link step to their build.

### 31.7 Cluster gossip doesn't preserve per-event fidelity

Gossip carries aggregated summaries, not raw events. "What was thread 42 doing at exactly 12:34:56.789?" requires deep-dive RPC to that node.

Mitigation: the raw events are in Cipher's warm tier for ~24 hours. Dashboard triggers the RPC automatically when user drills into a per-node view.

### 31.8 Determinism under replay is weaker for profile data than for state

Two replays produce identical model state but (necessarily) different real-time wall-clock samples. Profile data is a function of real hardware state, which varies run to run.

This is by design — profile data IS observational, not part of replay state. We can't make it bit-identical without disabling hardware counters.

### 31.9 Energy counters are coarse

RAPL updates at ~1 ms resolution. For per-scope energy attribution, we'd need to sample at 10 kHz+ which isn't possible. Energy attribution is only meaningful at 1-second+ granularity.

We present RAPL watts per node per second. Not per scope.

### 31.10 IBS/PEBS sample rates are limited

IBS on AMD caps at ~100 K samples/sec without overwhelming the NMI handler. At that rate, 1-second bursts of LLC misses > 100 K/sec miss some events.

For 60-second deep dives, period=1 captures everything even at burst rates. For always-on sampling, period=100 K caps at ~1 event per 100 K dispatched ops — statistical, not exhaustive.

This is a hardware limit, not a design choice.

### 31.11 ARM SPE and AMD IBS have different semantics

Our `MemLatency` abstraction unifies them but papers over differences:
- ARM SPE: sampling based on instruction count, arbitrary load instructions
- AMD IBS-Op: sampling based on completed micro-ops, specific to load ops

For precise cross-vendor comparisons, we expose the raw data in addition to the unified abstraction. Advanced users can parse either format.

### 31.12 We can't profile what we can't touch

Closed-source user binaries without debug info show as raw IPs. User decides whether to build with debug info; we can't add it retroactively.

---

## 32. Open questions deferred

- **Intel PT full-trace support**: do we add an on-demand Intel PT path for sub-nanosecond trace of a specific window? Power-user feature, niche. Deferred to post-M11.
- **GPU-side profiling integration**: NVIDIA's CUPTI, AMD's rocprof — these are vendor-specific and live in Mimic per CRUCIBLE.md §19. Perf consumes Mimic's `probe_counters` output; we don't reimplement GPU profiling here. But: how do we correlate host region scopes with GPU kernel samples? Via content_hash of the kernel in the scope's name. TBD on exact protocol.
- **Historical query API**: "show me cluster MFU on 2025-08-15 10:00 UTC". Requires Cipher cold-tier time-series query. Design TBD.
- **Alerting**: drift events could trigger pager notifications. Integration with PagerDuty / Slack / Opsgenie. Out of scope for initial delivery.
- **Anomaly detection**: beyond drift thresholds, could we auto-detect novel perf patterns? ML on perf data — research area, deferred.
- **Privacy for federated learning**: if a Canopy spans organizations with privacy requirements, source line names may leak proprietary algorithm details. Need an anonymization layer. Deferred.
- **Dashboard offline export**: allow dashboard to produce static HTML report for sharing. Nice-to-have post-M11.
- **Live `crucible plan bisect` binding**: wire `crucible plan bisect` subcommand to perf data streams. Depends on plan event tracking being complete.

---

## 33. Glossary

- **Augur**: Crucible's continuous monitoring and drift detection subsystem (CRUCIBLE.md §13). Perf is its data-production engine.
- **BPF_F_MMAPABLE**: flag for BPF array maps enabling userspace mmap access. Load-bearing for zero-syscall reads.
- **BPF ringbuf**: `BPF_MAP_TYPE_RINGBUF`, a shared-memory ring between kernel and userspace (Linux 5.8+).
- **Broker**: the per-Keeper aggregator thread — serves HTTP, listens on UDS, runs gossip.
- **build_id**: ELF NT_GNU_BUILD_ID note, 20-byte unique identifier for a compiled binary.
- **Canopy**: Crucible's fleet mesh (CRUCIBLE.md §7). Gossip-based; no master.
- **Cipher**: Crucible's content-addressed persistence (CRUCIBLE.md §9). Three tiers.
- **CNTP**: Crucible Native Transport Protocol (CRUCIBLE.md §5). RDMA + eBPF + AF_XDP control plane.
- **EventCounts**: 10-field struct of sample counts per event kind (cycles, L1D, LLC, branch, DTLB, IBS-Op, IBS-Fetch, page-fault, CPU-migration, alignment-fault). Same schema as symbiotic.
- **Genesis Kernel Pack**: per-chip precompiled seed kernels (MIMIC.md §39).
- **IBS**: AMD Instruction-Based Sampling. Precise instruction-level sampling with cache-level + latency metadata.
- **IR001 / IR002 / IR003***: Crucible's three-tier IR (CRUCIBLE.md / FORGE.md / MIMIC.md).
- **Keeper**: the per-Relay daemon process.
- **Line profiler**: the subsystem that produces `.symbiot` traces with per-(file, line) attribution.
- **MAP-Elites**: Mimic's search algorithm for kernel configurations.
- **MFU**: Model FLOPS Utilization (achieved/peak TFLOPs).
- **MultiLevelBundle**: build-time artifact bundle (source + LLVM IR + ASM) embedded in `.symbiot_ml` ELF section.
- **NodeSummary**: the per-Keeper flatbuffer broadcast to peers every 10 s via Canopy gossip.
- **PEBS**: Intel Precise Event-Based Sampling. Hardware sampling with minimal skid.
- **perf_event_mmap_page**: kernel-exported mmap metadata page exposing counter state for rdpmc.
- **RegionGuard**: RAII scope guard that records enter/exit events to the per-thread ring buffer.
- **rdpmc**: x86 Read Performance Monitor Counter instruction. Userspace-enabled via mmap protocol.
- **Relay**: a compute node (CLAUDE.md).
- **ScopeTable**: fixed-size BSS table of compile-time-registered scope metadata, indexed by u16 ID.
- **Sense hub**: the 96-counter BPF_F_MMAPABLE array producing kernel-side counters (`sense_hub.bpf.c`).
- **seqlock**: odd/even atomic sequence counter pattern for read-side-lock-free reads (used by perf mmap metadata).
- **SPE**: ARM Statistical Profiling Extension. ARM's equivalent of IBS/PEBS.
- **`.symbiot`**: zstd-compressed JSON trace file format for per-(file, line) perf attribution. Portable with symbiotic project.
- **`.symbiot_sym`**: compact ELF section containing IP→(function, file, line) symbol table for direct runtime lookup without libdw.
- **`.symbiot_ml`**: ELF section containing MultiLevelBundle (source↔IR↔ASM).
- **TraceRing**: Crucible's SPSC dispatch ring (CLAUDE.md L4). Our `enter_tsc` addition makes it the authoritative dispatch timeline.
- **TSC**: Time Stamp Counter. x86 CPU-wide clock source; invariant on modern CPUs.
- **Vessel**: frontend adapter (CRUCIBLE.md §3). Produces IR001 from PyTorch/JAX/etc.

---

## Summary

Perf is Crucible's measurement fabric — the subsystem that makes Augur, Forge Phase L, and Plan introspection go from spec to runtime. It runs in every Crucible binary with sub-0.1% overhead, federates across every Canopy node via existing CNTP gossip, consumes every BPF event and PMU counter without a single hot-path syscall, resolves IPs through Cipher's content-addressed cache, and exposes data through a clean JSON surface the user's Dioxus dashboard consumes. User modules loaded via dlopen or linked via Vessel get identical treatment automatically. The result is a dataset with no industry analog: instruction-level ML workload performance, content-addressed across runs and nodes, real-time federated, always-on.

The dominant design principle throughout: **everything Linux provides synchronously is abysmally slow; we replace every syscall with direct CPU instructions, mmap'd volatile loads, and BPF kernel-side aggregation with zero-syscall drain.** Scope enter/exit costs 5–23 ns. Counter reads cost 8 ns via rdpmc. Sense-hub snapshot costs 5 ns via AVX2 gather. BPF event drain costs 20 ns per event via mmap'd ringbuf. HTTP responses cost 1 µs via atomic shared_ptr cache. Gossip costs 50 KB/s at 100-node scale.

Twelve-week build plan with value-visible milestones every week. Every milestone is self-contained — M1 alone makes every bench Report richer; subsequent milestones add layers that compose cleanly on top. The final system is the measurement substrate that turns Crucible's adaptive-runtime design from paper into empirical feedback loop.

See CRUCIBLE.md §13 (Augur), §14 (Keeper), §17 (Observability), §18 (Security) for the consumer side. See FORGE.md §17 (Phase L VALIDATE) for compile-feedback integration. See MIMIC.md §23 (per-vendor counter access) for the GPU-side boundary.
