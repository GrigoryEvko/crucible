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

// The text of one field of a tag: a std::string_view over its string
// literal.  A std::string_view built from a pointer counts the characters
// one at a time in a constant evaluation.  Each translation unit that
// includes this header would do that count again for each field of each
// tag, and the fields hold about 100,000 characters.  GCC folds
// __builtin_strlen of a string literal to a constant in one step.  The
// constructor is not a template, so the fields of different lengths share
// one instantiation.
struct tag_text_literal : std::string_view {
    consteval tag_text_literal(const char (&literal)[]) noexcept : std::string_view{literal, __builtin_strlen(literal)} {}
};

// Error and Fatal behave identically at compile time. They differ in
// what tooling is meant to do: Fatal marks a violation whose silent
// passage is dangerous rather than merely wrong, and offers no override.
//
// The severity grades a tag, so it is declared beside the tags rather
// than beside the reader in Insights.h.  Insights.h includes this header.
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
// the compliant line beside the violating one. The tag carries them as
// members, and Insights.h reads the members off whichever tag it is
// asked about.
//
// When writing them: the why states the architectural constraint and
// answers what actually breaks if it is ignored. The symptom describes
// how the violation typically arrives, so a reader can match it against
// the change they just made. Both examples are one line of real C++,
// and they differ only in the thing the diagnostic is about. A tag that
// declares none of them still compiles, and prints the shorter block.
struct EffectRowMismatch : tag_base {
    static constexpr tag_text_literal name = "EffectRowMismatch";
    static constexpr tag_text_literal description = "Met(X) Subrow<R_callee, R_caller> failed: a function declared "
                                                    "with effect row R_callee was invoked from a context with "
                                                    "effect row R_caller, where R_callee is not a subrow of "
                                                    "R_caller.  The callee requires effects (Bg, IO, Block, "
                                                    "Alloc, Init, Test) the caller's row does not permit.  Row "
                                                    "arithmetic per Tang-Lindley POPL 2026.";
    static constexpr tag_text_literal remediation =
        "Either widen the caller's row by lifting through weaken<R_wider>() "
        "to include the callee's effects, OR narrow the callee's row by "
        "removing operations that introduce the offending effects.  Use "
        "row_difference_t<R_callee, R_caller> to identify exactly which "
        "atoms are missing.  See foundation/effects/Row.h for the row algebra.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "Met(X) effect rows (Tang-Lindley POPL 2026) encode capability "
        "propagation in the type system.  A function declared with row "
        "R promises to use AT MOST those effects; a caller with row R' "
        "MUST contain R' to invoke it (Subrow<R, R'>).  Without this "
        "discipline, hot-path code accidentally inherits Bg-thread "
        "capabilities (alloc / IO / block) from a transitively-called "
        "subroutine, blowing latency budgets and breaking the recording "
        "trace.  The row IS the function's contract.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces after a refactor that lifts a previously-bg-thread "
                                                        "helper into a Computation<Row<>, T> (pure) signature without "
                                                        "removing the helper's printf / arena alloc.  The transitive "
                                                        "Subrow check fires at the lowest call site that actually "
                                                        "needs the bg-row, NOT at the helper itself.";
    static constexpr tag_text_literal correct_example =
        "Computation<Row<Effect::Bg>, T> bg_helper(); // honest about Bg";
    static constexpr tag_text_literal violating_example = "Computation<Row<>, T> bg_helper(); // hides Bg → fires here";
};

struct UnknownParameterShape : tag_base {
    static constexpr tag_text_literal name = "UnknownParameterShape";
    static constexpr tag_text_literal description = "A function was offered as a pipeline stage, and its signature "
                                                    "is not one.  fixy/concurrent/StageShape.h recognizes one "
                                                    "shape: a void function that takes one or more consumer "
                                                    "handles, then one or more producer handles, each by non-const "
                                                    "rvalue reference.";
    static constexpr tag_text_literal remediation = "Either reshape the signature to the stage shape: return void, "
                                                    "and take each consumer handle and then each producer handle by "
                                                    "non-const rvalue reference.  Or drive the channel handles "
                                                    "directly, without mint_stage.";

    static constexpr Severity severity = Severity::Warning;
    static constexpr tag_text_literal why_this_matters = "mint_stage builds a stage only from a function whose "
                                                         "signature is the stage shape.  The shape tells which "
                                                         "handles the body consumes and in which order the data "
                                                         "flows.  A handle taken by lvalue reference or by value "
                                                         "leaves a moved-from endpoint with the caller, and a "
                                                         "non-void return is a second output that nothing drains.  "
                                                         "Severity::Warning rather than Error, because the caller "
                                                         "can still drive the handles without a stage.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when a function written the natural way, for "
                                                        "example with raw pointers and sizes or with a returned "
                                                        "value, is given to mint_stage.  The fix is to take the "
                                                        "handles by rvalue reference and return void, or to drive "
                                                        "the handles without a stage.";
    static constexpr tag_text_literal correct_example =
        "void stage(ConsumerHandle&& in, ProducerHandle&& out);  // a pipeline stage";
    static constexpr tag_text_literal violating_example =
        "Result stage(ConsumerHandle& in);  // lvalue handle and a return value";
};

struct GradedWrapperViolation : tag_base {
    static constexpr tag_text_literal name = "GradedWrapperViolation";
    static constexpr tag_text_literal description = "An attempt to construct a Graded-backed wrapper (Linear, "
                                                    "Refined, Tagged, Secret, Monotonic, AppendOnly, Stale, "
                                                    "TimeOrdered, etc.) violated the GradedWrapper concept "
                                                    "contract.  Common causes: substrate type mismatch (graded_type "
                                                    "is not a Graded<...> specialization), modality inconsistency "
                                                    "(declared Absolute but substrate is Comonad), forwarder "
                                                    "fidelity break (value_type_name() / lattice_name() return "
                                                    "strings inconsistent with substrate).  See "
                                                    "foundation/algebra/GradedTrait.h for the full concept "
                                                    "definition and the five cheats it rejects.";
    static constexpr tag_text_literal remediation = "Audit the wrapper against foundation/algebra/GradedTrait.h's "
                                                    "GradedWrapper concept clause-by-clause: verify graded_type is "
                                                    "Graded<M, L, T> for some M/L/T; verify W::modality matches "
                                                    "graded_modality_v<W::graded_type>; verify forwarders return "
                                                    "the SAME strings as the substrate's.  Run the cheat probe "
                                                    "harness (test/fixy/test_cheat_probe.cpp) after fixing.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "GradedWrapper is the structural concept (foundation/algebra/GradedTrait.h) "
        "every safety wrapper must satisfy: graded_type points at a "
        "real Graded<M, L, T>, the wrapper's modality matches the "
        "substrate's, value_type and lattice_type are consistent, "
        "diagnostic forwarders return the substrate's strings.  Each "
        "clause rejects one of five known 'cheats', and the "
        "concept's job is to keep them rejected.  Violating the concept "
        "means downstream dispatcher reflection produces "
        "wrong dispatch decisions (e.g., a 'wrapper' with mismatched "
        "modality routes to the wrong lowering target).";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when authoring a NEW wrapper "
                                                        "without following the canonical Stale.h "
                                                        "template.  Common cause: copying part of an existing wrapper "
                                                        "and forgetting to update graded_type, OR adding a "
                                                        "value_type_decoupled opt-in without justification, OR "
                                                        "publishing a custom string-returning value_type_name() that "
                                                        "doesn't forward to the substrate.";
    static constexpr tag_text_literal correct_example =
        "using graded_type = Graded<Absolute, MyLattice::At<X>, T>; // matches";
    static constexpr tag_text_literal violating_example =
        "using graded_type = void; // not a Graded<...> — concept rejects";
};

struct LinearityViolation : tag_base {
    static constexpr tag_text_literal name = "LinearityViolation";
    static constexpr tag_text_literal description = "A linear value (Linear<T>, Permission<Tag>, OwnedRegion<T, "
                                                    "Tag>) was used in a way that violates QTT linearity (Atkey "
                                                    "FLoC 2018): copied (must be moved); consumed twice (must be "
                                                    "consumed once); used after move (the moved-from state is "
                                                    "linear-zero, not linear-one).  CSL frame rule violation if "
                                                    "the value is a Permission token.";
    static constexpr tag_text_literal remediation =
        "Trace the value's flow.  Each linear value has exactly ONE "
        "consumer; any sharing requires either explicit duplication "
        "(if the substrate permits — most don't) or fractional "
        "permissions via SharedPermissionPool<Tag>.  Use std::move at "
        "the consumption point; capture by value not reference into a "
        "lambda that takes ownership.  See foundation/permissions/Permission.h for "
        "the CSL primitive surface.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "Quantitative type theory (Atkey FLoC 2018) and Concurrent "
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
    static constexpr tag_text_literal symptom_pattern = "Surfaces when capturing a Linear<T> by value into a lambda "
                                                        "that's then std::moved into TWO different jthread spawns, OR "
                                                        "when a Permission<Tag> is stored in a struct field that's "
                                                        "subsequently copied for parallel dispatch, OR when a refactor "
                                                        "removes std::move and the compiler's implicit copy attempt "
                                                        "fires the deleted-copy assertion.";
    static constexpr tag_text_literal correct_example = "consumer(std::move(linear_val)); // single consumption, OK";
    static constexpr tag_text_literal violating_example =
        "consumer1(linear_val); consumer2(linear_val); // double-use; rejected";
};

struct RefinementViolation : tag_base {
    static constexpr tag_text_literal name = "RefinementViolation";
    static constexpr tag_text_literal description = "mint_refined<Pred>(v) was called with a value that fails the "
                                                    "predicate.  The mint checks the predicate with a precondition.  "
                                                    "Under the enforce semantic (Debug) and the observe semantic "
                                                    "(Release) the violation aborts through the contract handler.  "
                                                    "Only a translation unit compiled with the ignore semantic "
                                                    "constructs the value with a violated invariant.";
    static constexpr tag_text_literal remediation = "Either validate the value before the mint (call Pred(v) "
                                                    "explicitly and branch), OR use mint_refined_trusted<Pred>(v) "
                                                    "at sites where the caller has already proven the invariant "
                                                    "by other means.  Never use the trusted mint as a general "
                                                    "escape hatch — every use is a documented load-bearing "
                                                    "assertion that the caller is responsible for.  See "
                                                    "fixy/Refined.h for the predicate catalog.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "Refined<P, T> attaches a compile-time-named predicate P to "
                                                         "values of type T (fixy/Refined.h).  mint_refined checks P(v) "
                                                         "at run time under the enforce semantic (Debug) and the "
                                                         "observe semantic (Release), and aborts on a violation.  A "
                                                         "translation unit compiled with the ignore semantic treats "
                                                         "the check as [[assume(P(v))]], and the downstream code is "
                                                         "OPTIMIZED on the assumption that P holds.  There, a violated "
                                                         "predicate at the construction site causes the optimizer to "
                                                         "make incorrect downstream decisions (UB).  The discipline is "
                                                         "to validate at the construction site.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when boundary-validating user input incorrectly: "
                                                        "e.g., using mint_refined<positive>(value) on a value that "
                                                        "could be zero or negative.  Or when a refactor changes the "
                                                        "source of a mint_refined<bounded_above<8>>(ndim) where ndim "
                                                        "came from a now-uncapped tensor metadata field.";
    static constexpr tag_text_literal correct_example = "if (n > 0) mint_refined<positive>(n); // validated first";
    static constexpr tag_text_literal violating_example =
        "mint_refined<positive>(maybe_zero); // the precondition fails and aborts";
};

struct HotPathViolation : tag_base {
    static constexpr tag_text_literal name = "HotPathViolation";
    static constexpr tag_text_literal description = "A function declared as HotPath<Hot, T> (the foreground recording "
                                                    "path: zero allocation, zero syscall, zero block) invoked a "
                                                    "callee whose HotPath grade is Warm (alloc OK, no syscall) or "
                                                    "Cold (block + IO OK).  The hot path's per-op latency budget is "
                                                    "shape-dependent; admitting Warm/Cold callees breaks the budget "
                                                    "structurally.  Crucible discipline per CLAUDE.md §IX.";
    static constexpr tag_text_literal remediation = "Either move the offending operation off the hot path (drain "
                                                    "to bg thread via SPSC ring; defer to a Warm-tier helper), OR "
                                                    "if the operation IS hot-path-safe, give its declaration a "
                                                    "HotPath<Hot, T> wrapper to admit it under the gate.  The "
                                                    "common refactor: a printf for debugging is Cold; replace with "
                                                    "atomic counter increment (Hot) and drain to a bg formatter.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "The hot-path latency floor is the MESI cache-line transfer "
                                                         "cost — ~10-40 ns intra-socket, ~30-100 ns cross-socket "
                                                         "(CLAUDE.md §IX latency hierarchy).  Admitting a Warm callee "
                                                         "(allocation, ~50-200 ns) or Cold callee (block / IO, "
                                                         "~10-100 us) on a 5 ns recording path is a 10x to 10000x "
                                                         "slowdown.  HotPath<Hot, T> is the type system's promise that "
                                                         "the recorded code path stays within the latency floor; the "
                                                         "rejection here proves a regression would have shipped.";
    static constexpr tag_text_literal symptom_pattern = "Almost always surfaces after adding logging, asserting, or "
                                                        "instrumentation to a previously-clean hot-path TU.  The new "
                                                        "fprintf / std::cout / std::format call is Cold-tier; the "
                                                        "containing function's HotPath<Hot> declaration rejects the "
                                                        "transitive call at compile time, before the regression can "
                                                        "ship.  Less commonly: an inline-friendly helper that grew a "
                                                        "vector<T>::push_back over time.";
    static constexpr tag_text_literal correct_example = "static std::atomic<uint64_t> debug_counter; // Hot-path-safe";
    static constexpr tag_text_literal violating_example =
        "fprintf(stderr, \"debug %d\\n\", x); // Cold; rejected by Hot caller";
};

struct DetSafeLeak : tag_base {
    static constexpr tag_text_literal name = "DetSafeLeak";
    static constexpr tag_text_literal description = "The 8th axiom (DetSafe per CLAUDE.md §II.8) is violated: a "
                                                    "function declared as DetSafe<Pure, T> or DetSafe<PhiloxRng, "
                                                    "T> invoked a callee carrying MonotonicClockRead, "
                                                    "WallClockRead, EntropyRead, FilesystemMtime, or "
                                                    "NonDeterministicSyscall.  Same inputs → same outputs is "
                                                    "structurally broken; bit-exact replay (CI invariant) is "
                                                    "broken; cross-vendor numerics CI will reject downstream "
                                                    "outputs.  This is the load-bearing diagnostic that the "
                                                    "cache row fence enforces.";
    static constexpr tag_text_literal remediation = "Either eliminate the non-deterministic source (replace "
                                                    "wall-clock seed with Philox-derived seed; replace "
                                                    "/dev/urandom read with seeded Philox), OR if the operation "
                                                    "is genuinely impure (e.g., runtime metric collection), "
                                                    "lift the caller out of DetSafe<Pure> into "
                                                    "DetSafe<MonotonicClockRead> or higher tier.  Cipher::record_event "
                                                    "refuses any tier above PhiloxRng; the replay log cannot be "
                                                    "constructed from impure values.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters = "DetSafe is the 8TH AXIOM (CLAUDE.md §II.8): same inputs → "
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
    static constexpr tag_text_literal symptom_pattern =
        "Surfaces when adding 'just one little timestamp' to a Cipher "
        "event-recording site for debugging.  Or when a refactor "
        "replaces seeded Philox with std::random_device for 'better "
        "randomness'.  Or when the runtime metric collector's wall-clock "
        "sample accidentally crosses into a record_event path.  All "
        "three are real production patterns the framework now catches.";
    static constexpr tag_text_literal correct_example =
        "DetSafe<Pure, uint64_t> seed = philox(counter, key); // deterministic";
    static constexpr tag_text_literal violating_example =
        "auto seed = std::chrono::steady_clock::now(); // wall clock; rejected";
};

struct NumericalTierMismatch : tag_base {
    static constexpr tag_text_literal name = "NumericalTierMismatch";
    static constexpr tag_text_literal description = "A function pinned at NumericalTier<BITEXACT_STRICT> or "
                                                    "NumericalTier<BITEXACT_TC> invoked a kernel whose recipe "
                                                    "tier is RELAXED, ULP_INT8, ULP_FP8, or ULP_FP16.  The "
                                                    "tier-pinned consumer requires bit-exact (or bounded-ULP) "
                                                    "outputs; the looser kernel cannot satisfy the contract.  "
                                                    "Recipe tier vocabulary per FORGE.md §20 / NumericalRecipe.h.";
    static constexpr tag_text_literal remediation = "Either select a recipe whose tier matches the caller's "
                                                    "requirement (use Forge Phase E.RecipeSelect's fleet "
                                                    "intersection picker constrained to the required tier), OR "
                                                    "loosen the caller's tier pin if bit-exact is not actually "
                                                    "required for this code path.  Cross-vendor numerics CI "
                                                    "pairwise-validates recipe outputs against the CPU scalar-FMA "
                                                    "oracle per tolerance — verify the looser tier still meets "
                                                    "the application's accuracy requirement.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
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
    static constexpr tag_text_literal symptom_pattern = "Often surfaces after a Mimic backend ships a fast-path "
                                                        "ALLREDUCE (UNORDERED tier) and a downstream training step "
                                                        "expecting BITEXACT_TC tries to consume its output.  Or after "
                                                        "a Forge Phase E recipe-select picker switches to a tighter "
                                                        "BITEXACT_STRICT recipe and a previously-OK ORDERED kernel is "
                                                        "now rejected.";
    static constexpr tag_text_literal correct_example =
        "select_recipe<Tolerance::BITEXACT_TC>(KernelKind::GEMM_MM, fleet);";
    static constexpr tag_text_literal violating_example =
        "auto k = unordered_allreduce(x); // UNORDERED; rejected by BITEXACT";
};

struct MemOrderViolation : tag_base {
    static constexpr tag_text_literal name = "MemOrderViolation";
    static constexpr tag_text_literal description = "A hot binding names a fence at or above seq_cst.  The "
                                                    "BarrierStrength atoms of fixy/atoms/Barrier.h record the "
                                                    "fence strength that a binding provides, and collision rule "
                                                    "V301 refuses a hot binding that names seq_cst or full_fence.  "
                                                    "A binding that names no strength provides no fence, and V301 "
                                                    "does not fire on it.";
    static constexpr tag_text_literal remediation = "Name the weakest fence that the protocol needs: acq_rel for a "
                                                    "read-modify-write, release_store for a publication, and "
                                                    "acquire_load for a read of a publication.  If the code needs "
                                                    "one total order across more than one atomic, examine the "
                                                    "ownership boundaries again.  A total order is almost always a "
                                                    "sign that they are wrong.  If the total order is necessary, "
                                                    "move the fence to a binding that is not hot.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "A standalone seq_cst fence emits MFENCE on x86 and DMB ISH "
                                                         "on ARM, and each one waits for the store buffer to drain.  "
                                                         "Acquire and release ordering is sufficient for every ring, "
                                                         "every snapshot and every lock-free pattern that Crucible "
                                                         "uses (CLAUDE.md section IX).  A seq_cst fence on the hot "
                                                         "path costs one or two orders of magnitude of latency, and it "
                                                         "usually shows that nobody examined the ordering that the "
                                                         "protocol needs.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces in code that came from outside the project, for "
                                                        "example a lock-free fragment copied from a reference.  Or in "
                                                        "code refactored from a region under a mutex without an "
                                                        "examination of the ordering that it needs.  The diagnostic "
                                                        "names the hot binding and the strength that it provides.";
    static constexpr tag_text_literal correct_example =
        "x.store(v, std::memory_order_release);  // release_store: a publication needs no more";
    static constexpr tag_text_literal violating_example =
        "std::atomic_thread_fence(std::memory_order_seq_cst);  // seq_cst on a hot binding: V301";
};

struct AllocClassViolation : tag_base {
    static constexpr tag_text_literal name = "AllocClassViolation";
    static constexpr tag_text_literal description = "A function pinned at AllocClass<Stack>, AllocClass<Pool>, or "
                                                    "AllocClass<Arena> attempted to allocate from Heap or Mmap "
                                                    "(or invoked a callee that does).  The hot path forbids heap "
                                                    "allocation: malloc round-trip is ~50-200ns and unpredictable; "
                                                    "Arena bump is ~2ns and lock-free.  Crucible discipline per "
                                                    "CLAUDE.md §XVIII.";
    static constexpr tag_text_literal remediation = "Replace heap allocation with arena allocation: use "
                                                    "Arena::alloc_obj<T>() / Arena::alloc_array<T>(n) for DAG-"
                                                    "lifetime objects; PoolAllocator for object-pool patterns; "
                                                    "static buffers for genuinely-fixed-size data.  If the "
                                                    "allocation is required and cannot be moved off the hot path, "
                                                    "lift the caller's AllocClass to Heap explicitly — but "
                                                    "document why the hot-path discipline is being relaxed.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "Hot-path code MUST NOT allocate from the heap (CLAUDE.md §XVIII): "
        "malloc round-trip is ~50-200 ns, unpredictable under "
        "contention, and breaks the per-iteration latency budget.  "
        "Arena bump allocation is ~2 ns and lock-free; PoolAllocator "
        "is ~5 ns and bounded.  The AllocClass wrapper makes the "
        "discipline visible in function signatures so a refactor that "
        "introduces std::vector::push_back into the hot path is "
        "rejected at the call site before it ships.";
    static constexpr tag_text_literal symptom_pattern = "Almost always surfaces after a refactor that 'just adds a "
                                                        "vector<T>' to collect intermediate results, OR after using "
                                                        "std::string concatenation in a logging path, OR after "
                                                        "refactoring an arena-backed buffer to std::array<T, N> via a "
                                                        "helper that returns std::vector instead.";
    static constexpr tag_text_literal correct_example = "auto* buf = arena.alloc_array<float>(n); // Arena, ~2 ns";
    static constexpr tag_text_literal violating_example =
        "auto buf = std::vector<float>(n); // Heap; rejected on hot path";
};

struct VendorBackendMismatch : tag_base {
    static constexpr tag_text_literal name = "VendorBackendMismatch";
    static constexpr tag_text_literal description = "A kernel pinned at Vendor<NV>, Vendor<AMD>, Vendor<TPU>, "
                                                    "Vendor<TRN>, or Vendor<CER> was emitted by, or routed to, "
                                                    "the wrong vendor's Mimic backend.  Each vendor's backend "
                                                    "owns its IR003* lowering and native ISA emission; cross-"
                                                    "vendor mismatches indicate a routing bug in Forge Phase H "
                                                    "(MIMIC.md §22) or a recipe-registry drift between "
                                                    "advertised native_on bitmaps and actual emit support.";
    static constexpr tag_text_literal remediation = "Verify the kernel's Vendor pin matches the target backend at "
                                                    "the dispatcher level.  Cross-vendor portability is an explicit "
                                                    "design choice (Vendor<Portable> admits any backend); if the "
                                                    "kernel is Portable, the routing layer should pick a backend "
                                                    "based on the active TargetCaps — not propagate the Portable "
                                                    "tag downstream.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "Mimic per-vendor backends (mimic::nv / am / tpu / trn / cpu) "
                                                         "are NOT interchangeable — each emits an instruction stream "
                                                         "valid only for its target ISA.  A Vendor-tagged kernel "
                                                         "(MIMIC.md §22) must be routed to the matching backend; "
                                                         "feeding an `Vendor::AMD`-rowed KernelNode<...> to "
                                                         "mimic::nv::compile_kernel produces SASS that doesn't match "
                                                         "the AMD GPU's instruction set, then surfaces as a runtime "
                                                         "kernel-launch failure or silent miscompile.  The compile-"
                                                         "time fence catches this BEFORE the silicon does.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when a kernel originally compiled for one vendor "
                                                        "is reused on another via a copy-paste of dispatch wiring "
                                                        "without updating the Vendor tag.  Less commonly: a fleet "
                                                        "join brings in peers of a different vendor and reshard "
                                                        "neglects to re-emit per-vendor.  The diagnostic fires at "
                                                        "the first mimic::*::compile_kernel call site whose row "
                                                        "doesn't include the backend's vendor tag.";
    static constexpr tag_text_literal correct_example = "mimic::nv::compile_kernel(node_with_Vendor_NV_row, ...);";
    static constexpr tag_text_literal violating_example =
        "mimic::nv::compile_kernel(node_with_Vendor_AMD_row, ...);  // WRONG";
};

struct CrashClassMismatch : tag_base {
    static constexpr tag_text_literal name = "CrashClassMismatch";
    static constexpr tag_text_literal description = "A binding that must leave its frame only by a return called "
                                                    "a callee that can leave it another way.  The ctrl atoms of "
                                                    "fixy/atoms/Ctrl.h record each other exit: throws, abort, "
                                                    "longjmp_unsafe and exit.  A binding with no ctrl atom leaves "
                                                    "only by a return, and that is the strict default.  Nothing in "
                                                    "this tree throws, and utils/scripts/check-no-throw-no-rtti.sh "
                                                    "fails the build when __cxa_throw reaches an artifact.";
    static constexpr tag_text_literal remediation = "Two routes.  (a) If the callee can fail, return "
                                                    "std::expected<T, E> from it.  The caller then handles the "
                                                    "failure explicitly.  (b) If the failure cannot occur at this "
                                                    "call site, call the callee through an adapter that calls a "
                                                    "cold [[noreturn]] helper, which calls std::abort(), on the "
                                                    "impossible case.  The adapter carries a "
                                                    "ctrl::abort atom that states the reason, and the caller admits "
                                                    "that exit by name.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "The ctrl atoms classify the ways a binding leaves its frame "
                                                         "other than by a return.  A binding with no ctrl atom leaves "
                                                         "only by a return, and an error comes back as a "
                                                         "std::expected value.  When such a binding calls a callee "
                                                         "that can abort, it loses that guarantee in the middle of a "
                                                         "protocol.  The rollback, recovery and replay reasoning that "
                                                         "depends on the guarantee then fails without a diagnostic.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces after a refactor that replaces a helper that returns "
                                                        "std::expected with one that aborts on failure, for example a "
                                                        "change to contract_assert.  The new helper carries a "
                                                        "ctrl::abort atom, and its caller admits no ctrl atom.  The "
                                                        "protocol step that a caller could recover from is now a "
                                                        "process kill.  The caller must go back to an error return, or "
                                                        "admit the abort by name and update the rollback logic.";
    static constexpr tag_text_literal correct_example = "std::expected<T, E> safe_op() noexcept;  // recoverable";
    static constexpr tag_text_literal violating_example =
        "T dangerous_op() noexcept;  // calls std::abort(), so it carries ctrl::abort";
};

struct ConsistencyMismatch : tag_base {
    static constexpr tag_text_literal name = "ConsistencyMismatch";
    static constexpr tag_text_literal description = "Reserved for a replicated value that reaches a consumer at a "
                                                    "weaker consistency level than the consumer needs.  No binding "
                                                    "or wrapper in this tree carries a consistency level, so nothing "
                                                    "emits this category.";
    static constexpr tag_text_literal remediation = "No action is necessary, because nothing emits this category.  "
                                                    "A consistency level needs a written meaning and a partial order "
                                                    "before a wrapper can carry it.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "The levels do not form a chain.  A chain puts "
                                                         "BOUNDED_STALENESS above READ_YOUR_WRITES.  But a read "
                                                         "that is K steps stale can miss a write that the reader "
                                                         "made.  So bounded staleness does not imply read-your-writes, "
                                                         "and the levels need a partial order.  "
                                                         "Bounded staleness also has a carrier already: the staleness "
                                                         "semiring of fixy/Stale.h.  The category keeps its index, "
                                                         "because each index is an ordinal pin of the federation "
                                                         "cache keys.";
    static constexpr tag_text_literal symptom_pattern = "Does not surface.  Nothing in this tree emits the category.";
    static constexpr tag_text_literal correct_example =
        "// No example: nothing in this tree carries a consistency level.";
    static constexpr tag_text_literal violating_example =
        "// No example: nothing in this tree emits ConsistencyMismatch.";
};

struct LifetimeViolation : tag_base {
    static constexpr tag_text_literal name = "LifetimeViolation";
    static constexpr tag_text_literal description = "An OpaqueLifetime<PER_REQUEST, T> value crossed a boundary "
                                                    "into a PER_PROGRAM or PER_FLEET scope.  The lifetime "
                                                    "lattice's chain order is "
                                                    "PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET; promoting a "
                                                    "PER_REQUEST value to longer lifetime leaks per-request data "
                                                    "across requests (security / isolation violation).  Pattern "
                                                    "from Pie SOSP 2025 inferlets.";
    static constexpr tag_text_literal remediation = "Either rebuild the value at the longer-lifetime boundary "
                                                    "(so the longer-lifetime cell holds a fresh value, not a "
                                                    "borrowed PER_REQUEST one), OR if the value is genuinely "
                                                    "PER_FLEET-correct, lift its construction site to declare "
                                                    "OpaqueLifetime<PER_FLEET, T>.  Cipher tier promotion: "
                                                    "PER_REQUEST goes to hot tier only; PER_FLEET writes go to "
                                                    "cold tier (S3); never promote across tiers via aliasing.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters =
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
    static constexpr tag_text_literal symptom_pattern = "Surfaces when a refactor consolidates a per-request handler "
                                                        "with a per-fleet broadcast path and forgets to scrub the "
                                                        "request-local fields before publication.  Or when adding "
                                                        "telemetry to an inferlet (per-request user state) without "
                                                        "marking the metric as PER_FLEET-aggregated.  The diagnostic "
                                                        "names the source value's lifetime AND the destination's "
                                                        "required lifetime; remediation is almost always 'redact "
                                                        "first, then promote'.";
    static constexpr tag_text_literal correct_example =
        "publish_to_fleet(OpaqueLifetime<PER_FLEET, Aggregate>{value});";
    static constexpr tag_text_literal violating_example =
        "publish_to_fleet(OpaqueLifetime<PER_REQUEST, RawTrace>{value});";
};

struct WaitStrategyViolation : tag_base {
    static constexpr tag_text_literal name = "WaitStrategyViolation";
    static constexpr tag_text_literal description = "A function pinned at Wait<SpinPause> (intra-core wait, "
                                                    "10-40ns latency) invoked a callee whose wait strategy is "
                                                    "Park (futex / mutex, 1-5μs), Block (kernel scheduler, "
                                                    "10-100μs), or worse.  The hot path's wait latency budget is "
                                                    "the MESI cache-line transfer cost; admitting park/block "
                                                    "callees blows the budget by 2-4 orders of magnitude.  "
                                                    "Wait-strategy hierarchy per CLAUDE.md §IX.";
    static constexpr tag_text_literal remediation = "Either replace the slow wait with a SpinPause loop "
                                                    "(_mm_pause on x86, yield on ARM) — appropriate when the "
                                                    "expected wait is sub-μs, OR move the wait off the hot path "
                                                    "to a bg-thread helper that can afford Park/Block.  If the "
                                                    "wait is genuinely necessary on the hot path (rare), document "
                                                    "the lift from SpinPause to Park with a justification "
                                                    "comment.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "Wait<Strategy, T> (28_04 §4.3.3) classifies wait costs: "
                                                         "SpinPause (≤40 ns intra-socket via MESI) ⊏ BoundedSpin "
                                                         "(N spins then back off) ⊏ UmwaitC01 (C0.1/C0.2 sleep) ⊏ "
                                                         "AcquireWait (atomic::wait/notify, futex-backed, ~1-5 us) "
                                                         "⊏ Park (jthread parking) ⊏ Block (mutex/condvar, ms).  "
                                                         "Hot-path callers admit only SpinPause; mixing in a Park "
                                                         "or Block via a transitively-called helper turns a 5 ns "
                                                         "recording into a 1-5 us syscall — a 1000× slowdown.  The "
                                                         "fence catches the regression at the call site, before "
                                                         "performance triage hours later.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces after adding logging or metrics to a hot-path TU "
                                                        "where the new logger uses std::cout (line-buffered → "
                                                        "fwrite_lock → futex → Block).  Or replacing a custom "
                                                        "spin-wait with std::condition_variable for 'simplicity'.  "
                                                        "The diagnostic names the offending callee's Wait grade "
                                                        "(usually Park or Block) and the caller's required grade "
                                                        "(usually SpinPause).";
    static constexpr tag_text_literal correct_example = "while (!flag.load(acquire)) Wait<SpinPause>{}.do_pause();";
    static constexpr tag_text_literal violating_example =
        "flag.wait(false);  // Wait<AcquireWait> in a SpinPause caller";
};

struct ProgressClassViolation : tag_base {
    static constexpr tag_text_literal name = "ProgressClassViolation";
    static constexpr tag_text_literal description = "A hot binding has Block in its row, so a call into it can wait "
                                                    "with no bound on when the wait ends.  This tree has no "
                                                    "termination class.  fixy/Aliases.h spells divergence as Block, "
                                                    "the effect atom for a wait with no guaranteed bound.  Collision "
                                                    "rule W001 refuses a hot binding whose row contains Block: a "
                                                    "kernel wait, a system call that can park the caller, or a "
                                                    "stated Block.";
    static constexpr tag_text_literal remediation = "Two routes.  (a) Replace the kernel wait with a wait that "
                                                    "stays in user space, which fixy/atoms/Sync.h records as "
                                                    "spin_pause or bounded_spin.  (b) If the binding must wait in "
                                                    "the kernel, it is not hot.  Move the wait to a background "
                                                    "binding, or remove the hot tier and keep Block in the row.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "The scheduler bounds a kernel wait, and that bound is "
                                                         "microseconds or more.  The hot path has a budget of "
                                                         "nanoseconds.  A hot binding that reaches such a wait "
                                                         "through a callee misses its budget, and nothing at the call "
                                                         "site shows the cause.  The row carries the wait up to the "
                                                         "binding, and W001 reads the row.  A new atom that can block "
                                                         "puts Block in its lift, and W001 then refuses it with no "
                                                         "edit to the rule.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces after a refactor adds a lock, a condition wait or a "
                                                        "system call that can park the caller to a helper that a hot "
                                                        "binding calls.  The atoms of the helper put Block in the row, "
                                                        "and the diagnostic names the hot binding.";
    static constexpr tag_text_literal correct_example =
        "while (!flag.load(std::memory_order_acquire)) CRUCIBLE_SPIN_PAUSE;  // user space, no Block";
    static constexpr tag_text_literal violating_example =
        "flag.wait(false);  // a kernel wait puts Block in the row of a hot binding: W001";
};

struct CipherTierViolation : tag_base {
    static constexpr tag_text_literal name = "CipherTierViolation";
    static constexpr tag_text_literal description = "A Cipher operation pinned at CipherTier<Hot> (other Relays' "
                                                    "RAM via RAID) was invoked at a context expecting Warm "
                                                    "(local NVMe) or Cold (S3 / GCS), or vice versa.  Tier "
                                                    "discipline per CRUCIBLE.md §L14: Hot writes are cheap and "
                                                    "ephemeral; Warm writes survive reboot; Cold writes survive "
                                                    "total cluster failure.  Mixing tiers loses the invariant the "
                                                    "tier was chosen for.";
    static constexpr tag_text_literal remediation = "Use the explicit per-tier API: Cipher::publish_hot for "
                                                    "RAID-replicated ephemeral state, publish_warm for NVMe "
                                                    "writes that survive reboot, publish_cold for S3 writes that "
                                                    "survive cluster failure.  Tier promotion is explicit: a "
                                                    "Hot value migrates to Warm via promote_to_warm() (cost: "
                                                    "fsync); to Cold via promote_to_cold() (cost: network).";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
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
    static constexpr tag_text_literal symptom_pattern = "Surfaces when Cipher::publish_warm is called on a value "
                                                        "whose CipherTier grade is Cold (e.g., a long-archive log "
                                                        "entry mistakenly routed through the warm tier).  Or when a "
                                                        "tier-promotion path (Hot → Warm at FSDP shard boundary) "
                                                        "loses the explicit tier marker via type erasure.  Diagnostic "
                                                        "names the source tier AND the destination's required tier.";
    static constexpr tag_text_literal correct_example =
        "Cipher::publish_warm(CipherTier<CipherTier::Warm, Shard>{value});";
    static constexpr tag_text_literal violating_example =
        "Cipher::publish_warm(CipherTier<CipherTier::Cold, Archive>{value});";
};

struct ResidencyHeatViolation : tag_base {
    static constexpr tag_text_literal name = "ResidencyHeatViolation";
    static constexpr tag_text_literal description =
        "Reserved for a value that gets to a consumer that accepts only a hotter residency.  fixy::ResidencyHeat in "
        "fixy/Bands.h pins the cache level that holds the working set of a value.  Hot is L1, Warm is L2, and Cold is "
        "L3 or DRAM.  These are the levels of the CPU cache hierarchy.  The L1, L2 and L3 tiers of the KernelCache "
        "grade portability, which is a different axis.  Nothing in this tree emits this category.";
    static constexpr tag_text_literal remediation =
        "No action is necessary for the category, because nothing emits it.  When the compiler rejects a residency, "
        "lower the residency that the consumer accepts, or make the value where its working set stays at that "
        "level.  relax moves only toward Cold, because a move up would claim a residency that nothing measured.";

    static constexpr Severity severity = Severity::Warning;
    static constexpr tag_text_literal why_this_matters =
        "The residency tells a consumer what one access costs.  An L1 hit costs a few cycles, and an L3 or DRAM "
        "access costs tens to hundreds of cycles.  A consumer that accepts Warm also accepts a Hot value, and a "
        "consumer that accepts only Hot rejects a Cold value.  A mismatch is a compile error: satisfies_v is false, "
        "relax rejects a move up, and a Hot parameter does not take a Cold value.  Severity::Warning rather than "
        "Error, because a residency error decreases performance but does not corrupt state.  The category keeps its "
        "index, because each index is an ordinal pin of the federation cache keys.";
    static constexpr tag_text_literal symptom_pattern =
        "Does not surface.  Nothing in this tree emits the category.  A residency mismatch shows as a compile error "
        "at the call that gives a Warm or Cold value to a consumer that accepts only residency_heat::Hot.";
    static constexpr tag_text_literal correct_example =
        "static_assert(fixy::satisfies_v<fixy::residency_heat::Hot<Key>, fixy::ResidencyHeatTag_v::Warm>);";
    static constexpr tag_text_literal violating_example =
        "static_assert(fixy::satisfies_v<fixy::residency_heat::Cold<Key>, fixy::ResidencyHeatTag_v::Hot>);  // false";
};

struct EpochMismatch : tag_base {
    static constexpr tag_text_literal name = "EpochMismatch";
    static constexpr tag_text_literal description =
        "Reserved for a fixy::EpochVersioned value whose version is too old for its gate.  fixy/EpochVersioned.h "
        "spells EpochVersioned<T>: a payload with the fleet epoch and the per-node generation.  Only a VersionSource, "
        "minted with an Init context, gives the VersionStamp that sets a version.  is_at_least returns false when one "
        "counter is below the requirement.  select_fresher returns VersionConflict::Incomparable or "
        "VersionConflict::Divergent.  Nothing in this tree emits this category.";
    static constexpr tag_text_literal remediation =
        "No action is necessary for the category, because nothing emits it.  When is_at_least returns false, make "
        "the value again with a stamp from VersionSource::stamp().  For a value from a peer, adopt the newer version "
        "at the source, and then stamp the value with stamp_received().  An Incomparable or Divergent pair has no "
        "fresher operand, and the caller decides which value holds.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "The version tells a reader which cluster state made the payload.  A gate accepts a value at or above a "
        "version, and a false high version makes a stale payload read as fresh.  A producer cannot state a version, "
        "and each stamp names a version that its source reached.  A value from an older epoch was calculated against "
        "a membership that no longer holds.  A collective that uses it calculates against the wrong partition.  The "
        "category keeps its index, because each index is an ordinal pin of the federation cache keys.";
    static constexpr tag_text_literal symptom_pattern =
        "Does not surface.  Nothing in this tree emits the category.  A caller sees the version check as the false "
        "return of is_at_least, or as the error of select_fresher.";
    static constexpr tag_text_literal correct_example =
        "fixy::EpochVersioned<Shard> const fresh{rebuild(shard), source.stamp()};";
    static constexpr tag_text_literal violating_example =
        "do_collective(stale);  // stamped at epoch N-1: is_at_least(EpochBound{N}, GenerationBound{0}) is false";
};

struct BudgetExceeded : tag_base {
    static constexpr tag_text_literal name = "BudgetExceeded";
    static constexpr tag_text_literal description =
        "Reserved for a fixy::Budgeted value with a grade above the bound of its gate.  fixy/Budgeted.h spells "
        "Budgeted<T> over BudgetLattice: a payload with the bits it transferred and the peak bytes it held.  Only a "
        "BudgetStamp from a BudgetAuthority, minted with an Init context, sets a finite grade.  satisfies returns "
        "false when one grade is above its bound, and a value that nothing measured is unbounded.  Nothing in this "
        "tree emits this category.";
    static constexpr tag_text_literal remediation =
        "No action is necessary for the category, because nothing emits it.  When satisfies returns false, decrease "
        "the use: smaller intermediate types, a smaller working set, or buffers that an arena uses again.  If the "
        "larger use is correct, get a larger grant from the BudgetAuthority.  A producer cannot state its own grade, "
        "and one stamp grades one value.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "A grade is a claim about what the payload used, and a smaller grade is a stronger claim.  A producer that "
        "states its own grade can claim zero and pass every gate.  Only a BudgetAuthority grants an allowance, and "
        "one stamp grades one value, because a stamp does not copy.  accumulate adds the grades of a chain of stages "
        "with a saturating sum, and combine_max takes the larger grades of two parallel paths.  A grant bounds work "
        "only when the work spends through resources that draw on the grant.  The category keeps its index, because "
        "each index is an ordinal pin of the federation cache keys.";
    static constexpr tag_text_literal symptom_pattern =
        "Does not surface.  Nothing in this tree emits the category.  A caller sees the budget check as the false "
        "return of satisfies, for example after accumulate sums a chain of stages above the bound, or for an "
        "unbounded value.";
    static constexpr tag_text_literal correct_example =
        "fixy::Budgeted<Shard> const v{shard, authority.grant(BitsBudgetBound{1024}, PeakBytesBound{1 << 20})};";
    static constexpr tag_text_literal violating_example =
        "a.accumulate(b).satisfies(BitsBudgetBound{1024}, PeakBytesBound{1 << 20});  // false: 800 + 500 bits";
};

struct NumaPlacementMismatch : tag_base {
    static constexpr tag_text_literal name = "NumaPlacementMismatch";
    static constexpr tag_text_literal description = "A NumaPlacement<Node, Affinity, T> value was consumed at a "
                                                    "thread whose CPU affinity is not on the value's declared NUMA "
                                                    "node.  Per CLAUDE.md §VIII OS-tuning: cross-node memory "
                                                    "access is 2-4× slower than NUMA-local; the AdaptiveScheduler "
                                                    "(THREADING.md §5.4) routes work to NUMA-local cores when "
                                                    "the working set is L3-resident or DRAM-bound.";
    static constexpr tag_text_literal remediation = "Either pin the consuming thread to the value's NUMA node "
                                                    "(pthread_setaffinity_np / sched_setaffinity), OR migrate the "
                                                    "value to the consuming thread's NUMA node before consumption "
                                                    "(numa_move_pages).  AdaptiveScheduler does this automatically "
                                                    "for parallel_for_views / parallel_apply_pair when the cost "
                                                    "model recommends NumaLocal placement.";

    static constexpr Severity severity = Severity::Warning;
    static constexpr tag_text_literal why_this_matters = "NumaPlacement<Node, Affinity, T> (28_04 §4.4.3) carries "
                                                         "a value's NUMA locality: which node owns its memory + the "
                                                         "thread affinity it expects.  A Node-3-tagged region "
                                                         "operated on by a Node-0-affined thread incurs cross-socket "
                                                         "memory latency on every access (~3-4× slower than local).  "
                                                         "Severity::Warning rather than Error because correctness is "
                                                         "preserved — only performance degrades — but on production "
                                                         "fleets this is a measurable revenue-cost regression.  The "
                                                         "fence catches the mis-placement BEFORE bench triage.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when AdaptiveScheduler dispatches a NumaLocal-"
                                                        "tagged work item to a NumaSpread thread pool (or vice "
                                                        "versa).  Or when a NumaPlacement<Node-N> region is fed to "
                                                        "a body whose own NUMA preference targets a different node.  "
                                                        "Diagnostic names the value's node + affinity AND the "
                                                        "consuming operation's expected combination.";
    static constexpr tag_text_literal correct_example =
        "scheduler.dispatch_local(NumaPlacement<Node{0}, Local>{region});";
    static constexpr tag_text_literal violating_example =
        "scheduler.dispatch_local(NumaPlacement<Node{3}, Spread>{region});";
};

struct RecipeSpecMismatch : tag_base {
    static constexpr tag_text_literal name = "RecipeSpecMismatch";
    static constexpr tag_text_literal description = "A RecipeSpec<Tier, Family, T> value was consumed at a Forge "
                                                    "Phase E.RecipeSelect picker that requires a different "
                                                    "(tier, family) combination, or was offered to a Mimic backend "
                                                    "whose native_on bitmap doesn't include the recipe's family "
                                                    "(PAIRWISE / LINEAR / KAHAN / BLOCK_STABLE).  Recipe registry "
                                                    "drift between recipes.json declarations and actual backend "
                                                    "support manifests as this diagnostic.";
    static constexpr tag_text_literal remediation = "Verify the recipe's declared (tier, family) matches the call "
                                                    "site's pin.  If the family is unsupported on the target "
                                                    "backend, the recipe registry's native_on bitmap should not "
                                                    "have advertised support — file a registry update.  If the "
                                                    "tier is wrong, see NumericalTierMismatch for tier-pinning "
                                                    "remediations.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "RecipeSpec<Tier, Family, T> (28_04 §4.4.4) carries a "
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
    static constexpr tag_text_literal symptom_pattern = "Surfaces when Forge Phase E.RecipeSelect picks a tier "
                                                        "different from the caller's declared bound, OR when a "
                                                        "fleet-intersection narrows the available recipes such "
                                                        "that no candidate satisfies the row's RecipeSpec "
                                                        "constraint.  Diagnostic names BOTH axes mismatched; "
                                                        "common remediation is a recipe-family change OR a "
                                                        "tier relaxation (with documented impact on bit-exactness).";
    static constexpr tag_text_literal correct_example = "compile(RecipeSpec<BITEXACT_TC, PAIRWISE, Kernel>{node});";
    static constexpr tag_text_literal violating_example =
        "compile(RecipeSpec<RELAXED, KAHAN, Kernel>{node});  // wrong axes";
};

// The next three tags cover the alias predicates over effect rows of
// fixy/Aliases.h. There are three and not more. PureRow is the empty row,
// and IsPure is its predicate. The universal row is the lattice top,
// satisfied by every row, so a violation of it is unreachable and carries no
// tag.
struct PureFunctionViolation : tag_base {
    static constexpr tag_text_literal name = "PureFunctionViolation";
    static constexpr tag_text_literal description = "A function declared as IsPure<R> was called with a row R that "
                                                    "contains at least one Effect atom.  PureRow encodes the EMPTY "
                                                    "effect row — pure functions have no observable effects in "
                                                    "F*.  Adding "
                                                    "Alloc, IO, Block, Bg, Init, or Test to a pure function's row "
                                                    "structurally violates F*'s PURE effect class.  Distinct from "
                                                    "EffectRowMismatch (Category 0) because the bound is the F* "
                                                    "lattice bottom, not an arbitrary caller-imposed row.";
    static constexpr tag_text_literal remediation = "Either remove the offending operation from the function body "
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
    static constexpr tag_text_literal why_this_matters =
        "The F* effect lattice (fixy/Aliases.h, Tang-Lindley POPL 2026) "
        "anchors at PURE — the empty row PureRow — as its bottom.  A "
        "function whose row satisfies IsPure promises NO observable "
        "effects: no Alloc, no IO, "
        "no Block, no Bg / Init / Test context tags.  Admitting any "
        "Effect atom turns a pure projection into a stateful operation "
        "that downstream callers (KernelCache content-addressing, "
        "deterministic replay, MAP-Elites cost-model fingerprinting) "
        "cannot consume — the cache key becomes invalidated by the "
        "function's hidden side effects, breaking the bit_exact_replay_"
        "invariant CI test in subtle ways.  See CLAUDE.md §L2 KernelCache "
        "content-addressing and §III.8 DetSafe axiom.";
    static constexpr tag_text_literal symptom_pattern =
        "Surfaces after adding 'just one debug line' (printf, fprintf, "
        "std::cout) to a function declared Pure<T>.  Or after a "
        "refactor that adds arena allocation "
        "to a previously-pointer-free pure helper.  The diagnostic "
        "names the function's required-empty-row contract and the "
        "atom that violates it (typically IO or Alloc).  The remediation "
        "is to lift the function up the F* lattice — Pure → Div → ST → "
        "All — to the strictest predicate the body actually needs.";
    static constexpr tag_text_literal correct_example =
        "Pure<uint64_t> compute_hash(Tagged<uint64_t, kind> k);  // empty row";
    static constexpr tag_text_literal violating_example =
        "Pure<uint64_t> compute_hash(...) { fprintf(stderr, ...); ... }  // IO";
};

struct DivergenceBudgetViolation : tag_base {
    static constexpr tag_text_literal name = "DivergenceBudgetViolation";
    static constexpr tag_text_literal description = "A function declared as IsDiv<R> was called with a row R "
                                                    "containing a state-effect atom (Alloc or IO).  DivRow = "
                                                    "Row<Block> — the F* DIV effect class extends PURE only with "
                                                    "non-termination, NOT with state mutation or external "
                                                    "observable effects.  In F*, ST is strictly above DIV in the "
                                                    "effect lattice; using state effects forces the function to "
                                                    "be lifted at least to IsST.";
    static constexpr tag_text_literal remediation = "Either remove the Alloc / IO operation from the function "
                                                    "body, OR lift the function's declaration to IsST (admits "
                                                    "Block + Alloc + IO) or higher.  The F* lattice is "
                                                    "Pure ⊑ Div ⊑ ST ⊑ All — moving up admits more effects but "
                                                    "narrows the call sites that can use the function (only callers "
                                                    "that already permit ST can pass arguments).  See "
                                                    "fixy/Aliases.h for the full alias-row catalog.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "The F* DIV effect class (DivRow in fixy/Aliases.h) extends PURE only "
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
    static constexpr tag_text_literal symptom_pattern =
        "Surfaces in fixed-point or DEQ-style convergence loops when "
        "the body grows a scratchpad allocation across iterations.  Or "
        "when a 'log progress' fprintf is added inside a loop body whose "
        "row satisfies IsDiv.  The diagnostic names the function's DivRow = "
        "Row<Block> requirement and the offending atom (Alloc or IO).  "
        "Remediation: lift the declaration to IsST (admits Block + "
        "Alloc + IO) or eliminate the allocation by reusing a "
        "caller-provided arena via a Pure helper signature.";
    static constexpr tag_text_literal correct_example =
        "Computation<DivRow, int> fixed_point(int x);  // Block only — terminates eventually";
    static constexpr tag_text_literal violating_example =
        "Computation<DivRow, int> fixed_point(int x) { void* p = std::malloc(64); ... }  // Alloc";
};

struct StateBudgetViolation : tag_base {
    static constexpr tag_text_literal name = "StateBudgetViolation";
    static constexpr tag_text_literal description = "A function declared as IsST<R> was called with a row R "
                                                    "containing a context-tag atom (Bg, Init, or Test).  STRow = "
                                                    "Row<Block, Alloc, IO> — the F* ST effect class admits state "
                                                    "mutation and divergence but NOT context-bound capabilities.  "
                                                    "Bg / Init / Test are dispatch hints, not state effects; they "
                                                    "carry caller-side capability that ST cannot accept "
                                                    "structurally.  Use IsAll for context-bound code.";
    static constexpr tag_text_literal remediation = "Either remove the context tag from the function's parameters "
                                                    "(if the function is genuinely state-only, the cap-tag "
                                                    "shouldn't be in its signature), OR lift the function's "
                                                    "declaration to IsAll (admits the universe row).  Context tags "
                                                    "encode dispatch position — Bg is bg-thread-only, Init is "
                                                    "startup-only, Test is test-harness-only.  A function that "
                                                    "needs a context tag MUST be IsAll because only IsAll's "
                                                    "AllRow includes the context-tag atoms.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "The F* ST effect class (STRow in fixy/Aliases.h) extends DIV with "
        "state effects (Alloc + IO) — admitting malloc, file handles, "
        "kernel ioctls — but NOT with context capabilities (Bg / Init / "
        "Test).  Context tags are NOT state effects; they encode WHERE "
        "the function runs (bg-thread-only, startup-only, test-harness-"
        "only).  Mixing a Bg cap-tag parameter into an IsST signature "
        "weakens the F* substitution principle: every IsPure caller can "
        "invoke IsST, but the Bg cap-tag would silently bypass the "
        "permission discipline that gates background-thread admission.  "
        "Use IsAll for functions that genuinely need context tags.";
    static constexpr tag_text_literal symptom_pattern =
        "Surfaces when a function is being lifted out of pure-functional "
        "code into stateful code and the author reaches for IsST<T> as "
        "'the catch-all', then realizes the function also takes an "
        "effects::Bg / Init / Test parameter for context-bound dispatch.  "
        "The diagnostic names the function's STRow = Row<Block, Alloc, "
        "IO> requirement and the context-tag atom that violates it.  "
        "Remediation: lift to IsAll (admits AllRow including context "
        "atoms), OR remove the cap-tag parameter if the function is "
        "genuinely state-only and the context is caller-provided.";
    static constexpr tag_text_literal correct_example =
        "Computation<STRow, int> stateful_compute(int x);  // Alloc + IO, no context tag";
    static constexpr tag_text_literal violating_example =
        "Computation<STRow, int> stateful_compute(eff::Bg bg, int x);  // Bg cap → rejected";
};

struct InsufficientWitness : tag_base {
    static constexpr tag_text_literal name = "InsufficientWitness";
    static constexpr tag_text_literal description = "A consumer demands a trust tag that the value does not carry.  "
                                                    "The trust tags of fixy/Tags.h form a one-way ratchet: "
                                                    "Unverified to Tested to Verified, and Assumed to Verified.  A "
                                                    "consumer that takes Tagged<T, trust::Tested> refuses a value "
                                                    "at a lower tag.  The only route up is a retag along an edge "
                                                    "of fixy::tags::admitted_retags.";
    static constexpr tag_text_literal remediation = "Either run the validator that discharges the edge and retag "
                                                    "the value along it, for example unverified_to_tested after "
                                                    "the test suite passes against the value, OR change the "
                                                    "consumer to take the lower tag if that tag is sufficient.  "
                                                    "Each edge of fixy::tags::admitted_retags names the validator "
                                                    "that discharges it.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "The trust tag records the evidence behind a value.  Unverified "
        "is a claim with no evidence, Tested is a value that a test "
        "suite ran against, and Verified is a value that a proof or a "
        "cryptographic check discharged.  A consumer states its floor "
        "in its parameter type.  A Cipher hot-tier promotion must not "
        "admit a kernel whose content-hash equivalence is only a claim, "
        "because replay determinism depends on it.  The cross-vendor "
        "numerics CI (MIMIC.md §41) is the evidence for that claim.  "
        "Without the floor, a kernel that no test covers can reach the "
        "hot tier and corrupt the replay logs.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when a refactor moves a value that only a developer "
                                                        "vouched for into a consumer that takes a higher trust tag, "
                                                        "with no validator run and no retag.  The compiler names the "
                                                        "parameter type that holds the floor and the tag that the "
                                                        "value carries.  Remediation: retag along the edge whose "
                                                        "validator ran, or lower the tag that the consumer takes.";
    static constexpr tag_text_literal correct_example =
        "auto tested = ::fixy::retag<trust::Tested>(std::move(claim));  // after the suite passes";
    static constexpr tag_text_literal violating_example =
        "admit_hot(::fixy::mint_tagged<trust::Unverified>(kernel));  // admit_hot takes trust::Tested";
};

struct ModalityMismatch : tag_base {
    static constexpr tag_text_literal name = "ModalityMismatch";
    static constexpr tag_text_literal description = "Two grants engaging the same fixy dim carry incompatible "
                                                    "modality classes — typically Frame (invariant) paired with "
                                                    "Declares (witness-producing) on the same axis (R018), or two "
                                                    "Quotient grants naming different equivalence-class "
                                                    "representatives (Version<3> vs Version<5>).  Modality classes: "
                                                    "Frame, Declares, Requires, Linear, Quotient.  See "
                                                    "fixy/Modality.h for the taxonomy.";
    static constexpr tag_text_literal remediation = "Audit the grant pack and pick a single grant per dim with the "
                                                    "correct modality class for the intended semantics.  A property "
                                                    "that is invariant of the value uses Frame; a property the "
                                                    "binding produces uses Declares; an input refinement the "
                                                    "caller must satisfy uses Requires; a consume-and-produce "
                                                    "resource transfer uses Linear; equivalence-class membership "
                                                    "uses Quotient.  Two grants on the same axis cannot mix Frame "
                                                    "with Declares — the invariant claim contradicts the "
                                                    "witness-producing claim.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "The modality taxonomy (fixy/Modality.h) classifies "
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
    static constexpr tag_text_literal symptom_pattern = "Surfaces when an author adds a grant pack mixing 'this is "
                                                        "invariant' (Frame) with 'this binding establishes the "
                                                        "invariant' (Declares) on the same axis — typically from "
                                                        "copy-pasting two related fn<> declarations.  Or when two "
                                                        "Quotient grants accidentally name different class "
                                                        "representatives (Version<3> on the implementation side, "
                                                        "Version<5> on the export side).  Remediation: audit the pack, "
                                                        "pick ONE modality class per axis matching the intended "
                                                        "semantics, and ensure Quotient grants name the same "
                                                        "equivalence-class representative across the binding.";
    static constexpr tag_text_literal correct_example =
        "fn<int, grant::frame<X>, grant::declares<Y>>  // distinct axes";
    static constexpr tag_text_literal violating_example =
        "fn<int, grant::frame<X>, grant::declares<X>>  // R018 same axis";
};

struct LinearAliasViolation : tag_base {
    static constexpr tag_text_literal name = "LinearAliasViolation";
    static constexpr tag_text_literal description = "Two Linear-modality grants on the same Permission tag in a "
                                                    "single binding's pack.  Linear modality encodes one-shot "
                                                    "consume-and-produce resource transfer (lifetime_region<Tag> + "
                                                    "Mutable); two Linear grants on the SAME tag means two parallel "
                                                    "consumers of the same exclusive permission — a CSL frame-rule "
                                                    "violation.  R017 fires before R013 because it catches the "
                                                    "binding-shape error earlier than the call-site rule.";
    static constexpr tag_text_literal remediation = "Either remove one of the duplicate lifetime_region<Tag> grants "
                                                    "(if the binding actually consumes the permission ONCE), OR "
                                                    "split the binding into two separate fixy fn instances each "
                                                    "consuming one borrow.  CSL discipline: each Permission<Tag> "
                                                    "has exactly one consumer; sharing requires SharedPermission + "
                                                    "explicit fractional borrow via SharedPermissionPool<Tag>.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "CSL frame discipline (O'Hearn 2007; CLAUDE.md §IX permission "
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
    static constexpr tag_text_literal symptom_pattern = "Surfaces when a function genuinely needs to consume two "
                                                        "exclusive permissions but the author accidentally tagged "
                                                        "both with the SAME tag (copy-paste from a single-permission "
                                                        "ancestor).  The diagnostic names the duplicated tag and the "
                                                        "two Linear grants competing for it.  Remediation: either "
                                                        "distinguish the tags (Permission<Tag1> vs Permission<Tag2>) "
                                                        "if the resources are genuinely distinct, OR switch one Linear "
                                                        "grant to a SharedPermission borrow via SharedPermissionPool"
                                                        "<Tag>::lend() if the resources should share read access.";
    static constexpr tag_text_literal correct_example =
        "fn<int, lifetime_region<Tag1>, lifetime_region<Tag2>>  // distinct";
    static constexpr tag_text_literal violating_example =
        "fn<int, lifetime_region<Tag>, lifetime_region<Tag>>  // R017 alias";
};

struct SharedPermissionPoolSaturated : tag_base {
    static constexpr tag_text_literal name = "SharedPermissionPoolSaturated";
    static constexpr tag_text_literal description = "SharedPermissionPool<Tag>::lend_raw_() observed an outstanding-"
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
    static constexpr tag_text_literal remediation = "Audit lifetime of every SharedPermissionGuard<Tag> sourced "
                                                    "from the pool — a leaked guard prevents the count from ever "
                                                    "decrementing.  If borrow lifetimes are correct but throughput "
                                                    "genuinely exceeds 2^63 lend operations across the pool's "
                                                    "lifetime, refactor to use a fresh pool rooted by "
                                                    "mint_permission_root<Tag>() at a higher boundary so older "
                                                    "pools can be sunk.  COUNT_MASK is structural; it is not "
                                                    "tunable because the layout shares its top bit with "
                                                    "EXCLUSIVE_OUT_BIT for lock-free upgrade signaling.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters =
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
    static constexpr tag_text_literal symptom_pattern =
        "Surfaces in long-lived pools after a leaked SharedPermission"
        "Guard (orphaned worker, exception during construct, missing "
        "RAII).  Reaching 2^63 lifetime lends on a single pool requires "
        "either a guard leak or genuinely unbounded reuse — the fix "
        "depends on which.";
    static constexpr tag_text_literal correct_example =
        "{ auto guard = pool.lend(); use(guard); }  // RAII drop releases";
    static constexpr tag_text_literal violating_example =
        "auto* leak = new std::optional<SharedPermissionGuard<Tag>>(pool.lend());  // never deleted";
};

struct HugePageAllocationFailed : tag_base {
    static constexpr tag_text_literal name = "HugePageAllocationFailed";
    static constexpr tag_text_literal description = "foundation::AlignedBuffer<T, huge_page_bytes>::allocate(count) "
                                                    "received a null pointer from std::aligned_alloc(huge_page_bytes, "
                                                    "bytes), where bytes is count * sizeof(T) rounded up to the "
                                                    "huge-page size.  The heap could not supply an extent of that "
                                                    "size at 2 MiB alignment.  The usual causes are an address space "
                                                    "too fragmented to hold such an extent, and a memory limit "
                                                    "(RLIMIT_AS or the memory.max of the cgroup) that the request "
                                                    "is more than.  The metadata log of the recording path lives in "
                                                    "this buffer, so the runtime cannot start without it.";
    static constexpr tag_text_literal remediation = "Examine the memory limits of the process: ulimit -v, and the "
                                                    "memory.max of its cgroup.  Increase the limit that the request "
                                                    "is more than, or decrease the capacity of the buffer.  The "
                                                    "alignment does not ask the kernel for huge pages, so the "
                                                    "huge-page pool (/proc/sys/vm/nr_hugepages) has no effect on "
                                                    "this failure.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters = "The metadata log of the recording path lives in this "
                                                         "buffer.  The runtime cannot record without it, so a failed "
                                                         "allocation stops the process while it starts.  The 2 MiB "
                                                         "alignment is necessary for the kernel to back the region "
                                                         "with huge pages, and it is not sufficient: the advice that "
                                                         "asks for them is a separate step.";
    static constexpr tag_text_literal symptom_pattern =
        "An abort while the first Vigil is constructed, on a host or in a "
        "container whose memory limit is less than the capacity of the "
        "metadata log.";
    static constexpr tag_text_literal correct_example =
        "// Increase memory.max of the container, or decrease the capacity of the log";
    static constexpr tag_text_literal violating_example =
        "auto log = AlignedBuffer<TensorMeta, huge_page_bytes>::allocate(1u << 20);  // memory.max is 128M";
};

struct PublishOnceDoublePublish : tag_base {
    static constexpr tag_text_literal name = "PublishOnceDoublePublish";
    static constexpr tag_text_literal description = "fixy::handle::PublishOnce<T>::publish found the slot already "
                                                    "published.  PublishOnce takes one publisher over the full life "
                                                    "of the slot, and any number of observers.  The exchange from "
                                                    "null to the new pointer failed, so a different publisher came "
                                                    "first.  publish then prints this entry and ends the process, "
                                                    "under each contract semantic.";
    static constexpr tag_text_literal remediation = "Find the second path that reaches publish.  One slot takes at "
                                                    "most one publish call.  The usual causes are two threads that "
                                                    "both establish one channel, a retry loop that calls publish "
                                                    "again after an error, and a refactored call site that adds a "
                                                    "second publisher.  When several threads can publish, call "
                                                    "publish inside fixy::handle::Once::call.  The first thread "
                                                    "publishes, and the other threads wait and do not publish.  "
                                                    "fixy::handle::LazyEstablishedChannel does not serialize "
                                                    "establish: establish calls publish directly, so a second "
                                                    "establish also ends the process.  After establish, the channel "
                                                    "gives its one session to the first observer, and it refuses "
                                                    "each later request with AlreadyClaimed.  When the slot holds a "
                                                    "federation cache entry, a second publish shows a key collision "
                                                    "or two compiles of one entry.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters =
        "Observers read the published pointer with an acquire load and "
        "keep it.  If a second publish could replace the pointer, an "
        "observer could hold a pointer that the slot no longer "
        "publishes, and two observers could disagree about the channel.  "
        "A contract assertion cannot carry this check, because hot-path "
        "translation units build with the contract semantic set to "
        "ignore, and the assertion then does nothing.  publish calls an "
        "abort helper instead, and the helper runs under each semantic.";
    static constexpr tag_text_literal symptom_pattern =
        "Two threads that each establish one channel, with no Once around "
        "the call.  Or a retry loop that calls publish again after an "
        "error and does not return the error.  When PublishOnce holds a "
        "federation cache entry, the entry fires on a key collision or "
        "on two compiles of one entry.";
    static constexpr tag_text_literal correct_example =
        "once.call([&]() noexcept { slot.publish(p); });  // one publisher";
    static constexpr tag_text_literal violating_example =
        "if (need_publish) slot.publish(p);  // a second thread can publish too";
};

struct BitsInvariantViolation : tag_base {
    static constexpr tag_text_literal name = "BitsInvariantViolation";
    static constexpr tag_text_literal description = "A fixy::Bits<EnumType> word holds a value outside the "
                                                    "invariant of its flag enum.  fixy::Bits does not check that "
                                                    "invariant: from_raw admits every value of the underlying type, "
                                                    "and set() admits two flags that exclude each other.  The code "
                                                    "that validates the word names this tag.  Three concrete "
                                                    "failure modes route through this tag: (a) Bits<E>::from_raw(b) "
                                                    "loaded a deserialized bit-pattern containing flags outside "
                                                    "E's declared mask (e.g., the on-disk word survived an enum-"
                                                    "tightening upgrade and now carries a bit no current E "
                                                    "enumerator names); (b) a mutual-exclusion invariant "
                                                    "observed two mutually-"
                                                    "exclusive flags set simultaneously (e.g., NodeFlags::Dirty + "
                                                    "NodeFlags::Sealed); (c) a subsumption invariant observed a "
                                                    "parent flag set without its declared child flag (e.g., "
                                                    "MetaFlags::Quantized set without MetaFlags::HasScale).  In "
                                                    "every case the bits_ field carries content the wrapper's "
                                                    "consumers (test() / set() / popcount() / serialization) "
                                                    "depend on EXCLUDING — silent acceptance routes the bad value "
                                                    "into the model state.";
    static constexpr tag_text_literal remediation = "Audit the producer of the failing bit-pattern.  For "
                                                    "from_raw() deserialization paths: validate the on-disk word "
                                                    "against the current E's full mask BEFORE constructing the "
                                                    "Bits<E> (compare against the OR of all enumerators), and "
                                                    "reject loads from older schema versions through a separate "
                                                    "migration path.  For mutual-exclusion / subsumption "
                                                    "violations: trace the set() / unset() / toggle() call site "
                                                    "that established the invalid combination; the bug is usually "
                                                    "an early-return that skipped the unset() of the conflicting "
                                                    "flag.  Bits<E> has no parameter for such an invariant, so the "
                                                    "permanent fix is a validated owner of the word that routes "
                                                    "each mutation through a guarded transition, which fails loud "
                                                    "at the source.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "fixy::Bits<EnumType> wraps a flag word over a scoped enum, and "
        "it checks none of the invariants that follow.  Three runtime "
        "invariants route through this tag: (a) "
        "from_raw(b) admitting a deserialized word with bits outside "
        "E's declared mask (post-schema-tightening drift, e.g., an "
        "on-disk word survives an enum-tightening upgrade and now "
        "carries a bit no current enumerator names); (b) mutual-"
        "exclusion violation — two flags declared MX-pair simultaneously "
        "set; (c) subsumption "
        "violation — parent flag set without its declared child (e.g., "
        "MetaFlags::Quantized without MetaFlags::HasScale).  Silent "
        "acceptance routes the bad value into the model state; "
        "Bits<E>'s consumers (test() / popcount() / serialization) "
        "depend on the invariant EXCLUDING these patterns.";
    static constexpr tag_text_literal symptom_pattern = "Deserialization paths reading an on-disk flag word into "
                                                        "Bits<E>::from_raw without first masking against E's full "
                                                        "enumerator OR-fold.  Mutation paths that established a flag "
                                                        "without unset()'ing the conflicting MX-peer — usually an "
                                                        "early-return that skipped the conflicting-flag clear.";
    static constexpr tag_text_literal correct_example =
        "Bits<E>::from_raw(raw & kValidMask)  // kValidMask is the OR of every enumerator of E";
    static constexpr tag_text_literal violating_example = "Bits<E>::from_raw(raw)  // unmasked deserialize";
};

struct BorrowedBoundsViolation : tag_base {
    static constexpr tag_text_literal name = "BorrowedBoundsViolation";
    static constexpr tag_text_literal description = "fixy::Borrowed<T, Source> (include/fixy/Borrowed.h) received "
                                                    "an accessor call outside its bounds.  Its accessors carry no "
                                                    "contract clause and forward to std::span, whose behavior is "
                                                    "undefined outside the bounds.  Concrete failure modes: (a) "
                                                    "operator[](i) where i >= size(); (b) subview(offset, count) "
                                                    "where offset + count > size(); (c) front() or back() on an "
                                                    "empty Borrowed.  In "
                                                    "every case the consumer reads memory belonging to whatever "
                                                    "follows the source object — common consequences are torn "
                                                    "reads against a sibling field or a SIGBUS at the end of the "
                                                    "Source's mapped region.  The lifetime tag prevents use-after-"
                                                    "destruction of the source; this diagnostic covers the bounds "
                                                    "axis the lifetime gate is silent on.";
    static constexpr tag_text_literal remediation = "Audit the indexing site for missing size()-comparisons.  The "
                                                    "canonical Borrowed iteration idiom uses the range-based for "
                                                    "or std::ranges algorithms which derive bounds from "
                                                    "begin() / end() — operator[] and subview() are escape hatches "
                                                    "for index-arithmetic call sites that already proved the "
                                                    "in-range invariant by other means.  Permanent fix: rewrite "
                                                    "the indexing site through size()-aware iteration, or add a "
                                                    "CRUCIBLE_PRE(i < size()) ahead of the operator[] call (the "
                                                    "pre catches at consteval AND under enforce semantic at "
                                                    "runtime — see foundation/contracts/Pre.h).  For subview: "
                                                    "compare offset + count with size() before the call.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters =
        "fixy::Borrowed<T, Source> forwards operator[], subview(), front() "
        "and back() to std::span with no contract clause "
        "(include/fixy/Borrowed.h).  The lifetime tag prevents "
        "use-after-destruction of the source; this diagnostic covers "
        "the bounds axis the lifetime gate is silent on.  An out-of-"
        "bounds operator[] reads memory belonging to whatever follows "
        "the source — typical consequences: torn read against a sibling "
        "field, SIGBUS at the end of the Source's mapped region, or "
        "(under ASAN) a heap-buffer-overflow report rooted at the "
        "indexing site rather than the missing size() guard.";
    static constexpr tag_text_literal symptom_pattern = "Index-arithmetic call sites using operator[] / subview that "
                                                        "rely on caller-side size() comparison — refactoring breaks "
                                                        "the comparison without breaking the indexing.  A subview with "
                                                        "offset + count > size() that no caller check stops at the "
                                                        "boundary.";
    static constexpr tag_text_literal correct_example = "if (i < b.size()) use(b[i]);  // explicit guard";
    static constexpr tag_text_literal violating_example =
        "for (size_t i = 0; i <= b.size(); ++i) use(b[i]);  // off-by-one";
};

struct AllocationSizeOverflow : tag_base {
    static constexpr tag_text_literal name = "AllocationSizeOverflow";
    static constexpr tag_text_literal description = "The byte size of an allocation did not fit in std::size_t.  The "
                                                    "size is the element count times the element size, plus the "
                                                    "slack that rounds it up to the alignment.  One of these steps "
                                                    "wrapped, or the result is more than the bound of the buffer.  "
                                                    "foundation::AlignedBuffer and foundation::SwissTableBuffer "
                                                    "calculate their sizes this way, and each stops the process "
                                                    "before it asks the heap for storage.";
    static constexpr tag_text_literal remediation = "Find the caller that supplied the count, and make sure that "
                                                    "the count comes from a bounded source.  A count that comes "
                                                    "from a file, from the network or from a subtraction that can "
                                                    "wrap is the usual cause.  Validate the count against the "
                                                    "capacity that the design permits before the call, and refuse "
                                                    "a larger value with an error return.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters = "A wrapped size is smaller than the storage that the count "
                                                         "needs.  The allocation then succeeds, and each write past "
                                                         "the small block goes into memory that another object owns.  "
                                                         "The damage shows far from its cause, often in a different "
                                                         "thread.  The size path stops the process before the "
                                                         "allocation, because no caller can recover a buffer of the "
                                                         "size that it asked for.";
    static constexpr tag_text_literal symptom_pattern = "An abort at the first allocation after a count is read from "
                                                        "untrusted bytes, such as a header field of a trace file.  Or "
                                                        "an abort after a subtraction of two sizes wraps and gives a "
                                                        "count near the top of std::size_t.";
    static constexpr tag_text_literal correct_example =
        "if (count > kMaxSlots) return std::unexpected(Error::TooLarge);  // bounded first";
    static constexpr tag_text_literal violating_example =
        "auto buf = AlignedBuffer<Slot>::allocate(header.count);  // count read from the file";
};

template <typename T>
inline constexpr bool is_diagnostic_class_v = std::is_base_of_v<tag_base, T> && !std::is_same_v<T, tag_base>;

namespace detail {

// One selector per text field.  Each reads its member directly, so a tag
// that declares no such member is a compile error at the read.  The
// catalog arrays further down vary over these three, and their walk is
// written once.
inline constexpr auto select_name = []<typename Tag>() consteval { return std::string_view{Tag::name}; };
inline constexpr auto select_description = []<typename Tag>() consteval { return std::string_view{Tag::description}; };
inline constexpr auto select_remediation = []<typename Tag>() consteval { return std::string_view{Tag::remediation}; };

// The direct form constrains the variable template itself with
// `requires is_diagnostic_class_v<T>`. A requires-clause failure on a variable
// template reports in compiler-chosen wording, which drifts between releases.
// Routing through this function puts the wording under our control.  The
// if constexpr keeps the read away from a type that declares no name.
template <typename T>
[[nodiscard]] consteval std::string_view tag_name_() noexcept {
    static_assert(is_diagnostic_class_v<T>, "foundation::diag [DiagnosticAccessor_NonTag]: "
                                            "diagnostic_name_v requires T to be derived from "
                                            "foundation::diag::tag_base.  See foundation/diag/Catalog.h's catalog "
                                            "for the shipped tag classes; user-extensions inherit "
                                            "tag_base and provide constexpr name/description/remediation.");
    if constexpr (is_diagnostic_class_v<T>) {
        return select_name.template operator()<T>();
    } else {
        return {};
    }
}

}  // namespace detail

template <typename T>
inline constexpr std::string_view diagnostic_name_v = detail::tag_name_<T>();

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
    AllocationSizeOverflow = 33,
};

namespace detail {

// One tag reflection per enumerator, in declaration order, or a reflection
// of void for an enumerator that no tag spells.  A tag is a class declared
// directly in foundation::diag and derived from tag_base, so a stray type
// of the same name in this namespace cannot stand in for a tag. The mirror check in the
// check file of this header pins declaration order to the integer values.
// One walk over the namespace collects the tags, and each enumerator then
// compares identifiers, so the cost is linear in the members of the
// namespace plus the product of the two counts of names.  It is a
// template, so a name that reads it at namespace scope can depend on a
// template parameter, and only a translation unit that instantiates that
// name evaluates the walk.
template <class Unused = void>
[[nodiscard]] consteval std::vector<std::meta::info> catalog_tags() noexcept {
    std::vector<std::meta::info> classes;
    std::vector<std::string_view> identifiers;
    for (const auto m : std::meta::members_of(^^::foundation::diag, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        if (!std::meta::has_identifier(m)) continue;
        if (m == ^^tag_base || !std::meta::is_base_of_type(^^tag_base, m)) continue;
        classes.push_back(m);
        identifiers.push_back(std::meta::identifier_of(m));
    }
    std::vector<std::meta::info> tags;
    for (const auto en : std::meta::enumerators_of(^^Category)) {
        const std::string_view identifier = std::meta::identifier_of(en);
        std::meta::info found = ^^void;
        for (std::size_t index = 0; index < identifiers.size(); ++index) {
            if (identifiers[index] == identifier) {
                found = classes[index];
                break;
            }
        }
        tags.push_back(found);
    }
    return tags;
}

// The two checks that each enumerator names a tag and that each tag names
// an enumerator are in the check file of this header.  GCC evaluates a
// call with constant arguments in the body of a function that is not a
// template.  In this header, such a check would walk the catalog in each
// includer, also in an includer that reads no tag.

// The tuple of the tag types at the enumerators' positions, derived from
// the enum. tag_of_t<C> and category_of_v<Tag> index it.
//
// Each table below is a template, and each name that reads a table
// depends on a template parameter.  So only a translation unit that
// names a table builds it, and a translation unit that includes this
// header for one tag pays for no table.
template <class Unused>
struct catalog_of {
    using type = [:std::meta::substitute(^^std::tuple, catalog_tags<Unused>()):];
};

}  // namespace detail

template <class Unused = void>
using Catalog = typename detail::catalog_of<Unused>::type;

// The count of the enumerators, which is the count of the tags.  It is a
// literal, so that no includer walks the enum.  The check file of this
// header derives the count and pins the literal and the tuple to it.
inline constexpr std::size_t catalog_size = 34;

// An alias template cannot carry a requires-clause, so the constraint on the
// Category value lives on a struct template that the alias forwards to.
namespace detail {

template <Category C, class Unused = void>
struct tag_of_impl {
    static_assert(static_cast<std::size_t>(C) < catalog_size,
                  "tag_of_t<C>: Category value is out of catalog range. "
                  "Likely cause: a Category value cast from an out-of-range "
                  "integer via reinterpret_cast / static_cast without a "
                  "preceding range check.");
    using type = std::tuple_element_t<static_cast<std::size_t>(C), Catalog<Unused>>;
};

}  // namespace detail

template <Category C>
using tag_of_t = typename detail::tag_of_impl<C>::type;

namespace detail {

template <typename Tag, typename Unused = void, std::size_t... Is>
[[nodiscard]] consteval std::size_t category_index_fold(std::index_sequence<Is...>) noexcept {
    std::size_t result = sizeof...(Is);  // sentinel: not found
    // A later match overwrites an earlier one. No tag type appears twice in
    // the catalog, so at most one term of the fold ever assigns.
    ((std::is_same_v<Tag, std::tuple_element_t<Is, Catalog<Unused>>> ? (void)(result = Is) : (void)0), ...);
    return result;
}

template <typename Tag>
[[nodiscard]] consteval std::size_t category_index_of() noexcept {
    return category_index_fold<Tag>(std::make_index_sequence<catalog_size>{});
}

template <typename Tag>
struct category_of_impl {
    static_assert(is_diagnostic_class_v<Tag>, "category_of_v<Tag>: Tag must be derived from "
                                              "foundation::diag::tag_base.  Read Tag::name, Tag::description "
                                              "and Tag::remediation directly to access the fields of a tag "
                                              "without the Category indirection.");
    static constexpr std::size_t index = category_index_of<Tag>();
    static_assert(index < catalog_size, "category_of_v<Tag>: Tag is not registered in the foundation "
                                        "Catalog.  The Category enum is CLOSED to the tags of the "
                                        "Catalog tuple; user-defined tags inherit from "
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
template <auto Select, class Unused, std::size_t... Is>
[[nodiscard]] consteval auto catalog_fields_impl(std::index_sequence<Is...>) noexcept
    -> std::array<std::string_view, sizeof...(Is)> {
    return {Select.template operator()<std::tuple_element_t<Is, Catalog<Unused>>>()...};
}

template <class Unused>
inline constexpr auto catalog_names_v =
    catalog_fields_impl<select_name, Unused>(std::make_index_sequence<catalog_size>{});
template <class Unused>
inline constexpr auto catalog_descriptions_v =
    catalog_fields_impl<select_description, Unused>(std::make_index_sequence<catalog_size>{});
template <class Unused>
inline constexpr auto catalog_remediations_v =
    catalog_fields_impl<select_remediation, Unused>(std::make_index_sequence<catalog_size>{});

inline constexpr std::string_view unknown_category_sentinel{"<unknown Category>"};

[[nodiscard]] constexpr std::string_view catalog_field(std::array<std::string_view, catalog_size> const& fields,
                                                       Category c) noexcept {
    const auto index = static_cast<std::size_t>(std::to_underlying(c));
    return index < catalog_size ? fields[index] : unknown_category_sentinel;
}

}  // namespace detail

// The accessors answer with the tag's own field for every enumerator and
// with the sentinel for a value cast in from an out-of-range integer.
// Each is a template with one defaulted parameter, so the table that it
// reads is built only where a call instantiates it.  A call names no
// template argument: name_of(c).
template <class Unused = void>
[[nodiscard]] constexpr std::string_view name_of(Category c) noexcept {
    return detail::catalog_field(detail::catalog_names_v<Unused>, c);
}

template <class Unused = void>
[[nodiscard]] constexpr std::string_view description_of(Category c) noexcept {
    return detail::catalog_field(detail::catalog_descriptions_v<Unused>, c);
}

template <class Unused = void>
[[nodiscard]] constexpr std::string_view remediation_of(Category c) noexcept {
    return detail::catalog_field(detail::catalog_remediations_v<Unused>, c);
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
