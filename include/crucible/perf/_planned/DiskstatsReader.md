# `DiskstatsReader` — `/proc/diskstats` per-block-device I/O statistics

**STATUS**: doc-only stub.  Tier-3 cheap polling reader.  Eventual
path: `include/crucible/perf/DiskstatsReader.h`.  Userspace-only.

## Problem

`/proc/diskstats` (verified exists on 6.17) exposes per-block-device
I/O statistics in a 17-column format documented in
`Documentation/admin-guide/iostats.rst`:

```
8       0 sda  reads  read_merges  read_sectors  read_ms
              writes write_merges write_sectors write_ms
              ios_in_progress  io_ms  weighted_io_ms
              discards discard_merges discard_sectors discard_ms
              flushes flush_ms
```

For Cipher cold-tier (NVMe) bench attribution: per-device IOPS,
queue depth (`ios_in_progress`), and request latency
(`weighted_io_ms / ios`).  Coarser than `block_rq.bpf.c` (per-bio
events) and `nvme_rq.bpf.c` (per-NVMe-command), but zero kernel-side
cost.

## Mechanism

Polled at 1 Hz.  Per-poll: ~30 µs (parse 5-50 device rows).

## Wire contract

```cpp
struct DiskstatsRow {
    uint16_t major;
    uint16_t minor;
    char     name[32];                // sda, nvme0n1, dm-0, ...
    uint64_t reads;
    uint64_t read_merges;
    uint64_t read_sectors;
    uint64_t read_ms;
    uint64_t writes;
    uint64_t write_merges;
    uint64_t write_sectors;
    uint64_t write_ms;
    uint32_t ios_in_progress;          // INSTANTANEOUS (queue depth)
    uint64_t io_ms;                    // total time with I/O in flight
    uint64_t weighted_io_ms;           // queue-time weighted (capacity proxy)
    uint64_t discards;
    uint64_t discard_merges;
    uint64_t discard_sectors;
    uint64_t discard_ms;
    uint64_t flushes;
    uint64_t flush_ms;
    uint64_t snapshot_ts_ns;
};
class DiskstatsReader {
    std::span<const DiskstatsRow> snapshot();
    // Per-device derived metrics (delta-based):
    double iops_read(uint16_t major, uint16_t minor);
    double iops_write(uint16_t major, uint16_t minor);
    double avg_latency_ms_read(...);
    double queue_depth_avg(...);       // io_ms_delta / wall_ns_delta
};
```

## Bench harness display

```
└─ disk: nvme0n1 r=12K iops (78µs avg, qd=2.3)  w=4K iops (130µs avg, qd=0.8)
         dm-0 r=8K iops (LVM atop nvme0n1)
```

## Cost model

- Per-poll: ~30 µs.
- 1 Hz cadence: 0.003% CPU.  Effectively free.

## Known limits

- Coarse per-device.  For per-process attribution use `block_rq.bpf.c`
  + `iter_task` (planned) for fd→inode→device lookup.
- `weighted_io_ms` is queue-depth proxy (not exact instantaneous
  depth — `ios_in_progress` is the instantaneous read).
- Logical devices (LVM dm-*, mdraid md*) sum the underlying physical
  devices; double-counting if both queried.  Tag rows by type.
- NVMe-PMU (planned, but `NvmePmu.md` was DELETED — no such kernel
  driver) — per-NVMe-queue stats need `nvme_rq.bpf.c` instead.

## Sibling refs

- **Aggregate complement** to: `block_rq.bpf.c` (per-bio timing)
  and `nvme_rq.bpf.c` (per-NVMe-command timing).
- **Cipher cold-tier**: pair with VFS `vfs_hot.bpf.c` (planned) +
  `filemap.bpf.c` for "which file → which device → how busy".
- **Bench reliability**: high disk queue depth during a CPU-only
  bench window indicates third-party I/O contamination — flag inline.
