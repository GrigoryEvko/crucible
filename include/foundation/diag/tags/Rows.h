#pragma once

// The tags of the effect rows: a row that is not a subrow of the row of its
// caller, and the three alias predicates of fixy/Aliases.h.  The authoring
// rule of a tag is in foundation/diag/Tag.h.

#include <foundation/diag/Tag.h>

namespace foundation::diag {

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

}  // namespace foundation::diag
