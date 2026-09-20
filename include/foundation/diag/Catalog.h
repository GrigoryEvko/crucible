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

// Error and Fatal behave identically at compile time. They differ in
// what tooling is meant to do: Fatal marks a violation whose silent
// passage is dangerous rather than merely wrong, and offers no override.
//
// The severity grades a tag, so it is declared beside the tags rather
// than beside the reader in Insights.h, which is where it used to sit.
// The spelling foundation::diag::Severity is unchanged, and Insights.h
// includes this header, so no consumer sees a difference.
enum class Severity : std::uint8_t {
    Hint = 0,  // the code is correct and could be better shaped
    Warning = 1,  // the code compiles and carries a runtime risk
    Error = 2,  // the assertion or the implementation has to change
    Fatal = 3,  // as Error, and no override path
};

// Authoring rule for every tag below: the description states what the bug
// class is, the remediation states how to fix an instance of it. Both read as
// standalone sentences, because a build log shows them without this file.
//
// A tag also carries the prose a structured rejection prints: a
// severity, why the rule exists, how the violation usually arrives, and
// the compliant line beside the violating one. These sat in
// foundation/diag/Insights.h as one explicit insight_provider
// specialization per tag, which restated the tag name a second time.
// The tag carries them itself now, and Insights.h reads the members off
// whichever tag it is asked about.
//
// When writing them: the why states the architectural constraint and
// answers what actually breaks if it is ignored. The symptom describes
// how the violation typically arrives, so a reader can match it against
// the change they just made. Both examples are one line of real C++,
// and they differ only in the thing the diagnostic is about. A tag that
// declares none of them still compiles, and prints the shorter block.
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "Met(X) effect rows (Tang-Lindley POPL 2026) encode capability "
        "propagation in the type system.  A function declared with row "
        "R promises to use AT MOST those effects; a caller with row R' "
        "MUST contain R' to invoke it (Subrow<R, R'>).  Without this "
        "discipline, hot-path code accidentally inherits Bg-thread "
        "capabilities (alloc / IO / block) from a transitively-called "
        "subroutine, blowing latency budgets and breaking the recording "
        "trace.  The row IS the function's contract.";
    static constexpr std::string_view symptom_pattern = "Surfaces after a refactor that lifts a previously-bg-thread "
                                                        "helper into a Computation<Row<>, T> (pure) signature without "
                                                        "removing the helper's printf / arena alloc.  The transitive "
                                                        "Subrow check fires at the lowest call site that actually "
                                                        "needs the bg-row, NOT at the helper itself.";
    static constexpr std::string_view correct_example =
        "Computation<Row<Effect::Bg>, T> bg_helper(); // honest about Bg";
    static constexpr std::string_view violating_example = "Computation<Row<>, T> bg_helper(); // hides Bg → fires here";
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

    static constexpr Severity severity = Severity::Warning;
    static constexpr std::string_view why_this_matters = "The FOUND-D dispatcher (28_04 §6) reads function signatures "
                                                         "and routes them to one of the seven canonical lowerings "
                                                         "(UnaryTransform, BinaryTransform, Reduction, Producer/"
                                                         "Consumer endpoint, SwmrWriter/Reader, PipelineStage).  "
                                                         "Functions whose parameter types don't match any canonical "
                                                         "shape can't be auto-dispatched — the user must either "
                                                         "reshape the signature or manually call the underlying "
                                                         "primitives.  Severity::Warning rather than Error because "
                                                         "the manual orchestration path is documented and supported; "
                                                         "auto-dispatch is the default but not the only option.";
    static constexpr std::string_view symptom_pattern = "Surfaces when a user writes a free function the natural way "
                                                        "(e.g., taking raw pointers + sizes) and tries to dispatch it "
                                                        "via dispatch().  The dispatcher rejects because "
                                                        "raw pointers aren't OwnedRegion<T, Tag>; user reshapes to "
                                                        "use the wrapper or calls parallel_for_views directly.";
    static constexpr std::string_view correct_example =
        "void f(OwnedRegion<float, Tag>&&); // canonical UnaryTransform";
    static constexpr std::string_view violating_example = "void f(float*, size_t); // raw ptr; not a canonical shape";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "GradedWrapper is the structural concept (algebra/GradedTrait.h) "
        "every safety wrapper must satisfy: graded_type points at a "
        "real Graded<M, L, T>, the wrapper's modality matches the "
        "substrate's, value_type and lattice_type are consistent, "
        "diagnostic forwarders return the substrate's strings.  These "
        "five 'cheats' are the audit cluster from Round-4; the "
        "concept's job is to lock them in.  Violating the concept "
        "means downstream FOUND-D dispatcher reflection produces "
        "wrong dispatch decisions (e.g., a 'wrapper' with mismatched "
        "modality routes to the wrong lowering target).";
    static constexpr std::string_view symptom_pattern = "Surfaces when authoring a NEW wrapper (e.g., for FOUND-G "
                                                        "wrappers G01-G80) without following the canonical Stale.h "
                                                        "template.  Common cause: copying part of an existing wrapper "
                                                        "and forgetting to update graded_type, OR adding a "
                                                        "value_type_decoupled opt-in without justification, OR "
                                                        "publishing a custom string-returning value_type_name() that "
                                                        "doesn't forward to the substrate.";
    static constexpr std::string_view correct_example =
        "using graded_type = Graded<Absolute, MyLattice::At<X>, T>; // matches";
    static constexpr std::string_view violating_example =
        "using graded_type = void; // not a Graded<...> — concept rejects";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "Quantitative type theory (Atkey FLoC 2018) and Concurrent "
                                                         "Separation Logic (O'Hearn 2007) both require that linear "
                                                         "values are consumed EXACTLY ONCE.  Crucible's Linear<T> / "
                                                         "Permission<Tag> / OwnedRegion<T, Tag> encode this in the "
                                                         "type system: copy is deleted, move transfers ownership, "
                                                         "double-consume is a compile error.  Without linearity, two "
                                                         "threads can simultaneously hold the 'exclusive' permission "
                                                         "for a region (data race), or a Linear<File> can be closed "
                                                         "twice (heap corruption).  The discipline is what makes the "
                                                         "BorrowSafe + ThreadSafe + LeakSafe axioms (CLAUDE.md §II "
                                                         "5/6/7) actually hold at the type level.";
    static constexpr std::string_view symptom_pattern = "Surfaces when capturing a Linear<T> by value into a lambda "
                                                        "that's then std::moved into TWO different jthread spawns, OR "
                                                        "when a Permission<Tag> is stored in a struct field that's "
                                                        "subsequently copied for parallel dispatch, OR when a refactor "
                                                        "removes std::move and the compiler's implicit copy attempt "
                                                        "fires the deleted-copy assertion.";
    static constexpr std::string_view correct_example = "consumer(std::move(linear_val)); // single consumption, OK";
    static constexpr std::string_view violating_example =
        "consumer1(linear_val); consumer2(linear_val); // double-use; rejected";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "Refined<P, T> attaches a compile-time-named predicate P to "
                                                         "values of type T (safety/Refined.h).  The constructor's "
                                                         "pre() clause checks P(v) at runtime under contract semantic="
                                                         "enforce (debug builds, CI) and treats it as [[assume(P(v))]] "
                                                         "under semantic=ignore (release builds).  The downstream code "
                                                         "is OPTIMIZED on the assumption that P holds; violating the "
                                                         "predicate at the construction site causes the optimizer to "
                                                         "make incorrect downstream decisions (UB).  The discipline is "
                                                         "to validate at the construction site.";
    static constexpr std::string_view symptom_pattern = "Surfaces when boundary-validating user input incorrectly: "
                                                        "e.g., using Refined<positive>(value) on a value that could "
                                                        "be zero or negative.  Or when a refactor changes the source "
                                                        "of a Refined<bounded_above<8>>(ndim) where ndim came from a "
                                                        "now-uncapped tensor metadata field.";
    static constexpr std::string_view correct_example = "if (n > 0) Refined<positive, int>(n); // validated first";
    static constexpr std::string_view violating_example =
        "Refined<positive, int>(maybe_zero); // pre() fails; UB on optimize";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "The hot-path latency floor is the MESI cache-line transfer "
                                                         "cost — ~10-40 ns intra-socket, ~30-100 ns cross-socket "
                                                         "(CLAUDE.md §IX latency hierarchy).  Admitting a Warm callee "
                                                         "(allocation, ~50-200 ns) or Cold callee (block / IO, "
                                                         "~10-100 us) on a 5 ns recording path is a 10x to 10000x "
                                                         "slowdown.  HotPath<Hot, T> is the type system's promise that "
                                                         "the recorded code path stays within the latency floor; the "
                                                         "rejection here proves a regression would have shipped.";
    static constexpr std::string_view symptom_pattern = "Almost always surfaces after adding logging, asserting, or "
                                                        "instrumentation to a previously-clean hot-path TU.  The new "
                                                        "fprintf / std::cout / std::format call is Cold-tier; the "
                                                        "containing function's HotPath<Hot> declaration rejects the "
                                                        "transitive call at compile time, before the regression can "
                                                        "ship.  Less commonly: an inline-friendly helper that grew a "
                                                        "vector<T>::push_back over time.";
    static constexpr std::string_view correct_example = "static std::atomic<uint64_t> debug_counter; // Hot-path-safe";
    static constexpr std::string_view violating_example =
        "fprintf(stderr, \"debug %d\\n\", x); // Cold; rejected by Hot caller";
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

    static constexpr Severity severity = Severity::Fatal;
    static constexpr std::string_view why_this_matters = "DetSafe is the 8TH AXIOM (CLAUDE.md §II.8): same inputs → "
                                                         "same outputs, bit-identical under BITEXACT replay.  It is "
                                                         "the ONE axiom historically un-fenced — caught only by the "
                                                         "bit_exact_replay_invariant CI test ~12 hours after the "
                                                         "commit lands.  This rejection at compile time is what fences "
                                                         "the axiom.  A wall-clock read or /dev/urandom call recorded "
                                                         "into the replay log makes EVERY subsequent replay produce "
                                                         "different outputs; cross-vendor numerics CI then rejects "
                                                         "Mimic backends that 'fail' to match the corrupted oracle.  "
                                                         "Severity::Fatal because silent breakage cascades into hours "
                                                         "of debugging lost replay determinism.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces when adding 'just one little timestamp' to a Cipher "
        "event-recording site for debugging.  Or when a refactor "
        "replaces seeded Philox with std::random_device for 'better "
        "randomness'.  Or when the runtime metric collector's wall-clock "
        "sample accidentally crosses into a record_event path.  All "
        "three are real production patterns the framework now catches.";
    static constexpr std::string_view correct_example =
        "DetSafe<Pure, uint64_t> seed = philox(counter, key); // deterministic";
    static constexpr std::string_view violating_example =
        "auto seed = std::chrono::steady_clock::now(); // wall clock; rejected";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "Recipe tiers (FORGE.md §20) discipline the cross-vendor "
        "numerics CI: BITEXACT_STRICT recipes produce byte-identical "
        "output across all backends; BITEXACT_TC tolerate ≤1 ULP "
        "tensor-core deviation; ORDERED enforce a per-recipe tolerance; "
        "UNORDERED admit reduction-order changes.  A caller pinned at "
        "BITEXACT_STRICT calling an UNORDERED kernel silently propagates "
        "non-determinism through the model — checkpoint loads from a "
        "BITEXACT chain produce divergent outputs after the UNORDERED "
        "kernel runs.  The recipe-tier discipline is what makes "
        "federation work.";
    static constexpr std::string_view symptom_pattern = "Often surfaces after a Mimic backend ships a fast-path "
                                                        "ALLREDUCE (UNORDERED tier) and a downstream training step "
                                                        "expecting BITEXACT_TC tries to consume its output.  Or after "
                                                        "a Forge Phase E recipe-select picker switches to a tighter "
                                                        "BITEXACT_STRICT recipe and a previously-OK ORDERED kernel is "
                                                        "now rejected.";
    static constexpr std::string_view correct_example =
        "select_recipe<Tolerance::BITEXACT_TC>(KernelKind::GEMM_MM, fleet);";
    static constexpr std::string_view violating_example =
        "auto k = unordered_allreduce(x); // UNORDERED; rejected by BITEXACT";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "memory_order_seq_cst emits MFENCE on x86 (~30 ns serializing "
                                                         "the store buffer) and DMB ISH on ARM (full system barrier).  "
                                                         "Acquire/release semantics suffice for every SPSC ring, every "
                                                         "snapshot, every lock-free pattern Crucible needs (CLAUDE.md "
                                                         "§IX).  seq_cst on the hot path is a 30-100x latency hit and "
                                                         "is almost always wrong — it usually indicates the engineer "
                                                         "didn't think about the actual ordering requirement and "
                                                         "reached for the strongest available primitive 'just to be "
                                                         "safe'.  The discipline is to think about acquire/release.";
    static constexpr std::string_view symptom_pattern = "Surfaces in code that was written outside the project's "
                                                        "discipline (e.g., copied from a stack-overflow lock-free "
                                                        "snippet) or refactored from a mutex-protected region without "
                                                        "the author auditing the actual ordering need.  Less commonly: "
                                                        "true total-order requirement (rare; if you genuinely need "
                                                        "this, the ownership boundary is wrong — escalate to design "
                                                        "review).";
    static constexpr std::string_view correct_example = "x.store(v, std::memory_order_release); // publish";
    static constexpr std::string_view violating_example =
        "x.store(v, std::memory_order_seq_cst); // banned; emit MFENCE";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "Hot-path code MUST NOT allocate from the heap (CLAUDE.md HS10): "
        "malloc round-trip is ~50-200 ns, unpredictable under "
        "contention, and breaks the per-iteration latency budget.  "
        "Arena bump allocation is ~2 ns and lock-free; PoolAllocator "
        "is ~5 ns and bounded.  The AllocClass wrapper makes the "
        "discipline visible in function signatures so a refactor that "
        "introduces std::vector::push_back into the hot path is "
        "rejected at the call site before it ships.";
    static constexpr std::string_view symptom_pattern = "Almost always surfaces after a refactor that 'just adds a "
                                                        "vector<T>' to collect intermediate results, OR after using "
                                                        "std::string concatenation in a logging path, OR after "
                                                        "refactoring an arena-backed buffer to std::array<T, N> via a "
                                                        "helper that returns std::vector instead.";
    static constexpr std::string_view correct_example = "auto* buf = arena.alloc_array<float>(n); // Arena, ~2 ns";
    static constexpr std::string_view violating_example =
        "auto buf = std::vector<float>(n); // Heap; rejected on hot path";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "Mimic per-vendor backends (mimic::nv / am / tpu / trn / cpu) "
                                                         "are NOT interchangeable — each emits an instruction stream "
                                                         "valid only for its target ISA.  A Vendor-tagged kernel "
                                                         "(MIMIC.md §22) must be routed to the matching backend; "
                                                         "feeding an `Vendor::AMD`-rowed KernelNode<...> to "
                                                         "mimic::nv::compile_kernel produces SASS that doesn't match "
                                                         "the AMD GPU's instruction set, then surfaces as a runtime "
                                                         "kernel-launch failure or silent miscompile.  The compile-"
                                                         "time fence catches this BEFORE the silicon does.";
    static constexpr std::string_view symptom_pattern = "Surfaces when a kernel originally compiled for one vendor "
                                                        "is reused on another via a copy-paste of dispatch wiring "
                                                        "without updating the Vendor tag.  Less commonly: a fleet "
                                                        "join brings in peers of a different vendor and reshard "
                                                        "neglects to re-emit per-vendor.  The diagnostic fires at "
                                                        "the first mimic::*::compile_kernel call site whose row "
                                                        "doesn't include the backend's vendor tag.";
    static constexpr std::string_view correct_example = "mimic::nv::compile_kernel(node_with_Vendor_NV_row, ...);";
    static constexpr std::string_view violating_example =
        "mimic::nv::compile_kernel(node_with_Vendor_AMD_row, ...);  // WRONG";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "BSYZ22 crash-stop session typing (sessions/SessionCrash.h) "
                                                         "classifies callees by failure mode: NoThrow (cannot fail), "
                                                         "ErrorReturn (returns std::expected), Throw (exceptions — "
                                                         "BANNED in Crucible), Abort (calls std::abort).  A NoThrow-"
                                                         "constrained context that invokes a Throw or Abort callee "
                                                         "loses its noexcept guarantee mid-protocol; the type system's "
                                                         "crash-aware composition rules silently break.  The fence "
                                                         "preserves the protocol-level reasoning for rollback / "
                                                         "recovery / replay.";
    static constexpr std::string_view symptom_pattern = "Surfaces after a refactor that replaces an Error-returning "
                                                        "helper with one that aborts on failure (e.g., switching from "
                                                        "std::expected to a contract_assert).  The new helper's "
                                                        "Crash<Abort> grade no longer satisfies the caller's "
                                                        "Crash<NoThrow> requirement; the protocol step that was a "
                                                        "cleanly-recoverable boundary is now a process kill.  Caller "
                                                        "must either re-introduce error-returning, or relax the "
                                                        "Crash<...> bound (and update downstream rollback logic).";
    static constexpr std::string_view correct_example = "Crash<Crash::ErrorReturn, T> safe_op();  // recoverable";
    static constexpr std::string_view violating_example =
        "Crash<Crash::Abort, T> dangerous_op();  // kills NoThrow caller";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "Distributed-state consistency tiers (algebra/lattices/"
                                                         "ConsistencyLattice.h: EVENTUAL ⊑ READ_YOUR_WRITES ⊑ "
                                                         "CAUSAL_PREFIX ⊑ BOUNDED_STALENESS ⊑ STRONG) are NOT "
                                                         "interchangeable.  TP/PP axes in 5D parallelism (CRUCIBLE.md "
                                                         "§L13) require STRONG (every replica sees identical state at "
                                                         "every step); DP axes tolerate BOUNDED_STALENESS (DiLoCo "
                                                         "outer-step model).  Configuring a TP axis as EVENTUAL "
                                                         "produces silently-wrong gradients — the symptom is loss "
                                                         "divergence after some hours of training, attribution is "
                                                         "near-impossible without compile-time fencing.";
    static constexpr std::string_view symptom_pattern = "Surfaces at fleet-join (Canopy reshard) when a peer's "
                                                        "declared consistency tier doesn't satisfy the partition's "
                                                        "axis requirement.  Or at BatchPolicy<Axis, Level> "
                                                        "construction in Forge Phase K when the level lattice fails "
                                                        "the axis's minimum.  Diagnostic names BOTH the axis and the "
                                                        "two consistency tiers (caller's vs callee's required).";
    static constexpr std::string_view correct_example = "BatchPolicy<TpAxis, Consistency::Strong>(...);";
    static constexpr std::string_view violating_example =
        "BatchPolicy<TpAxis, Consistency::Eventual>(...);  // BREAKS TP";
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

    static constexpr Severity severity = Severity::Fatal;
    static constexpr std::string_view why_this_matters =
        "OpaqueLifetime<L, T> (algebra/lattices/LifetimeLattice.h) "
        "tags a value's persistence scope: PER_REQUEST ⊏ "
        "PER_PROGRAM ⊏ PER_FLEET.  A PER_REQUEST value contains "
        "request-local data (PII, session IDs, transient credentials); "
        "promoting it to PER_FLEET via Cipher cold tier OR Canopy "
        "Raft replication LEAKS the data across requests / tenants / "
        "machines.  In multi-tenant deployments this is a security "
        "incident requiring customer notification and (depending on "
        "jurisdiction) regulatory disclosure.  Severity::Fatal "
        "because the consequence class is data exfiltration, not "
        "performance.";
    static constexpr std::string_view symptom_pattern = "Surfaces when a refactor consolidates a per-request handler "
                                                        "with a per-fleet broadcast path and forgets to scrub the "
                                                        "request-local fields before publication.  Or when adding "
                                                        "telemetry to an inferlet (per-request user state) without "
                                                        "marking the metric as PER_FLEET-aggregated.  The diagnostic "
                                                        "names the source value's lifetime AND the destination's "
                                                        "required lifetime; remediation is almost always 'redact "
                                                        "first, then promote'.";
    static constexpr std::string_view correct_example =
        "publish_to_fleet(OpaqueLifetime<PER_FLEET, Aggregate>{value});";
    static constexpr std::string_view violating_example =
        "publish_to_fleet(OpaqueLifetime<PER_REQUEST, RawTrace>{value});";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "Wait<Strategy, T> (28_04 §4.3.3) classifies wait costs: "
                                                         "SpinPause (≤40 ns intra-socket via MESI) ⊏ BoundedSpin "
                                                         "(N spins then back off) ⊏ UmwaitC01 (C0.1/C0.2 sleep) ⊏ "
                                                         "AcquireWait (atomic::wait/notify, futex-backed, ~1-5 us) "
                                                         "⊏ Park (jthread parking) ⊏ Block (mutex/condvar, ms).  "
                                                         "Hot-path callers admit only SpinPause; mixing in a Park "
                                                         "or Block via a transitively-called helper turns a 5 ns "
                                                         "recording into a 1-5 us syscall — a 1000× slowdown.  The "
                                                         "fence catches the regression at the call site, before "
                                                         "performance triage hours later.";
    static constexpr std::string_view symptom_pattern = "Surfaces after adding logging or metrics to a hot-path TU "
                                                        "where the new logger uses std::cout (line-buffered → "
                                                        "fwrite_lock → futex → Block).  Or replacing a custom "
                                                        "spin-wait with std::condition_variable for 'simplicity'.  "
                                                        "The diagnostic names the offending callee's Wait grade "
                                                        "(usually Park or Block) and the caller's required grade "
                                                        "(usually SpinPause).";
    static constexpr std::string_view correct_example = "while (!flag.load(acquire)) Wait<SpinPause>{}.do_pause();";
    static constexpr std::string_view violating_example =
        "flag.wait(false);  // Wait<AcquireWait> in a SpinPause caller";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "Progress<Class, T> (28_04 §4.3.5) classifies termination "
                                                         "guarantees: MayDiverge ⊏ Terminating ⊏ Productive ⊏ "
                                                         "Bounded.  Forge phases are declared Bounded (FORGE.md §5 "
                                                         "hard wall-clock budgets); embedding a MayDiverge helper "
                                                         "(e.g., a `while (!converged)` loop without an iteration "
                                                         "cap) breaks the phase's wall-clock guarantee.  Lean proofs "
                                                         "demand Terminating; embedding a MayDiverge fact in a "
                                                         "Lean-extractable kernel breaks proof discharge.  The fence "
                                                         "preserves both the operational (wall-clock) and logical "
                                                         "(proof) termination contracts.";
    static constexpr std::string_view symptom_pattern = "Surfaces when a refactor adds an unbounded fixed-point "
                                                        "iteration to a Bounded-phase helper without adding a "
                                                        "cap_iters parameter.  Or when an inferlet (MayDiverge by "
                                                        "design — escape hatch) is composed into a Terminating "
                                                        "outer wrapper.  Diagnostic names the offending helper's "
                                                        "Progress class and the outer's required class.";
    static constexpr std::string_view correct_example = "Progress<Bounded, T> phase_step(int max_iters) noexcept;";
    static constexpr std::string_view violating_example =
        "Progress<MayDiverge, T> unbounded_loop();  // breaks Bounded outer";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "CipherTier<Tier, T> (CRUCIBLE.md §L14: Hot ⊏ Warm ⊏ Cold) "
        "carries a value's persistence tier.  Hot tier is other-Relay "
        "RAM (RAID-style replication; ns-scale recovery on single-node "
        "failure), Warm is local NVMe (1/N FSDP shard; second-scale "
        "recovery from reboot), Cold is durable storage S3/GCS "
        "(minute-scale recovery from total cluster failure).  "
        "Mis-tiered persistence has hard consequences: a Hot value "
        "demoted to Cold loses ns-recovery; a Cold value promoted "
        "to Hot occupies precious other-Relay RAM that should hold "
        "active state.  The fence preserves the recovery-time SLA.";
    static constexpr std::string_view symptom_pattern = "Surfaces when Cipher::publish_warm is called on a value "
                                                        "whose CipherTier grade is Cold (e.g., a long-archive log "
                                                        "entry mistakenly routed through the warm tier).  Or when a "
                                                        "tier-promotion path (Hot → Warm at FSDP shard boundary) "
                                                        "loses the explicit tier marker via type erasure.  Diagnostic "
                                                        "names the source tier AND the destination's required tier.";
    static constexpr std::string_view correct_example =
        "Cipher::publish_warm(CipherTier<CipherTier::Warm, Shard>{value});";
    static constexpr std::string_view violating_example =
        "Cipher::publish_warm(CipherTier<CipherTier::Cold, Archive>{value});";
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

    static constexpr Severity severity = Severity::Warning;
    static constexpr std::string_view why_this_matters = "ResidencyHeat<Tier, T> (28_04 §4.3.8) is the storage-heat "
                                                         "lattice for any subsystem with hot/warm/cold residency: "
                                                         "KernelCache L1/L2/L3, runtime metrics, MAP-Elites archives.  "
                                                         "Distinct from CipherTier (which is Cipher-specific); "
                                                         "ResidencyHeat is the generic heat axis.  A Hot value "
                                                         "demoted to Cold loses LRU residency guarantees and slows "
                                                         "by orders of magnitude; a Cold value promoted to Hot "
                                                         "evicts other Hot entries.  Severity::Warning rather than "
                                                         "Error because mis-tiering degrades performance but doesn't "
                                                         "corrupt state — the framework can still serve correctly, "
                                                         "just slower.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces when a KernelCache::lookup at L1 is fed a "
        "ResidencyHeat<Cold>-tagged key (the cache will never find "
        "it because L1 only holds Hot entries).  Or when runtime observer's "
        "drift attribution misclassifies a Cold-tier metric drift "
        "as a Hot-path regression.  Diagnostic names the value's "
        "current residency and the operation's required residency.";
    static constexpr std::string_view correct_example = "KernelCache::lookup_l1(ResidencyHeat<Hot, KeyId>{kid});";
    static constexpr std::string_view violating_example =
        "KernelCache::lookup_l1(ResidencyHeat<Cold, KeyId>{kid});  // miss";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "EpochVersioned<Epoch, Generation, T> (28_04 §4.4.2) carries "
                                                         "Canopy's Raft-committed fleet epoch alongside a per-Relay "
                                                         "generation counter.  After a membership change (peer join "
                                                         "OR peer death), the fleet advances epoch; values constructed "
                                                         "in the OLD epoch are stale relative to the NEW epoch's "
                                                         "topology.  Operating on a stale-epoch value during a "
                                                         "collective produces wrong gradients (the partition layout "
                                                         "the value was computed against no longer matches the live "
                                                         "fleet).  The fence prevents the silent staleness — caller "
                                                         "must rebuild the value at the new epoch via Canopy::reshard.";
    static constexpr std::string_view symptom_pattern = "Surfaces immediately after Canopy::reshard fires (peer "
                                                        "join or peer-down detected) — any pending operation whose "
                                                        "operand was minted in the previous epoch fails the epoch "
                                                        "check.  Less commonly: a long-running Raft log replay "
                                                        "interleaves an old-epoch value with new-epoch metadata.  "
                                                        "Diagnostic names BOTH epochs and points at "
                                                        "Canopy::reshard for the rebuild path.";
    static constexpr std::string_view correct_example = "auto fresh = Canopy::reshard(stale_value).at_current_epoch();";
    static constexpr std::string_view violating_example =
        "do_collective(stale_value);  // epoch=N-1 but fleet at epoch=N";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "Budgeted<{BitsBudget, PeakBytes}, T> (28_04 §4.4.1, "
                                                         "Resource-bounded type theory arXiv:2512.06952) carries a "
                                                         "compile-time bound on the value's resource footprint: "
                                                         "either bits transferred (network) or peak bytes resident "
                                                         "(memory).  A composition that sums two Budgeted values into "
                                                         "a budget that exceeds the wrapper's declared cap fails the "
                                                         "ProductLattice's join check.  The fence prevents accidental "
                                                         "OOM (memory budget) or bandwidth saturation (bits budget) "
                                                         "in production deployments where the budget is the contract.";
    static constexpr std::string_view symptom_pattern = "Surfaces when a precision-budget calibrator (28_04 §4.4.1) "
                                                        "composes two BitsBudget<N>-tagged candidates whose sum "
                                                        "exceeds the declared per-step cap.  Less commonly: a "
                                                        "Canopy collective whose per-peer PeakBytes sum exceeds the "
                                                        "configured fleet memory ceiling.  The diagnostic names "
                                                        "BOTH operands' budgets and the cap that was violated.";
    static constexpr std::string_view correct_example =
        "compose(Budgeted<{B<512>, P<1MB>}, T>, Budgeted<{B<256>, P<512KB>}, U>);";
    static constexpr std::string_view violating_example =
        "compose(Budgeted<{B<800>, P<2MB>}, T>, Budgeted<{B<500>, P<1MB>}, U>);"
        "  // sum=1300 > cap=1024 OR sum=3MB > cap=2MB";
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

    static constexpr Severity severity = Severity::Warning;
    static constexpr std::string_view why_this_matters = "NumaPlacement<Node, Affinity, T> (28_04 §4.4.3) carries "
                                                         "a value's NUMA locality: which node owns its memory + the "
                                                         "thread affinity it expects.  A Node-3-tagged region "
                                                         "operated on by a Node-0-affined thread incurs cross-socket "
                                                         "memory latency on every access (~3-4× slower than local).  "
                                                         "Severity::Warning rather than Error because correctness is "
                                                         "preserved — only performance degrades — but on production "
                                                         "fleets this is a measurable revenue-cost regression.  The "
                                                         "fence catches the mis-placement BEFORE bench triage.";
    static constexpr std::string_view symptom_pattern = "Surfaces when AdaptiveScheduler dispatches a NumaLocal-"
                                                        "tagged work item to a NumaSpread thread pool (or vice "
                                                        "versa).  Or when a NumaPlacement<Node-N> region is fed to "
                                                        "a body whose own NUMA preference targets a different node.  "
                                                        "Diagnostic names the value's node + affinity AND the "
                                                        "consuming operation's expected combination.";
    static constexpr std::string_view correct_example =
        "scheduler.dispatch_local(NumaPlacement<Node{0}, Local>{region});";
    static constexpr std::string_view violating_example =
        "scheduler.dispatch_local(NumaPlacement<Node{3}, Spread>{region});";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "RecipeSpec<Tier, Family, T> (28_04 §4.4.4) carries a "
                                                         "kernel's numerical recipe along TWO axes: ToleranceLattice "
                                                         "tier (BITEXACT_STRICT ⊐ BITEXACT_TC ⊐ ULP_FP64 ⊐ ... ⊐ "
                                                         "RELAXED) AND RecipeFamily (PAIRWISE / LINEAR / KAHAN / "
                                                         "BLOCK_STABLE).  Mismatched tiers produce nonequivalent "
                                                         "outputs (wrong by ULP); mismatched families break "
                                                         "associativity assumptions (a PAIRWISE-callable site fed "
                                                         "a LINEAR result has different reduction order, breaking "
                                                         "BITEXACT replay).  The fence ENFORCES the cross-vendor "
                                                         "numerics CI's (MIMIC.md §41) per-recipe equivalence "
                                                         "contract at the call site rather than in CI 12 hours later.";
    static constexpr std::string_view symptom_pattern = "Surfaces when Forge Phase E.RecipeSelect picks a tier "
                                                        "different from the caller's declared bound, OR when a "
                                                        "fleet-intersection narrows the available recipes such "
                                                        "that no candidate satisfies the row's RecipeSpec "
                                                        "constraint.  Diagnostic names BOTH axes mismatched; "
                                                        "common remediation is a recipe-family change OR a "
                                                        "tier relaxation (with documented impact on bit-exactness).";
    static constexpr std::string_view correct_example = "compile(RecipeSpec<BITEXACT_TC, PAIRWISE, Kernel>{node});";
    static constexpr std::string_view violating_example =
        "compile(RecipeSpec<RELAXED, KAHAN, Kernel>{node});  // wrong axes";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "The F* effect lattice (FxAliases.h, Tang-Lindley POPL 2026) "
        "anchors at PURE — the empty row — as its bottom.  Pure / Tot / "
        "Ghost functions promise NO observable effects: no Alloc, no IO, "
        "no Block, no Bg / Init / Test context tags.  Admitting any "
        "Effect atom turns a pure projection into a stateful operation "
        "that downstream callers (KernelCache content-addressing, "
        "deterministic replay, MAP-Elites cost-model fingerprinting) "
        "cannot consume — the cache key becomes invalidated by the "
        "function's hidden side effects, breaking the bit_exact_replay_"
        "invariant CI test in subtle ways.  See CLAUDE.md §L2 KernelCache "
        "content-addressing and §III.8 DetSafe axiom.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces after adding 'just one debug line' (printf, fprintf, "
        "std::cout) to a function declared Pure<T>, Tot<E, T>, or "
        "Ghost<R, T>.  Or after a refactor that adds arena allocation "
        "to a previously-pointer-free pure helper.  The diagnostic "
        "names the function's required-empty-row contract and the "
        "atom that violates it (typically IO or Alloc).  The remediation "
        "is to lift the function up the F* lattice — Pure → Div → ST → "
        "All — to the strictest predicate the body actually needs.";
    static constexpr std::string_view correct_example =
        "Pure<uint64_t> compute_hash(Tagged<uint64_t, kind> k);  // empty row";
    static constexpr std::string_view violating_example =
        "Pure<uint64_t> compute_hash(...) { fprintf(stderr, ...); ... }  // IO";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "The F* DIV effect class (FxAliases.h Div<T>) extends PURE only "
        "with potential-non-termination (Block) — NOT with state mutation "
        "or external observable effects.  This narrow widening is "
        "load-bearing for fixed-point iterators, convergence loops, and "
        "LoopNode bodies that must terminate eventually but cannot be "
        "proved to do so structurally.  Admitting Alloc or IO turns a "
        "divergent-but-pure operation into a stateful one; in F* such a "
        "function must lift to ST or higher.  Without the fence, a "
        "Cipher event-recorder that quietly grows to call malloc per "
        "iteration breaks deterministic replay across heap-layout "
        "variations.  See CLAUDE.md §L7 LoopNode semantics.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces in fixed-point or DEQ-style convergence loops when "
        "the body grows a scratchpad allocation across iterations.  Or "
        "when a 'log progress' fprintf is added inside a Div<T>-declared "
        "loop body.  The diagnostic names the function's DivRow = "
        "Row<Block> requirement and the offending atom (Alloc or IO).  "
        "Remediation: lift the declaration to IsST (admits Block + "
        "Alloc + IO) or eliminate the allocation by reusing a "
        "caller-provided arena via a Pure helper signature.";
    static constexpr std::string_view correct_example =
        "Div<int> fixed_point(int x);  // Block only — terminates eventually";
    static constexpr std::string_view violating_example =
        "Div<int> fixed_point(int x) { void* p = std::malloc(64); ... }  // Alloc";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "The F* ST effect class (FxAliases.h ST<T>) extends DIV with "
        "state effects (Alloc + IO) — admitting malloc, file handles, "
        "kernel ioctls — but NOT with context capabilities (Bg / Init / "
        "Test).  Context tags are NOT state effects; they encode WHERE "
        "the function runs (bg-thread-only, startup-only, test-harness-"
        "only).  Mixing a Bg cap-tag parameter into an IsST signature "
        "weakens the F* substitution principle: every IsPure caller can "
        "invoke IsST, but the Bg cap-tag would silently bypass the "
        "permission discipline that gates background-thread admission.  "
        "Use IsAll for functions that genuinely need context tags.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces when a function is being lifted out of pure-functional "
        "code into stateful code and the author reaches for IsST<T> as "
        "'the catch-all', then realizes the function also takes an "
        "effects::Bg / Init / Test parameter for context-bound dispatch.  "
        "The diagnostic names the function's STRow = Row<Block, Alloc, "
        "IO> requirement and the context-tag atom that violates it.  "
        "Remediation: lift to IsAll (admits AllRow including context "
        "atoms), OR remove the cap-tag parameter if the function is "
        "genuinely state-only and the context is caller-provided.";
    static constexpr std::string_view correct_example =
        "ST<int> stateful_compute(int x);  // Alloc + IO, no context tag";
    static constexpr std::string_view violating_example =
        "ST<int> stateful_compute(eff::Bg bg, int x);  // Bg cap → rejected";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "The witness lattice (safety/witness/Witness.h, FIXY-G9) encodes "
        "proof-relevance per axis: Asserted (developer claim) ⊑ Tested "
        "(unit-test-witnessed) ⊑ CrossValidated (CI-witnessed across "
        "vendors / configurations) ⊑ FormallyVerified (machine-checked "
        "by the verify preset's small SMT solver).  Production consumers "
        "encode their evidence floor: Cipher's hot-tier promotion will "
        "not admit a binding whose KernelCache content-hash equivalence "
        "claim is merely Asserted — replay determinism demands at least "
        "CrossValidated, witnessed by the cross-vendor numerics CI "
        "(MIMIC.md §41).  Without the fence, untested kernels would "
        "graduate to hot-tier and silently corrupt replay logs.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces during production refactors that try to promote a "
        "developer-only sketch into a Cipher-cached / federation-shared "
        "binding without first adding a test that registers the proof.  "
        "The diagnostic names the consumer's floor (e.g., 'requires "
        "Tested<test_id>') and the binding's actual witness (Asserted "
        "by default).  Remediation: either upgrade the grant to a "
        "*_e<W> evidenced variant pointing at a real test_id, or "
        "lower the consumer's witness-floor demand if Asserted is "
        "actually acceptable for this consumption.";
    static constexpr std::string_view correct_example =
        "auto g = grant::reentrant_e<Tested<test_kernel_xxx>>();  // tier ≥ Tested";
    static constexpr std::string_view violating_example =
        "auto g = grant::reentrant();  // Asserted only — rejected by Tested floor";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "The modality taxonomy (fixy/Modality.h, FIXY-G10) classifies "
        "grants by their categorical role: Frame (invariant of the "
        "value), Declares (witness-producing — the binding establishes "
        "the property), Requires (caller-side refinement the binding "
        "demands), Linear (one-shot consume-and-produce resource "
        "transfer), Quotient (equivalence-class membership).  Two "
        "grants on the same axis cannot mix Frame with Declares — the "
        "invariant claim contradicts the witness-producing claim, and "
        "neither downstream consumer (audit logger, federation cache "
        "router) can disambiguate which is authoritative.  Without "
        "the fence, conflicting claims would silently mis-route "
        "Cipher entries across federation peers.";
    static constexpr std::string_view symptom_pattern = "Surfaces when an author adds a grant pack mixing 'this is "
                                                        "invariant' (Frame) with 'this binding establishes the "
                                                        "invariant' (Declares) on the same axis — typically from "
                                                        "copy-pasting two related fn<> declarations.  Or when two "
                                                        "Quotient grants accidentally name different class "
                                                        "representatives (Version<3> on the implementation side, "
                                                        "Version<5> on the export side).  Remediation: audit the pack, "
                                                        "pick ONE modality class per axis matching the intended "
                                                        "semantics, and ensure Quotient grants name the same "
                                                        "equivalence-class representative across the binding.";
    static constexpr std::string_view correct_example =
        "fn<int, grant::frame<X>, grant::declares<Y>>  // distinct axes";
    static constexpr std::string_view violating_example =
        "fn<int, grant::frame<X>, grant::declares<X>>  // R018 same axis";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters = "CSL frame discipline (O'Hearn 2007; CLAUDE.md §IX permission "
                                                         "discipline) demands that every Permission<Tag> has exactly "
                                                         "one consumer — sharing requires SharedPermission via the "
                                                         "fractional-permission pool, NOT duplicated Linear grants.  "
                                                         "Two Linear-modality grants on the same tag in a binding's "
                                                         "pack would let two call sites simultaneously claim exclusive "
                                                         "ownership of the same resource — the canonical aliased-"
                                                         "mutation footgun.  Catching it as a binding-shape error "
                                                         "(R017) at the fn<> declaration is strictly stronger than "
                                                         "catching it at the call site (R013) because the binding "
                                                         "shape persists across every invocation; one binding-shape "
                                                         "fix protects every caller.";
    static constexpr std::string_view symptom_pattern = "Surfaces when a function genuinely needs to consume two "
                                                        "exclusive permissions but the author accidentally tagged "
                                                        "both with the SAME tag (copy-paste from a single-permission "
                                                        "ancestor).  The diagnostic names the duplicated tag and the "
                                                        "two Linear grants competing for it.  Remediation: either "
                                                        "distinguish the tags (Permission<Tag1> vs Permission<Tag2>) "
                                                        "if the resources are genuinely distinct, OR switch one Linear "
                                                        "grant to a SharedPermission borrow via SharedPermissionPool"
                                                        "<Tag>::lend() if the resources should share read access.";
    static constexpr std::string_view correct_example =
        "fn<int, lifetime_region<Tag1>, lifetime_region<Tag2>>  // distinct";
    static constexpr std::string_view violating_example =
        "fn<int, lifetime_region<Tag>, lifetime_region<Tag>>  // R017 alias";
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

    static constexpr Severity severity = Severity::Fatal;
    static constexpr std::string_view why_this_matters =
        "SharedPermissionPool<Tag> encodes Boyland 2003 fractional "
        "permissions in a single atomic state word: low 63 bits count "
        "outstanding borrows, top bit signals exclusive-upgrade in "
        "progress.  Saturation at COUNT_MASK (2^63 - 1) means the next "
        "lend would either alias the upgrade bit (silently marking the "
        "pool busy while admitting a phantom borrower) or wrap to zero "
        "(losing every outstanding guard's ledger entry).  Either "
        "outcome breaks CSL frame discipline — the type system loses "
        "its proof that read borrows are disjoint from writes.  Pre-fix "
        "the saturation site std::abort()ed with no breadcrumb; "
        "post-fix this tag is emitted before terminating.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces in long-lived pools after a leaked SharedPermission"
        "Guard (orphaned worker, exception during construct, missing "
        "RAII).  Reaching 2^63 lifetime lends on a single pool requires "
        "either a guard leak or genuinely unbounded reuse — the fix "
        "depends on which.";
    static constexpr std::string_view correct_example =
        "{ auto guard = pool.lend(); use(guard); }  // RAII drop releases";
    static constexpr std::string_view violating_example =
        "auto* leak = new SharedPermissionGuard(pool.lend());  // never freed";
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

    static constexpr Severity severity = Severity::Fatal;
    static constexpr std::string_view why_this_matters = "Crucible's SPSC backings (TraceRing, MetaLog) require "
                                                         "2-MB-aligned regions for TLB efficiency — every page miss on "
                                                         "the recording hot path is amortized across 512 small pages.  "
                                                         "std::aligned_alloc(2 MB, n) returns nullptr when the kernel "
                                                         "cannot satisfy a 2-MB-aligned extent: depleted "
                                                         "/proc/sys/vm/nr_hugepages, fragmented address space, or "
                                                         "RLIMIT_AS / cgroup memory.max exhaustion.  Bootstrap cannot "
                                                         "complete without these buffers — recording is structural to "
                                                         "the runtime, not an optional optimization.";
    static constexpr std::string_view symptom_pattern =
        "First-boot failure on a host where nr_hugepages was never "
        "tuned, or a container where the hugepage cgroup controller "
        "zeroed the per-cgroup reservation.  /proc/meminfo:HugePages_Free "
        "shows 0 at the moment of failure.";
    static constexpr std::string_view correct_example = "// Tune host: echo 256 > /proc/sys/vm/nr_hugepages then start";
    static constexpr std::string_view violating_example =
        "auto* ring = HugePageBuffer<T>::allocate(n);  // no host pool tune";
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

    static constexpr Severity severity = Severity::Fatal;
    static constexpr std::string_view why_this_matters =
        "handles::PublishOnce<T> is a one-shot publication primitive — "
        "exactly one publisher across the slot's lifetime, arbitrarily "
        "many observers.  The publish CAS goes nullptr → ptr and the "
        "single-publisher invariant is a soundness gate: a second "
        "successful publish would silently overwrite the channel "
        "payload, racing against observers that already acquired the "
        "prior value.  Pre-fix the post-CAS check was contract_assert "
        "only — under -fcontract-evaluation-semantic=ignore (the "
        "hot-path default per CLAUDE.md §V) the assert is elided, so "
        "the collision was silent.  Post-fix the publish path emits "
        "this tag before terminating, restoring the gate independent "
        "of contract semantic.";
    static constexpr std::string_view symptom_pattern =
        "Two threads racing to establish the same channel without an "
        "outer Once / OneShotFlag guard.  Or a retry loop that "
        "re-enters publish after a transient error instead of failing "
        "upward.  When PublishOnce backs a federation-cache slot, the "
        "tag fires on a cache-key collision or duplicate compile entry.";
    static constexpr std::string_view correct_example =
        "Once::call_once(latch, [&]{ slot.publish(p); });  // serialized";
    static constexpr std::string_view violating_example =
        "if (need_publish) slot.publish(p);  // no outer serialization";
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

    static constexpr Severity severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "safety::Bits<EnumType> wraps a uint flag-bitset over a scoped "
        "enum.  Three runtime invariants route through this tag: (a) "
        "from_raw(b) admitting a deserialized word with bits outside "
        "E's declared mask (post-schema-tightening drift, e.g., an "
        "on-disk word survives an enum-tightening upgrade and now "
        "carries a bit no current enumerator names); (b) mutual-"
        "exclusion violation — two flags declared MX-pair simultaneously "
        "set (landing with WRAP-Bits-Integration-4); (c) subsumption "
        "violation — parent flag set without its declared child (e.g., "
        "MetaFlags::Quantized without MetaFlags::HasScale).  Silent "
        "acceptance routes the bad value into the model state; "
        "Bits<E>'s consumers (test() / popcount() / serialization) "
        "depend on the invariant EXCLUDING these patterns.";
    static constexpr std::string_view symptom_pattern = "Deserialization paths reading an on-disk flag word into "
                                                        "Bits<E>::from_raw without first masking against E's full "
                                                        "enumerator OR-fold.  Mutation paths that established a flag "
                                                        "without unset()'ing the conflicting MX-peer — usually an "
                                                        "early-return that skipped the conflicting-flag clear.";
    static constexpr std::string_view correct_example = "Bits<E>::from_raw(raw & valid_mask_v<E>)  // masked";
    static constexpr std::string_view violating_example = "Bits<E>::from_raw(raw)  // unmasked deserialize";
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

    static constexpr Severity severity = Severity::Fatal;
    static constexpr std::string_view why_this_matters =
        "safety::Borrowed<T, Source> forwards std::span's UB-forwarding "
        "operator[] / subspan() / front() / back() to preserve hot-path "
        "costs (Borrowed.h:250 doc-block).  The lifetime tag prevents "
        "use-after-destruction of the source; this diagnostic covers "
        "the bounds axis the lifetime gate is silent on.  An out-of-"
        "bounds operator[] reads memory belonging to whatever follows "
        "the source — typical consequences: torn read against a sibling "
        "field, SIGBUS at the end of the Source's mapped region, or "
        "(under ASAN) a heap-buffer-overflow report rooted at the "
        "indexing site rather than the missing size() guard.";
    static constexpr std::string_view symptom_pattern = "Index-arithmetic call sites using operator[] / subspan that "
                                                        "rely on caller-side size() comparison — refactoring breaks "
                                                        "the comparison without breaking the indexing.  Subspan with "
                                                        "offset + count > size() that doesn't trigger first() / last() "
                                                        "filtering at the boundary.";
    static constexpr std::string_view correct_example = "if (i < b.size()) use(b[i]);  // explicit guard";
    static constexpr std::string_view violating_example =
        "for (size_t i = 0; i <= b.size(); ++i) use(b[i]);  // off-by-one";
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
