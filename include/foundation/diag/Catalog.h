#pragma once

#include <foundation/Platform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace foundation::diag {

struct tag_base {
    constexpr tag_base() noexcept = default;
    constexpr tag_base(const tag_base&) noexcept = default;
    constexpr tag_base(tag_base&&) noexcept = default;
    constexpr tag_base& operator=(const tag_base&) noexcept = default;
    constexpr tag_base& operator=(tag_base&&) noexcept = default;
    ~tag_base() = default;
};

// Authoring rule for every tag below: the description states what the bug
// class is, the remediation states how to fix an instance of it. Both read as
// standalone sentences, because a build log shows them without this file.
struct EffectRowMismatch : tag_base {
    static constexpr std::string_view name = "EffectRowMismatch";
    static constexpr std::string_view description = "Met(X) Subrow<R_callee, R_caller> failed: a function declared "
                                                    "with effect row R_callee was invoked from a context with "
                                                    "effect row R_caller, where R_callee is not a subrow of "
                                                    "R_caller.  The callee requires effects (Bg, IO, Block, "
                                                    "Alloc, Init, Test) the caller's row does not permit.  Row "
                                                    "arithmetic per Tang-Lindley POPL 2026.";
    static constexpr std::string_view remediation =
        "Either widen the caller's row by lifting through weaken<R_wider>() "
        "to include the callee's effects, OR narrow the callee's row by "
        "removing operations that introduce the offending effects.  Use "
        "row_difference_t<R_callee, R_caller> to identify exactly which "
        "atoms are missing.  See effects/EffectRow.h for the row algebra.";
};

struct UnknownParameterShape : tag_base {
    static constexpr std::string_view name = "UnknownParameterShape";
    static constexpr std::string_view description = "The dispatcher (FOUND-D) could not classify the function's "
                                                    "parameter list against any of the seven canonical shapes "
                                                    "(UnaryTransform, BinaryTransform, Reduction, ProducerEndpoint, "
                                                    "ConsumerEndpoint, SwmrWriter, SwmrReader, PipelineStage).  No "
                                                    "automatic lowering is selected; manual orchestration via "
                                                    "parallel_for_views / mint_permission_fork / Queue::* is required.";
    static constexpr std::string_view remediation =
        "Either reshape the function signature to match a canonical "
        "shape (most commonly: change a raw `T*` to `OwnedRegion<T, Tag>&&` "
        "for the unary/binary transform path), or call the underlying "
        "primitives directly via the manual orchestration surface.  See "
        "27_04_2026.md §3 for the full shape catalog.";
};

struct GradedWrapperViolation : tag_base {
    static constexpr std::string_view name = "GradedWrapperViolation";
    static constexpr std::string_view description = "An attempt to construct a Graded-backed wrapper (Linear, "
                                                    "Refined, Tagged, Secret, Monotonic, AppendOnly, Stale, "
                                                    "TimeOrdered, etc.) violated the GradedWrapper concept "
                                                    "contract.  Common causes: substrate type mismatch (graded_type "
                                                    "is not a Graded<...> specialization), modality inconsistency "
                                                    "(declared Absolute but substrate is Comonad), forwarder "
                                                    "fidelity break (value_type_name() / lattice_name() return "
                                                    "strings inconsistent with substrate).  See algebra/GradedTrait.h "
                                                    "for the full concept definition and the CHEAT-1..CHEAT-5 "
                                                    "audit cluster.";
    static constexpr std::string_view remediation = "Audit the wrapper against algebra/GradedTrait.h's "
                                                    "GradedWrapper concept clause-by-clause: verify graded_type is "
                                                    "Graded<M, L, T> for some M/L/T; verify W::modality matches "
                                                    "graded_modality_v<W::graded_type>; verify forwarders return "
                                                    "the SAME strings as the substrate's.  Run the cheat probe "
                                                    "harness (test/test_concept_cheat_probe.cpp) after fixing.";
};

struct LinearityViolation : tag_base {
    static constexpr std::string_view name = "LinearityViolation";
    static constexpr std::string_view description = "A linear value (Linear<T>, Permission<Tag>, OwnedRegion<T, "
                                                    "Tag>) was used in a way that violates QTT linearity (Atkey "
                                                    "FLoC 2018): copied (must be moved); consumed twice (must be "
                                                    "consumed once); used after move (the moved-from state is "
                                                    "linear-zero, not linear-one).  CSL frame rule violation if "
                                                    "the value is a Permission token.";
    static constexpr std::string_view remediation = "Trace the value's flow.  Each linear value has exactly ONE "
                                                    "consumer; any sharing requires either explicit duplication "
                                                    "(if the substrate permits — most don't) or fractional "
                                                    "permissions via SharedPermissionPool<Tag>.  Use std::move at "
                                                    "the consumption point; capture by value not reference into a "
                                                    "lambda that takes ownership.  See permissions/Permission.h for "
                                                    "the CSL primitive surface.";
};

struct RefinementViolation : tag_base {
    static constexpr std::string_view name = "RefinementViolation";
    static constexpr std::string_view description = "A Refined<Pred, T> constructor was called with a value that "
                                                    "fails the predicate.  The predicate evaluation happens in "
                                                    "the constructor's pre() clause; under contract semantic="
                                                    "enforce the failure aborts via std::terminate, under "
                                                    "semantic=ignore the value is constructed with a violated "
                                                    "invariant (caller's responsibility to validate first).";
    static constexpr std::string_view remediation = "Either validate the value before construction (call Pred(v) "
                                                    "explicitly and branch), OR use Refined<Pred, T>::Trusted{} "
                                                    "construction at sites where the caller has already proven "
                                                    "the invariant by other means.  Never use Trusted{} as a "
                                                    "general escape hatch — every use is a documented "
                                                    "load-bearing assertion that the caller is responsible for.  "
                                                    "See safety/Refined.h for the predicate catalog.";
};

struct HotPathViolation : tag_base {
    static constexpr std::string_view name = "HotPathViolation";
    static constexpr std::string_view description = "A function declared as HotPath<Hot, T> (the foreground recording "
                                                    "path: zero allocation, zero syscall, zero block) invoked a "
                                                    "callee whose HotPath grade is Warm (alloc OK, no syscall) or "
                                                    "Cold (block + IO OK).  The hot path's per-op latency budget is "
                                                    "shape-dependent; admitting Warm/Cold callees breaks the budget "
                                                    "structurally.  Crucible discipline per CLAUDE.md §IX.";
    static constexpr std::string_view remediation = "Either move the offending operation off the hot path (drain "
                                                    "to bg thread via SPSC ring; defer to a Warm-tier helper), OR "
                                                    "if the operation IS hot-path-safe, give its declaration a "
                                                    "HotPath<Hot, T> wrapper to admit it under the gate.  The "
                                                    "common refactor: a printf for debugging is Cold; replace with "
                                                    "atomic counter increment (Hot) and drain to a bg formatter.";
};

struct DetSafeLeak : tag_base {
    static constexpr std::string_view name = "DetSafeLeak";
    static constexpr std::string_view description = "The 8th axiom (DetSafe per CLAUDE.md §II.8) is violated: a "
                                                    "function declared as DetSafe<Pure, T> or DetSafe<PhiloxRng, "
                                                    "T> invoked a callee carrying MonotonicClockRead, "
                                                    "WallClockRead, EntropyRead, FilesystemMtime, or "
                                                    "NonDeterministicSyscall.  Same inputs → same outputs is "
                                                    "structurally broken; bit-exact replay (CI invariant) is "
                                                    "broken; cross-vendor numerics CI will reject downstream "
                                                    "outputs.  This is the load-bearing diagnostic that the "
                                                    "FOUND-I cache row fence enforces.";
    static constexpr std::string_view remediation = "Either eliminate the non-deterministic source (replace "
                                                    "wall-clock seed with Philox-derived seed; replace "
                                                    "/dev/urandom read with seeded Philox), OR if the operation "
                                                    "is genuinely impure (e.g., runtime metric collection), "
                                                    "lift the caller out of DetSafe<Pure> into "
                                                    "DetSafe<MonotonicClockRead> or higher tier.  Cipher::record_event "
                                                    "refuses any tier above PhiloxRng; the replay log cannot be "
                                                    "constructed from impure values.";
};

struct NumericalTierMismatch : tag_base {
    static constexpr std::string_view name = "NumericalTierMismatch";
    static constexpr std::string_view description = "A function pinned at NumericalTier<BITEXACT_STRICT> or "
                                                    "NumericalTier<BITEXACT_TC> invoked a kernel whose recipe "
                                                    "tier is RELAXED, ULP_INT8, ULP_FP8, or ULP_FP16.  The "
                                                    "tier-pinned consumer requires bit-exact (or bounded-ULP) "
                                                    "outputs; the looser kernel cannot satisfy the contract.  "
                                                    "Recipe tier vocabulary per FORGE.md §20 / NumericalRecipe.h.";
    static constexpr std::string_view remediation = "Either select a recipe whose tier matches the caller's "
                                                    "requirement (use Forge Phase E.RecipeSelect's fleet "
                                                    "intersection picker constrained to the required tier), OR "
                                                    "loosen the caller's tier pin if bit-exact is not actually "
                                                    "required for this code path.  Cross-vendor numerics CI "
                                                    "pairwise-validates recipe outputs against the CPU scalar-FMA "
                                                    "oracle per tolerance — verify the looser tier still meets "
                                                    "the application's accuracy requirement.";
};

struct MemOrderViolation : tag_base {
    static constexpr std::string_view name = "MemOrderViolation";
    static constexpr std::string_view description = "A function in concurrent/* used or required MemOrder<SeqCst>.  "
                                                    "Crucible discipline (CLAUDE.md §IX) forbids seq_cst on the "
                                                    "hot path: x86 emits MFENCE (~30ns latency); ARM emits DMB "
                                                    "ISH (~2-5 cycles); both serialize the store buffer.  "
                                                    "Acquire/release semantics suffice for every SPSC/MPMC ring, "
                                                    "every snapshot, every lock-free pattern Crucible needs.";
    static constexpr std::string_view remediation = "Replace memory_order_seq_cst with memory_order_acq_rel "
                                                    "(for read-modify-write), memory_order_release (for store), "
                                                    "or memory_order_acquire (for load).  Audit the ordering "
                                                    "requirement: if you genuinely need a total store order "
                                                    "across multiple atomics, reconsider the design — it's "
                                                    "almost always a sign that ownership boundaries are wrong.  "
                                                    "See CLAUDE.md §IX 'The latency hierarchy' for the structural "
                                                    "argument.";
};

struct AllocClassViolation : tag_base {
    static constexpr std::string_view name = "AllocClassViolation";
    static constexpr std::string_view description = "A function pinned at AllocClass<Stack>, AllocClass<Pool>, or "
                                                    "AllocClass<Arena> attempted to allocate from Heap or Mmap "
                                                    "(or invoked a callee that does).  The hot path forbids heap "
                                                    "allocation: malloc round-trip is ~50-200ns and unpredictable; "
                                                    "Arena bump is ~2ns and lock-free.  Crucible discipline per "
                                                    "HS10 (CLAUDE.md §XVIII).";
    static constexpr std::string_view remediation = "Replace heap allocation with arena allocation: use "
                                                    "Arena::alloc_obj<T>() / Arena::alloc_array<T>(n) for DAG-"
                                                    "lifetime objects; PoolAllocator for object-pool patterns; "
                                                    "static buffers for genuinely-fixed-size data.  If the "
                                                    "allocation is required and cannot be moved off the hot path, "
                                                    "lift the caller's AllocClass to Heap explicitly — but "
                                                    "document why the hot-path discipline is being relaxed.";
};

struct VendorBackendMismatch : tag_base {
    static constexpr std::string_view name = "VendorBackendMismatch";
    static constexpr std::string_view description = "A kernel pinned at Vendor<NV>, Vendor<AMD>, Vendor<TPU>, "
                                                    "Vendor<TRN>, or Vendor<CER> was emitted by, or routed to, "
                                                    "the wrong vendor's Mimic backend.  Each vendor's backend "
                                                    "owns its IR003* lowering and native ISA emission; cross-"
                                                    "vendor mismatches indicate a routing bug in Forge Phase H "
                                                    "(MIMIC.md §22) or a recipe-registry drift between "
                                                    "advertised native_on bitmaps and actual emit support.";
    static constexpr std::string_view remediation = "Verify the kernel's Vendor pin matches the target backend at "
                                                    "the dispatcher level.  Cross-vendor portability is an explicit "
                                                    "design choice (Vendor<Portable> admits any backend); if the "
                                                    "kernel is Portable, the routing layer should pick a backend "
                                                    "based on the active TargetCaps — not propagate the Portable "
                                                    "tag downstream.";
};

struct CrashClassMismatch : tag_base {
    static constexpr std::string_view name = "CrashClassMismatch";
    static constexpr std::string_view description = "A function pinned at Crash<NoThrow> invoked a callee declared "
                                                    "as Crash<Throw>, Crash<Abort>, or Crash<ErrorReturn>.  "
                                                    "Crucible compiles with -fno-exceptions (CLAUDE.md §III); "
                                                    "throwing across a NoThrow boundary terminates the program.  "
                                                    "BSYZ22 crash-stop session types relate this discipline to "
                                                    "the OneShotFlag-guarded boundaries.";
    static constexpr std::string_view remediation = "Two routes.  (a) If the callee is genuinely fallible, "
                                                    "convert its return type to std::expected<T, E> — the caller "
                                                    "then handles the failure explicitly without exception "
                                                    "machinery.  (b) If the callee's failure mode is "
                                                    "structurally impossible at this call site, wrap it in a "
                                                    "noexcept adapter that crucible_abort's on the impossible "
                                                    "case (documents the assumption).";
};

struct ConsistencyMismatch : tag_base {
    static constexpr std::string_view name = "ConsistencyMismatch";
    static constexpr std::string_view description = "A Forge Phase K BatchPolicy axis pinned at "
                                                    "Consistency<STRONG> was configured against a runtime "
                                                    "consistency tier of EVENTUAL, READ_YOUR_WRITES, "
                                                    "CAUSAL_PREFIX, or BOUNDED_STALENESS (or vice versa: "
                                                    "EVENTUAL caller invoked a STRONG-required collective).  "
                                                    "TP / DP / PP / EP / CP axes have different consistency "
                                                    "requirements per CRUCIBLE.md §L13 5D parallelism rules.";
    static constexpr std::string_view remediation = "Verify the per-axis consistency declaration in BatchPolicy "
                                                    "matches the axis's actual requirement: TP must be STRONG "
                                                    "(weight identity within a step is unconditional); DP can be "
                                                    "BOUNDED_STALENESS (DiLoCo allows pseudo-gradient drift); "
                                                    "EP can be EVENTUAL (expert routing converges over rounds).  "
                                                    "See FORGE.md §K for the per-axis specification.";
};

struct LifetimeViolation : tag_base {
    static constexpr std::string_view name = "LifetimeViolation";
    static constexpr std::string_view description = "An OpaqueLifetime<PER_REQUEST, T> value crossed a boundary "
                                                    "into a PER_PROGRAM or PER_FLEET scope.  The lifetime "
                                                    "lattice's chain order is "
                                                    "PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET; promoting a "
                                                    "PER_REQUEST value to longer lifetime leaks per-request data "
                                                    "across requests (security / isolation violation).  Pattern "
                                                    "from Pie SOSP 2025 inferlets.";
    static constexpr std::string_view remediation = "Either rebuild the value at the longer-lifetime boundary "
                                                    "(so the longer-lifetime cell holds a fresh value, not a "
                                                    "borrowed PER_REQUEST one), OR if the value is genuinely "
                                                    "PER_FLEET-correct, lift its construction site to declare "
                                                    "OpaqueLifetime<PER_FLEET, T>.  Cipher tier promotion: "
                                                    "PER_REQUEST goes to hot tier only; PER_FLEET writes go to "
                                                    "cold tier (S3); never promote across tiers via aliasing.";
};

struct WaitStrategyViolation : tag_base {
    static constexpr std::string_view name = "WaitStrategyViolation";
    static constexpr std::string_view description = "A function pinned at Wait<SpinPause> (intra-core wait, "
                                                    "10-40ns latency) invoked a callee whose wait strategy is "
                                                    "Park (futex / mutex, 1-5μs), Block (kernel scheduler, "
                                                    "10-100μs), or worse.  The hot path's wait latency budget is "
                                                    "the MESI cache-line transfer cost; admitting park/block "
                                                    "callees blows the budget by 2-4 orders of magnitude.  "
                                                    "Wait-strategy hierarchy per CLAUDE.md §IX.";
    static constexpr std::string_view remediation = "Either replace the slow wait with a SpinPause loop "
                                                    "(_mm_pause on x86, yield on ARM) — appropriate when the "
                                                    "expected wait is sub-μs, OR move the wait off the hot path "
                                                    "to a bg-thread helper that can afford Park/Block.  If the "
                                                    "wait is genuinely necessary on the hot path (rare), document "
                                                    "the lift from SpinPause to Park with a justification "
                                                    "comment.";
};

struct ProgressClassViolation : tag_base {
    static constexpr std::string_view name = "ProgressClassViolation";
    static constexpr std::string_view description = "A function declared with Progress<Bounded> (terminates within "
                                                    "a fixed wall-clock budget) or Progress<Productive> (every "
                                                    "step makes observable progress) invoked a callee whose "
                                                    "progress class is MayDiverge.  Forge phases are declared "
                                                    "Bounded per FORGE.md §5 wall-clock budgets; admitting "
                                                    "MayDiverge callees breaks the compile-time-burden contract.";
    static constexpr std::string_view remediation = "Either bound the callee's iteration count (replace while-true "
                                                    "loops with bounded-iteration loops; replace recursion with "
                                                    "tail iteration plus a depth limit), OR if the callee genuinely "
                                                    "may diverge (Inferlet user code, by design), the caller's "
                                                    "Progress declaration is wrong — relax to MayDiverge and "
                                                    "wrap the call in a wall-clock-bounded supervisor.";
};

struct CipherTierViolation : tag_base {
    static constexpr std::string_view name = "CipherTierViolation";
    static constexpr std::string_view description = "A Cipher operation pinned at CipherTier<Hot> (other Relays' "
                                                    "RAM via RAID) was invoked at a context expecting Warm "
                                                    "(local NVMe) or Cold (S3 / GCS), or vice versa.  Tier "
                                                    "discipline per CRUCIBLE.md §L14: Hot writes are cheap and "
                                                    "ephemeral; Warm writes survive reboot; Cold writes survive "
                                                    "total cluster failure.  Mixing tiers loses the invariant the "
                                                    "tier was chosen for.";
    static constexpr std::string_view remediation = "Use the explicit per-tier API: Cipher::publish_hot for "
                                                    "RAID-replicated ephemeral state, publish_warm for NVMe "
                                                    "writes that survive reboot, publish_cold for S3 writes that "
                                                    "survive cluster failure.  Tier promotion is explicit: a "
                                                    "Hot value migrates to Warm via promote_to_warm() (cost: "
                                                    "fsync); to Cold via promote_to_cold() (cost: network).";
};

struct ResidencyHeatViolation : tag_base {
    static constexpr std::string_view name = "ResidencyHeatViolation";
    static constexpr std::string_view description = "A storage-tier operation (KernelCache L1/L2/L3, runtime metrics "
                                                    "ring, etc.) was invoked at the wrong heat class.  L1 is "
                                                    "vendor-portable IR002 (federation-shareable across "
                                                    "organizations); L2 is per-vendor-family IR003* (intra-vendor "
                                                    "shareable); L3 is per-chip compiled bytes (machine-local).  "
                                                    "Mixing heat classes either loses portability (writing per-chip "
                                                    "bytes to L1) or wastes capacity (writing portable IR to L3).";
    static constexpr std::string_view remediation = "Verify the storage tier matches the artifact's portability "
                                                    "level: portable bytecode → L1; vendor-family IR → L2; "
                                                    "compiled native bytes → L3.  See FORGE.md §23 for the "
                                                    "three-level cache architecture and the federation discipline.";
};

struct EpochMismatch : tag_base {
    static constexpr std::string_view name = "EpochMismatch";
    static constexpr std::string_view description = "An EpochVersioned<Epoch, Generation, T> value carried an "
                                                    "epoch tag that does not match the consuming Canopy collective's "
                                                    "current epoch.  Canopy fleet membership changes (Raft-"
                                                    "committed epoch bumps); a value carrying the prior epoch is "
                                                    "stale and must be rebuilt.  Pattern from CRUCIBLE.md §L13 "
                                                    "Canopy distribution layer.";
    static constexpr std::string_view remediation = "Rebuild the value at the new epoch via Canopy::reshard, or "
                                                    "if the value is epoch-independent, declare its construction "
                                                    "without the EpochVersioned wrapper.  Reshard checks include "
                                                    "row intersection across the new fleet (FOUND-K07/K10) — a "
                                                    "stale-epoch value triggers the diagnostic at the first "
                                                    "operation that consumes it.";
};

struct BudgetExceeded : tag_base {
    static constexpr std::string_view name = "BudgetExceeded";
    static constexpr std::string_view description = "A Budgeted<{BitsBudget, PeakBytes}, T> operation exceeded its "
                                                    "declared resource bound.  Bits-budget is the cumulative bits-"
                                                    "transferred allowance for a precision-budget calibrator step; "
                                                    "peak-bytes is the high-water memory residency allowance.  "
                                                    "Budget overshoot indicates the operation needs either a "
                                                    "tighter algorithm or a relaxed budget.  Pattern per "
                                                    "arXiv:2512.06952 resource-bounded type theory.";
    static constexpr std::string_view remediation = "Two routes.  (a) Tighten the algorithm: lower-precision "
                                                    "intermediate types, smaller working sets, reuse buffers via "
                                                    "arena allocation.  (b) Relax the budget at the declaration "
                                                    "site if the larger resource use is justified by application "
                                                    "requirements.  Budget exceedance silently is NOT acceptable "
                                                    "— the diagnostic must fire so the choice is explicit.";
};

struct NumaPlacementMismatch : tag_base {
    static constexpr std::string_view name = "NumaPlacementMismatch";
    static constexpr std::string_view description = "A NumaPlacement<Node, Affinity, T> value was consumed at a "
                                                    "thread whose CPU affinity is not on the value's declared NUMA "
                                                    "node.  Per CLAUDE.md §VIII OS-tuning: cross-node memory "
                                                    "access is 2-4× slower than NUMA-local; the AdaptiveScheduler "
                                                    "(THREADING.md §5.4) routes work to NUMA-local cores when "
                                                    "the working set is L3-resident or DRAM-bound.";
    static constexpr std::string_view remediation = "Either pin the consuming thread to the value's NUMA node "
                                                    "(pthread_setaffinity_np / sched_setaffinity), OR migrate the "
                                                    "value to the consuming thread's NUMA node before consumption "
                                                    "(numa_move_pages).  AdaptiveScheduler does this automatically "
                                                    "for parallel_for_views / parallel_apply_pair when the cost "
                                                    "model recommends NumaLocal placement.";
};

struct RecipeSpecMismatch : tag_base {
    static constexpr std::string_view name = "RecipeSpecMismatch";
    static constexpr std::string_view description = "A RecipeSpec<Tier, Family, T> value was consumed at a Forge "
                                                    "Phase E.RecipeSelect picker that requires a different "
                                                    "(tier, family) combination, or was offered to a Mimic backend "
                                                    "whose native_on bitmap doesn't include the recipe's family "
                                                    "(PAIRWISE / LINEAR / KAHAN / BLOCK_STABLE).  Recipe registry "
                                                    "drift between recipes.json declarations and actual backend "
                                                    "support manifests as this diagnostic.";
    static constexpr std::string_view remediation = "Verify the recipe's declared (tier, family) matches the call "
                                                    "site's pin.  If the family is unsupported on the target "
                                                    "backend, the recipe registry's native_on bitmap should not "
                                                    "have advertised support — file a registry update.  If the "
                                                    "tier is wrong, see NumericalTierMismatch for tier-pinning "
                                                    "remediations.";
};

// The next three tags cover the alias predicates over effect rows. There are
// three and not more. The pure, total and ghost rows are all the empty row, so
// a single tag classifies a failure against any of them. The universal row is
// the lattice top, satisfied by every row, so a violation of it is unreachable
// and carries no tag.
struct PureFunctionViolation : tag_base {
    static constexpr std::string_view name = "PureFunctionViolation";
    static constexpr std::string_view description = "A function declared as IsPure<R> / IsTot<R> / IsGhost<R> was "
                                                    "called with a row R that contains at least one Effect atom.  "
                                                    "PureRow / TotRow / GhostRow all encode the EMPTY effect row "
                                                    "— pure functions have no observable effects in F*.  Adding "
                                                    "Alloc, IO, Block, Bg, Init, or Test to a pure function's row "
                                                    "structurally violates F*'s PURE effect class.  Distinct from "
                                                    "EffectRowMismatch (Category 0) because the bound is the F* "
                                                    "lattice bottom, not an arbitrary caller-imposed row.";
    static constexpr std::string_view remediation = "Either remove the offending operation from the function body "
                                                    "(replace allocation with stack storage; replace I/O with "
                                                    "in-memory data; replace blocking call with a non-blocking "
                                                    "alternative), OR lift the function's declaration up the F* "
                                                    "lattice — IsPure → IsDiv (admits Block) → IsST (admits Block "
                                                    "+ Alloc + IO) → IsAll (admits anything).  Use the most "
                                                    "restrictive predicate that the function actually needs; the "
                                                    "F* substitution principle (every IsPure function automatically "
                                                    "satisfies IsDiv / IsST / IsAll) propagates the looser bound "
                                                    "to every caller.";
};

struct DivergenceBudgetViolation : tag_base {
    static constexpr std::string_view name = "DivergenceBudgetViolation";
    static constexpr std::string_view description = "A function declared as IsDiv<R> was called with a row R "
                                                    "containing a state-effect atom (Alloc or IO).  DivRow = "
                                                    "Row<Block> — the F* DIV effect class extends PURE only with "
                                                    "non-termination, NOT with state mutation or external "
                                                    "observable effects.  In F*, ST is strictly above DIV in the "
                                                    "effect lattice; using state effects forces the function to "
                                                    "be lifted at least to IsST.";
    static constexpr std::string_view remediation = "Either remove the Alloc / IO operation from the function "
                                                    "body, OR lift the function's declaration to IsST (admits "
                                                    "Block + Alloc + IO) or higher.  The F* lattice is "
                                                    "Pure ⊑ Div ⊑ ST ⊑ All — moving up admits more effects but "
                                                    "narrows the call sites that can use the function (only callers "
                                                    "that already permit ST can pass arguments).  See "
                                                    "effects/FxAliases.h for the full alias-row catalog.";
};

struct StateBudgetViolation : tag_base {
    static constexpr std::string_view name = "StateBudgetViolation";
    static constexpr std::string_view description = "A function declared as IsST<R> was called with a row R "
                                                    "containing a context-tag atom (Bg, Init, or Test).  STRow = "
                                                    "Row<Block, Alloc, IO> — the F* ST effect class admits state "
                                                    "mutation and divergence but NOT context-bound capabilities.  "
                                                    "Bg / Init / Test are dispatch hints, not state effects; they "
                                                    "carry caller-side capability that ST cannot accept "
                                                    "structurally.  Use IsAll for context-bound code.";
    static constexpr std::string_view remediation = "Either remove the context tag from the function's parameters "
                                                    "(if the function is genuinely state-only, the cap-tag "
                                                    "shouldn't be in its signature), OR lift the function's "
                                                    "declaration to IsAll (admits the universe row).  Context tags "
                                                    "encode dispatch position — Bg is bg-thread-only, Init is "
                                                    "startup-only, Test is test-harness-only.  A function that "
                                                    "needs a context tag MUST be IsAll because only IsAll's "
                                                    "AllRow includes the context-tag atoms.";
};

struct InsufficientWitness : tag_base {
    static constexpr std::string_view name = "InsufficientWitness";
    static constexpr std::string_view description = "A binding's proof-relevance witness is below the floor demanded "
                                                    "by a downstream consumer.  Witness lattice: Asserted ⊑ Tested "
                                                    "⊑ CrossValidated ⊑ FormallyVerified.  Downstream consumers "
                                                    "(Cipher hot-tier promotion, Federation peering, AdaptiveScheduler "
                                                    "hot-path admission) require a minimum witness tier per axis; "
                                                    "bindings with weaker witness are refused.  See "
                                                    "safety/witness/Witness.h for the four-tier hierarchy.";
    static constexpr std::string_view remediation = "Either upgrade the binding's witness tier on the offending "
                                                    "axis by switching to a `cg::*_e<W>` evidenced grant variant "
                                                    "with a stronger W (e.g., grant::reentrant → grant::reentrant_e"
                                                    "<Tested<test_id>>), OR loosen the consumer's witness-floor "
                                                    "demand if the weaker tier is acceptable for this consumer.  "
                                                    "Tested<id> references safety/diag/TestRegistry.h entries; "
                                                    "CrossValidated<id> references safety/diag/CiRunRegistry.h.";
};

struct ModalityMismatch : tag_base {
    static constexpr std::string_view name = "ModalityMismatch";
    static constexpr std::string_view description = "Two grants engaging the same fixy dim carry incompatible "
                                                    "modality classes — typically Frame (invariant) paired with "
                                                    "Declares (witness-producing) on the same axis (R018), or two "
                                                    "Quotient grants naming different equivalence-class "
                                                    "representatives (Version<3> vs Version<5>).  Modality classes: "
                                                    "Frame, Declares, Requires, Linear, Quotient.  See "
                                                    "fixy/Modality.h for the taxonomy.";
    static constexpr std::string_view remediation = "Audit the grant pack and pick a single grant per dim with the "
                                                    "correct modality class for the intended semantics.  A property "
                                                    "that is invariant of the value uses Frame; a property the "
                                                    "binding produces uses Declares; an input refinement the "
                                                    "caller must satisfy uses Requires; a consume-and-produce "
                                                    "resource transfer uses Linear; equivalence-class membership "
                                                    "uses Quotient.  Two grants on the same axis cannot mix Frame "
                                                    "with Declares — the invariant claim contradicts the "
                                                    "witness-producing claim.";
};

struct LinearAliasViolation : tag_base {
    static constexpr std::string_view name = "LinearAliasViolation";
    static constexpr std::string_view description = "Two Linear-modality grants on the same Permission tag in a "
                                                    "single binding's pack.  Linear modality encodes one-shot "
                                                    "consume-and-produce resource transfer (lifetime_region<Tag> + "
                                                    "Mutable); two Linear grants on the SAME tag means two parallel "
                                                    "consumers of the same exclusive permission — a CSL frame-rule "
                                                    "violation.  R017 fires before R013 because it catches the "
                                                    "binding-shape error earlier than the call-site rule.";
    static constexpr std::string_view remediation = "Either remove one of the duplicate lifetime_region<Tag> grants "
                                                    "(if the binding actually consumes the permission ONCE), OR "
                                                    "split the binding into two separate fixy fn instances each "
                                                    "consuming one borrow.  CSL discipline: each Permission<Tag> "
                                                    "has exactly one consumer; sharing requires SharedPermission + "
                                                    "explicit fractional borrow via SharedPermissionPool<Tag>.";
};

struct SharedPermissionPoolSaturated : tag_base {
    static constexpr std::string_view name = "SharedPermissionPoolSaturated";
    static constexpr std::string_view description = "SharedPermissionPool<Tag>::lend_raw_() observed an outstanding-"
                                                    "lend count at COUNT_MASK (2^63 - 1) — the atomic state word "
                                                    "cannot admit another lend without aliasing the count field "
                                                    "with the EXCLUSIVE_OUT_BIT.  Incrementing past the mask would "
                                                    "silently mark the pool as upgrade-in-progress AND wrap the "
                                                    "count to zero, producing two contradictory states at once.  "
                                                    "Reaching this site indicates either (a) a runaway borrow leak "
                                                    "(legitimate guards never destructed), (b) a pool reused across "
                                                    "an unbounded lifetime that should have been re-rooted via "
                                                    "mint_permission_root, or (c) a wraparound attack against the "
                                                    "fractional-permission accounting.";
    static constexpr std::string_view remediation = "Audit lifetime of every SharedPermissionGuard<Tag> sourced "
                                                    "from the pool — a leaked guard prevents the count from ever "
                                                    "decrementing.  If borrow lifetimes are correct but throughput "
                                                    "genuinely exceeds 2^63 lend operations across the pool's "
                                                    "lifetime, refactor to use a fresh pool rooted by "
                                                    "mint_permission_root<Tag>() at a higher boundary so older "
                                                    "pools can be sunk.  COUNT_MASK is structural; it is not "
                                                    "tunable because the layout shares its top bit with "
                                                    "EXCLUSIVE_OUT_BIT for lock-free upgrade signaling.";
};

struct HugePageAllocationFailed : tag_base {
    static constexpr std::string_view name = "HugePageAllocationFailed";
    static constexpr std::string_view description = "safety::HugePageBuffer<T>::allocate(count) observed a null "
                                                    "return from std::aligned_alloc(huge_page_bytes, "
                                                    "round_up_huge(count * sizeof(T))).  The kernel could not "
                                                    "satisfy a 2-MB-aligned heap allocation — typical causes are "
                                                    "(a) the transparent-hugepage pool is depleted (nr_hugepages "
                                                    "exhausted under sustained hot-region pressure), (b) the "
                                                    "process address space is fragmented enough that no aligned "
                                                    "extent of the requested size exists, or (c) RLIMIT_AS / "
                                                    "cgroup memory.max has been hit.  Reaching this site is "
                                                    "fatal for the SPSC buffers (TraceRing / MetaLog) that back "
                                                    "Crucible's foreground recording pipeline — bootstrap cannot "
                                                    "complete without them.";
    static constexpr std::string_view remediation = "Audit /proc/sys/vm/nr_hugepages and /proc/meminfo:HugePages_Free "
                                                    "on the host.  Raise the reservation if persistent demand "
                                                    "exceeds the kernel's current pool, or fall back to the "
                                                    "non-hugepage allocation path at a higher boundary.  For "
                                                    "containerized workloads verify that memory.max admits the "
                                                    "requested allocation AND that the hugepage cgroup controller "
                                                    "(if enabled) does not zero the per-cgroup reservation.  At "
                                                    "the architectural level: callers that can tolerate small "
                                                    "pages with TLB pressure should consume HugePageBuffer via "
                                                    "try_allocate (returning std::expected) — this abort path is "
                                                    "reserved for the foundational SPSC backings that genuinely "
                                                    "cannot proceed without a 2-MB-aligned region.";
};

struct PublishOnceDoublePublish : tag_base {
    static constexpr std::string_view name = "PublishOnceDoublePublish";
    static constexpr std::string_view description = "handles::PublishOnce<T>::publish(T*) observed a non-nullptr "
                                                    "value in the slot at the moment of the publish CAS.  The "
                                                    "channel was already claimed by a prior publisher, and the "
                                                    "incoming call attempted to overwrite the published payload.  "
                                                    "PublishOnce is a one-shot publication primitive — exactly one "
                                                    "publisher across the lifetime of the slot, with arbitrarily "
                                                    "many observers.  A second publish is a structural soundness "
                                                    "violation: it (a) silently discards the new payload (CAS "
                                                    "fails, but the caller's wire contract assumed success), or "
                                                    "(b) under a weaker primitive would race against readers that "
                                                    "already acquired the prior value via observe() / try_observe() "
                                                    "and produce a torn channel state.";
    static constexpr std::string_view remediation = "Audit the publisher tree feeding this PublishOnce — at most "
                                                    "one call site must reach the publish path.  Common causes: "
                                                    "(a) two threads racing to establish the same channel without "
                                                    "an outer Once/OneShotFlag guard, (b) a retry loop that "
                                                    "re-enters publish after a transient error instead of failing "
                                                    "upward, (c) a refactor that introduced a second publisher "
                                                    "without the original's `if (already_published()) return;` "
                                                    "early-out.  Permanent fix is structural: front the publish "
                                                    "site with Once::call_once / OneShotFlag::try_set, or use "
                                                    "LazyEstablishedChannel::establish (which serializes via the "
                                                    "Once latch and routes the would-be-second-establisher into "
                                                    "the wait-for-published-pointer observer path instead).  When "
                                                    "PublishOnce is used as a federation-cache slot, the upstream "
                                                    "compile-and-publish pipeline owns the single-publisher "
                                                    "contract — a second publish indicates a cache-key collision "
                                                    "or a duplicate compile entry in flight.";
};

struct BitsInvariantViolation : tag_base {
    static constexpr std::string_view name = "BitsInvariantViolation";
    static constexpr std::string_view description = "safety::Bits<EnumType> observed a runtime value outside the "
                                                    "declared invariant for the wrapped flag enum.  Three concrete "
                                                    "failure modes route through this tag: (a) Bits<E>::from_raw(b) "
                                                    "loaded a deserialized bit-pattern containing flags outside "
                                                    "E's declared mask (e.g., the on-disk word survived an enum-"
                                                    "tightening upgrade and now carries a bit no current E "
                                                    "enumerator names); (b) the future mutual-exclusion invariants "
                                                    "landing with WRAP-Bits-Integration-4 observed two mutually-"
                                                    "exclusive flags set simultaneously (e.g., NodeFlags::Dirty + "
                                                    "NodeFlags::Sealed); (c) a subsumption invariant observed a "
                                                    "parent flag set without its declared child flag (e.g., "
                                                    "MetaFlags::Quantized set without MetaFlags::HasScale).  In "
                                                    "every case the bits_ field carries content the wrapper's "
                                                    "consumers (test() / set() / popcount() / serialization) "
                                                    "depend on EXCLUDING — silent acceptance routes the bad value "
                                                    "into the model state.";
    static constexpr std::string_view remediation = "Audit the producer of the failing bit-pattern.  For "
                                                    "from_raw() deserialization paths: validate the on-disk word "
                                                    "against the current E's full mask BEFORE constructing the "
                                                    "Bits<E> (compare against the OR of all enumerators), and "
                                                    "reject loads from older schema versions through a separate "
                                                    "migration path.  For mutual-exclusion / subsumption "
                                                    "violations: trace the set() / unset() / toggle() call site "
                                                    "that established the invalid combination; the bug is usually "
                                                    "an early-return that skipped the unset() of the conflicting "
                                                    "flag.  Permanent fix: make the invariant a declared "
                                                    "constraint on the Bits<E, Invariants...> instantiation (per "
                                                    "WRAP-Bits-Integration-4) and route mutation through guarded "
                                                    "transitions that fail-loud at the source.";
};

struct BorrowedBoundsViolation : tag_base {
    static constexpr std::string_view name = "BorrowedBoundsViolation";
    static constexpr std::string_view description = "safety::Borrowed<T, Source> observed a bounds-violating "
                                                    "accessor call.  Concrete failure modes: (a) operator[](i) "
                                                    "where i >= size() (per the wrapper's doc-block at "
                                                    "Borrowed.h:250 — `std::span::operator[]` is UB on OOB, the "
                                                    "wrapper forwards without bounds enforcement to preserve hot-"
                                                    "path costs); (b) subspan(offset, count) where "
                                                    "offset + count > size() (per Borrowed.h:276 — same UB "
                                                    "forwarding); (c) front() / back() on an empty Borrowed.  In "
                                                    "every case the consumer reads memory belonging to whatever "
                                                    "follows the source object — common consequences are torn "
                                                    "reads against a sibling field or a SIGBUS at the end of the "
                                                    "Source's mapped region.  The lifetime tag prevents use-after-"
                                                    "destruction of the source; this diagnostic covers the bounds "
                                                    "axis the lifetime gate is silent on.";
    static constexpr std::string_view remediation = "Audit the indexing site for missing size()-comparisons.  The "
                                                    "canonical Borrowed iteration idiom uses the range-based for "
                                                    "or std::ranges algorithms which derive bounds from "
                                                    "begin() / end() — operator[] and subspan() are escape hatches "
                                                    "for index-arithmetic call sites that already proved the "
                                                    "in-range invariant by other means.  Permanent fix: rewrite "
                                                    "the indexing site through size()-aware iteration, or add a "
                                                    "CRUCIBLE_PRE(i < size()) ahead of the operator[] call (the "
                                                    "pre catches at consteval AND under enforce semantic at "
                                                    "runtime — see safety/Pre.h).  For subspan: prefer "
                                                    "subspan(offset).first(count) which reports the misuse at "
                                                    "first() rather than after the offset slice has already "
                                                    "advanced past size().";
};

template <typename T>
inline constexpr bool is_diagnostic_class_v = std::is_base_of_v<tag_base, T> && !std::is_same_v<T, tag_base>;

namespace detail {

// One selector per text field.  Each reads its member directly, so a tag
// that declares no such member is a compile error at the read.  These
// three are the whole of what the accessors below and the catalog arrays
// further down vary over, and each of those walks is written once.
inline constexpr auto select_name = []<typename Tag>() consteval { return std::string_view{Tag::name}; };
inline constexpr auto select_description = []<typename Tag>() consteval { return std::string_view{Tag::description}; };
inline constexpr auto select_remediation = []<typename Tag>() consteval { return std::string_view{Tag::remediation}; };

// The direct form constrains the variable template itself with
// `requires is_diagnostic_class_v<T>`. A requires-clause failure on a variable
// template reports in compiler-chosen wording, which drifts between releases.
// Routing through this function puts the wording under our control.  The
// if constexpr keeps the read away from a type that declares no such
// member, which is the work the false arm of the two-arm struct did.
template <typename T, auto Select>
[[nodiscard]] consteval std::string_view accessor_field_() noexcept {
    static_assert(is_diagnostic_class_v<T>, "foundation::diag [DiagnosticAccessor_NonTag]: "
                                            "diagnostic_name_v / diagnostic_description_v / "
                                            "diagnostic_remediation_v requires T to be derived from "
                                            "foundation::diag::tag_base.  See foundation/diag/Catalog.h's catalog "
                                            "for the shipped tag classes; user-extensions inherit "
                                            "tag_base and provide constexpr name/description/remediation.");
    if constexpr (is_diagnostic_class_v<T>) {
        return Select.template operator()<T>();
    } else {
        return {};
    }
}

}  // namespace detail

template <typename T>
inline constexpr std::string_view diagnostic_name_v = detail::accessor_field_<T, detail::select_name>();

template <typename T>
inline constexpr std::string_view diagnostic_description_v = detail::accessor_field_<T, detail::select_description>();

template <typename T>
inline constexpr std::string_view diagnostic_remediation_v = detail::accessor_field_<T, detail::select_remediation>();

template <typename DiagnosticClass, typename... Context>
    requires is_diagnostic_class_v<DiagnosticClass>
struct Diagnostic {
    using diagnostic_class = DiagnosticClass;
    using context = std::tuple<Context...>;

    static constexpr std::string_view name = DiagnosticClass::name;
    static constexpr std::string_view description = DiagnosticClass::description;
    static constexpr std::string_view remediation = DiagnosticClass::remediation;
};

template <typename T>
struct is_diagnostic : std::false_type {};

template <typename C, typename... Ctx>
struct is_diagnostic<Diagnostic<C, Ctx...>> : std::true_type {};

template <typename T>
inline constexpr bool is_diagnostic_v = is_diagnostic<T>::value;

// Append-only. A new tag is a struct above, and a Category enumerator
// appended here at the next integer value; the tuple below is derived
// from this enum and is not written by hand. The integer values are the
// ordinal pins: reordering or renumbering existing enumerators changes the
// indices that federation cache keys are built from, which invalidates
// every stored key.
enum class Category : std::uint8_t {
    EffectRowMismatch = 0,
    UnknownParameterShape = 1,
    GradedWrapperViolation = 2,
    LinearityViolation = 3,
    RefinementViolation = 4,
    HotPathViolation = 5,
    DetSafeLeak = 6,
    NumericalTierMismatch = 7,
    MemOrderViolation = 8,
    AllocClassViolation = 9,
    VendorBackendMismatch = 10,
    CrashClassMismatch = 11,
    ConsistencyMismatch = 12,
    LifetimeViolation = 13,
    WaitStrategyViolation = 14,
    ProgressClassViolation = 15,
    CipherTierViolation = 16,
    ResidencyHeatViolation = 17,
    EpochMismatch = 18,
    BudgetExceeded = 19,
    NumaPlacementMismatch = 20,
    RecipeSpecMismatch = 21,
    PureFunctionViolation = 22,
    DivergenceBudgetViolation = 23,
    StateBudgetViolation = 24,
    InsufficientWitness = 25,
    ModalityMismatch = 26,
    LinearAliasViolation = 27,
    SharedPermissionPoolSaturated = 28,
    HugePageAllocationFailed = 29,
    PublishOnceDoublePublish = 30,
    BitsInvariantViolation = 31,
    BorrowedBoundsViolation = 32,
};

namespace detail {

// The tag class declared directly in foundation::diag whose identifier is
// `identifier`, or a reflection of void when no tag spells it. Only a
// class derived from tag_base counts, so a stray type of the same name in
// this namespace cannot stand in for a tag.
[[nodiscard]] consteval std::meta::info tag_named(std::string_view identifier) noexcept {
    for (const auto m : std::meta::members_of(^^::foundation::diag, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        if (!std::meta::has_identifier(m) || std::meta::identifier_of(m) != identifier) continue;
        if (m == ^^tag_base || !std::meta::is_base_of_type(^^tag_base, m)) continue;
        return m;
    }
    return ^^void;
}

// One tag reflection per enumerator, in declaration order. The mirror
// check in the self-test pins declaration order to the integer values.
[[nodiscard]] consteval std::vector<std::meta::info> catalog_tags() noexcept {
    std::vector<std::meta::info> tags;
    for (const auto en : std::meta::enumerators_of(^^Category)) {
        tags.push_back(tag_named(std::meta::identifier_of(en)));
    }
    return tags;
}

[[nodiscard]] consteval bool every_category_names_a_tag() noexcept {
    for (const auto tag : catalog_tags()) {
        if (tag == ^^void) return false;
    }
    return true;
}

static_assert(every_category_names_a_tag(), "A Category enumerator names no tag: no class derived from "
                                            "tag_base with that identifier is declared in foundation::diag. "
                                            "Declare the tag struct above the enum, spelled exactly as the "
                                            "enumerator, or remove the enumerator.");

// True when some Category enumerator spells `identifier`.
[[nodiscard]] consteval bool category_names_(std::string_view identifier) noexcept {
    for (const auto en : std::meta::enumerators_of(^^Category)) {
        if (std::meta::identifier_of(en) == identifier) return true;
    }
    return false;
}

// The walk above runs from the enum to the tags. This one runs the other
// way, over the tag classes declared directly in foundation::diag. A tag
// written above the enum and then forgotten in it reaches no Category,
// so tag_of_t and category_of_v never name it and the three accessors
// never answer with its text. Nothing said so before this check.
//
// The walk reads the namespace at this point in the header, which is
// after every shipped tag and after the enum. A user extension declared
// in a later header is a different thing: the Category enum is closed,
// and such a tag is meant to have no enumerator.
[[nodiscard]] consteval bool every_tag_names_a_category() noexcept {
    for (const auto m : std::meta::members_of(^^::foundation::diag, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        if (m == ^^tag_base || !std::meta::is_base_of_type(^^tag_base, m)) continue;
        if (!std::meta::has_identifier(m)) continue;
        if (!category_names_(std::meta::identifier_of(m))) return false;
    }
    return true;
}

static_assert(every_tag_names_a_category(), "A tag class declared in foundation::diag has no Category "
                                            "enumerator. The tag is then unreachable through tag_of_t, "
                                            "category_of_v, name_of, description_of and remediation_of. "
                                            "Append an enumerator spelled exactly as the class, at the next "
                                            "free value, or move the class out of foundation::diag if it is "
                                            "meant to stay outside the catalog.");

}  // namespace detail

// The tuple of tag types at the enumerators' positions, derived from the
// enum. tag_of_t<C> and category_of_v<Tag> index it.
using Catalog = [:std::meta::substitute(^^std::tuple, detail::catalog_tags()):];

inline constexpr std::size_t catalog_size = std::tuple_size_v<Catalog>;

// An alias template cannot carry a requires-clause, so the constraint on the
// Category value lives on a struct template that the alias forwards to.
namespace detail {

template <Category C>
struct tag_of_impl {
    static_assert(static_cast<std::size_t>(C) < catalog_size,
                  "tag_of_t<C>: Category value is out of catalog range. "
                  "Likely cause: a Category value cast from an out-of-range "
                  "integer via reinterpret_cast / static_cast without a "
                  "preceding range check.");
    using type = std::tuple_element_t<static_cast<std::size_t>(C), Catalog>;
};

}  // namespace detail

template <Category C>
using tag_of_t = typename detail::tag_of_impl<C>::type;

namespace detail {

template <typename Tag, std::size_t... Is>
[[nodiscard]] consteval std::size_t category_index_fold(std::index_sequence<Is...>) noexcept {
    std::size_t result = sizeof...(Is);  // sentinel: not found
    // A later match overwrites an earlier one. No tag type appears twice in
    // the catalog, so at most one term of the fold ever assigns.
    ((std::is_same_v<Tag, std::tuple_element_t<Is, Catalog>> ? (void)(result = Is) : (void)0), ...);
    return result;
}

template <typename Tag>
[[nodiscard]] consteval std::size_t category_index_of() noexcept {
    return category_index_fold<Tag>(std::make_index_sequence<catalog_size>{});
}

template <typename Tag>
struct category_of_impl {
    static_assert(is_diagnostic_class_v<Tag>, "category_of_v<Tag>: Tag must be derived from "
                                              "foundation::diag::tag_base.  Use diagnostic_name_v / diagnostic_"
                                              "description_v / diagnostic_remediation_v to access tag "
                                              "fields directly without the Category indirection.");
    static constexpr std::size_t index = category_index_of<Tag>();
    static_assert(index < catalog_size, "category_of_v<Tag>: Tag is not registered in the foundation "
                                        "Catalog.  The Category enum is CLOSED to the foundation's 22 "
                                        "wrapper-axis categories; user-defined tags inherit from "
                                        "tag_base and participate in the type-level diagnostic surface "
                                        "(diagnostic_name_v, Diagnostic<UserTag, Ctx...>) without "
                                        "occupying a Category slot.  If you genuinely need this tag "
                                        "in the Category enum, add it to foundation/diag/Catalog.h's Catalog "
                                        "and Category at the same integer index (APPEND-ONLY).");
    static constexpr Category value = static_cast<Category>(index);
};

}  // namespace detail

template <typename Tag>
inline constexpr Category category_of_v = detail::category_of_impl<Tag>::value;

namespace detail {

// One text field of every tag, at the tag's Category index, derived from
// the tuple. Select says which field, so the three arrays share one walk
// rather than repeating it once per field. An accessor indexes one of
// these with a range check, so a value cast in from an out-of-range
// integer answers with the sentinel rather than reading past the array.
template <auto Select, std::size_t... Is>
[[nodiscard]] consteval auto catalog_fields_impl(std::index_sequence<Is...>) noexcept
    -> std::array<std::string_view, sizeof...(Is)> {
    return {Select.template operator()<std::tuple_element_t<Is, Catalog>>()...};
}

inline constexpr auto catalog_names_v = catalog_fields_impl<select_name>(std::make_index_sequence<catalog_size>{});
inline constexpr auto catalog_descriptions_v =
    catalog_fields_impl<select_description>(std::make_index_sequence<catalog_size>{});
inline constexpr auto catalog_remediations_v =
    catalog_fields_impl<select_remediation>(std::make_index_sequence<catalog_size>{});

inline constexpr std::string_view unknown_category_sentinel{"<unknown Category>"};

[[nodiscard]] constexpr std::string_view catalog_field(std::array<std::string_view, catalog_size> const& fields,
                                                       Category c) noexcept {
    const auto index = static_cast<std::size_t>(std::to_underlying(c));
    return index < catalog_size ? fields[index] : unknown_category_sentinel;
}

}  // namespace detail

// The accessors answer with the tag's own field for every enumerator and
// with the sentinel for a value cast in from an out-of-range integer.
[[nodiscard]] constexpr std::string_view name_of(Category c) noexcept {
    return detail::catalog_field(detail::catalog_names_v, c);
}

[[nodiscard]] constexpr std::string_view description_of(Category c) noexcept {
    return detail::catalog_field(detail::catalog_descriptions_v, c);
}

[[nodiscard]] constexpr std::string_view remediation_of(Category c) noexcept {
    return detail::catalog_field(detail::catalog_remediations_v, c);
}

namespace detail {

template <std::size_t... Is>
[[nodiscard]] consteval auto categories_array_impl(std::index_sequence<Is...>) noexcept
    -> std::array<Category, sizeof...(Is)> {
    return std::array<Category, sizeof...(Is)>{static_cast<Category>(Is)...};
}

}  // namespace detail

inline constexpr auto categories_v = detail::categories_array_impl(std::make_index_sequence<catalog_size>{});

namespace detail {

template <typename F, std::size_t... Is>
constexpr void enumerate_categories_impl(F&& f, std::index_sequence<Is...>) noexcept {
    (f.template operator()<static_cast<Category>(Is)>(), ...);
}

}  // namespace detail

template <typename F>
constexpr void enumerate_categories(F&& f) noexcept {
    detail::enumerate_categories_impl(std::forward<F>(f), std::make_index_sequence<catalog_size>{});
}

template <typename Tag, typename... Args>
    requires is_diagnostic_class_v<Tag>
[[nodiscard]] consteval auto mint_diagnostic(Args&&...) noexcept -> Diagnostic<Tag, std::remove_cvref_t<Args>...> {
    return Diagnostic<Tag, std::remove_cvref_t<Args>...>{};
}

}  // namespace foundation::diag

// A condition holding a comma, such as a template argument list, must be
// parenthesised whole, or the preprocessor splits it across the parameters.
#define CRUCIBLE_DIAG_ASSERT(cond, tag, msg) static_assert(cond, "foundation::diag [" #tag "]: " msg)

namespace foundation::diag::detail::diag_self_test {

static_assert(is_diagnostic_class_v<EffectRowMismatch>);
static_assert(is_diagnostic_class_v<DetSafeLeak>);
static_assert(is_diagnostic_class_v<NumericalTierMismatch>);

static_assert(!is_diagnostic_class_v<tag_base>);

static_assert(!is_diagnostic_class_v<int>);
static_assert(!is_diagnostic_class_v<void>);

struct random_struct_for_test {};
static_assert(!is_diagnostic_class_v<random_struct_for_test>);

struct user_defined_tag : tag_base {
    static constexpr std::string_view name = "UserDefinedTag";
    static constexpr std::string_view description = "user-extension test";
    static constexpr std::string_view remediation = "this is a self-test";
};
static_assert(is_diagnostic_class_v<user_defined_tag>);
static_assert(diagnostic_name_v<user_defined_tag> == "UserDefinedTag");

// The visitor further down walks indices 0 to catalog_size - 1 and so counts
// the tuple. The tuple is derived from the enum, so the two counts agree by
// construction; the count is kept as the witness that the derivation saw
// every enumerator.
inline constexpr auto category_enumerators = std::define_static_array(std::meta::enumerators_of(^^Category));

inline constexpr std::size_t category_count = category_enumerators.size();

static_assert(category_count == catalog_size, "FIXY-FOUND-139: Category enum cardinality and Catalog tuple "
                                              "size diverged.  Every Category enumerator must have a matching "
                                              "tag type at the same integer index in the Catalog tuple "
                                              "(append-only discipline).  Likely cause: a new Category value "
                                              "was added without appending the corresponding tag struct + tag "
                                              "specialization, OR a tag was appended to Catalog without "
                                              "shipping the Category enumerator.");

[[nodiscard]] consteval std::size_t enumerator_position(std::meta::info enumerator) noexcept {
    for (std::size_t i = 0; i < category_enumerators.size(); ++i) {
        if (category_enumerators[i] == enumerator) return i;
    }
    return category_enumerators.size();  // sentinel: not found
}

// The tuple is derived from the enum in declaration order, so what is
// checked here is the pin: the enumerator at position I must have the value
// I, the tuple must hold its tag at I, that tag's name must spell the
// enumerator, and the three accessors must answer for it with the tag's own
// fields rather than the sentinel.
[[nodiscard]] consteval bool category_mirrors_catalog() noexcept {
    constexpr std::string_view sentinel{"<unknown Category>"};
    bool mirrors = true;
    // -Wshadow fires spuriously on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : category_enumerators) {
        constexpr std::size_t index = enumerator_position(en);
        constexpr Category c = [:en:];
        using tag = std::tuple_element_t<index, Catalog>;
        mirrors = mirrors && static_cast<std::size_t>(std::to_underlying(c)) == index
               && std::is_same_v<tag, tag_of_t<c>> && std::meta::identifier_of(en) == tag::name
               && name_of(c) == tag::name && description_of(c) == tag::description
               && remediation_of(c) == tag::remediation && name_of(c) != sentinel && description_of(c) != sentinel
               && remediation_of(c) != sentinel;
    }
#pragma GCC diagnostic pop
    return mirrors;
}

static_assert(category_mirrors_catalog(), "The Category enum and the Catalog tuple drifted apart. For the "
                                          "enumerator at position I: its value must be I, the tuple must hold "
                                          "its tag at I, the tag's name must spell the enumerator, and name_of "
                                          "/ description_of / remediation_of must return that tag's fields. "
                                          "Likely cause: an enumerator inserted at a non-terminal position "
                                          "(violates the APPEND-ONLY discipline), an enumerator given a "
                                          "non-matching value, or a tag whose name field does not spell its "
                                          "class.");

template <std::size_t... Is>
[[nodiscard]] consteval bool category_of_reverse_map_impl(std::index_sequence<Is...>) noexcept {
    return ((category_of_v<std::tuple_element_t<Is, Catalog>> == static_cast<Category>(Is)) && ...);
}

[[nodiscard]] consteval bool category_of_reverse_map() noexcept {
    return category_of_reverse_map_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(category_of_reverse_map(), "category_of_v<Tag> does not round-trip with tag_of_t<C>. "
                                         "Likely cause: catalog_category_bijection drift (see preceding "
                                         "assertion) or category_index_fold's match dispatch was modified "
                                         "to break uniqueness.");

template <std::size_t... Is>
[[nodiscard]] consteval bool catalog_names_distinct_impl(std::index_sequence<Is...>) noexcept {
    constexpr auto names = std::array<std::string_view, sizeof...(Is)>{std::tuple_element_t<Is, Catalog>::name...};
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            if (names[i] == names[j]) return false;
        }
    }
    return true;
}

[[nodiscard]] consteval bool catalog_names_distinct() noexcept {
    return catalog_names_distinct_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_names_distinct(), "Two or more tags in Catalog share the same `name` field. "
                                        "Diagnostic names must be unique so build-log greps return one "
                                        "tag per match.");

template <std::size_t... Is>
[[nodiscard]] consteval bool catalog_fields_nonempty_impl(std::index_sequence<Is...>) noexcept {
    return ((!std::tuple_element_t<Is, Catalog>::description.empty()
             && !std::tuple_element_t<Is, Catalog>::remediation.empty()
             && !std::tuple_element_t<Is, Catalog>::name.empty())
            && ...);
}

[[nodiscard]] consteval bool catalog_fields_nonempty() noexcept {
    return catalog_fields_nonempty_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_fields_nonempty(), "One or more tags in Catalog has empty name/description/"
                                         "remediation.  Every tag must carry user-readable prose for all "
                                         "three fields — diagnostic output without remediation guidance "
                                         "is half a diagnostic.");

using d1_t = Diagnostic<EffectRowMismatch, int, float>;
using d2_t = Diagnostic<HotPathViolation>;

static_assert(is_diagnostic_v<d1_t>);
static_assert(is_diagnostic_v<d2_t>);
static_assert(!is_diagnostic_v<EffectRowMismatch>);
static_assert(!is_diagnostic_v<int>);

static_assert(std::is_same_v<typename d1_t::diagnostic_class, EffectRowMismatch>);
static_assert(std::is_same_v<typename d1_t::context, std::tuple<int, float>>);
static_assert(std::is_same_v<typename d2_t::context, std::tuple<>>);

static_assert(d1_t::name == "EffectRowMismatch");
static_assert(d2_t::name == "HotPathViolation");

CRUCIBLE_DIAG_ASSERT(true, EffectRowMismatch, "Self-test happy path: condition is true, macro compiles silently.");

CRUCIBLE_DIAG_ASSERT((std::is_same_v<int, int>), HotPathViolation,
                     "Comma in condition protected by parentheses; preprocessor "
                     "passes the entire is_same_v expression to static_assert.");

static_assert(categories_v.size() == catalog_size, "categories_v cardinality drifted from catalog_size — both must "
                                                   "track the same source of truth.");
static_assert(categories_v[0] == Category::EffectRowMismatch);
static_assert(categories_v[catalog_size - 1] == category_of_v<std::tuple_element_t<catalog_size - 1, Catalog>>);

template <std::size_t... Is>
[[nodiscard]] consteval bool categories_array_matches_enum_impl(std::index_sequence<Is...>) noexcept {
    return ((categories_v[Is] == static_cast<Category>(Is)) && ...);
}

[[nodiscard]] consteval bool categories_array_matches_enum() noexcept {
    return categories_array_matches_enum_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(categories_array_matches_enum(), "categories_v ordering drifted from Category enum integer values. "
                                               "categories_v[I] must equal static_cast<Category>(I) for every "
                                               "in-range I — this is the runtime mirror of catalog_category_"
                                               "bijection.");

[[nodiscard]] consteval std::size_t enumerate_categories_count() noexcept {
    std::size_t count = 0;
    enumerate_categories([&count]<Category /*C*/>() noexcept { ++count; });
    return count;
}

static_assert(enumerate_categories_count() == catalog_size,
              "enumerate_categories<F> did not invoke F for every Category value. "
              "Likely cause: index_sequence dispatch broken or fold expression "
              "regression.");

static_assert(std::is_same_v<decltype(mint_diagnostic<EffectRowMismatch>(int{}, float{})),
                             Diagnostic<EffectRowMismatch, int, float>>);

static_assert(std::is_same_v<decltype(mint_diagnostic<HotPathViolation>()), Diagnostic<HotPathViolation>>);

}  // namespace foundation::diag::detail::diag_self_test

namespace foundation::diag {

namespace detail::smoke {

// A local class may not have static data members, so the fixture tag for the
// smoke test below lives at namespace scope.
struct smoke_local_tag : tag_base {
    static constexpr std::string_view name = "SmokeLocalTag";
    static constexpr std::string_view description = "runtime smoke probe";
    static constexpr std::string_view remediation = "this tag exists only as the runtime_smoke_test fixture";
};

}  // namespace detail::smoke

// The static assertions above run every surface through the constant
// evaluator. This probe runs the same surfaces at runtime, where the compiler
// takes a different path through the same inline bodies.
inline void runtime_smoke_test() noexcept {
    // The volatile bound stops the optimizer from folding the loop back into a
    // constant, which would put the switches back on the compile-time path.
    volatile std::size_t const cap = catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        Category const c = static_cast<Category>(i);
        std::string_view const n = name_of(c);
        std::string_view const d = description_of(c);
        std::string_view const r = remediation_of(c);

        volatile std::size_t sink = 0;
        sink ^= n.size();
        sink ^= d.size();
        sink ^= r.size();
        (void)sink;
    }

    bool const is_tag = is_diagnostic_class_v<detail::smoke::smoke_local_tag>;
    volatile bool sink_b = is_tag;
    (void)sink_b;

    using d_t = Diagnostic<EffectRowMismatch, int, float>;
    volatile std::size_t sink_n = d_t::name.size();
    (void)sink_n;
}

}  // namespace foundation::diag
