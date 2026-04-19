# Unused BPF Programs

The `.bpf.c` files in this directory are **not currently compiled or loaded** by
the Crucible bench harness. They are preserved here as a reference seed for
future wiring. If none of them ever get wired, delete the directory.

## Source

Copied verbatim from `~/iprit/symbiotic/bpf/` at the last sync on
**2026-04-19**. `symbiotic` is a separate project (an embedded profiling
runtime for Rust) that already uses these programs against production kernels.
They were dropped here so that the Crucible bench harness has a starting point
when it eventually grows PMU-sample / scheduler / syscall-latency / lock-
contention senses beyond the single `sense_hub.bpf.c` program currently
compiled in `bench/bpf/`.

## Status

- Not referenced by `bench/CMakeLists.txt`.
- Not loaded by `bench/bpf_senses.cpp`.
- `bench/CMakeLists.txt` explicitly names `bpf/sense_hub.bpf.c` as the sole
  BPF source; there is no glob that would pick up files in this directory, so
  moving these out of `bench/bpf/` does not change what gets built.
- The files compile standalone against a kernel with BTF, but the Crucible
  bench binary neither links their bytecode nor attaches their programs.

## Intent

When wiring any of these into the bench harness:

1. Move the file back up one directory: `git mv _unused/foo.bpf.c ../foo.bpf.c`
   (drop the `_unused/` prefix).
2. Extend `bench/CMakeLists.txt`: add a second `add_custom_command` block
   mirroring the `sense_hub.bpf.c` → `sense_hub.bpf.o` → xxd embed pipeline,
   or factor that pipeline into a function and invoke it per program.
3. Extend `bench/bpf_senses.cpp` (or add a peer loader) with the matching
   `extern unsigned char foo_bpf_bytecode[]` + `bpf_object__open_mem` +
   per-program `bpf_program__attach` calls.
4. Declare any new maps in `bench/bpf_senses.h` so userspace readers can mmap
   them.
5. Apply the same `cap_bpf,cap_perfmon,cap_dac_read_search=eip` setcap via
   the existing `bench-caps` target — no new caps needed for these programs.

## Pitfall

These BPF programs read kernel tracepoint contexts (`struct trace_event_raw_*`,
`struct pt_regs`, task\_struct fields). The exact field layouts change between
kernel versions, and **the copies in this directory were last validated
against whatever kernel was running when symbiotic shipped them**. Before
enabling any of these:

- Verify the tracepoint format against
  `/sys/kernel/tracing/events/<subsystem>/<event>/format` on the target
  kernel. Field offsets, sizes, and even field names drift.
- Prefer the BTF-typed variants (`*_tp_btf.bpf.c`) when a choice exists —
  they follow kernel BTF relocations automatically instead of encoding a
  snapshot of the tracepoint struct.
- For `pmu_sample.bpf.c` in particular, the `bpf_perf_event_data` layout is
  stable, but the PMU event configuration (hardware counter IDs, sample
  frequency) must match the bench harness's event-open code — currently
  there is none.
- Re-run `clang -target bpf -O2 -c <file>` against the checked-out kernel's
  vmlinux BTF before assuming a copy from this directory will load.
