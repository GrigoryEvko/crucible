# 05_05_2026 — Autorouter design notes

*Ultrathink pass on what a "best in class ever" autorouter for compute-shaped
tasks looks like for Crucible.  Status: foundations shipped (intent, efficiency
gate, type-level dispatch); rest is design exploration.*

## TL;DR — the load-bearing principle

**Crucible's autorouter is not a scheduler — it is a compile-time fold over
typed substrate blocks.**  Every primitive in Crucible (HotPath,
ResidencyHeat, NumericalTier, Vendor, AllocClass, Linear, Pure rows, …)
already encodes routing-relevant facts.  The router just consults those
facts, optionally caches the answer, and exits.

This means **Crucible should be built of router-friendly blocks by
construction**.  Every type in the substrate carries enough information
for the router to answer "should this fan out?" without measuring
anything.  When a body is constructed, the answer is either obvious
from its type (the common case, sub-ns) or requires a fallback cost
model evaluation (rare, sub-100ns).  No system anywhere in Crucible
ever pays scheduler-overhead for a routing decision the type system
could have provided.

The five corollaries:

1. Every dispatch site is **a 4-tuple decision** — partition × schedule ×
   placement × completion — not a scalar "factor".  The current router
   answers axis I; the rest are hard-coded to safe defaults until the
   substrate is ready to surface them.
2. The router optimizes a **declared loss function** that mixes wall
   time, CPU time, tail latency, energy, cache amortization, queue
   pressure, NUMA cost.  Caller declares the weights via a typed
   `SchedulingIntent` grade — **shipped** as of this commit.
3. The decision runs through a **5-tier funnel** — most calls answered
   at compile time (0 ns), rare cold misses pay full cost-model
   evaluation (~30–50 ns), no call path exceeds ~100 ns.  Tier 0
   (`auto_split_plan_typed<Body>`) **shipped**; tiers 1–4 designed.
4. Subsystems **federate** through shared gauges — Vessel, BgThread,
   KernelCompile, Cipher each have their own per-domain router; they
   coordinate through a shared load gauge, not a single global queue.
5. Async-completion modes (spawn-then, streaming yield, fire-forget,
   speculative-race) — narrow but real wins, surface separately from
   axis I.

The single most important conceptual upgrade: **treat the body type as
the source of truth**, not a hint.  C++26 reflection (P2996) lets the
router fingerprint operation counts at compile time; for production
bodies with declared `WorkloadHint`s the router never has to guess.

## Router-friendly by construction — the design discipline

The phrase **"router-friendly blocks"** is the architectural mandate
this doc serves.  Every new Crucible primitive is evaluated against
this checklist:

```
1. Does it carry a routing-relevant grade as part of its type?
   • HotPath / ResidencyHeat / NumericalTier / Vendor / AllocClass / Wait
   • effects::Pure / Bg / IO / Block / Init rows
   • Linear ownership (forbids fanout entirely)

2. Can it be specialized via workload_traits<T> to advertise its
   per-call cost and parallelism appetite?

3. Does it decay to a stateless lambda for default routing? (is_empty_v
   = true → tier-0 sequential default)

4. Does sizeof(T) stay small (≤ 256 B)? Heavy captures cause lambda
   ballooning that defeats fanout amortization.

5. If it's a pipeline stage: does it surface as Stage<FnPtr, Ctx> so
   that the typed Pipeline can plumb the row union?
```

A primitive that fails 3+ of these is not router-friendly.  Code review
should flag it.  The substrate's job is to make the router's job free.

### Concrete examples — what "router-friendly" looks like

| Primitive | Type-level grades it carries | Router consults | Verdict |
|---|---|---|---|
| `HotPath<Hot, T>` | `is_hot_path_v == true` | tier-0 → never fanout | router-friendly |
| `Pure` row body | `effects::is_pure_v<row> == true` | tier-0 → safe to fanout | router-friendly |
| `ResidencyHeat<L1, T>` | `residency_tier_v == L1` | tier-0 → keep inline (cache-warm) | router-friendly |
| `NumericalTier<BITEXACT_STRICT, T>` | deterministic order required | tier-0 → fanout disallowed | router-friendly |
| `Linear<T>` arg in body | `takes_linear_arg_v == true` | tier-0 → no fanout (single-consume) | router-friendly |
| Stateless lambda | `std::is_empty_v == true` | tier-0 → sequential default | router-friendly |
| Heavy-capture lambda | `sizeof > 256` | tier-0 → cap shards at 4 | router-friendly |
| `Bg` row body | `effects::has_bg_v<row>` | tier-0 → safe to fanout to bg pool | router-friendly |
| `Block` row body | `effects::has_block_v<row>` | tier-0 → fanout PAST core count | router-friendly |
| Untagged callback `void (*)(T)` | nothing | router falls through to byte-tier | **not** router-friendly |
| Type-erased `std::function` | nothing visible | router falls through, often wrong | **not** router-friendly |
| Body that internally chooses parallelism | router can't see it | router can't help | **not** router-friendly |

The goal across the codebase: **eliminate the bottom three rows**.
Every dispatch site that lands in production should reach for
`auto_split_plan_typed<Body>` and have the body's type carry enough
grades that the router answers in tier 0.

## What's shipped (this commit)

- **`SchedulingIntent` enum** — `LatencyCritical / Throughput /
  Background / Overlapped / Sequential / Adaptive`.  Caller declares
  the loss-function weights.
- **`min_efficiency_pct` field** on `AutoSplitRuntimeProfile` — default
  70%.  Throughput intent walks F down (16 → 8 → 4 → 2 → 1) until
  parallel efficiency clears the floor.
- **`efficiency_pct(items, per_item_ns, dispatch_cost_ns, F)`** —
  constexpr integer-percent model.  No FP, no allocation, ~10 cycles.
- **Intent gate** in `auto_split_plan` — Sequential collapses,
  LatencyCritical skips break-even and efficiency gates, Throughput
  applies both, others fall through.
- **`workload_traits<Body>` + `WorkloadTagged<H>` + `infer_workload_hint`**
  — type-level hint surface.  Tier-0 auto-inference: `is_empty_v` →
  sequential, `sizeof > 256` → cap shards at 4.  CRTP and explicit
  specialization both supported.
- **`merge_request_with_hint(req, hint)`** — caller intent always wins;
  hint fills gaps.
- **`dispatch_auto_split_typed<Body>(...)` and `auto_split_plan_typed<Body>`**
  — type-level entry points.  Constexpr `infer_workload_hint<Body>()`
  fold + merge before planner.
- **5 new tests in `test_auto_split.cpp`**:
  - `test_latency_critical_skips_efficiency_gate`
  - `test_sequential_intent_always_collapses`
  - `test_higher_efficiency_floor_demotes_more` (90% floor walks 16→8→4)
  - `test_typed_planner_is_empty_forces_sequential`
  - `test_typed_planner_crtp_supplies_intent_and_per_item`

All static_assert.  16 of 16 tests green.

---

## Why the current router is below ceiling

`auto_split_plan` (today) does:

```
shard_count = clamp(min(bytes / l2_cliff, max_shards, worker_count), 1, items)
              [optionally demoted by break-even gate when per_item_compute_ns set]
```

Three axes are silently fixed at the lowest-common-denominator:

| Axis | Today | Cost of fixing |
|---|---|---|
| Partition strategy | even chunking | adaptive / strided / hierarchical = ~30 LoC each |
| Schedule mode | sync fork-join | async future / streaming yield / fire-forget = ~100 LoC |
| Placement | pool default | NUMA-aware + SMT-disjoint = needs Topology + cpuset |
| Completion | `wait_idle` blocking | callback / SWMR / channel-yield = composes with axis II |

The bench data already showed the cost:

- `DRAM.stream/mem` 16-shard win: −68% wall time, **+5.2× CPU time**.
  The system pays heavily for 16 cores busy 54 µs each rather than 1 core
  busy 166 µs.
- `L3.scan/mem`  4-shard fanout: **−201% wall time, +12× CPU time**.
  Pure loss — the byte-tier rule sharded a workload that should have
  stayed inline.

Both failures share a root cause: **the router has no visibility into
the body's compute density relative to the dispatch cost**, so it falls
back to the only signal it has (byte footprint), which is correct for
maybe 30% of real workloads.

---

## The 4-tuple decision

State of the art treats parallel dispatch as **four independent
decisions**, which the type system + cost model jointly determine:

```
┌──────────────────────────────────────────────────────────────────────┐
│ I. PARTITION  — how to split work                                    │
│   • Even chunking         (current)                                  │
│   • Adaptive chunking     (Cilk-style, work stealing)                │
│   • Strided               (cache-line interleaved across shards)     │
│   • Hierarchical          (NUMA-block × LLC-tile × work-steal)       │
│   • Recursive split       (binary split until grain ≤ threshold)     │
│   • Cooperative groups    (SMT pair owns shard, splits SIMD lanes)   │
├──────────────────────────────────────────────────────────────────────┤
│ II. SCHEDULE   — when to dispatch                                    │
│   • Inline                (caller stack, factor=1)                   │
│   • Sync fork-join        (current; caller blocks on wait_idle)      │
│   • Async spawn           (returns future; caller continues)         │
│   • Pipeline-stage handoff(producer pushes; consumer pulls)          │
│   • Speculative race      (replicated factors; first-wins; cancel)   │
│   • Priority-coupled      (FIFO/LIFO/EEVDF/Deadline policy)          │
├──────────────────────────────────────────────────────────────────────┤
│ III. PLACEMENT — where to put workers                                │
│   • Caller core           (no migration; default for inline)         │
│   • Pool worker, anywhere (current; cheapest)                        │
│   • Pool worker, NUMA-local                                          │
│   • Pool worker, SMT-shared (sibling HT — for cooperative groups)    │
│   • Pool worker, LLC-shared but SMT-disjoint                         │
│   • Dedicated worker      (per-callsite reserved core)               │
├──────────────────────────────────────────────────────────────────────┤
│ IV. COMPLETION — how to gather results                               │
│   • Blocking wait_idle    (current)                                  │
│   • Async future          (caller polls / awaits)                    │
│   • Streaming yield       (results arrive on channel as ready)       │
│   • Fire-and-forget       (no completion signal at all)              │
│   • Callback              (worker invokes on completion)             │
│   • SWMR snapshot publish (workers write into AtomicSnapshot)        │
└──────────────────────────────────────────────────────────────────────┘
```

The four axes are **mostly orthogonal** — every reasonable combination
exists in real workloads.  Selected examples:

| Workload | Partition | Schedule | Placement | Completion |
|---|---|---|---|---|
| Empty body | none | inline | caller | n/a |
| L1 batch (warm) | none | inline | caller | sync |
| L3 stream (memcpy) | even/4 | sync fork | NUMA-spread | sync |
| DRAM compute | even/N | sync fork | NUMA-spread + SMT-disjoint | sync |
| Heterogeneous compute | adaptive | sync fork | LLC-shared | sync |
| Pipeline lowering (Forge) | even/F | async spawn | NUMA-local | streaming yield |
| Cipher persistence flush | even/F | async spawn | NUMA-local | callback |
| Telemetry log drain | none | async spawn | dedicated low-prio | fire-forget |
| Kernel cache publish | F=1 | inline | caller | SWMR snapshot |
| Latency-critical SLA | replicated×3 | speculative race | SMT-disjoint | first-wins |

The current router answers axis I only.  The other three are forced
to defaults that win on simplicity but lose ~30–50% of the available
performance for non-default workloads.

---

## The actual loss function

I keep gesturing at "wall vs CPU vs efficiency".  Here's what the router
should actually minimize:

```
L(F | workload, system, intent) =
       w_wall    · wall_time(F)
     + w_cpu     · cpu_time(F)             [cpu = wall × active_cores]
     + w_p99     · p99_estimate(F)
     + w_energy  · DVFS_state(F) · cpu(F)
     - w_amort   · cache_warmth_after(F)   [F=1 keeps caller's L1 hot]
     + w_pool    · queue_pressure(F)
     + w_numa    · numa_cross_count(F)
     + w_spec    · cancellation_cost(F)
     - w_overlap · max(0, follow_up_announced - wall(F))
```

The **weights come from the SchedulingIntent grade** declared by the
caller via the type system.  Concrete bindings I think are right:

```cpp
enum class SchedulingIntent : std::uint8_t {
    // w_wall = 1.0, w_cpu = 0, w_p99 = 0.5
    // "I'm on a deadline; burn cores to hit it."
    LatencyCritical,
    
    // w_wall = 0.3, w_cpu = 1.0, w_amort = 0.5
    // "Maximize items/sec; only fanout if efficiency ≥ 0.7."
    Throughput,
    
    // w_wall = 0.0, w_cpu = 1.0, w_pool = 10.0
    // "Steal from idle cores only; never if pool is busy."
    Background,
    
    // w_wall = 0.5, w_cpu = 0.5, w_overlap = 1.0
    // "I have follow-up work; overlap helps."
    Overlapped,
    
    // F = 1 always; no router involvement.
    Sequential,
    
    // Default: w determined by current pool load.
    // - pool idle  → behaves like LatencyCritical
    // - pool busy  → behaves like Throughput
    // - pool full  → behaves like Sequential
    Adaptive,
};
```

The intent is a **type-level grade** declared via:

```cpp
struct MyBody : WorkloadTagged<{
    .intent = SchedulingIntent::Throughput,
    .per_item_ns = 50,
    .max_natural_shards = 8,
    .is_pure = true,
    .touches_memory = true,
}> { ... };
```

Or via specialization of `workload_traits<MyBody>`.  Or auto-inferred
when the body type provides enough signal (see "Tier 0" below).

---

## The 5-tier funnel — router cost budget by tier

The router itself must be cheaper than the work it routes.  That gives
a hard cost ceiling per tier; design accordingly:

| Tier | Mechanism | Budget | Hit rate | Cumulative cost |
|---|---|---|---|---|
| 0 | compile-time hint (`if constexpr`) | **0 ns** | ~70% | 0 |
| 1 | hot-shape memoization cache | ≤ 1 ns | ~20% | ~0.2 ns |
| 2 | precomputed branch table | ≤ 5 ns | ~7% | ~0.4 ns |
| 3 | full cost model + break-even | ≤ 50 ns | ~3% | ~1.6 ns |
| 4 | online recalibration (EWMA) | ≤ 1 µs amortized | rare | ~negligible |

The amortized router cost across a representative training iteration
(millions of dispatches) is **~1.6 ns/call** — well below the 5–10 ns
cost of the cheapest hot-path body.  The router earns its keep.

### Tier 0 — pure compile-time signals (free)

What can be read off the body type without ever calling it.  These
already exist as wrappers/concepts in the substrate:

```cpp
// std-library trait signals:
std::is_empty_v<Body>                     // stateless lambda → seq default
sizeof(Body)                              // > 256 → cap shards (copy bloat)
std::is_trivially_copyable_v<Body>        // false → heap/refcount risk
std::is_nothrow_invocable_v<Body, Shard>  // false → exception path
alignof(Body) >= 64                       // hot-path-aligned target

// Crucible substrate grades:
is_hot_path_v<HotPath<Hot, T>>            // hot path → NEVER fanout
residency_tier_v<ResidencyHeat<X, T>>     // L1/L2 → seq, L3/DRAM → parallel
vendor_v<Vendor<NV, T>>                   // NV launches batch at high latency
numerical_tier_v<NumericalTier<X, T>>     // BITEXACT_STRICT → deterministic only
alloc_class_v<AllocClass<Arena, T>>       // arena → parallel-safe; heap → contention
wait_strategy_v<Wait<Spin, T>>            // bounded wait OK on hot path
crucible::effects::is_pure_v<row>         // pure row → safe to fanout
crucible::effects::has_block_v<row>       // body blocks → fanout PAST cores
crucible::effects::has_io_v<row>          // body does I/O → latency hide via fanout
permission_split_v<Tag>                   // split rules exist → grid partition OK
```

That's **~12 free signals** already in the substrate.  For most lambdas:
- `is_empty_v == true` → suggest sequential
- effect row inferred `Pure` → safe to fanout
- combined with caller's intent → decision resolved at compile time

**~70% of dispatches answered by tier 0.  Zero runtime cost.**

### Tier 1 — hot-shape memoization (≤ 1 ns)

When the same dispatch site fires repeatedly with similar shapes
(every training loop, every recording iteration, every kernel-cache
probe), the second call can be free:

```cpp
struct ShapeCache {
    alignas(64) struct Slot {
        uint64_t key;        // body_addr ^ shape_hash
        uint8_t  factor;
        uint8_t  hits;       // confidence; promote after 4 hits
        uint8_t  intent;
        uint8_t  pad[5];
    } slots[64];

    // 1-2 cycle lookup: index, load, compare.
    uint8_t lookup_or(uint64_t key, uint8_t fallback) noexcept {
        const Slot& s = slots[(key >> 3) & 63];
        return (s.key == key && s.hits >= 4) ? s.factor : fallback;
    }
};
```

64 slots × 16 bytes = 1024 bytes, fits in L1 with room to spare.
Lookup is bitmask + load + compare + branch.  **~1-2 cycles in the
warm-cache common case.**  Eviction policy: just stomp on the slot
when keys collide; the precision lost rarely matters.

Adversarial defense: hash the key with Philox-style mixer to spread
collisions evenly; otherwise systematic dispatches at the same site
all collide on one slot.

### Tier 2 — branch table (≤ 5 ns)

A 128-byte table indexed by `(log2(items), log2(bytes_per_item))`,
precomputed at startup from Topology + measured dispatch cost:

```cpp
struct RoutingTable {
    uint8_t factor[16][8];   // 128 B = 2 cache lines
};

uint8_t branch_table_lookup(size_t items, size_t bpi) noexcept {
    const unsigned ib = std::min(15u, std::countr_zero(items >> 4) ?: 0);
    const unsigned bb = std::min(7u,  std::countr_zero(bpi   >> 3) ?: 0);
    return ROUTE_TABLE.factor[ib][bb];
}
```

3 cycles: shift, count-trailing-zeros, table lookup.  The table
encodes **this host's** byte-tier choice, not a hardcoded one.

### Tier 3 — full cost-model evaluation (≤ 50 ns)

The current `auto_split_plan` body, augmented with the efficiency gate:

```cpp
auto wall = [&](size_t F) {
    return F <= 1 ? total_compute
                  : (total_compute / F + F * dispatch_cost);
};
auto efficiency = [&](size_t F) {
    return F <= 1 ? 1.0 : double(total_compute) / double(wall(F) * F);
};

// Throughput intent: smallest F with efficiency ≥ min_eff that wins wall.
for (size_t F : {1, 2, 4, 8, 16}) {
    if (F > req.max_shards) break;
    if (efficiency(F) < prof.min_efficiency) continue;
    if (wall(F) < wall(1) * 0.9) return {.factor = F};
}
return {.factor = 1};
```

The body is arithmetic on five integers.  GCC compiles to ~25
instructions.  **~30–50 ns measured today.**  Sub-100 ns ceiling intact.

### Tier 4 — online learning (amortized free)

Workers report shard wall time and shard work; the router maintains
EWMAs for `dispatch_cost_ns` and `per_shard_speedup`:

```cpp
struct OnlineCalibrator {
    std::atomic<uint64_t> dispatch_cost_ewma_ns{10000};
    std::atomic<uint64_t> per_item_ewma_ns_x_1000{0};
    std::atomic<uint64_t> samples{0};
    
    void record_shard(uint64_t observed_ns, size_t shard_items) noexcept {
        const uint64_t per_item_x1000 = (observed_ns * 1000) / std::max<size_t>(1, shard_items);
        const auto old = per_item_ewma_ns_x_1000.load(std::memory_order_relaxed);
        // α = 1/16
        const auto mix = (old * 15 + per_item_x1000) / 16;
        per_item_ewma_ns_x_1000.store(mix, std::memory_order_relaxed);
        samples.fetch_add(1, std::memory_order_relaxed);
    }
};
```

After ~16 dispatches the cost model self-tunes to **this hardware,
this thermal state, this workload pattern**.  No syscall, no lock.
Atomic updates are relaxed because precision is bounded by the EWMA
anyway.

---

## Async completion — when does it actually win

The user's prompt about "asynchronous result collection" — let me be
honest about the narrow cases where it strictly beats sync.

### Pattern A — overlap with caller's follow-up work

```cpp
// SYNC: caller blocks 1 ms, then spends 800 µs on prep.  Total 1.8 ms.
result = dispatch_auto_split(work);
prep_next();
use(result);

// ASYNC: caller continues prep while workers run.  Total ~1.2 ms.
auto fut = dispatch_async(work);
prep_next();             // 800 µs concurrent with workers
result = fut.get();      // ~200 µs additional wait
use(result);
```

**Win condition**: `caller_continuation_work ≥ body_work × 0.5`.  When
the continuation is short, the wall-time overlap is small and async's
extra book-keeping (future allocation, atomic flag handshake) loses.

**Crucible candidates**: kernel-compile pipeline (lower IR for region
N+1 while region N executes), Cipher cold-tier flush (caller continues
while page writes drain), Augur metrics roll-up (caller continues iter
while previous iter's metrics aggregate).

### Pattern B — pipeline staging via streaming yield

```cpp
// FORK-JOIN: stage1 finishes ALL items, stage2 starts.  Total 2 ms.
auto s1 = parallel_map(stage1, input);
auto s2 = parallel_map(stage2, s1);

// STREAMING: each shard's stage1 result feeds stage2 immediately.
// stage1 N+1 overlaps stage2 N.  Total ~1.1 ms.
parallel_pipeline(stage1, stage2, input);
```

**Win condition**: `time(stage1) ≈ time(stage2)`, both fan out, output
of stage1 is small enough to handoff via a permissioned channel.

**Crucible candidates**: Forge phases (B → C → D each emit per-region
output), TraceRing → MetaLog → DAG-build chain.  Already shipped as
`mint_pipeline<Stages...>`; routing mode is **separate** from per-stage
factor choice.

### Pattern C — fire-and-forget instrumentation

When the workload is "log telemetry" or "prefetch hint", the caller
genuinely doesn't care about completion.  Saves the entire `wait_idle`
cost (~10–30 µs).

**Win condition**: caller has no data dependency on result.

**Crucible candidates**: Augur metric publish, BPF event drain, Cipher
warm-tier sync (best effort), trace-record callback.  Should NOT be
used for anything that affects correctness.

### Pattern D — speculative race

Run two strategies (factor=1 and factor=8), use whichever finishes
first, cancel the other.  Costs **min(F_1, F_8) × CPU** in wasted
work.

**Win condition**: `(1 - p_factor8_meets_deadline) × deadline_miss_cost
> factor8_extra_cpu_cost`.  Almost never satisfied except for hard
real-time fenceposts.

**Crucible candidates**: rare; possibly Vigil divergence-recovery
where both compiled and eager paths race for next op.

### Pattern E — callback completion

Last completing worker invokes `cont(combined_result)` on its own
thread without bouncing back to caller.  Saves a wait_idle round-trip
+ a context switch.

**Win condition**: continuation logic is small and stateless.

**Crucible candidates**: KernelCache publish (worker N publishes
compiled bytes via SWMR), Cipher cold-tier write completion.

---

## Federated routing — multiple cooperating routers

A pattern I haven't seen in scheduler literature but I think is right
for Crucible: **per-domain routers with global coordination**.

Each subsystem has its own dispatch cadence and cost shape:

| Subsystem | Per-call cost | Frequency | Right router |
|---|---|---|---|
| Vessel dispatch (ATen interception) | 5 ns | 10⁶/sec | tier-0 only, no fanout |
| BgThread drain (TraceRing → DAG) | 100 µs | 10/sec | full cost model, async OK |
| Forge phases (kernel compile) | 10 ms | few/sec | async pipeline, dedicated cores |
| Cipher persistence | 1 ms | 100/sec | async fire-forget for warm tier |
| Augur metrics roll-up | 100 ns | 10⁴/sec | tier-0 only, fire-forget OK |
| Canopy gossip | 1 ms | per-second | dedicated low-prio worker |

Each domain has a **specialized router** that knows its shape; they
coordinate via a shared load gauge:

```cpp
struct GlobalLoadCoordinator {
    std::atomic<uint64_t> domain_active_bitmap;
    std::atomic<uint32_t> domain_pending_workers[8];
    
    bool can_take(Domain mine, size_t F) const noexcept {
        for (size_t d = 0; d < 8; ++d) {
            if (d == size_t(mine)) continue;
            if (priority(d) > priority(mine) &&
                domain_pending_workers[d].load(std::memory_order_relaxed) > 0)
                return false;
        }
        return enough_free_workers(F);
    }
};
```

**Vessel hot-path always wins; Augur background always defers.**  The
router becomes a participant in a tiny committee, not a god.

---

## The catalog of failure modes the router must address

Each is a real workload shape that breaks the simple byte-tier rule:

1. **Empty-body fanout** — body does nothing → 800× CPU waste.  *Fix:
   tier-0 `is_empty_v` check.*
2. **L1-resident over-shard** — many small items, but fits in L1.  *Fix:
   tier-2 byte+items joint table.*
3. **Memory-bound under-fanout** — 4-shard for L3 when DRAM has 4 channels
   and would benefit from 8 cores hammering bus.  *Fix: per-host
   memory-controller calibration → tier-2 table.*
4. **Memory-bound over-fanout** — 16 cores on 4-channel controller, cores
   5–16 stall.  *Fix: per-host bw_max / per_item_bytes ceiling.*
5. **Compute-bound under-fanout** — cliff-bucket says 4 but body is 100
   ns/item.  *Fix: break-even gate (already shipped).*
6. **Imbalanced shards** — one shard 10× longer.  *Fix: adaptive
   partition (work-stealing).*
7. **Cache thrash on parallel** — 16 cores fighting for shared L3.
   *Fix: Topology-aware placement; SMT-disjoint scheduling.*
8. **Pool oversubscription** — every call requests 16 shards, 8 workers
   → queue blow-up.  *Fix: pool_pressure_factor() at dispatch time.*
9. **Hyperthread contention** — 1.2× speedup not 2×.  *Fix: SMT-disjoint
   placement or factor adjustment.*
10. **NUMA crossing** — shard 0 on node 0, data on node 1.  *Fix:
    NumaPlacement grade + first-touch awareness.*
11. **Frequency droop** — all 16 active cores → DVFS down.  *Fix: online
    EWMA learns true per-shard ns; recalibrates.*
12. **TLB shootdowns** — 16 cores touching disjoint pages.  *Fix:
    transparent-hugepage default; partition strategy = strided not even.*
13. **First-touch race** — shard 0 allocates buffer everyone reads.
    *Fix: madvise(MADV_HUGEPAGE) + caller pre-touches; or per-shard
    arenas.*
14. **False sharing inside shards** — boundaries cut cache lines.  *Fix:
    grain rounded up to cache-line multiple.*
15. **Latency vs throughput intent mismatch** — same workload should get
    different policy.  *Fix: SchedulingIntent grade.*
16. **Long-tail outliers** — p99 hidden by mean.  *Fix: replicate
    speculatively; cancel slow shard if fast one finishes.*

Every one of these has a route through the cost model; most have
type-level signals available for free.

---

## Creative single-program multi-thread patterns

Beyond the standard fork-join, patterns I think are worth surfacing:

### 1. Continuation passing for async dispatch

```cpp
auto dispatch_then(Pool& p, Request r, Body&& body, Cont&& cont)
    -> std::shared_ptr<Future<Cont::result_type>>;
```

When workers finish, the **last completing worker** runs `cont(combined)`
on its own thread without bouncing back.  Saves wait_idle + context
switch (~10–20 µs).  Useful for kernel-compile completion → KernelCache
publish.

### 2. Worker locality groups (sticky shards)

Each repeated dispatch site keeps **the same worker** for the same
shard index across iterations.  Worker N stays warm for shard N's data;
cross-iteration cache reuse jumps from ~0% to ~80%.  Implemented via
shard_index → worker_index hash that's stable across calls.

**Net effect on training-loop dispatches**: per-shard wall time can
drop 2–3× because the worker's L2 is already populated with the right
data from the previous iteration.

### 3. Backpressure-aware partitioning

Instead of fixed even chunks, partitioner asks the pool: "how many
workers have <2 tasks queued right now?" and partitions only that many.
Avoids the standing failure mode of creating 16 shards when only 4
workers are actually free.

### 4. Deadline-sloppy speculative parallelism

When intent = LatencyCritical: dispatch factor=8 AND factor=2
simultaneously.  First to finish wins; cancel the other.

Cancellation cost: 1 atomic + early-exit per shard.  Worth it only
when `(1 - p(F8 meets deadline)) × miss_cost > F8_extra_cpu_cost`.

### 5. Producer-pacing (flow-controlled async)

When dispatching into a pipeline, the producer rate-limits itself based
on consumer queue depth.  Avoids dispatching 64 stage-1 results that
buffer in memory because stage-2 is slow.

### 6. Exit-cheap parallelism

Body returns `bool` (continue / stop).  When any shard returns false,
all peers cancel ASAP via atomic flag.  Saves ~50% of expected work for
early-exit searches ("find first matching tensor in cache").

### 7. Phase-coupled batching

Many small dispatches collapse into one larger dispatch when they target
the same body type and arrive within `K µs`.  Reduces dispatch
amortization to N⁻¹ × dispatch_cost.

Phase boundary signals: iteration end, region transition, op-count
threshold.  Implementation: a per-thread "pending" buffer that flushes
on phase transition or after K µs.

### 8. Result-aggregation policies

Instead of "each shard writes its result, caller folds":

- **Tree reduction** — shard pairs combine recursively (log₂ depth)
- **Atomic accumulator** — fetch_add into single counter (1 cache line, contention)
- **Per-shard accumulator** — own slot per shard, caller sums (no contention, false-sharing risk)
- **SWMR snapshot** — workers write into AtomicSnapshot, caller observes

The right pick depends on `bytes_returned_per_shard`:
- ≤ 8 bytes → atomic
- ≤ 64 bytes → per-shard slot, padded to cache line
- > 64 bytes → tree reduction or SWMR snapshot

### 9. Reflection-driven body fingerprinting

C++26 reflection (P2996) lets the router walk the body's call expressions
at compile time:

```cpp
template <typename Body>
consteval BodyFingerprint compute_fingerprint() {
    constexpr auto info = ^Body;
    BodyFingerprint fp;
    template for (constexpr auto call :
                  std::meta::call_expressions_of(operator_method_of(^Body))) {
        if (std::meta::name_of(call) == "fetch_add"     ) fp.atomics++;
        if (std::meta::name_of(call) == "memcpy"        ) fp.memcpys++;
        if (std::meta::name_of(call) == "operator new"  ) fp.allocs++;
        if (std::meta::name_of(call) == "futex_wait"    ) fp.blocks++;
    }
    fp.estimated_ns = fp.atomics * 5
                    + fp.memcpys * 8
                    + fp.allocs * 50
                    + fp.blocks * 1000;
    return fp;
}
```

The body **advertises its own cost based on what it actually does**,
no annotation needed.  A body that calls `futex_wait` once has
`blocks == 1`, the router sees `estimated_ns ≥ 1000`, automatically
routes to a wait-pool with separate workers.

This is the most powerful idea here.  GCC 16's P2996 surface is the
enabling technology; implementation is a few hundred LoC of consteval
introspection.  Result: production bodies never need to declare
`per_item_compute_ns` manually — the type system computes it from the
body's operation count.

---

## What "best in class" actually means here

Goalposts for the Crucible autorouter.  None of these are aspirational;
each is a measurable property:

1. **Cold-path cost ≤ 100 ns** — never adds noticeable overhead even on
   the worst-case dispatch.
2. **Hot-path cost ≤ 2 ns** — amortized across a training iteration,
   the router itself contributes <0.1% of wall time.
3. **Skill ≥ 95% on representative bench corpus** — among the cases
   in `bench_auto_split_compare`, the router picks within 5% of the
   best-fixed strategy on at least 95% of cases.  Provable via the
   skill table.
4. **Zero pessimization on hot-path bodies** — bodies tagged
   `HotPath<Hot, T>` must NEVER be dispatched off-thread.  Static
   assertion.
5. **Self-calibrating** — `dispatch_cost_ns` and `per_item_ns` derived
   from observation, not hardcoded.  EWMA tracks thermal state.
6. **Cooperative under load** — `pool_pressure_factor()` clamps; no
   request gets workers that aren't actually free.
7. **Type-level provenance** — every routing decision is traceable
   back to (body, intent, shape) via the cache key.  Debugging the
   router means reading the cache, not stepping through arithmetic.
8. **Federated** — no single global queue; per-domain routers
   coordinate via small shared load gauge.
9. **Async-mode-aware** — when caller declares `Overlapped`, router
   prefers async-spawn even at small wall-time wins.
10. **Tail-aware** — when intent includes `p99`, router includes
    speculation/replication in candidate set.

If a router scores 10/10 on these, it's at ceiling for current
hardware.  Next-gen hardware (CXL, accelerator co-routing) would push
the ceiling up but the router architecture transfers.

---

## Implementation priority — what to land first

Each piece is small and pays its own freight.  In order:

1. ~~**`min_efficiency` field + efficiency gate**~~ — **shipped**.
2. ~~**`SchedulingIntent` enum on `WorkloadHint`**~~ — **shipped**.
3. ~~**Type-level `dispatch_auto_split_typed`**~~ — **shipped**.
4. **Bench surfaces parallel efficiency + CPU time** — the third skill
   column needed to read the right answer from the data.  ~30 LoC.
5. **Hot-shape memoization cache** — killer perf trick for repeated
   dispatches.  ~50 LoC, sub-ns hits.
6. **Pool-pressure feedback** — cooperative routing; eliminates
   oversubscription.  `Pool::idle_workers_approx()` + clamp.  ~30 LoC.
7. **Online dispatch-cost EWMA** — self-calibration so dispatch_cost_ns
   reflects this hardware, not hardcoded guess.  ~40 LoC.
8. **Substrate-grade trait specializations** — give every existing
   Crucible primitive its `workload_traits<>` specialization.  Enables
   tier-0 short-circuits across the whole codebase.  ~10 LoC per type
   × ~30 production types ≈ ~300 LoC.  **Highest leverage** for the
   router-friendly-by-construction goal.
9. **Branch table (Tier 2)** — startup-precomputed per-host table.
   ~60 LoC, ~5 ns lookup.
10. **Async-spawn dispatch primitive** — `dispatch_async` returning a
    future.  Composes with existing `dispatch_at_factor`.  ~80 LoC.
11. **Streaming-yield primitive (already exists as `mint_pipeline`)** —
    wire `WorkloadHint::async_pipeline_eligible` so the router suggests
    it for chained stages.  ~40 LoC.
12. **Reflection-driven fingerprint** — most ambitious; ~150 LoC of
    P2996 introspection.  The endgame, removes the need for manual
    `per_item_compute_ns` declarations.

Items 4 turns the bench from a wall-time sketch into a tool that
**shows the right answer**, not just a number.  Items 5–8 make the
router production-quality on amortized cost.  Items 9–12 unlock the
remaining 30% of the design space (axes II + IV).

---

## Open questions / things I'm not sure about

- **Speculative race cancellation**: cleanup cost may exceed wait_idle
  cost.  Worth it only if `p99/p50 > 3` AND deadline-miss-cost is
  high.  Need real measurements to decide.
- **NUMA placement when accelerators are involved**: GPU-fed pipelines
  prefer everything on the GPU's NUMA node, but compile pool prefers
  spreading.  No single answer.
- **Reflection cost vs declared annotation cost**: P2996 `consteval`
  introspection compiles to nothing at runtime, but compile time
  itself may grow noticeably for large codebases.  Need bench.
- **Streaming yield vs fork-join for medium-grain pipelines**: at
  what `(stage_count, item_count, per_stage_ns)` does streaming win?
  Three-axis sweep needed.
- **Per-domain router federation handshake**: shared atomic load
  counter is simple but maybe too coarse.  CRDT? Vector clock?
  Probably overkill.
- **Online-learning EWMA window**: 1/16 alpha is a guess.  Adaptive
  alpha based on variance might be better, but may also be over-
  engineering.
- **Hot-shape cache eviction policy**: stomp on collision is simple,
  but pathological dispatch patterns (many shapes round-robin) can
  thrash.  LRU or 2-way associative may help; tradeoff is 2× cache
  size.

---

## What this is not

- Not a DAG scheduler.  Crucible has one (Forge Phase J), separate
  problem.
- Not a real-time scheduler.  Hard real-time wants different
  guarantees (admission control, priority inheritance).  This router
  is best-effort throughput/latency optimizer.
- Not a thread-pool implementation.  AdaptiveScheduler::Pool exists;
  this router targets it.
- Not a cost-model proof.  All the math here is heuristic/measured.
  No theorem-prover involvement.

---

## Concrete change set (referencing existing files)

| File | Change |
|---|---|
| `include/crucible/concurrent/AutoSplit.h` | + `SchedulingIntent` enum, + `min_efficiency` field on profile, + efficiency gate in `auto_split_plan`, + `WorkloadHint::pipeline_eligible` |
| `include/crucible/concurrent/AutoSplit.h` | + `auto_split_plan_async` returning future-shaped result |
| `include/crucible/concurrent/AutoSplit.h` | + `ShapeCache` 64-slot memoization, plumb into `dispatch_auto_split` cold path |
| `include/crucible/concurrent/AutoSplit.h` | + `OnlineCalibrator` EWMA struct |
| `include/crucible/concurrent/AdaptiveScheduler.h` | + `Pool::idle_workers_approx()`, + cooperative `submit_if_capacity()` |
| `include/crucible/concurrent/Topology.h` | + per-host calibration table for branch-table tier |
| `include/crucible/safety/Workload.h` | + `WorkloadTagged<H>` already there; extend with `intent`, `pipeline_eligible` fields |
| `bench/bench_auto_split_compare.cpp` | + CPU-time column, + parallel-efficiency column, + intent variation per scenario |
| `test/test_auto_split.cpp` | + tests for efficiency gate (verify it correctly rejects cores-burning fanout) |
| `test/test_auto_split.cpp` | + tests for ShapeCache hit/miss, EWMA convergence |
| (new) `bench/bench_auto_split_async.cpp` | strategy comparison for sync vs async-spawn vs streaming |
| (new) `bench/bench_auto_split_pressure.cpp` | router behavior under simulated pool oversubscription |
| (new) `test/test_auto_split_intent.cpp` | type-level intent grade enforces correct factor |

---

## The construction contract — what every new Crucible primitive owes the router

This is the load-bearing discipline.  Every type that participates in
dispatch (which means: every type that ends up inside a body lambda,
or that ends up as a body lambda) signs a contract with the router.
The contract has six clauses; satisfying them is "router-friendly by
construction".

### Clause 1 — declare your residency tier

If the type's data lives in a known cache tier, say so via
`ResidencyHeat<Tier, T>`.  The router uses this to skip fanout for
L1/L2-resident bodies and to enable NUMA-spread for DRAM-bound ones.

```cpp
// L1-resident hot-path counter — never fanout
using HotCounter = ResidencyHeat<L1, std::atomic<uint64_t>>;

// DRAM-bound payload — fanout candidate
using DramTile  = ResidencyHeat<DRAM, std::array<float, 8192>>;
```

### Clause 2 — declare your numerical determinism

If reductions over the body must be bit-exact, use
`NumericalTier<BITEXACT_STRICT, T>`.  The router will refuse fanout
that would reorder reductions.

```cpp
using BitexactSum = NumericalTier<BITEXACT_STRICT, double>;  // → seq only
using OrderedSum  = NumericalTier<ORDERED, double>;          // → fanout OK
using LooseSum    = NumericalTier<UNORDERED, double>;        // → fanout free
```

### Clause 3 — declare your effect row

Pure bodies are always fanout-safe.  IO-bound bodies benefit from
fanout past core count.  Block-bound bodies want async-spawn placement.
The effect row ALREADY captures this — the router just needs to read
it.

```cpp
struct PureFold { effects::Pure } body;        // fanout free
struct IoDrain  { effects::IO   } body;        // fanout past N cores
struct BlockEvent { effects::Block } body;     // async-spawn preferred
```

### Clause 4 — opt into a `WorkloadHint` if you have one

When you know your body's per-item cost, declare it.  The router uses
the cost in break-even and efficiency gates.  If you don't know,
**don't guess** — leave it 0 and the router falls through to the
byte-tier rule.

```cpp
// Body author KNOWS this hash chase is ~50 ns/item.  Tell the router.
struct HashChase : WorkloadTagged<{
    .per_item_ns = 50,
    .max_natural_shards = 16,
    .intent = SchedulingIntent::Throughput,
    .is_pure = true,
}> {
    void operator()(AutoSplitShard) const noexcept;
};
```

### Clause 5 — keep `sizeof(Body) ≤ 256`

Heavy captures cause lambda-copy ballooning when fanned out.  The
auto-inference rule sets `max_natural_shards = 4` for any body
exceeding 256 B, but you should aim lower — capture pointers, not
buffers.

```cpp
// BAD: copies 4 KB into the task queue 16 times
auto bad = [arena = std::array<float, 1024>{}](AutoSplitShard s) { ... };

// GOOD: captures one pointer; arena lives elsewhere
auto good = [arena = &g_arena](AutoSplitShard s) { ... };
static_assert(sizeof(decltype(good)) == sizeof(void*));
```

### Clause 6 — reach for `dispatch_auto_split_typed<Body>`, not the
generic version

The typed entry point is **strictly cheaper or equal** to the untyped
one — it folds in compile-time hints before calling the planner.
There's no reason to use the untyped form except in dynamic-dispatch
cases (type-erased callbacks), and those should be rare.

```cpp
// Preferred — type-level hints fold in for free
auto result = dispatch_auto_split_typed<MyBody>(pool, request, body);

// Acceptable only when Body is type-erased
auto result = dispatch_auto_split(pool, request, std::move(any_body));
```

### Review checklist for new code

When merging a new primitive that participates in dispatch:

- [ ] Does the body type carry residency / numerical / row grades?
- [ ] If per-item cost is known, is `WorkloadHint` populated?
- [ ] Is `sizeof(Body) ≤ 256`?
- [ ] Does the call site use `dispatch_auto_split_typed<Body>`?
- [ ] Are there `static_assert`s pinning the routing decision?

If 4 of 5 are checked, the primitive is router-friendly.  If <3, the
router falls back to the byte-tier rule — possibly correct, possibly
not.  Reviewers should ask why the type is opaque.

## Closing thought

The sharpest realization in this pass: **the router should be the
substrate's dumbest component.**  It composes signals that already
exist in the type system (effect rows, residency tier, hot-path
grade, vendor, alloc class, wait strategy, NUMA placement) and turns
them into a 4-tuple decision through a 5-tier funnel.

Everything clever lives in the substrate; the router just reads the
type and obeys.  That's why it can be sub-ns in steady state — there's
no decision logic to run, just a cache lookup or a `if constexpr` that
the optimizer folded away three layers up.

The "best in class ever" router for Crucible isn't the smartest
function in the codebase.  It's the one that **delegates everything
to the type system** and stays out of the way.

The phrase **"router-friendly by construction"** is the architectural
mandate.  Every new Crucible primitive owes the router enough type-level
information to answer its routing question without measurement.  The
substrate makes this trivial — the grades already exist.  The job
across the codebase is to surface them at every dispatch site.

When that job is done, **the autorouter as a runtime construct
disappears**.  What remains is a constexpr fold over typed substrate
blocks, and a cache for the rare cases where runtime profile data
is actually needed.  That's the endpoint.
