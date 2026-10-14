#pragma once

// The tags of the wrappers and the bindings: the shape of a stage, a graded
// wrapper, linearity, refinement, trust, modality and a linear alias.  The
// authoring rule of a tag is in foundation/diag/Tag.h.

#include <foundation/diag/Tag.h>

namespace foundation::diag {

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

}  // namespace foundation::diag
