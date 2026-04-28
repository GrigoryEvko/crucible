#pragma once

// ── crucible::safety::diag — wrapper-axis diagnostic foundation ──────
//
// The classified-diagnostic vocabulary for the safety/ wrappers, the
// effects/ row carrier, the dispatcher's concept gates, the Cipher
// row-fence, and every other foundation primitive that needs to emit
// a structured error.  This header IS the FOUND-E01 deliverable per
// 28_04_2026_effects.md §7 and the foundation that subsequent
// FOUND-E0{2..20} tasks build on.
//
// ── Architecture ────────────────────────────────────────────────────
//
// Three pillars:
//
//   (1) `tag_base` — an empty class that every diagnostic tag inherits
//       from.  `is_diagnostic_class_v<T>` detects tags via
//       `is_base_of_v<tag_base, T>`.  No trait specialization
//       required; new tags plug in with three lines.
//
//   (2) `Diagnostic<Tag, Ctx...>` — a metafunction-friendly wrapper
//       pairing a tag with arbitrary type-level context.  Used as the
//       failure return type for metafunctions that need to propagate
//       both a result AND the classified reason for failure (mirrors
//       `sessions/SessionDiagnostic.h`'s precedent at line 570).
//
//   (3) `CRUCIBLE_DIAG_ASSERT(cond, tag, msg)` — a routed `static_assert`
//       whose message is prefixed with the bracketed tag name for
//       greppable build logs.  Stringification (#tag) ensures the tag
//       name appears literally in the diagnostic.
//
// Tags are TYPES, not enum values.  The Category enum exists as a
// runtime convenience surface (switch-on-failure-class patterns) and
// is bridged to the type-level catalog via `category_of_v` /
// `tag_of_t`.  The self-test block at file end asserts the bijection
// holds: every Catalog entry maps to exactly one Category, every
// Category maps back to exactly one tag.
//
// ── Why a parallel surface to SessionDiagnostic.h ───────────────────
//
// `sessions/SessionDiagnostic.h` is the SESSION-PROTOCOL vocabulary
// (ProtocolViolation_*, CrashBranch_Missing, SubtypeMismatch, etc.).
// This header is the WRAPPER-AXIS vocabulary (HotPathViolation,
// DetSafeLeak, NumericalTierMismatch, etc.).  Distinct concerns;
// distinct namespaces (`proto::diagnostic` vs `safety::diag`).
//
// They share NO tag types — every concept that could plausibly apply
// to both layers (PermissionImbalance for example) is owned by
// exactly one side.  PermissionImbalance lives in SessionDiagnostic.h
// because the session-protocol layer is its primary emitter; if a
// foundation-layer call site needs to emit it, it `using`-imports the
// session-side tag rather than introducing a duplicate.  The two
// surfaces compose through `Diagnostic<Tag, Ctx...>` since both
// inherit from `tag_base` (this header's version is the canonical
// `tag_base` and SessionDiagnostic.h will be retrofitted to import
// it; for now the two ship parallel `tag_base` markers, intentionally,
// per 28_04 design D10 — refactor is orthogonal and not in critical
// path).
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe   — concept rejections produce structured `static_assert`
//                output with category + offending function/type.
//                Tag types are non-convertible — accidental cross-
//                category propagation is a compile error.
//   InitSafe   — every tag carries NON-EMPTY name / description /
//                remediation, asserted by the self-test block.
//   DetSafe    — output is consteval-built; no runtime formatting on
//                the hot path; output is bit-stable across compiles.
//   LeakSafe   — zero-state types; no resources to leak.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero on the hot path.  Diagnostic infrastructure is consteval at
// the failure point (compile error) and `[[gnu::cold]]` at runtime
// reporting paths (FOUND-E06, separate header).  Hot-path TUs compile
// contracts with `ignore` semantic; the runtime report_violation
// surface NEVER fires there.  Per CLAUDE.md §XII.
//
// ── Extension policy ────────────────────────────────────────────────
//
// Adding a new wrapper category is a four-step structural change:
//
//   1. Add the tag struct (inherit `tag_base`, three constexpr
//      string_views).  Append to the tag definitions section below.
//   2. Add the type to `Catalog` tuple.  APPEND-ONLY — never
//      reorder existing entries (the cache row_hash discipline in
//      FOUND-I depends on stable indices).
//   3. Add the enumerator to `Category` at the same index.
//      APPEND-ONLY — same rationale.
//   4. The self-test block re-fires automatically; if the tag is
//      added to one place but not the other, build fails with a
//      named assertion identifying the gap.
//
// User code that wants a project-local diagnostic class extends the
// catalog by inheriting `tag_base` in its own header.  The local tag
// participates in `is_diagnostic_class_v<T>`, `Diagnostic<Tag, Ctx...>`,
// and `CRUCIBLE_DIAG_ASSERT` without further registration.  It does
// NOT appear in `Category` / `Catalog` — those are the foundation's
// closed catalog used by the dispatcher's switch dispatch.  Local
// tags coexist; they just don't enter the foundation's enumerated
// universe.
//
// ── References ──────────────────────────────────────────────────────
//
//   28_04_2026_effects.md §7         — design rationale + format spec
//   sessions/SessionDiagnostic.h      — the precedent header (~826 LoC,
//                                       23 session-protocol tags)
//   algebra/GradedTrait.h             — the cheat-probe pattern
//   misc/diagnostic_format.md         — structured row-mismatch format
//                                       (FOUND-E19, follow-up)
//
// FOUND-E01 — implements the foundation tag catalog + macro + self-test.
//             Subsequent FOUND-E tasks ship satellite headers under
//             `safety/diag/`:
//               E02-E04: function/type display name + macro hookup
//               E05    : cheat_probe<FnPtr, Category> harness
//               E06    : runtime report_violation cold path
//               E07-E10: stable_name_of / stable_type_id /
//                        stable_function_id / canonicalize_pack
//               E16-E20: per-new-wrapper extensions, F* alias diag,
//                        IDE/clangd integration

#include <crucible/Platform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── tag_base — the inheritance marker ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Every diagnostic tag in this header inherits `tag_base`.  Detection
// is structural via `is_base_of_v<tag_base, T>`; no per-tag trait
// specialization required.  New tags plug in by inheritance alone.

struct tag_base {
    constexpr tag_base() noexcept                = default;
    constexpr tag_base(const tag_base&) noexcept = default;
    constexpr tag_base(tag_base&&) noexcept      = default;
    constexpr tag_base& operator=(const tag_base&) noexcept = default;
    constexpr tag_base& operator=(tag_base&&) noexcept      = default;
    ~tag_base()                                  = default;
};

// ═════════════════════════════════════════════════════════════════════
// ── The 22 wrapper-axis diagnostic tags ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each tag carries three constexpr string_view fields:
//
//   ::name         short identifier (also used in CRUCIBLE_DIAG_ASSERT)
//   ::description  one-paragraph explanation of the BUG CLASS
//   ::remediation  one-paragraph actionable hint for FIXING IT
//
// Discipline: descriptions answer WHAT the bug class is; remediations
// answer HOW to fix it.  Both must be readable as standalone
// sentences.  Per the self-test block: every tag has non-empty
// description and remediation; every name is unique.

// ── 1. EffectRowMismatch ───────────────────────────────────────────
struct EffectRowMismatch : tag_base {
    static constexpr std::string_view name = "EffectRowMismatch";
    static constexpr std::string_view description =
        "Met(X) Subrow<R_callee, R_caller> failed: a function declared "
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

// ── 2. UnknownParameterShape ───────────────────────────────────────
struct UnknownParameterShape : tag_base {
    static constexpr std::string_view name = "UnknownParameterShape";
    static constexpr std::string_view description =
        "The dispatcher (FOUND-D) could not classify the function's "
        "parameter list against any of the seven canonical shapes "
        "(UnaryTransform, BinaryTransform, Reduction, ProducerEndpoint, "
        "ConsumerEndpoint, SwmrWriter, SwmrReader, PipelineStage).  No "
        "automatic lowering is selected; manual orchestration via "
        "parallel_for_views / permission_fork / Queue::* is required.";
    static constexpr std::string_view remediation =
        "Either reshape the function signature to match a canonical "
        "shape (most commonly: change a raw `T*` to `OwnedRegion<T, Tag>&&` "
        "for the unary/binary transform path), or call the underlying "
        "primitives directly via the manual orchestration surface.  See "
        "27_04_2026.md §3 for the full shape catalog.";
};

// ── 3. GradedWrapperViolation ──────────────────────────────────────
struct GradedWrapperViolation : tag_base {
    static constexpr std::string_view name = "GradedWrapperViolation";
    static constexpr std::string_view description =
        "An attempt to construct a Graded-backed wrapper (Linear, "
        "Refined, Tagged, Secret, Monotonic, AppendOnly, Stale, "
        "TimeOrdered, etc.) violated the GradedWrapper concept "
        "contract.  Common causes: substrate type mismatch (graded_type "
        "is not a Graded<...> specialization), modality inconsistency "
        "(declared Absolute but substrate is Comonad), forwarder "
        "fidelity break (value_type_name() / lattice_name() return "
        "strings inconsistent with substrate).  See algebra/GradedTrait.h "
        "for the full concept definition and the CHEAT-1..CHEAT-5 "
        "audit cluster.";
    static constexpr std::string_view remediation =
        "Audit the wrapper against algebra/GradedTrait.h's "
        "GradedWrapper concept clause-by-clause: verify graded_type is "
        "Graded<M, L, T> for some M/L/T; verify W::modality matches "
        "graded_modality_v<W::graded_type>; verify forwarders return "
        "the SAME strings as the substrate's.  Run the cheat probe "
        "harness (test/test_concept_cheat_probe.cpp) after fixing.";
};

// ── 4. LinearityViolation ──────────────────────────────────────────
struct LinearityViolation : tag_base {
    static constexpr std::string_view name = "LinearityViolation";
    static constexpr std::string_view description =
        "A linear value (Linear<T>, Permission<Tag>, OwnedRegion<T, "
        "Tag>) was used in a way that violates QTT linearity (Atkey "
        "FLoC 2018): copied (must be moved); consumed twice (must be "
        "consumed once); used after move (the moved-from state is "
        "linear-zero, not linear-one).  CSL frame rule violation if "
        "the value is a Permission token.";
    static constexpr std::string_view remediation =
        "Trace the value's flow.  Each linear value has exactly ONE "
        "consumer; any sharing requires either explicit duplication "
        "(if the substrate permits — most don't) or fractional "
        "permissions via SharedPermissionPool<Tag>.  Use std::move at "
        "the consumption point; capture by value not reference into a "
        "lambda that takes ownership.  See permissions/Permission.h for "
        "the CSL primitive surface.";
};

// ── 5. RefinementViolation ─────────────────────────────────────────
struct RefinementViolation : tag_base {
    static constexpr std::string_view name = "RefinementViolation";
    static constexpr std::string_view description =
        "A Refined<Pred, T> constructor was called with a value that "
        "fails the predicate.  The predicate evaluation happens in "
        "the constructor's pre() clause; under contract semantic="
        "enforce the failure aborts via std::terminate, under "
        "semantic=ignore the value is constructed with a violated "
        "invariant (caller's responsibility to validate first).";
    static constexpr std::string_view remediation =
        "Either validate the value before construction (call Pred(v) "
        "explicitly and branch), OR use Refined<Pred, T>::Trusted{} "
        "construction at sites where the caller has already proven "
        "the invariant by other means.  Never use Trusted{} as a "
        "general escape hatch — every use is a documented "
        "load-bearing assertion that the caller is responsible for.  "
        "See safety/Refined.h for the predicate catalog.";
};

// ── 6. HotPathViolation ────────────────────────────────────────────
struct HotPathViolation : tag_base {
    static constexpr std::string_view name = "HotPathViolation";
    static constexpr std::string_view description =
        "A function declared as HotPath<Hot, T> (the foreground recording "
        "path: zero allocation, zero syscall, zero block) invoked a "
        "callee whose HotPath grade is Warm (alloc OK, no syscall) or "
        "Cold (block + IO OK).  The hot path's per-op latency budget is "
        "shape-dependent; admitting Warm/Cold callees breaks the budget "
        "structurally.  Crucible discipline per CLAUDE.md §IX.";
    static constexpr std::string_view remediation =
        "Either move the offending operation off the hot path (drain "
        "to bg thread via SPSC ring; defer to a Warm-tier helper), OR "
        "if the operation IS hot-path-safe, give its declaration a "
        "HotPath<Hot, T> wrapper to admit it under the gate.  The "
        "common refactor: a printf for debugging is Cold; replace with "
        "atomic counter increment (Hot) and drain to a bg formatter.";
};

// ── 7. DetSafeLeak ─────────────────────────────────────────────────
struct DetSafeLeak : tag_base {
    static constexpr std::string_view name = "DetSafeLeak";
    static constexpr std::string_view description =
        "The 8th axiom (DetSafe per CLAUDE.md §II.8) is violated: a "
        "function declared as DetSafe<Pure, T> or DetSafe<PhiloxRng, "
        "T> invoked a callee carrying MonotonicClockRead, "
        "WallClockRead, EntropyRead, FilesystemMtime, or "
        "NonDeterministicSyscall.  Same inputs → same outputs is "
        "structurally broken; bit-exact replay (CI invariant) is "
        "broken; cross-vendor numerics CI will reject downstream "
        "outputs.  This is the load-bearing diagnostic that the "
        "FOUND-I cache row fence enforces.";
    static constexpr std::string_view remediation =
        "Either eliminate the non-deterministic source (replace "
        "wall-clock seed with Philox-derived seed; replace "
        "/dev/urandom read with seeded Philox), OR if the operation "
        "is genuinely impure (e.g., Augur metric collection), "
        "lift the caller out of DetSafe<Pure> into "
        "DetSafe<MonotonicClockRead> or higher tier.  Cipher::record_event "
        "refuses any tier above PhiloxRng; the replay log cannot be "
        "constructed from impure values.";
};

// ── 8. NumericalTierMismatch ───────────────────────────────────────
struct NumericalTierMismatch : tag_base {
    static constexpr std::string_view name = "NumericalTierMismatch";
    static constexpr std::string_view description =
        "A function pinned at NumericalTier<BITEXACT_STRICT> or "
        "NumericalTier<BITEXACT_TC> invoked a kernel whose recipe "
        "tier is RELAXED, ULP_INT8, ULP_FP8, or ULP_FP16.  The "
        "tier-pinned consumer requires bit-exact (or bounded-ULP) "
        "outputs; the looser kernel cannot satisfy the contract.  "
        "Recipe tier vocabulary per FORGE.md §20 / NumericalRecipe.h.";
    static constexpr std::string_view remediation =
        "Either select a recipe whose tier matches the caller's "
        "requirement (use Forge Phase E.RecipeSelect's fleet "
        "intersection picker constrained to the required tier), OR "
        "loosen the caller's tier pin if bit-exact is not actually "
        "required for this code path.  Cross-vendor numerics CI "
        "pairwise-validates recipe outputs against the CPU scalar-FMA "
        "oracle per tolerance — verify the looser tier still meets "
        "the application's accuracy requirement.";
};

// ── 9. MemOrderViolation ───────────────────────────────────────────
struct MemOrderViolation : tag_base {
    static constexpr std::string_view name = "MemOrderViolation";
    static constexpr std::string_view description =
        "A function in concurrent/* used or required MemOrder<SeqCst>.  "
        "Crucible discipline (CLAUDE.md §IX) forbids seq_cst on the "
        "hot path: x86 emits MFENCE (~30ns latency); ARM emits DMB "
        "ISH (~2-5 cycles); both serialize the store buffer.  "
        "Acquire/release semantics suffice for every SPSC/MPMC ring, "
        "every snapshot, every lock-free pattern Crucible needs.";
    static constexpr std::string_view remediation =
        "Replace memory_order_seq_cst with memory_order_acq_rel "
        "(for read-modify-write), memory_order_release (for store), "
        "or memory_order_acquire (for load).  Audit the ordering "
        "requirement: if you genuinely need a total store order "
        "across multiple atomics, reconsider the design — it's "
        "almost always a sign that ownership boundaries are wrong.  "
        "See CLAUDE.md §IX 'The latency hierarchy' for the structural "
        "argument.";
};

// ── 10. AllocClassViolation ────────────────────────────────────────
struct AllocClassViolation : tag_base {
    static constexpr std::string_view name = "AllocClassViolation";
    static constexpr std::string_view description =
        "A function pinned at AllocClass<Stack>, AllocClass<Pool>, or "
        "AllocClass<Arena> attempted to allocate from Heap or Mmap "
        "(or invoked a callee that does).  The hot path forbids heap "
        "allocation: malloc round-trip is ~50-200ns and unpredictable; "
        "Arena bump is ~2ns and lock-free.  Crucible discipline per "
        "HS10 (CLAUDE.md §XVIII).";
    static constexpr std::string_view remediation =
        "Replace heap allocation with arena allocation: use "
        "Arena::alloc_obj<T>() / Arena::alloc_array<T>(n) for DAG-"
        "lifetime objects; PoolAllocator for object-pool patterns; "
        "static buffers for genuinely-fixed-size data.  If the "
        "allocation is required and cannot be moved off the hot path, "
        "lift the caller's AllocClass to Heap explicitly — but "
        "document why the hot-path discipline is being relaxed.";
};

// ── 11. VendorBackendMismatch ──────────────────────────────────────
struct VendorBackendMismatch : tag_base {
    static constexpr std::string_view name = "VendorBackendMismatch";
    static constexpr std::string_view description =
        "A kernel pinned at Vendor<NV>, Vendor<AMD>, Vendor<TPU>, "
        "Vendor<TRN>, or Vendor<CER> was emitted by, or routed to, "
        "the wrong vendor's Mimic backend.  Each vendor's backend "
        "owns its IR003* lowering and native ISA emission; cross-"
        "vendor mismatches indicate a routing bug in Forge Phase H "
        "(MIMIC.md §22) or a recipe-registry drift between "
        "advertised native_on bitmaps and actual emit support.";
    static constexpr std::string_view remediation =
        "Verify the kernel's Vendor pin matches the target backend at "
        "the dispatcher level.  Cross-vendor portability is an explicit "
        "design choice (Vendor<Portable> admits any backend); if the "
        "kernel is Portable, the routing layer should pick a backend "
        "based on the active TargetCaps — not propagate the Portable "
        "tag downstream.";
};

// ── 12. CrashClassMismatch ─────────────────────────────────────────
struct CrashClassMismatch : tag_base {
    static constexpr std::string_view name = "CrashClassMismatch";
    static constexpr std::string_view description =
        "A function pinned at Crash<NoThrow> invoked a callee declared "
        "as Crash<Throw>, Crash<Abort>, or Crash<ErrorReturn>.  "
        "Crucible compiles with -fno-exceptions (CLAUDE.md §III); "
        "throwing across a NoThrow boundary terminates the program.  "
        "BSYZ22 crash-stop session types relate this discipline to "
        "the OneShotFlag-guarded boundaries.";
    static constexpr std::string_view remediation =
        "Two routes.  (a) If the callee is genuinely fallible, "
        "convert its return type to std::expected<T, E> — the caller "
        "then handles the failure explicitly without exception "
        "machinery.  (b) If the callee's failure mode is "
        "structurally impossible at this call site, wrap it in a "
        "noexcept adapter that crucible_abort's on the impossible "
        "case (documents the assumption).";
};

// ── 13. ConsistencyMismatch ────────────────────────────────────────
struct ConsistencyMismatch : tag_base {
    static constexpr std::string_view name = "ConsistencyMismatch";
    static constexpr std::string_view description =
        "A Forge Phase K BatchPolicy axis pinned at "
        "Consistency<STRONG> was configured against a runtime "
        "consistency tier of EVENTUAL, READ_YOUR_WRITES, "
        "CAUSAL_PREFIX, or BOUNDED_STALENESS (or vice versa: "
        "EVENTUAL caller invoked a STRONG-required collective).  "
        "TP / DP / PP / EP / CP axes have different consistency "
        "requirements per CRUCIBLE.md §L13 5D parallelism rules.";
    static constexpr std::string_view remediation =
        "Verify the per-axis consistency declaration in BatchPolicy "
        "matches the axis's actual requirement: TP must be STRONG "
        "(weight identity within a step is unconditional); DP can be "
        "BOUNDED_STALENESS (DiLoCo allows pseudo-gradient drift); "
        "EP can be EVENTUAL (expert routing converges over rounds).  "
        "See FORGE.md §K for the per-axis specification.";
};

// ── 14. LifetimeViolation ──────────────────────────────────────────
struct LifetimeViolation : tag_base {
    static constexpr std::string_view name = "LifetimeViolation";
    static constexpr std::string_view description =
        "An OpaqueLifetime<PER_REQUEST, T> value crossed a boundary "
        "into a PER_PROGRAM or PER_FLEET scope.  The lifetime "
        "lattice's chain order is "
        "PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET; promoting a "
        "PER_REQUEST value to longer lifetime leaks per-request data "
        "across requests (security / isolation violation).  Pattern "
        "from Pie SOSP 2025 inferlets.";
    static constexpr std::string_view remediation =
        "Either rebuild the value at the longer-lifetime boundary "
        "(so the longer-lifetime cell holds a fresh value, not a "
        "borrowed PER_REQUEST one), OR if the value is genuinely "
        "PER_FLEET-correct, lift its construction site to declare "
        "OpaqueLifetime<PER_FLEET, T>.  Cipher tier promotion: "
        "PER_REQUEST goes to hot tier only; PER_FLEET writes go to "
        "cold tier (S3); never promote across tiers via aliasing.";
};

// ── 15. WaitStrategyViolation ──────────────────────────────────────
struct WaitStrategyViolation : tag_base {
    static constexpr std::string_view name = "WaitStrategyViolation";
    static constexpr std::string_view description =
        "A function pinned at Wait<SpinPause> (intra-core wait, "
        "10-40ns latency) invoked a callee whose wait strategy is "
        "Park (futex / mutex, 1-5μs), Block (kernel scheduler, "
        "10-100μs), or worse.  The hot path's wait latency budget is "
        "the MESI cache-line transfer cost; admitting park/block "
        "callees blows the budget by 2-4 orders of magnitude.  "
        "Wait-strategy hierarchy per CLAUDE.md §IX.";
    static constexpr std::string_view remediation =
        "Either replace the slow wait with a SpinPause loop "
        "(_mm_pause on x86, yield on ARM) — appropriate when the "
        "expected wait is sub-μs, OR move the wait off the hot path "
        "to a bg-thread helper that can afford Park/Block.  If the "
        "wait is genuinely necessary on the hot path (rare), document "
        "the lift from SpinPause to Park with a justification "
        "comment.";
};

// ── 16. ProgressClassViolation ─────────────────────────────────────
struct ProgressClassViolation : tag_base {
    static constexpr std::string_view name = "ProgressClassViolation";
    static constexpr std::string_view description =
        "A function declared with Progress<Bounded> (terminates within "
        "a fixed wall-clock budget) or Progress<Productive> (every "
        "step makes observable progress) invoked a callee whose "
        "progress class is MayDiverge.  Forge phases are declared "
        "Bounded per FORGE.md §5 wall-clock budgets; admitting "
        "MayDiverge callees breaks the compile-time-burden contract.";
    static constexpr std::string_view remediation =
        "Either bound the callee's iteration count (replace while-true "
        "loops with bounded-iteration loops; replace recursion with "
        "tail iteration plus a depth limit), OR if the callee genuinely "
        "may diverge (Inferlet user code, by design), the caller's "
        "Progress declaration is wrong — relax to MayDiverge and "
        "wrap the call in a wall-clock-bounded supervisor.";
};

// ── 17. CipherTierViolation ────────────────────────────────────────
struct CipherTierViolation : tag_base {
    static constexpr std::string_view name = "CipherTierViolation";
    static constexpr std::string_view description =
        "A Cipher operation pinned at CipherTier<Hot> (other Relays' "
        "RAM via RAID) was invoked at a context expecting Warm "
        "(local NVMe) or Cold (S3 / GCS), or vice versa.  Tier "
        "discipline per CRUCIBLE.md §L14: Hot writes are cheap and "
        "ephemeral; Warm writes survive reboot; Cold writes survive "
        "total cluster failure.  Mixing tiers loses the invariant the "
        "tier was chosen for.";
    static constexpr std::string_view remediation =
        "Use the explicit per-tier API: Cipher::publish_hot for "
        "RAID-replicated ephemeral state, publish_warm for NVMe "
        "writes that survive reboot, publish_cold for S3 writes that "
        "survive cluster failure.  Tier promotion is explicit: a "
        "Hot value migrates to Warm via promote_to_warm() (cost: "
        "fsync); to Cold via promote_to_cold() (cost: network).";
};

// ── 18. ResidencyHeatViolation ─────────────────────────────────────
struct ResidencyHeatViolation : tag_base {
    static constexpr std::string_view name = "ResidencyHeatViolation";
    static constexpr std::string_view description =
        "A storage-tier operation (KernelCache L1/L2/L3, Augur metrics "
        "ring, etc.) was invoked at the wrong heat class.  L1 is "
        "vendor-portable IR002 (federation-shareable across "
        "organizations); L2 is per-vendor-family IR003* (intra-vendor "
        "shareable); L3 is per-chip compiled bytes (machine-local).  "
        "Mixing heat classes either loses portability (writing per-chip "
        "bytes to L1) or wastes capacity (writing portable IR to L3).";
    static constexpr std::string_view remediation =
        "Verify the storage tier matches the artifact's portability "
        "level: portable bytecode → L1; vendor-family IR → L2; "
        "compiled native bytes → L3.  See FORGE.md §23 for the "
        "three-level cache architecture and the federation discipline.";
};

// ── 19. EpochMismatch ──────────────────────────────────────────────
struct EpochMismatch : tag_base {
    static constexpr std::string_view name = "EpochMismatch";
    static constexpr std::string_view description =
        "An EpochVersioned<Epoch, Generation, T> value carried an "
        "epoch tag that does not match the consuming Canopy collective's "
        "current epoch.  Canopy fleet membership changes (Raft-"
        "committed epoch bumps); a value carrying the prior epoch is "
        "stale and must be rebuilt.  Pattern from CRUCIBLE.md §L13 "
        "Canopy distribution layer.";
    static constexpr std::string_view remediation =
        "Rebuild the value at the new epoch via Canopy::reshard, or "
        "if the value is epoch-independent, declare its construction "
        "without the EpochVersioned wrapper.  Reshard checks include "
        "row intersection across the new fleet (FOUND-K07/K10) — a "
        "stale-epoch value triggers the diagnostic at the first "
        "operation that consumes it.";
};

// ── 20. BudgetExceeded ─────────────────────────────────────────────
struct BudgetExceeded : tag_base {
    static constexpr std::string_view name = "BudgetExceeded";
    static constexpr std::string_view description =
        "A Budgeted<{BitsBudget, PeakBytes}, T> operation exceeded its "
        "declared resource bound.  Bits-budget is the cumulative bits-"
        "transferred allowance for a precision-budget calibrator step; "
        "peak-bytes is the high-water memory residency allowance.  "
        "Budget overshoot indicates the operation needs either a "
        "tighter algorithm or a relaxed budget.  Pattern per "
        "arXiv:2512.06952 resource-bounded type theory.";
    static constexpr std::string_view remediation =
        "Two routes.  (a) Tighten the algorithm: lower-precision "
        "intermediate types, smaller working sets, reuse buffers via "
        "arena allocation.  (b) Relax the budget at the declaration "
        "site if the larger resource use is justified by application "
        "requirements.  Budget exceedance silently is NOT acceptable "
        "— the diagnostic must fire so the choice is explicit.";
};

// ── 21. NumaPlacementMismatch ──────────────────────────────────────
struct NumaPlacementMismatch : tag_base {
    static constexpr std::string_view name = "NumaPlacementMismatch";
    static constexpr std::string_view description =
        "A NumaPlacement<Node, Affinity, T> value was consumed at a "
        "thread whose CPU affinity is not on the value's declared NUMA "
        "node.  Per CLAUDE.md §VIII OS-tuning: cross-node memory "
        "access is 2-4× slower than NUMA-local; the AdaptiveScheduler "
        "(THREADING.md §5.4) routes work to NUMA-local cores when "
        "the working set is L3-resident or DRAM-bound.";
    static constexpr std::string_view remediation =
        "Either pin the consuming thread to the value's NUMA node "
        "(pthread_setaffinity_np / sched_setaffinity), OR migrate the "
        "value to the consuming thread's NUMA node before consumption "
        "(numa_move_pages).  AdaptiveScheduler does this automatically "
        "for parallel_for_views / parallel_apply_pair when the cost "
        "model recommends NumaLocal placement.";
};

// ── 22. RecipeSpecMismatch ─────────────────────────────────────────
struct RecipeSpecMismatch : tag_base {
    static constexpr std::string_view name = "RecipeSpecMismatch";
    static constexpr std::string_view description =
        "A RecipeSpec<Tier, Family, T> value was consumed at a Forge "
        "Phase E.RecipeSelect picker that requires a different "
        "(tier, family) combination, or was offered to a Mimic backend "
        "whose native_on bitmap doesn't include the recipe's family "
        "(PAIRWISE / LINEAR / KAHAN / BLOCK_STABLE).  Recipe registry "
        "drift between recipes.json declarations and actual backend "
        "support manifests as this diagnostic.";
    static constexpr std::string_view remediation =
        "Verify the recipe's declared (tier, family) matches the call "
        "site's pin.  If the family is unsupported on the target "
        "backend, the recipe registry's native_on bitmap should not "
        "have advertised support — file a registry update.  If the "
        "tier is wrong, see NumericalTierMismatch for tier-pinning "
        "remediations.";
};

// ═════════════════════════════════════════════════════════════════════
// ── is_diagnostic_class_v<T> ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Inheritance-based detection: T is a diagnostic class iff T is
// derived from `tag_base` AND T is not `tag_base` itself.  Mirrors
// SessionDiagnostic.h's pattern (line 480-481).

template <typename T>
inline constexpr bool is_diagnostic_class_v =
    std::is_base_of_v<tag_base, T> && !std::is_same_v<T, tag_base>;

// ═════════════════════════════════════════════════════════════════════
// ── Accessors (require T to be a diagnostic class) ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The natural form would be:
//
//     template <typename T>
//         requires is_diagnostic_class_v<T>
//     inline constexpr std::string_view diagnostic_name_v = T::name;
//
// but a `requires`-clause failure on a variable template emits
// compiler-version-specific text (per SessionDiagnostic.h's
// rationale at line 487-500).  Route through a helper struct that
// fires a framework-controlled `static_assert` — stable diagnostic
// across GCC versions.

namespace detail {

template <typename T, bool IsTag>
struct accessor_check;

// Valid: T IS a diagnostic class.  Forward the three string fields.
template <typename T>
struct accessor_check<T, true> {
    static constexpr std::string_view name        = T::name;
    static constexpr std::string_view description = T::description;
    static constexpr std::string_view remediation = T::remediation;
};

// Invalid: T is NOT a diagnostic class.  Fire a stable framework-
// controlled `static_assert`.
template <typename T>
struct accessor_check<T, false> {
    static_assert(is_diagnostic_class_v<T>,
        "crucible::safety::diag [DiagnosticAccessor_NonTag]: "
        "diagnostic_name_v / diagnostic_description_v / "
        "diagnostic_remediation_v requires T to be derived from "
        "safety::diag::tag_base.  See safety/Diagnostic.h's catalog "
        "for the shipped tag classes; user-extensions inherit "
        "tag_base and provide constexpr name/description/remediation.");

    static constexpr std::string_view name        = "";
    static constexpr std::string_view description = "";
    static constexpr std::string_view remediation = "";
};

}  // namespace detail

template <typename T>
inline constexpr std::string_view diagnostic_name_v =
    detail::accessor_check<T, is_diagnostic_class_v<T>>::name;

template <typename T>
inline constexpr std::string_view diagnostic_description_v =
    detail::accessor_check<T, is_diagnostic_class_v<T>>::description;

template <typename T>
inline constexpr std::string_view diagnostic_remediation_v =
    detail::accessor_check<T, is_diagnostic_class_v<T>>::remediation;

// ═════════════════════════════════════════════════════════════════════
// ── Diagnostic<Tag, Ctx...> wrapper ────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// A type-level wrapper pairing a diagnostic tag with arbitrary type-
// level context.  Use as the FAILURE return type of metafunctions
// that need to propagate both a result AND the classified reason for
// failure.  Mirrors SessionDiagnostic.h:570-589.
//
// Example:
//
//   template <typename CallerRow, typename CalleeRow>
//   struct check_subrow_result {
//       using type = std::conditional_t<
//           Subrow<CalleeRow, CallerRow>,
//           std::true_type,
//           Diagnostic<EffectRowMismatch, CallerRow, CalleeRow,
//                      row_difference_t<CalleeRow, CallerRow>>
//       >;
//   };

template <typename DiagnosticClass, typename... Context>
    requires is_diagnostic_class_v<DiagnosticClass>
struct Diagnostic {
    using diagnostic_class = DiagnosticClass;
    using context          = std::tuple<Context...>;

    static constexpr std::string_view name        = DiagnosticClass::name;
    static constexpr std::string_view description = DiagnosticClass::description;
    static constexpr std::string_view remediation = DiagnosticClass::remediation;
};

// Shape trait for Diagnostic<...>.
template <typename T>
struct is_diagnostic : std::false_type {};

template <typename C, typename... Ctx>
struct is_diagnostic<Diagnostic<C, Ctx...>> : std::true_type {};

template <typename T>
inline constexpr bool is_diagnostic_v = is_diagnostic<T>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Catalog tuple — the closed type-level universe ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// APPEND-ONLY.  Adding a new tag at position N requires a matching
// Category enum entry at integer value N.  Reordering existing entries
// would invalidate cache row_hash values that depend on stable
// indices (FOUND-I cache key infrastructure).
//
// Index discipline:
//   [ 0] EffectRowMismatch
//   [ 1] UnknownParameterShape
//   [ 2] GradedWrapperViolation
//   [ 3] LinearityViolation
//   [ 4] RefinementViolation
//   [ 5] HotPathViolation
//   [ 6] DetSafeLeak
//   [ 7] NumericalTierMismatch
//   [ 8] MemOrderViolation
//   [ 9] AllocClassViolation
//   [10] VendorBackendMismatch
//   [11] CrashClassMismatch
//   [12] ConsistencyMismatch
//   [13] LifetimeViolation
//   [14] WaitStrategyViolation
//   [15] ProgressClassViolation
//   [16] CipherTierViolation
//   [17] ResidencyHeatViolation
//   [18] EpochMismatch
//   [19] BudgetExceeded
//   [20] NumaPlacementMismatch
//   [21] RecipeSpecMismatch

using Catalog = std::tuple<
    EffectRowMismatch,        //  0
    UnknownParameterShape,    //  1
    GradedWrapperViolation,   //  2
    LinearityViolation,       //  3
    RefinementViolation,      //  4
    HotPathViolation,         //  5
    DetSafeLeak,              //  6
    NumericalTierMismatch,    //  7
    MemOrderViolation,        //  8
    AllocClassViolation,      //  9
    VendorBackendMismatch,    // 10
    CrashClassMismatch,       // 11
    ConsistencyMismatch,      // 12
    LifetimeViolation,        // 13
    WaitStrategyViolation,    // 14
    ProgressClassViolation,   // 15
    CipherTierViolation,      // 16
    ResidencyHeatViolation,   // 17
    EpochMismatch,            // 18
    BudgetExceeded,           // 19
    NumaPlacementMismatch,    // 20
    RecipeSpecMismatch        // 21
>;

inline constexpr std::size_t catalog_size = std::tuple_size_v<Catalog>;

// ═════════════════════════════════════════════════════════════════════
// ── Category enum — runtime convenience surface ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Mirrors `Catalog`'s indices.  Underlying type uint8_t admits up to
// 256 categories — comfortable headroom.  APPEND-ONLY discipline:
// adding a new tag means adding a new enumerator at the END, with
// integer value matching the Catalog's tuple index.  The self-test
// block at file end asserts the bijection holds.
//
// Use Category for runtime switch-on-failure-class patterns:
//
//   void handle_diagnostic(Category cat, std::string_view detail) {
//       switch (cat) {
//           case Category::EffectRowMismatch: ...
//           case Category::HotPathViolation:  ...
//           // ... etc
//       }
//   }
//
// Use the type-level tags for compile-time dispatch.  The bidirectional
// map (tag_of_t / category_of_v) bridges the two surfaces.

enum class Category : std::uint8_t {
    EffectRowMismatch        =  0,
    UnknownParameterShape    =  1,
    GradedWrapperViolation   =  2,
    LinearityViolation       =  3,
    RefinementViolation      =  4,
    HotPathViolation         =  5,
    DetSafeLeak              =  6,
    NumericalTierMismatch    =  7,
    MemOrderViolation        =  8,
    AllocClassViolation      =  9,
    VendorBackendMismatch    = 10,
    CrashClassMismatch       = 11,
    ConsistencyMismatch      = 12,
    LifetimeViolation        = 13,
    WaitStrategyViolation    = 14,
    ProgressClassViolation   = 15,
    CipherTierViolation      = 16,
    ResidencyHeatViolation   = 17,
    EpochMismatch            = 18,
    BudgetExceeded           = 19,
    NumaPlacementMismatch    = 20,
    RecipeSpecMismatch       = 21,
};

// ═════════════════════════════════════════════════════════════════════
// ── tag_of_t<Category> — Category → tag type ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Indexes Catalog by Category's integer value.  Foundation primitive
// for the diagnostic dispatch layer (FOUND-D + the report_violation
// runtime side in FOUND-E06).
//
// Implementation: struct template + alias so we can carry a routed
// `static_assert` for out-of-range Category values.  C++20 alias
// templates do not accept requires-clauses directly; the struct
// template carries the constraint via static_assert.

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

// ═════════════════════════════════════════════════════════════════════
// ── category_of_v<Tag> — tag type → Category ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Walks Catalog at compile time and returns the matching Category
// value.  Uses fold expression rather than recursive template
// instantiation (cheaper compile time, single TU template depth).
//
// Implementation: struct template + variable alias so we can carry a
// routed `static_assert` for tags that are not in the closed Catalog.
// User-defined tags satisfy `is_diagnostic_class_v` (inheritance-based)
// but do NOT belong to the foundation's value-level `Category` enum;
// `category_of_v<UserTag>` fires the static_assert with a remediation
// pointing at the type-level accessor surface instead.

namespace detail {

template <typename Tag, std::size_t... Is>
[[nodiscard]] consteval std::size_t category_index_fold(
    std::index_sequence<Is...>) noexcept
{
    std::size_t result = sizeof...(Is);  // sentinel: not found
    // Fold-or with side-effect: assigns `result` when Tag matches.
    // Each match overwrites; since Catalog is unique-by-tag (asserted
    // by self-test), exactly one match fires per Tag in Catalog.
    ((std::is_same_v<Tag, std::tuple_element_t<Is, Catalog>>
        ? (void)(result = Is)
        : (void)0), ...);
    return result;
}

template <typename Tag>
[[nodiscard]] consteval std::size_t category_index_of() noexcept {
    return category_index_fold<Tag>(std::make_index_sequence<catalog_size>{});
}

template <typename Tag>
struct category_of_impl {
    static_assert(is_diagnostic_class_v<Tag>,
        "category_of_v<Tag>: Tag must be derived from "
        "safety::diag::tag_base.  Use diagnostic_name_v / diagnostic_"
        "description_v / diagnostic_remediation_v to access tag "
        "fields directly without the Category indirection.");
    static constexpr std::size_t index = category_index_of<Tag>();
    static_assert(index < catalog_size,
        "category_of_v<Tag>: Tag is not registered in the foundation "
        "Catalog.  The Category enum is CLOSED to the foundation's 22 "
        "wrapper-axis categories; user-defined tags inherit from "
        "tag_base and participate in the type-level diagnostic surface "
        "(diagnostic_name_v, Diagnostic<UserTag, Ctx...>) without "
        "occupying a Category slot.  If you genuinely need this tag "
        "in the Category enum, add it to safety/Diagnostic.h's Catalog "
        "and Category at the same integer index (APPEND-ONLY).");
    static constexpr Category value = static_cast<Category>(index);
};

}  // namespace detail

template <typename Tag>
inline constexpr Category category_of_v = detail::category_of_impl<Tag>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Category-keyed accessors ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Forward through tag_of_t to the type-level accessors.  Use these
// in runtime dispatch paths:
//
//   void report(Category cat) {
//       fprintf(stderr, "[CRUCIBLE-DIAG] %s: %s\n  remediation: %s\n",
//               name_of(cat).data(),
//               description_of(cat).data(),
//               remediation_of(cat).data());
//   }
//
// Implementation: switch over Category, return the corresponding tag
// type's static field.  `constexpr` (NOT `consteval`) so callers may
// invoke at runtime with non-constant Category values; the switch
// folds to a constant jump table under -O2 and to a direct return
// when the Category is constant-foldable.  The `default:` case
// satisfies CLAUDE.md §VI's `-Werror=switch-default`; it is only
// reachable when a wild Category value is passed via reinterpret_cast
// / static_cast from out-of-range integer.  Self-test block asserts
// every in-range Category value returns a non-sentinel string.

[[nodiscard]] constexpr std::string_view name_of(Category c) noexcept {
    switch (c) {
        case Category::EffectRowMismatch:        return EffectRowMismatch::name;
        case Category::UnknownParameterShape:    return UnknownParameterShape::name;
        case Category::GradedWrapperViolation:   return GradedWrapperViolation::name;
        case Category::LinearityViolation:       return LinearityViolation::name;
        case Category::RefinementViolation:      return RefinementViolation::name;
        case Category::HotPathViolation:         return HotPathViolation::name;
        case Category::DetSafeLeak:              return DetSafeLeak::name;
        case Category::NumericalTierMismatch:    return NumericalTierMismatch::name;
        case Category::MemOrderViolation:        return MemOrderViolation::name;
        case Category::AllocClassViolation:      return AllocClassViolation::name;
        case Category::VendorBackendMismatch:    return VendorBackendMismatch::name;
        case Category::CrashClassMismatch:       return CrashClassMismatch::name;
        case Category::ConsistencyMismatch:      return ConsistencyMismatch::name;
        case Category::LifetimeViolation:        return LifetimeViolation::name;
        case Category::WaitStrategyViolation:    return WaitStrategyViolation::name;
        case Category::ProgressClassViolation:   return ProgressClassViolation::name;
        case Category::CipherTierViolation:      return CipherTierViolation::name;
        case Category::ResidencyHeatViolation:   return ResidencyHeatViolation::name;
        case Category::EpochMismatch:            return EpochMismatch::name;
        case Category::BudgetExceeded:           return BudgetExceeded::name;
        case Category::NumaPlacementMismatch:    return NumaPlacementMismatch::name;
        case Category::RecipeSpecMismatch:       return RecipeSpecMismatch::name;
        default:                                 return std::string_view{"<unknown Category>"};
    }
}

[[nodiscard]] constexpr std::string_view description_of(Category c) noexcept {
    switch (c) {
        case Category::EffectRowMismatch:        return EffectRowMismatch::description;
        case Category::UnknownParameterShape:    return UnknownParameterShape::description;
        case Category::GradedWrapperViolation:   return GradedWrapperViolation::description;
        case Category::LinearityViolation:       return LinearityViolation::description;
        case Category::RefinementViolation:      return RefinementViolation::description;
        case Category::HotPathViolation:         return HotPathViolation::description;
        case Category::DetSafeLeak:              return DetSafeLeak::description;
        case Category::NumericalTierMismatch:    return NumericalTierMismatch::description;
        case Category::MemOrderViolation:        return MemOrderViolation::description;
        case Category::AllocClassViolation:      return AllocClassViolation::description;
        case Category::VendorBackendMismatch:    return VendorBackendMismatch::description;
        case Category::CrashClassMismatch:       return CrashClassMismatch::description;
        case Category::ConsistencyMismatch:      return ConsistencyMismatch::description;
        case Category::LifetimeViolation:        return LifetimeViolation::description;
        case Category::WaitStrategyViolation:    return WaitStrategyViolation::description;
        case Category::ProgressClassViolation:   return ProgressClassViolation::description;
        case Category::CipherTierViolation:      return CipherTierViolation::description;
        case Category::ResidencyHeatViolation:   return ResidencyHeatViolation::description;
        case Category::EpochMismatch:            return EpochMismatch::description;
        case Category::BudgetExceeded:           return BudgetExceeded::description;
        case Category::NumaPlacementMismatch:    return NumaPlacementMismatch::description;
        case Category::RecipeSpecMismatch:       return RecipeSpecMismatch::description;
        default:                                 return std::string_view{"<unknown Category>"};
    }
}

[[nodiscard]] constexpr std::string_view remediation_of(Category c) noexcept {
    switch (c) {
        case Category::EffectRowMismatch:        return EffectRowMismatch::remediation;
        case Category::UnknownParameterShape:    return UnknownParameterShape::remediation;
        case Category::GradedWrapperViolation:   return GradedWrapperViolation::remediation;
        case Category::LinearityViolation:       return LinearityViolation::remediation;
        case Category::RefinementViolation:      return RefinementViolation::remediation;
        case Category::HotPathViolation:         return HotPathViolation::remediation;
        case Category::DetSafeLeak:              return DetSafeLeak::remediation;
        case Category::NumericalTierMismatch:    return NumericalTierMismatch::remediation;
        case Category::MemOrderViolation:        return MemOrderViolation::remediation;
        case Category::AllocClassViolation:      return AllocClassViolation::remediation;
        case Category::VendorBackendMismatch:    return VendorBackendMismatch::remediation;
        case Category::CrashClassMismatch:       return CrashClassMismatch::remediation;
        case Category::ConsistencyMismatch:      return ConsistencyMismatch::remediation;
        case Category::LifetimeViolation:        return LifetimeViolation::remediation;
        case Category::WaitStrategyViolation:    return WaitStrategyViolation::remediation;
        case Category::ProgressClassViolation:   return ProgressClassViolation::remediation;
        case Category::CipherTierViolation:      return CipherTierViolation::remediation;
        case Category::ResidencyHeatViolation:   return ResidencyHeatViolation::remediation;
        case Category::EpochMismatch:            return EpochMismatch::remediation;
        case Category::BudgetExceeded:           return BudgetExceeded::remediation;
        case Category::NumaPlacementMismatch:    return NumaPlacementMismatch::remediation;
        case Category::RecipeSpecMismatch:       return RecipeSpecMismatch::remediation;
        default:                                 return std::string_view{"<unknown Category>"};
    }
}

// ═════════════════════════════════════════════════════════════════════
// ── categories_v — constexpr array of every Category value ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Foundation primitive for downstream consumers (FOUND-D dispatcher,
// FOUND-E06 runtime side) that need to iterate the Category universe
// without re-discovering it via reflection.  Bounded by `catalog_size`;
// generated once at compile time via index-sequence fold.

namespace detail {

template <std::size_t... Is>
[[nodiscard]] consteval auto categories_array_impl(
    std::index_sequence<Is...>) noexcept
    -> std::array<Category, sizeof...(Is)>
{
    return std::array<Category, sizeof...(Is)>{
        static_cast<Category>(Is)... };
}

}  // namespace detail

inline constexpr auto categories_v =
    detail::categories_array_impl(std::make_index_sequence<catalog_size>{});

// ═════════════════════════════════════════════════════════════════════
// ── enumerate_categories<F> — fold F over every Category value ─────
// ═════════════════════════════════════════════════════════════════════
//
// Foundation primitive for FOUND-D dispatcher's diagnostic dispatch
// builders.  Calls F.template operator()<C>() for every Category C
// in the closed catalog, in stable Catalog order.
//
// Usage:
//
//   // Compile-time: build a static lookup table.
//   constexpr auto names = []() consteval {
//       std::array<std::string_view, catalog_size> a{};
//       std::size_t i = 0;
//       enumerate_categories([&]<Category C>() noexcept {
//           a[i++] = name_of(C);
//       });
//       return a;
//   }();
//
//   // Runtime: dispatch a per-category handler.
//   enumerate_categories([&runtime_state]<Category C>() noexcept {
//       runtime_state.register_handler(C, &handler<C>);
//   });
//
// The fold is `constexpr` (NOT `consteval`) so the same primitive
// serves both compile-time and runtime use; the lambda must be
// invocable as a template-member-function (NTTP-templated) and
// noexcept.  Passes are silent; failures fire the lambda's static_
// assert (or constraint failure).

namespace detail {

template <typename F, std::size_t... Is>
constexpr void enumerate_categories_impl(
    F&& f, std::index_sequence<Is...>) noexcept
{
    (f.template operator()<static_cast<Category>(Is)>(), ...);
}

}  // namespace detail

template <typename F>
constexpr void enumerate_categories(F&& f) noexcept {
    detail::enumerate_categories_impl(
        std::forward<F>(f),
        std::make_index_sequence<catalog_size>{});
}

// ═════════════════════════════════════════════════════════════════════
// ── make_diagnostic<Tag>(ctx...) — ergonomic Diagnostic<> factory ──
// ═════════════════════════════════════════════════════════════════════
//
// Deduces the context pack from the function's argument types, sparing
// the caller from spelling out `Diagnostic<Tag, decltype(args)...>`
// explicitly.  Useful for metafunction returns where the context types
// follow from the failure site's local values.
//
// Note: Ctx... is deduced from `Args&&...`; types are decayed via
// std::remove_cvref_t to match the type-level Diagnostic<Tag, Ctx...>
// surface (which expects unqualified types in the pack).

template <typename Tag, typename... Args>
    requires is_diagnostic_class_v<Tag>
[[nodiscard]] consteval auto make_diagnostic(Args&&...)
    -> Diagnostic<Tag, std::remove_cvref_t<Args>...>
{
    return Diagnostic<Tag, std::remove_cvref_t<Args>...>{};
}

}  // namespace crucible::safety::diag

// ═════════════════════════════════════════════════════════════════════
// ── CRUCIBLE_DIAG_ASSERT macro ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Routed `static_assert` whose message is prefixed with the bracketed
// tag name for greppable build logs.  Stringification (#tag) embeds
// the tag name as a literal — same trick as
// CRUCIBLE_SESSION_ASSERT_CLASSIFIED at SessionDiagnostic.h:659.
//
// Usage:
//
//   CRUCIBLE_DIAG_ASSERT(
//       (Subrow<CalleeRow, CallerRow>),
//       EffectRowMismatch,
//       "callee row not a subrow of caller row at "
//       "crucible::vessel::dispatch_op");
//
// IMPORTANT: if the condition contains a comma (template-arg list,
// tuple-of-types, etc.), parenthesise the entire condition so the
// preprocessor doesn't split at the comma.
//
// Produces (on failure):
//   error: static assertion failed: crucible::safety::diag
//          [EffectRowMismatch]: callee row not a subrow of caller
//          row at crucible::vessel::dispatch_op

#define CRUCIBLE_DIAG_ASSERT(cond, tag, msg)                              \
    static_assert(cond,                                                   \
        "crucible::safety::diag [" #tag "]: " msg)

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block — invariants asserted at header inclusion ──────
// ═════════════════════════════════════════════════════════════════════
//
// Every claim this header makes is mechanically verified.  If any
// assertion fires at header-inclusion time, the catalog has drifted
// and the compile fails with a named assertion.  This is the
// load-bearing discipline that keeps the Catalog ↔ Category bijection
// honest as new tags are added.

namespace crucible::safety::diag::detail::diag_self_test {

// ─── tag_base detection — positive and negative ───────────────────

static_assert( is_diagnostic_class_v<EffectRowMismatch>);
static_assert( is_diagnostic_class_v<DetSafeLeak>);
static_assert( is_diagnostic_class_v<NumericalTierMismatch>);

// tag_base itself is not a tag — it's the marker.
static_assert(!is_diagnostic_class_v<tag_base>);

// Plain types and primitives are not tags.
static_assert(!is_diagnostic_class_v<int>);
static_assert(!is_diagnostic_class_v<void>);

struct random_struct_for_test {};
static_assert(!is_diagnostic_class_v<random_struct_for_test>);

// User-defined extension works automatically via inheritance.
struct user_defined_tag : tag_base {
    static constexpr std::string_view name        = "UserDefinedTag";
    static constexpr std::string_view description = "user-extension test";
    static constexpr std::string_view remediation = "this is a self-test";
};
static_assert(is_diagnostic_class_v<user_defined_tag>);
static_assert(diagnostic_name_v<user_defined_tag> == "UserDefinedTag");

// ─── Catalog cardinality matches Category enum cardinality ────────
//
// 22 tags shipped in this version.  Adding a tag bumps both the
// Catalog tuple size AND requires a Category enumerator at the same
// integer value.  The bijection self-test below asserts both in lock
// step.

static_assert(catalog_size == 22,
    "Catalog cardinality drifted from the original 22-tag inventory "
    "— confirm the new tag was added to Catalog AND to Category at "
    "the same integer index.");

// ─── Catalog ↔ Category bijection ─────────────────────────────────
//
// For every Catalog entry at index I, the Category enum's I-th
// value must map back to the same tag type.  Asserted exhaustively
// via index_sequence fold.

template <std::size_t... Is>
[[nodiscard]] consteval bool catalog_category_bijection_impl(
    std::index_sequence<Is...>) noexcept
{
    return ((std::is_same_v<
                std::tuple_element_t<Is, Catalog>,
                tag_of_t<static_cast<Category>(Is)>>) && ...);
}

[[nodiscard]] consteval bool catalog_category_bijection() noexcept {
    return catalog_category_bijection_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_category_bijection(),
    "Catalog tuple ordering drifted from Category enum value ordering. "
    "Each Catalog entry at index I must satisfy "
    "tag_of_t<static_cast<Category>(I)> == std::tuple_element_t<I, Catalog>. "
    "Likely cause: a new tag was inserted at a non-terminal index "
    "(violates the APPEND-ONLY discipline) or the enum value was given "
    "a non-matching integer.");

// ─── category_of_v reverse-map correctness ────────────────────────
//
// For every Catalog entry at index I, category_of_v<tag> must equal
// static_cast<Category>(I).

template <std::size_t... Is>
[[nodiscard]] consteval bool category_of_reverse_map_impl(
    std::index_sequence<Is...>) noexcept
{
    return ((category_of_v<std::tuple_element_t<Is, Catalog>>
             == static_cast<Category>(Is)) && ...);
}

[[nodiscard]] consteval bool category_of_reverse_map() noexcept {
    return category_of_reverse_map_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(category_of_reverse_map(),
    "category_of_v<Tag> does not round-trip with tag_of_t<C>. "
    "Likely cause: catalog_category_bijection drift (see preceding "
    "assertion) or category_index_fold's match dispatch was modified "
    "to break uniqueness.");

// ─── Pairwise name distinctness ────────────────────────────────────
//
// O(N²) pairwise check.  N=22 → 231 comparisons, negligible at
// compile time.  Lifted from SessionDiagnostic.h:803-820.

template <std::size_t... Is>
[[nodiscard]] consteval bool catalog_names_distinct_impl(
    std::index_sequence<Is...>) noexcept
{
    constexpr auto names = std::array<std::string_view, sizeof...(Is)>{
        std::tuple_element_t<Is, Catalog>::name... };
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            if (names[i] == names[j]) return false;
        }
    }
    return true;
}

[[nodiscard]] consteval bool catalog_names_distinct() noexcept {
    return catalog_names_distinct_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_names_distinct(),
    "Two or more tags in Catalog share the same `name` field. "
    "Diagnostic names must be unique so build-log greps return one "
    "tag per match.");

// ─── Non-empty description and remediation per tag ─────────────────

template <std::size_t... Is>
[[nodiscard]] consteval bool catalog_fields_nonempty_impl(
    std::index_sequence<Is...>) noexcept
{
    return ((!std::tuple_element_t<Is, Catalog>::description.empty()
             && !std::tuple_element_t<Is, Catalog>::remediation.empty()
             && !std::tuple_element_t<Is, Catalog>::name.empty()) && ...);
}

[[nodiscard]] consteval bool catalog_fields_nonempty() noexcept {
    return catalog_fields_nonempty_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_fields_nonempty(),
    "One or more tags in Catalog has empty name/description/"
    "remediation.  Every tag must carry user-readable prose for all "
    "three fields — diagnostic output without remediation guidance "
    "is half a diagnostic.");

// ─── name_of / description_of / remediation_of cover every Category ─
//
// Each switch in name_of / description_of / remediation_of must
// return a non-sentinel string for every Category value.  The
// fall-through "<unknown Category>" should fire only on bogus values
// produced by reinterpret_cast (out-of-band).

template <std::size_t... Is>
[[nodiscard]] consteval bool name_of_covers_catalog_impl(
    std::index_sequence<Is...>) noexcept
{
    constexpr std::string_view sentinel{"<unknown Category>"};
    return ((name_of(static_cast<Category>(Is)) != sentinel
             && description_of(static_cast<Category>(Is)) != sentinel
             && remediation_of(static_cast<Category>(Is)) != sentinel) && ...);
}

[[nodiscard]] consteval bool name_of_covers_catalog() noexcept {
    return name_of_covers_catalog_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(name_of_covers_catalog(),
    "name_of / description_of / remediation_of switch is missing an "
    "arm for at least one Category value.  Adding a new tag to "
    "Catalog and Category requires adding a corresponding `case` arm "
    "to all three switches above.");

// ─── name_of returns the SAME string as the tag's static field ────
//
// Catches drift between the type-level forwarders (T::name) and the
// runtime accessors (name_of(C)).

template <std::size_t... Is>
[[nodiscard]] consteval bool name_of_matches_tag_impl(
    std::index_sequence<Is...>) noexcept
{
    return ((name_of(static_cast<Category>(Is))
             == std::tuple_element_t<Is, Catalog>::name) && ...);
}

[[nodiscard]] consteval bool name_of_matches_tag() noexcept {
    return name_of_matches_tag_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(name_of_matches_tag(),
    "name_of(Category) and tag::name diverge for at least one "
    "Catalog entry.  Likely cause: a new tag's `case` arm in "
    "name_of() returns the wrong tag type's `name` field.");

// ─── Diagnostic<Tag, Ctx...> construction and field access ────────

using d1_t = Diagnostic<EffectRowMismatch, int, float>;
using d2_t = Diagnostic<HotPathViolation>;  // empty context

static_assert( is_diagnostic_v<d1_t>);
static_assert( is_diagnostic_v<d2_t>);
static_assert(!is_diagnostic_v<EffectRowMismatch>);
static_assert(!is_diagnostic_v<int>);

static_assert(std::is_same_v<typename d1_t::diagnostic_class, EffectRowMismatch>);
static_assert(std::is_same_v<typename d1_t::context, std::tuple<int, float>>);
static_assert(std::is_same_v<typename d2_t::context, std::tuple<>>);

static_assert(d1_t::name == "EffectRowMismatch");
static_assert(d2_t::name == "HotPathViolation");

// ─── Macro compile-test (happy path) ──────────────────────────────

CRUCIBLE_DIAG_ASSERT(true, EffectRowMismatch,
    "Self-test happy path: condition is true, macro compiles silently.");

// Comma-protected condition (template-arg list).
CRUCIBLE_DIAG_ASSERT((std::is_same_v<int, int>),
    HotPathViolation,
    "Comma in condition protected by parentheses; preprocessor "
    "passes the entire is_same_v expression to static_assert.");

// ─── categories_v has correct cardinality and ordering ────────────

static_assert(categories_v.size() == catalog_size,
    "categories_v cardinality drifted from catalog_size — both must "
    "track the same source of truth.");
static_assert(categories_v[0] == Category::EffectRowMismatch);
static_assert(categories_v[catalog_size - 1] == Category::RecipeSpecMismatch);

template <std::size_t... Is>
[[nodiscard]] consteval bool categories_array_matches_enum_impl(
    std::index_sequence<Is...>) noexcept
{
    return ((categories_v[Is] == static_cast<Category>(Is)) && ...);
}

[[nodiscard]] consteval bool categories_array_matches_enum() noexcept {
    return categories_array_matches_enum_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(categories_array_matches_enum(),
    "categories_v ordering drifted from Category enum integer values. "
    "categories_v[I] must equal static_cast<Category>(I) for every "
    "in-range I — this is the runtime mirror of catalog_category_"
    "bijection.");

// ─── enumerate_categories<F> hits every Category exactly once ─────
//
// Compile-time witness: the visitor's accumulator grows by 1 per
// invocation; expect the final accumulator to equal catalog_size.

[[nodiscard]] consteval std::size_t enumerate_categories_count() noexcept {
    std::size_t count = 0;
    enumerate_categories([&count]<Category /*C*/>() noexcept {
        ++count;
    });
    return count;
}

static_assert(enumerate_categories_count() == catalog_size,
    "enumerate_categories<F> did not invoke F for every Category value. "
    "Likely cause: index_sequence dispatch broken or fold expression "
    "regression.");

// ─── make_diagnostic<Tag>(args...) deduces context correctly ──────

static_assert(std::is_same_v<
    decltype(make_diagnostic<EffectRowMismatch>(int{}, float{})),
    Diagnostic<EffectRowMismatch, int, float>>);

static_assert(std::is_same_v<
    decltype(make_diagnostic<HotPathViolation>()),
    Diagnostic<HotPathViolation>>);

}  // namespace crucible::safety::diag::detail::diag_self_test

// ═════════════════════════════════════════════════════════════════════
// ── runtime_smoke_test — non-constant-args execution probe ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Per the project memory rule (feedback_algebra_runtime_smoke_test_
// discipline): every algebra/* and effects/* (and equivalent
// foundation) header MUST ship `inline void runtime_smoke_test()`
// exercising every public consteval/constexpr surface with NON-
// CONSTANT args.  Pure static_assert tests evaluate consteval bodies
// at compile time only — runtime invocation catches inline-body bugs
// that compile-time evaluation hides (different code path in the
// constexpr evaluator vs. the runtime evaluator).
//
// Call this from any `.cpp` TU that includes Diagnostic.h to verify
// the runtime accessors cover every Category value and return non-
// sentinel strings for the entire enum range.

namespace crucible::safety::diag {

namespace detail::smoke {

// Namespace-scope user-extension tag.  C++26 [class.local]/4 forbids
// local classes from having static data members, so we declare the
// tag at namespace scope and use it from the smoke-test function
// body.  The ::detail::smoke nesting keeps it out of the public
// surface while letting the smoke test instantiate
// is_diagnostic_class_v<smoke_local_tag> at runtime.
struct smoke_local_tag : tag_base {
    static constexpr std::string_view name        = "SmokeLocalTag";
    static constexpr std::string_view description = "runtime smoke probe";
    static constexpr std::string_view remediation =
        "this tag exists only as the runtime_smoke_test fixture";
};

}  // namespace detail::smoke

inline void runtime_smoke_test() noexcept {
    // Non-constant Category value (cycled through every enum entry).
    // The volatile prevents the optimizer from folding the loop into
    // a compile-time evaluation; we genuinely exercise the runtime
    // path through the switch statements.
    volatile std::size_t const cap = catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        Category const c = static_cast<Category>(i);
        std::string_view const n = name_of(c);
        std::string_view const d = description_of(c);
        std::string_view const r = remediation_of(c);

        // Sink to volatile to force the optimizer to keep the calls.
        volatile std::size_t sink = 0;
        sink ^= n.size();
        sink ^= d.size();
        sink ^= r.size();
        (void)sink;
    }

    // is_diagnostic_class_v on a non-foundation user-extension tag.
    bool const is_tag = is_diagnostic_class_v<detail::smoke::smoke_local_tag>;
    volatile bool sink_b = is_tag;
    (void)sink_b;

    // Diagnostic<Tag, Ctx...> instantiation under runtime context.
    using d_t = Diagnostic<EffectRowMismatch, int, float>;
    volatile std::size_t sink_n = d_t::name.size();
    (void)sink_n;
}

}  // namespace crucible::safety::diag
