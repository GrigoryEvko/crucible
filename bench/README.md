# Crucible bench harness

Per-sample tail-latency benchmarks on a pinned core, with optional kernel-side context via eBPF.
RDTSC bracketed with LFENCE/RDTSCP+LFENCE; percentiles via R type-7 linear interpolation;
optional bootstrap CIs; optional Mann-Whitney U for A/B comparisons.

## Quick start

```
cmake --preset bench
cmake --build --preset bench -j$(nproc)
cmake --build --preset bench --target bench-caps   # one-time sudo, after every relink
./build-bench/bench/bench_arena
```

The `bench-caps` target runs `sudo setcap cap_bpf,cap_perfmon,cap_dac_read_search=eip` on
every bench binary so the eBPF sense hub can load unprivileged. Re-run after any rebuild
(the xattr is destroyed on relink). Without caps, ns/cycle numbers are unchanged but the
sensory grid is omitted — see **How to enable BPF** below.

## Output anatomy

```
  arena.alloc_array<u64>(100)    p50=   5.12  p90=   6.04  p99=   8.77  p99.9=  14.30  max=   42.10  μ=   5.48  σ=  0.71  cv= 3.4%  cyc=  18.3  @4.25GHz  n=100000  cpu3  [batch-avg]  [noisy]
     └─ sched   preempt=·  yield=·  migrate=·  runtime=42.1ms  wait=·  sleep=·  iowait=·  blocked=·  softirq=·  wake_rx=·  wake_tx=·  freq_chg=·  tid_new=·  tid_end=·
     └─ mem     pgflt=17  majflt=·  mmap=·  munmap=·  brk=·  reclaim_n=·  reclaim_t=·  swap_out=·  thp_ok=·  thp_fail=·  numa=·  compact=·  extfrag=·
     └─ io      read=·  write=·  r_ops=·  w_ops=·  disk_r=·  disk_w=·  disk_t=·  disk_n=·  pg_miss=·  readahead=·  unplug=·  throttle=·  fd_open=·
     └─ net     tx=·  rx=·  retrans=·  rst=·  sk_err=·  skb_drop=·  cng_loss=·
     └─ sync    futex=·  futex_t=·  klock=·  klock_t=·
     └─ sys     sig_fatal=·  oom_kills=·  mce=·
```

### First line — timing

| Column | Meaning |
|--------|---------|
| `name` | Label passed to `bench::Run("...")`. |
| `p50` / `p90` / `p99` / `p99.9` / `max` | Percentiles of per-sample ns, via Hyndman & Fan (1996) type 7 linear interpolation. With `[batch-avg]`, each sample is the mean over one auto-batch, so percentiles are over batch means. |
| `μ` | Arithmetic mean of per-sample ns. |
| `σ` | Sample standard deviation (n-1 denominator). |
| `cv` | Coefficient of variation, `σ/μ`, in percent. Anything > 5% is flagged `[noisy]`. |
| `cyc` | Cycles per op, derived as `μ × TSC_frequency` where TSC frequency comes from a 200 ms steady_clock correlation. On a pinned P-core this matches PMU retired cycles within a few percent; on an E-core running below the TSC invariant rate, this figure **overstates** retired cycles proportionally to the frequency ratio. |
| `@X.YYGHz` | sysfs `scaling_cur_freq` at run start. |
| `n` | Sample count. |
| `cpuN` | CPU the bench was actually pinned to (post `sched_setaffinity` + `sched_getcpu`). |

### Flags

| Flag | Condition |
|------|-----------|
| `[batch-avg]` | Auto-batcher ramped batch size by 2× until one batch exceeded 1000 cycles. Percentiles are over batch means, not individual ops. |
| `[noisy]` | `cv > 5%`. Tail percentiles are probably contaminated. Re-run on a quieter core. |
| `[drift]` | First-half `p50` and second-half `p50` differ by more than 10% (only computed when `n ≥ 200`). Indicates cache warm-up, frequency transitions, or scheduler interference mid-run. |
| `[freq-drift]` | sysfs `scaling_cur_freq` moved more than 5% between run start and run end. Cross-reference with `freq_chg` counter. |

### Sensory grid

Each `└─` line shows the delta of the named subsystem's counters over the measured run.
A raised `·` (U+00B7) means zero — the schema stays visible so you can tell "measured zero"
apart from "not measured". Non-zero values auto-scale: counts use `k/M/G`, nanoseconds use
`µs/ms/s`, bytes use `KB/MB/GB`.

## Sensory counter legend

| Label | Subsystem | Unit | Meaning | Typical fix if non-zero |
|-------|-----------|------|---------|-------------------------|
| `preempt` | sched | count | Involuntary context switches. Scheduler evicted our thread. | `isolcpus=` at boot + `.core(N)` on an isolated CPU. |
| `yield` | sched | count | Voluntary context switches. Thread blocked on syscall or sleep. | Benched code is making syscalls; eliminate blocking ops. |
| `migrate` | sched | count | CPU migrations. Thread ran on a different core — cache state lost. | `.core(N)` or set affinity via `taskset`. |
| `runtime` | sched | ns | Time the scheduler accounted as on-CPU for our thread during the run. Expected to roughly track wall time on a pinned core. | Benign; a big gap vs wall time means we were preempted. |
| `wait` | sched | ns | Runqueue wait time — scheduler wanted to run us but the CPU was busy. | Pin to a core not shared with other load; lower system utilization. |
| `sleep` | sched | ns | Time blocked in `S` state (interruptible sleep). | Remove syscalls / sleeps from the benched path. |
| `iowait` | sched | ns | Blocked on I/O. | Cache or prefetch the data path. |
| `blocked` | sched | ns | Time in uninterruptible sleep (D state, typically I/O). | Same. |
| `softirq` | sched | ns | Softirq time stolen on our CPU (net rx, timer, block completion). | Pin IRQs away: `echo <mask> > /proc/irq/<n>/smp_affinity`. |
| `wake_rx` / `wake_tx` | sched | count | Wakeups received by / sent from our thread. | High values mean CV-style synchronization; make it lock-free. |
| `freq_chg` | sched | count | CPU frequency transitions on our CPU during the run. Correlates with `[freq-drift]`. | `cpupower frequency-set -g performance`, disable turbo. |
| `tid_new` / `tid_end` | sched | count | Threads created / exited system-wide. | Benched code should not spawn threads; if system did, pin tighter. |
| `pgflt` | mem | count | Minor page faults. Fresh page touched, zero-filled or COW satisfied from RAM. | Pre-fault the arena with `mlock` or `madvise(MADV_POPULATE_WRITE)`; reuse pages. |
| `majflt` | mem | count | Major page faults. Page fetched from disk / swap. | Add RAM, kill swap, pre-fault. |
| `mmap` / `munmap` | mem | count | `mmap` / `munmap` syscall invocations. | Benched code should allocate upfront; arena-only on hot path. |
| `brk` | mem | count | `brk`/`sbrk` syscalls (heap grow). | Glibc fell back for a large alloc; pre-reserve. |
| `reclaim_n` / `reclaim_t` | mem | count / ns | Direct reclaim invocations and time. Your syscall was parked running the MM reclaimer. | Reduce memory pressure; add RAM; `vm.min_free_kbytes`. |
| `swap_out` | mem | count | Pages written to swap. | Disable swap for benches: `swapoff -a` or isolate via cgroup. |
| `thp_ok` / `thp_fail` | mem | count | Transparent huge page collapses succeeded / failed. | Either is informational; non-zero fails hint at fragmentation. |
| `numa` | mem | count | NUMA pages migrated. | Pin to the socket owning the arena, or `numactl --membind`. |
| `compact` | mem | count | Compaction stalls. | Fragmentation is forcing the MM to shuffle pages; lower THP pressure. |
| `extfrag` | mem | count | External-fragmentation events. | Same as above. |
| `read` / `write` | io | bytes | `read`/`write` syscall bytes. | Benched code is hitting the VFS; remove I/O from the hot path. |
| `r_ops` / `w_ops` | io | count | `read`/`write` syscall invocations. | Same. |
| `disk_r` / `disk_w` / `disk_t` / `disk_n` | io | bytes / ns / count | Block-layer read/write bytes, total I/O latency, and completion count. | Move bench data into RAM; use `O_DIRECT` only when benching the device. |
| `pg_miss` | io | count | Page-cache misses forcing backing-store reads. | Warm the cache first (`cat file > /dev/null`). |
| `readahead` | io | count | Readahead pages queued. | Benign unless dominating; tune `blockdev --setra 0`. |
| `unplug` | io | count | Block-layer queue unplugs. | Informational. |
| `throttle` | io | count | Writeback-throttled jiffies. | Writer running at a faster-than-dirty-limit pace. |
| `fd_open` | io | count | `open`-family syscalls. | Benched code is opening files; pre-open fds. |
| `tx` / `rx` | net | bytes | Net TX / RX bytes. | Benched code is sending/receiving; unrelated traffic means wrong core pin. |
| `retrans` | net | count | TCP retransmits. | Packet loss on the measurement path. |
| `rst` | net | count | TCP RST sent. | Connection abort. |
| `sk_err` | net | count | TCP-socket error count. | Check `ss -s`. |
| `skb_drop` | net | count | Dropped sk_buffs. | Receiver overrun or firewall drop. |
| `cng_loss` | net | count | TCP congestion-loss events. | Same as `retrans`. |
| `futex` | sync | count | Futex syscall invocations (mutex / condvar fast-path fell through). | Reduce contention; use lock-free primitives. |
| `futex_t` | sync | ns | Total ns spent blocked on futex. | Same. |
| `klock` | sync | count | Kernel lock acquisitions attributed to our context. | Fewer syscalls, or faster ones. |
| `klock_t` | sync | ns | Total ns inside kernel locks. | Same. |
| `sig_fatal` | sys | count | Fatal signals delivered system-wide during the run. | Usually zero; non-zero means another process is crashing. |
| `oom_kills` | sys | count | OOM-killer invocations system-wide. | Add RAM; shrink working set. |
| `mce` | sys | count | Machine check exceptions. | Hardware is failing; `mcelog` for details. |

Gauge fields that only make sense as a snapshot (`FD_CURRENT`, `TCP_MIN_SRTT_US`,
`TCP_MAX_SRTT_US`, `TCP_LAST_CWND`, `SIGNAL_LAST_SIGNO`, `THERMAL_MAX_TRIP`,
`RSS_*`) are exposed in `Snapshot` but intentionally omitted from the text grid —
their delta is meaningless.

## How to enable BPF

The BPF sense hub needs three capabilities:

- `CAP_BPF` — `bpf_prog_load` for tracepoint programs (kernel ≥ 5.8).
- `CAP_PERFMON` — `perf_event_open` on tracepoint ids.
- `CAP_DAC_READ_SEARCH` — read `/sys/kernel/tracing/events/*/id` (mode 640 root:root).
  Without this, `bpf_program__attach()` silently fails per tracepoint.

And the sysctl permits unprivileged BPF:

```
sudo sysctl kernel.unprivileged_bpf_disabled=0
```

Persist in `/etc/sysctl.d/99-perf.conf`:

```
kernel.unprivileged_bpf_disabled = 0
```

The preferred workflow is the `bench-caps` target, which batches `setcap` on every bench
binary in one sudo invocation:

```
cmake --build --preset bench --target bench-caps
```

The ELF xattr is destroyed on every relink, so re-run after rebuilds. If BPF still fails to
load, set `CRUCIBLE_PERF_VERBOSE=1` (or the legacy `CRUCIBLE_BENCH_BPF_VERBOSE=1`) to see
libbpf diagnostics on stderr.

## Environment variables

The `CRUCIBLE_PERF_*` names are the canonical post-promotion (GAPS-004a, 2026-05-03) form:
the SenseHub now lives at `<crucible/perf/SenseHub.h>` and is shared between bench and
production code. The legacy `CRUCIBLE_BENCH_BPF_*` names are still honoured as
backward-compatible aliases for existing bench workflows; either form works at any time.

| Variable | Controls | Default | Example |
|----------|----------|---------|---------|
| `CRUCIBLE_BENCH_SAMPLES` | Per-bench sample count when `.samples(N)` not set. | `100000` | `CRUCIBLE_BENCH_SAMPLES=1000000 ./bench_arena` |
| `CRUCIBLE_BENCH_CORE` | CPU to pin to (read by individual bench mains). `-1` → harness auto-pick (first isolcpu, else current). | `-1` | `CRUCIBLE_BENCH_CORE=3 ./bench_arena` |
| `CRUCIBLE_BENCH_JSON` | Emit one-JSON-object-per-bench instead of text when non-empty and not `"0"`. | unset | `CRUCIBLE_BENCH_JSON=1 ./bench_arena > out.jsonl` |
| `CRUCIBLE_PERF_VERBOSE` (or legacy `CRUCIBLE_BENCH_BPF_VERBOSE`) | Route libbpf log callbacks to stderr (load / verifier diagnostics). | unset | `CRUCIBLE_PERF_VERBOSE=1 ./bench_arena` |
| `CRUCIBLE_PERF_QUIET` (or legacy `CRUCIBLE_BENCH_BPF_QUIET`) | Suppress the one-line BPF load-failure diagnostic. | unset | `CRUCIBLE_PERF_QUIET=1 ./bench_arena` |

## Interpreting "clean" vs non-clean

If every `└─` line is mostly `·` with at most a small non-zero `sched.runtime` (which
should roughly track wall time on a pinned core) and a modest `mem.pgflt` from first-touch
faults, the ns/cycle numbers above are trustworthy. Any non-zero on `preempt`, `yield`,
`migrate`, `majflt`, `softirq`, `iowait`, `futex`, `swap_out`, or the `io`/`net` rows means
the kernel was doing work on our behalf during the measured region, and the tail percentiles
(p99, p99.9, max) are contaminated. Re-run on a quieter core — ideally boot with
`isolcpus=<N>` and pass `.core(N)` / `CRUCIBLE_BENCH_CORE=N` — before treating those tails
as signal.

## Methodology citations

- Intel, *How to Benchmark Code Execution Times on Intel IA-32 and IA-64 Instruction Set Architectures* (2010) — LFENCE + RDTSC / RDTSCP + LFENCE bracketing.
- Hyndman & Fan (1996), *Sample Quantiles in Statistical Packages* — type 7 linear-interpolation percentiles (R default).
- Efron (1979), *Bootstrap Methods: Another Look at the Jackknife* — resampled confidence intervals on percentile estimators.
- Mann & Whitney (1947), *On a Test of Whether one of Two Random Variables is Stochastically Larger than the Other* — rank-sum U test for A/B distinguishability.
