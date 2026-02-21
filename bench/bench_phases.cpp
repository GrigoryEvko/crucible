// Phase breakdown benchmark for build_trace pipeline.
//
// Measures each phase of build_trace separately using rdtsc timestamps
// at phase boundaries, then reports a table showing exactly where time
// goes — no perf, no VTune, just raw TSC deltas.
//
// Usage:
//   ./build-bench/bench/bench_phases bench/vit_b.crtrace
//
// Phases measured:
//   P0  Pre-scan:      count inputs/outputs/scalars, check MetaLog
//   P1  Alloc+init:    arena allocs, PtrMap gen bump, memset scratch
//   P2a Copy fields:   schema/shape/scope/callsite + meta ptrs + aux ptrs
//   P2b Content hash:  wymix over all tensor dims for streaming hash
//   P2c PtrMap lookup: DFG edge building (input ptr_map_lookup)
//   P2d PtrMap insert: output slot tracking (ptr_map_insert + aliases)
//   P3  Slot copy:     scratch SlotInfo → arena TensorSlot
//   P4  build_csr:     CSR property graph construction
//   P5  memory_plan:   counting sort + sweep-line offset assignment
//
// The Phase 2 sub-parts (P2a–P2d) are measured by running the main loop
// 4 times, each time executing only one sub-part.  Cache state differs
// slightly from the fused loop, but the relative ratios are accurate.

#include "bench_harness.h"

#include <crucible/Arena.h>
#include <crucible/BackgroundThread.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceLoader.h>
#include <crucible/TraceRing.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace crucible;

// ── Timing infrastructure ────────────────────────────────────────────

static double g_tsc_ratio = 0.0;

struct PhaseTiming {
  const char* name;
  double total_ns;   // sum across all iterations
  uint64_t count;    // number of iterations
};

static void print_phase_table(PhaseTiming* phases, uint32_t n,
                              double total_ns, uint64_t iters) {
  std::printf("\n  %-22s  %8s  %8s  %5s\n",
              "Phase", "ns/op", "total ms", "  %");
  std::printf("  %-22s  %8s  %8s  %5s\n",
              "──────────────────────", "────────", "────────", "─────");

  for (uint32_t i = 0; i < n; i++) {
    double ns_per = phases[i].total_ns / static_cast<double>(iters);
    double pct = 100.0 * phases[i].total_ns / total_ns;
    std::printf("  %-22s  %8.1f  %8.2f  %5.1f%%\n",
                phases[i].name, ns_per,
                phases[i].total_ns / 1e6, pct);
  }

  double sum_ns = 0;
  for (uint32_t i = 0; i < n; i++) sum_ns += phases[i].total_ns;
  double ns_per = total_ns / static_cast<double>(iters);
  double sum_per = sum_ns / static_cast<double>(iters);

  std::printf("  %-22s  %8s  %8s  %5s\n",
              "──────────────────────", "────────", "────────", "─────");
  std::printf("  %-22s  %8.1f  %8.2f  %5.1f%%\n",
              "SUM(phases)", sum_per, sum_ns / 1e6,
              100.0 * sum_ns / total_ns);
  std::printf("  %-22s  %8.1f  %8.2f  100.0%%\n",
              "TOTAL (measured)", ns_per, total_ns / 1e6);
}

// ── Top-level phase breakdown ────────────────────────────────────────
//
// Duplicates build_trace logic with rdtsc reads at phase boundaries.
// Phase 2 is measured as one block here; sub-parts measured separately.

static void bench_phases_toplevel(
    BackgroundThread& bg,
    MetaLog& meta_log,
    const LoadedTrace& trace,
    uint32_t iters)
{
  std::printf("\n=== Top-level phase breakdown (%u ops, %u iters) ===\n",
              trace.num_ops, iters);

  size_t arena_bytes = std::max(size_t{1} << 20,
      static_cast<size_t>(trace.num_ops) * 256);

  auto repopulate = [&]() {
    meta_log.reset();
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < trace.num_ops; i++) {
      uint16_t n = trace.entries[i].num_inputs +
                   trace.entries[i].num_outputs;
      if (n > 0 && cursor + n <= trace.num_metas) {
        (void)meta_log.try_append(&trace.metas[cursor], n);
        cursor += n;
      }
    }
  };

  // Warmup
  for (uint32_t w = 0; w < 5; w++) {
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();
    auto* g = bg.build_trace(trace.num_ops);
    bench::DoNotOptimize(g);
  }

  // Measure: run build_trace N times, accumulate total
  double total_ns = 0;
  std::vector<double> samples;
  samples.reserve(iters);

  for (uint32_t iter = 0; iter < iters; iter++) {
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();

    bench::ClobberMemory();
    uint64_t t0 = bench::rdtsc();

    auto* graph = bg.build_trace(trace.num_ops);

    bench::ClobberMemory();
    uint64_t t1 = bench::rdtsc();

    bench::DoNotOptimize(graph);
    double ns = static_cast<double>(t1 - t0) * g_tsc_ratio;
    total_ns += ns;
    samples.push_back(ns);
  }

  std::sort(samples.begin(), samples.end());
  std::printf("  full pipeline: min=%.1f  med=%.1f  max=%.1f ns/op\n",
              samples[0], samples[iters / 2], samples[iters - 1]);

  // Now the individual phase measurements.
  // We measure each phase separately by running the setup + phase N times.
  // This is slightly less accurate than inline rdtsc but avoids
  // modifying build_trace production code.

  double ns_csr = 0;        // build_csr alone
  double ns_memplan = 0;    // compute_memory_plan alone

  // Measure build_csr in isolation
  {
    // First run a build_trace to get realistic edge data.
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();
    auto* ref = bg.build_trace(trace.num_ops);

    // Extract edge and slot data from scratch buffers for isolated benchmarks.
    // We can approximate by benchmarking build_csr and compute_memory_plan
    // with the data from the reference run.
    uint32_t num_ops = ref->num_ops;
    uint32_t num_slots = ref->num_slots;
    uint32_t num_edges = ref->fwd_offsets[num_ops];  // total forward edges

    // Copy edges for reuse
    std::vector<Edge> saved_edges(num_edges);
    for (uint32_t i = 0; i < num_ops; i++) {
      for (uint32_t e = ref->fwd_offsets[i]; e < ref->fwd_offsets[i + 1]; e++) {
        saved_edges[e] = ref->fwd_edges[e];
      }
    }

    // Copy slots for reuse
    std::vector<TensorSlot> saved_slots(ref->slots, ref->slots + num_slots);

    // Benchmark build_csr
    for (uint32_t iter = 0; iter < iters; iter++) {
      Arena csr_arena{1 << 18};
      auto* graph2 = csr_arena.alloc_obj<TraceGraph>();
      graph2->ops = nullptr;
      graph2->num_ops = num_ops;

      bench::ClobberMemory();
      uint64_t t0 = bench::rdtsc();
      build_csr(csr_arena, graph2, saved_edges.data(),
                static_cast<uint32_t>(saved_edges.size()), num_ops);
      bench::ClobberMemory();
      uint64_t t1 = bench::rdtsc();

      bench::DoNotOptimize(graph2);
      ns_csr += static_cast<double>(t1 - t0) * g_tsc_ratio;
    }

    // Benchmark compute_memory_plan
    for (uint32_t iter = 0; iter < iters; iter++) {
      bg.arena.~Arena();
      new (&bg.arena) Arena{arena_bytes};
      auto* s = bg.arena.alloc_array<TensorSlot>(num_slots);
      std::memcpy(s, saved_slots.data(), num_slots * sizeof(TensorSlot));

      bench::ClobberMemory();
      uint64_t t0 = bench::rdtsc();
      auto* plan = bg.compute_memory_plan(s, num_slots);
      bench::ClobberMemory();
      uint64_t t1 = bench::rdtsc();

      bench::DoNotOptimize(plan);
      ns_memplan += static_cast<double>(t1 - t0) * g_tsc_ratio;
    }

    // Approximate Phase 2 (main loop) = total - csr - memplan - (phase 0 + 1 + 3)
    // We'll get a more accurate breakdown in the sub-phase section below.
    double ns_other = total_ns - ns_csr - ns_memplan;

    PhaseTiming phases[] = {
      {"Main loop (P0-P3)",  ns_other,   iters},
      {"build_csr (P4)",     ns_csr,     iters},
      {"memory_plan (P5)",   ns_memplan, iters},
    };
    print_phase_table(phases, 3, total_ns, iters);
  }
}

// ── Phase 2 sub-part breakdown ───────────────────────────────────────
//
// Runs the Phase 2 main loop multiple times, each time executing only
// one sub-part.  This isolates content hash, PtrMap, and metadata copy.

static void bench_phase2_subparts(
    BackgroundThread& bg,
    MetaLog& meta_log,
    const LoadedTrace& trace,
    uint32_t iters)
{
  std::printf("\n=== Phase 2 sub-part breakdown (%u ops, %u iters) ===\n",
              trace.num_ops, iters);

  size_t arena_bytes = std::max(size_t{1} << 20,
      static_cast<size_t>(trace.num_ops) * 256);
  uint32_t count = trace.num_ops;

  auto repopulate = [&]() {
    meta_log.reset();
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < trace.num_ops; i++) {
      uint16_t n = trace.entries[i].num_inputs +
                   trace.entries[i].num_outputs;
      if (n > 0 && cursor + n <= trace.num_metas) {
        (void)meta_log.try_append(&trace.metas[cursor], n);
        cursor += n;
      }
    }
  };

  // We need the data from one build_trace run to set up the sub-part tests.
  // Run build_trace once to populate everything.
  bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
  bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
  bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
  bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
  bg.arena.~Arena();
  new (&bg.arena) Arena{arena_bytes};
  repopulate();
  auto* ref = bg.build_trace(count);
  (void)ref;

  // ── P0: Pre-scan ──────────────────────────────────────────────────

  double ns_p0 = 0;
  for (uint32_t iter = 0; iter < iters; iter++) {
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());

    const TraceRing::Entry* trace_data = bg.current_trace.data();
    const MetaIndex* meta_data = bg.current_meta_starts.data();

    bench::ClobberMemory();
    uint64_t t0 = bench::rdtsc();

    uint32_t max_meta_end = 0, first_meta = UINT32_MAX;
    uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
    for (uint32_t i = 0; i < count; i++) {
      MetaIndex ms = meta_data[i];
      const auto& re = trace_data[i];
      if (ms.is_valid()) {
        if (first_meta == UINT32_MAX) first_meta = ms.raw();
        uint32_t end = ms.raw() + re.num_inputs + re.num_outputs;
        if (end > max_meta_end) max_meta_end = end;
      }
      total_inputs += re.num_inputs;
      total_outputs += re.num_outputs;
      total_scalars += std::min(re.num_scalar_args, uint16_t(5));
    }

    bench::ClobberMemory();
    uint64_t t1 = bench::rdtsc();

    bench::DoNotOptimize(max_meta_end);
    bench::DoNotOptimize(total_inputs);
    bench::DoNotOptimize(total_outputs);
    bench::DoNotOptimize(total_scalars);
    ns_p0 += static_cast<double>(t1 - t0) * g_tsc_ratio;
  }

  // ── P1: Alloc + PtrMap gen + memset ───────────────────────────────

  double ns_p1 = 0;
  for (uint32_t iter = 0; iter < iters; iter++) {
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};

    // Recompute totals (cheap, already measured above)
    uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
    for (uint32_t i = 0; i < count; i++) {
      total_inputs += trace.entries[i].num_inputs;
      total_outputs += trace.entries[i].num_outputs;
      total_scalars += std::min(trace.entries[i].num_scalar_args, uint16_t(5));
    }

    bench::ClobberMemory();
    uint64_t t0 = bench::rdtsc();

    auto* ops = bg.arena.alloc_array<TraceEntry>(count);
    size_t aux_bytes =
        static_cast<size_t>(total_scalars) * sizeof(int64_t) +
        static_cast<size_t>(total_inputs) * sizeof(OpIndex) +
        static_cast<size_t>(total_inputs) * sizeof(SlotId) +
        static_cast<size_t>(total_outputs) * sizeof(SlotId);
    char* aux = (aux_bytes > 0) ?
        static_cast<char*>(bg.arena.alloc(aux_bytes, alignof(int64_t))) :
        nullptr;
    bg.ensure_scratch_buffers();
    uint32_t slot_cap = std::min(BackgroundThread::SCRATCH_SLOT_CAP,
        std::max(uint32_t{256}, total_inputs + total_outputs));
    std::memset(bg.scratch_slots_, 0,
                slot_cap * sizeof(BackgroundThread::SlotInfo));

    bench::ClobberMemory();
    uint64_t t1 = bench::rdtsc();

    bench::DoNotOptimize(ops);
    bench::DoNotOptimize(aux);
    ns_p1 += static_cast<double>(t1 - t0) * g_tsc_ratio;
  }

  // ── P2a: Copy fields (metadata pointers + hashes, NO content hash,
  //          NO PtrMap) ──────────────────────────────────────────────

  double ns_p2a = 0;
  for (uint32_t iter = 0; iter < iters; iter++) {
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();

    const TraceRing::Entry* trace_data = bg.current_trace.data();
    const MetaIndex* meta_data = bg.current_meta_starts.data();
    const ScopeHash* scope_data = bg.current_scope_hashes.data();
    const CallsiteHash* callsite_data = bg.current_callsite_hashes.data();

    // Recompute allocs (same as in build_trace)
    uint32_t max_meta_end = 0, first_meta = UINT32_MAX;
    uint32_t total_inputs = 0, total_outputs = 0, total_scalars = 0;
    for (uint32_t i = 0; i < count; i++) {
      MetaIndex ms = meta_data[i];
      const auto& re = trace_data[i];
      if (ms.is_valid()) {
        if (first_meta == UINT32_MAX) first_meta = ms.raw();
        uint32_t end = ms.raw() + re.num_inputs + re.num_outputs;
        if (end > max_meta_end) max_meta_end = end;
      }
      total_inputs += re.num_inputs;
      total_outputs += re.num_outputs;
      total_scalars += std::min(re.num_scalar_args, uint16_t(5));
    }

    auto* ops = bg.arena.alloc_array<TraceEntry>(count);
    uint32_t total_metas = (first_meta != UINT32_MAX) ?
        max_meta_end - first_meta : 0;
    TensorMeta* meta_base = (total_metas > 0) ?
        meta_log.try_contiguous(first_meta, total_metas) : nullptr;
    if (!meta_base && total_metas > 0) {
      meta_base = bg.arena.alloc_array<TensorMeta>(total_metas);
      for (uint32_t m = 0; m < total_metas; m++)
        meta_base[m] = meta_log.at(first_meta + m);
    }
    size_t aux_bytes =
        static_cast<size_t>(total_scalars) * sizeof(int64_t) +
        static_cast<size_t>(total_inputs) * sizeof(OpIndex) +
        static_cast<size_t>(total_inputs) * sizeof(SlotId) +
        static_cast<size_t>(total_outputs) * sizeof(SlotId);
    char* aux_cursor = (aux_bytes > 0) ?
        static_cast<char*>(bg.arena.alloc(aux_bytes, alignof(int64_t))) :
        nullptr;

    bench::ClobberMemory();
    uint64_t t0 = bench::rdtsc();

    // Phase 2a: ONLY copy fields + pointer arithmetic (no hash, no PtrMap)
    for (uint32_t i = 0; i < count; i++) {
      const auto& re = trace_data[i];
      MetaIndex ms = meta_data[i];
      auto& te = ops[i];

      te.schema_hash = re.schema_hash;
      te.shape_hash = re.shape_hash;
      te.scope_hash = scope_data[i];
      te.callsite_hash = callsite_data[i];
      te.num_inputs = re.num_inputs;
      te.num_outputs = re.num_outputs;
      te.grad_enabled = re.grad_enabled;
      te.inference_mode = re.inference_mode;
      te.kernel_id = CKernelId::OPAQUE;
      te.pad_te = 0;
      uint16_t n_scalars = std::min(re.num_scalar_args, uint16_t(5));
      te.num_scalar_args = n_scalars;

      if (ms.is_valid()) {
        uint16_t n_in = re.num_inputs;
        uint16_t n_out = re.num_outputs;
        uint32_t meta_offset = ms.raw() - first_meta;
        te.input_metas = meta_base + meta_offset;
        te.output_metas = meta_base + meta_offset + n_in;
        te.scalar_args = (n_scalars > 0) ?
            reinterpret_cast<int64_t*>(aux_cursor) : nullptr;
        aux_cursor += n_scalars * sizeof(int64_t);
        te.input_trace_indices = reinterpret_cast<OpIndex*>(aux_cursor);
        aux_cursor += n_in * sizeof(OpIndex);
        te.input_slot_ids = reinterpret_cast<SlotId*>(aux_cursor);
        aux_cursor += n_in * sizeof(SlotId);
        te.output_slot_ids = reinterpret_cast<SlotId*>(aux_cursor);
        aux_cursor += n_out * sizeof(SlotId);
        if (n_scalars > 0)
          std::memcpy(te.scalar_args, re.scalar_values,
                      n_scalars * sizeof(int64_t));
      }
    }

    bench::ClobberMemory();
    uint64_t t1 = bench::rdtsc();
    bench::DoNotOptimize(ops);
    ns_p2a += static_cast<double>(t1 - t0) * g_tsc_ratio;
  }

  // ── P2b: Content hash only ────────────────────────────────────────
  //
  // Set up ops array (from a reference run), then time ONLY the
  // content hash loop over all ops.

  double ns_p2b = 0;
  {
    // Build a reference ops array once.
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();
    auto* ref_graph = bg.build_trace(count);
    TraceEntry* ref_ops = ref_graph->ops;

    for (uint32_t iter = 0; iter < iters; iter++) {
      bench::ClobberMemory();
      uint64_t t0 = bench::rdtsc();

      uint64_t content_h = 0x9E3779B97F4A7C15ULL;
      for (uint32_t i = 0; i < count; i++) {
        const auto& te = ref_ops[i];
        content_h = detail::wymix(content_h, te.schema_hash.raw());
        for (uint16_t j = 0; j < te.num_inputs; j++) {
          const TensorMeta& m = te.input_metas[j];
          for (uint8_t d = 0; d < m.ndim; d++) {
            content_h = detail::wymix(
                content_h ^ static_cast<uint64_t>(m.sizes[d]),
                static_cast<uint64_t>(m.strides[d]));
          }
          content_h ^=
              static_cast<uint64_t>(std::to_underlying(m.dtype)) |
              (static_cast<uint64_t>(std::to_underlying(m.device_type)) << 8) |
              (static_cast<uint64_t>(static_cast<uint8_t>(m.device_idx)) << 16);
          content_h *= 0x9E3779B97F4A7C15ULL;
        }
        if (te.num_scalar_args > 0) {
          for (uint16_t s = 0; s < te.num_scalar_args; s++) {
            content_h ^= static_cast<uint64_t>(te.scalar_args[s]);
            content_h *= 0x100000001b3ULL;
          }
        }
      }

      bench::ClobberMemory();
      uint64_t t1 = bench::rdtsc();
      bench::DoNotOptimize(content_h);
      ns_p2b += static_cast<double>(t1 - t0) * g_tsc_ratio;
    }
  }

  // ── P2c: PtrMap lookup (input DFG edges) ──────────────────────────

  double ns_p2c = 0;
  {
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();
    auto* ref_graph = bg.build_trace(count);
    TraceEntry* ref_ops = ref_graph->ops;

    bg.ensure_scratch_buffers();

    for (uint32_t iter = 0; iter < iters; iter++) {
      // Reset PtrMap + populate with outputs (so lookups hit)
      bg.map_gen_++;
      if (bg.map_gen_ == 0) {
        std::memset(bg.scratch_map_, 0,
                    BackgroundThread::PTR_MAP_CAP *
                    sizeof(BackgroundThread::PtrSlot));
        bg.map_gen_ = 1;
      }
      auto* local_map = bg.scratch_map_;
      uint8_t local_gen = bg.map_gen_;

      // Insert all outputs so lookups can find them
      for (uint32_t i = 0; i < count; i++) {
        const auto& te = ref_ops[i];
        for (uint16_t j = 0; j < te.num_outputs; j++) {
          void* ptr = te.output_metas[j].data_ptr;
          if (ptr)
            (void)BackgroundThread::ptr_map_insert(
                local_map, local_gen, ptr, OpIndex{i},
                static_cast<uint8_t>(j), SlotId{0});
        }
      }

      bench::ClobberMemory();
      uint64_t t0 = bench::rdtsc();

      // Now measure ONLY the input lookups
      uint32_t dummy_edges = 0;
      for (uint32_t i = 0; i < count; i++) {
        const auto& te = ref_ops[i];
        for (uint16_t j = 0; j < te.num_inputs; j++) {
          void* ptr = te.input_metas[j].data_ptr;
          auto lookup = BackgroundThread::ptr_map_lookup(
              local_map, local_gen, ptr);
          if (lookup.op_index.is_valid()) dummy_edges++;
        }
      }

      bench::ClobberMemory();
      uint64_t t1 = bench::rdtsc();
      bench::DoNotOptimize(dummy_edges);
      ns_p2c += static_cast<double>(t1 - t0) * g_tsc_ratio;
    }
  }

  // ── P2d: PtrMap insert (output slot tracking) ─────────────────────

  double ns_p2d = 0;
  {
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();
    auto* ref_graph = bg.build_trace(count);
    TraceEntry* ref_ops = ref_graph->ops;

    bg.ensure_scratch_buffers();

    for (uint32_t iter = 0; iter < iters; iter++) {
      // Fresh PtrMap each iteration
      bg.map_gen_++;
      if (bg.map_gen_ == 0) {
        std::memset(bg.scratch_map_, 0,
                    BackgroundThread::PTR_MAP_CAP *
                    sizeof(BackgroundThread::PtrSlot));
        bg.map_gen_ = 1;
      }
      auto* local_map = bg.scratch_map_;
      uint8_t local_gen = bg.map_gen_;

      bench::ClobberMemory();
      uint64_t t0 = bench::rdtsc();

      uint32_t aliases = 0;
      for (uint32_t i = 0; i < count; i++) {
        const auto& te = ref_ops[i];
        for (uint16_t j = 0; j < te.num_outputs; j++) {
          void* ptr = te.output_metas[j].data_ptr;
          if (!ptr) continue;
          auto result = BackgroundThread::ptr_map_insert(
              local_map, local_gen, ptr, OpIndex{i},
              static_cast<uint8_t>(j), SlotId{0});
          if (result.was_alias) aliases++;
        }
      }

      bench::ClobberMemory();
      uint64_t t1 = bench::rdtsc();
      bench::DoNotOptimize(aliases);
      ns_p2d += static_cast<double>(t1 - t0) * g_tsc_ratio;
    }
  }

  // ── P3: Slot copy (SlotInfo → TensorSlot) ─────────────────────────

  double ns_p3 = 0;
  {
    bg.ensure_scratch_buffers();
    // Set up realistic slot data
    bg.current_trace.assign(trace.entries.begin(), trace.entries.end());
    bg.current_meta_starts.assign(trace.meta_starts.begin(), trace.meta_starts.end());
    bg.current_scope_hashes.assign(trace.scope_hashes.begin(), trace.scope_hashes.end());
    bg.current_callsite_hashes.assign(trace.callsite_hashes.begin(), trace.callsite_hashes.end());
    bg.arena.~Arena();
    new (&bg.arena) Arena{arena_bytes};
    repopulate();
    auto* ref_graph = bg.build_trace(count);
    uint32_t num_slots = ref_graph->num_slots;

    // Save the scratch_slots_ state
    std::vector<BackgroundThread::SlotInfo> saved_slots(
        bg.scratch_slots_, bg.scratch_slots_ + num_slots);

    for (uint32_t iter = 0; iter < iters; iter++) {
      // Restore slot data
      std::memcpy(bg.scratch_slots_, saved_slots.data(),
                  num_slots * sizeof(BackgroundThread::SlotInfo));
      bg.arena.~Arena();
      new (&bg.arena) Arena{arena_bytes};

      bench::ClobberMemory();
      uint64_t t0 = bench::rdtsc();

      auto* slots = bg.arena.alloc_array<TensorSlot>(num_slots);
      for (uint32_t s = 0; s < num_slots; s++) {
        slots[s].offset_bytes = 0;
        std::memcpy(&slots[s].nbytes, &bg.scratch_slots_[s],
                    sizeof(BackgroundThread::SlotInfo));
        slots[s].slot_id = SlotId{s};
        std::memset(slots[s].pad2, 0, sizeof(slots[s].pad2));
      }

      bench::ClobberMemory();
      uint64_t t1 = bench::rdtsc();
      bench::DoNotOptimize(slots);
      ns_p3 += static_cast<double>(t1 - t0) * g_tsc_ratio;
    }
  }

  PhaseTiming phases[] = {
    {"P0  pre-scan",       ns_p0,  iters},
    {"P1  alloc+memset",   ns_p1,  iters},
    {"P2a copy fields",    ns_p2a, iters},
    {"P2b content hash",   ns_p2b, iters},
    {"P2c PtrMap lookup",  ns_p2c, iters},
    {"P2d PtrMap insert",  ns_p2d, iters},
    {"P3  slot copy",      ns_p3,  iters},
  };

  double sum = 0;
  for (auto& p : phases) sum += p.total_ns;
  print_phase_table(phases, 7, sum, iters);
}

// ── Main ─────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <file.crtrace> [iters]\n", argv[0]);
    return 1;
  }

  g_tsc_ratio = bench::tsc_ns_ratio();
  std::printf("TSC ratio: %.4f ns/cycle\n", g_tsc_ratio);

  auto trace = load_trace(argv[1]);
  if (!trace) {
    std::fprintf(stderr, "error: could not load %s\n", argv[1]);
    return 1;
  }
  std::printf("Loaded %s: %u ops, %u metas\n",
              argv[1], trace->num_ops, trace->num_metas);

  uint32_t iters = (argc >= 3) ? static_cast<uint32_t>(std::atoi(argv[2])) : 1000;

  MetaLog meta_log;
  meta_log.reset();

  BackgroundThread bg;
  bg.meta_log = &meta_log;

  bench_phases_toplevel(bg, meta_log, *trace, iters);
  bench_phase2_subparts(bg, meta_log, *trace, iters);

  std::printf("\nDone.\n");
  return 0;
}
