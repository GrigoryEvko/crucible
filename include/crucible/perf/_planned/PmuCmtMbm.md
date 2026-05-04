# `PmuCmtMbm` — Intel CMT (LLC occupancy) + MBM (memory bandwidth) per-process

**STATUS**: doc-only stub.  Tier-B.  Eventual path:
`include/crucible/perf/PmuCmtMbm.h`.  Userspace-only.

## Problem

Cache Monitoring Technology (CMT) and Memory Bandwidth Monitoring
(MBM) are Intel RDT primitives that ATTRIBUTE LLC occupancy + memory
bandwidth to a process (or class of service / cgroup).  Unlike
uncore_imc which is system-wide, this answers "how much LLC and DRAM
BW is OUR workload using vs the noisy neighbors".

Critical for noisy-neighbor diagnosis on shared-cache deployments
(public cloud, multi-tenant edge).

## Mechanism

Intel RDT exposed via `resctrl` filesystem AND via `intel_cqm`
perf events.  Two paths:

1. **resctrl path** (file-based):
   ```
   /sys/fs/resctrl/CRUCIBLE_GROUP/mon_data/mon_L3_00/llc_occupancy
   /sys/fs/resctrl/CRUCIBLE_GROUP/mon_data/mon_L3_00/mbm_total_bytes
   /sys/fs/resctrl/CRUCIBLE_GROUP/mon_data/mon_L3_00/mbm_local_bytes
   ```
   Setup: mkdir + echo `<pid>` to `tasks` file.

2. **perf_event path** — REMOVED.  arch/x86/events/intel/cqm.c was
   deleted from mainline; PERF_TYPE_INTEL_CQM is no longer available.
   Only the resctrl filesystem remains.

## API shape

```cpp
struct PmuCmtMbmSnapshot {
    uint64_t llc_occupancy_bytes;        // current LLC bytes used by us
    uint64_t mbm_total_bytes;            // cumulative DRAM BW (local + remote)
    uint64_t mbm_local_bytes;            // local-socket BW only
    [[nodiscard]] PmuCmtMbmSnapshot operator-(...) const noexcept;
    [[nodiscard]] uint64_t remote_bytes() const noexcept; // total - local
};

class PmuCmtMbm {
public:
    [[nodiscard]] static std::optional<PmuCmtMbm> load(::crucible::effects::Init) noexcept;
    [[nodiscard]] PmuCmtMbmSnapshot read() const noexcept;
    // Lifecycle: load() creates resctrl group + adds our PIDs;
    // dtor removes group.  Tag inherits across thread spawn via
    // tasks file write at thread create time.
};
```

## Cost

- `read()`: 3 file reads × ~5 µs each ≈ 15 µs total.  Acceptable for
  bench-end; not for per-iteration.
- Per-iteration: 0 ns (resctrl tracks in HW; we read snapshot at end).

## Known limits

- Intel (Skylake-SP+) AND AMD (Zen2+ via resctrl, see
  arch/x86/kernel/cpu/resctrl/core.c X86_VENDOR_AMD branches and
  MBM_CNTR_WIDTH_OFFSET_AMD).  Load() inspects /sys/fs/resctrl/info
  to detect supported events; returns nullopt only when CONFIG_X86_CPU_RESCTRL
  is off or the CPU does not advertise any L3_MON capabilities.
- LLC occupancy is INSTANTANEOUS (gauge); use snapshot value
  directly, not delta.
- MBM has counter-wrap concerns at high BW (24-bit counter on some
  uarchs).  Kernel reports as 64-bit via resctrl, handles wrap.
- resctrl group cleanup: ALWAYS rmdir on shutdown (or sysctl-cleanup
  takes hours).  Pinned via `Linear<resctrl_group>` + RAII dtor.
- Multi-process Crucible (e.g., bench harness + workload child):
  put both in same resctrl group via tasks-file appends.

## Sibling refs

- `PmuUncoreImc.md` — system-wide BW; this is per-process
- `iter_cgroup.bpf.c` — cgroup hierarchy; resctrl group is its own
  hierarchy parallel to cgroup
