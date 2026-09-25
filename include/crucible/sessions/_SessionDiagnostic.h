#pragma once

#include <crucible/Platform.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::diagnostic {

struct tag_base {};

struct ProtocolViolation_Label : tag_base {
    static constexpr std::string_view name = "ProtocolViolation_Label";
    static constexpr std::string_view description = "A wrong label was selected in a Select<> or offered in an "
                                                    "Offer<>.";
    static constexpr std::string_view remediation = "Check that the branch index picked matches the intended "
                                                    "label; verify the peer's dual protocol has a matching "
                                                    "Offer/Select at the same position.";
};

struct ProtocolViolation_Payload : tag_base {
    static constexpr std::string_view name = "ProtocolViolation_Payload";
    static constexpr std::string_view description = "A message was sent or received with the wrong payload type.";
    static constexpr std::string_view remediation = "Check the Send<P, K> / Recv<P, K> payload type matches what "
                                                    "the transport's serializer produces; verify the peer's dual "
                                                    "uses the same payload.";
};

struct ProtocolViolation_State : tag_base {
    static constexpr std::string_view name = "ProtocolViolation_State";
    static constexpr std::string_view description = "An operation was invoked on a SessionHandle whose protocol "
                                                    "state does not provide that operation.";
    static constexpr std::string_view remediation = "Check the handle's current protocol — only Send-state handles "
                                                    "have .send(), only Recv-state have .recv(), only Select-state "
                                                    "have .select<I>(), etc.  Use is_head_v / protocol nested "
                                                    "alias to introspect.";
};

struct Deadlock_Detected : tag_base {
    static constexpr std::string_view name = "Deadlock_Detected";
    static constexpr std::string_view description = "Causality analysis detected a cycle in the protocol's "
                                                    "send/recv dependency graph — no participant can make "
                                                    "progress.";
    static constexpr std::string_view remediation = "Examine the cycle: identify the mutual-wait pattern "
                                                    "(A waits for B; B waits for A).  Break it by having one "
                                                    "participant send unconditionally first, or restructure the "
                                                    "global type so cross-participant sequencing is acyclic.";
};

struct Livelock_Detected : tag_base {
    static constexpr std::string_view name = "Livelock_Detected";
    static constexpr std::string_view description = "The protocol has a cycle of events with no Send or Recv that "
                                                    "actually advances a participant's state — infinite loop with "
                                                    "no progress.";
    static constexpr std::string_view remediation = "Check Loop<...Continue> bodies — each iteration must contain "
                                                    "at least one Send or Recv for every participating role; "
                                                    "empty loops are livelocks.";
};

struct StarvationPossible : tag_base {
    static constexpr std::string_view name = "StarvationPossible";
    static constexpr std::string_view description = "Under some scheduling a pending I/O might never fire — "
                                                    "liveness property is weaker than required.";
    static constexpr std::string_view remediation = "Escalate the φ-level: if the protocol needs live+ (fair "
                                                    "scheduling), verify the scheduler is fair; if live++ (any "
                                                    "scheduling), the protocol itself must guarantee progress "
                                                    "without fairness assumptions.";
};

struct CrashBranch_Missing : tag_base {
    static constexpr std::string_view name = "CrashBranch_Missing";
    static constexpr std::string_view description = "An Offer<> receives from a peer not in the reliability set R, "
                                                    "but has no Recv<Crash<Peer>, _> branch to handle that peer's "
                                                    "crash.";
    static constexpr std::string_view remediation = "Either add a Recv<Crash<Peer>, RecoveryBody> branch to the "
                                                    "Offer, or add Peer to the ReliableSet<> for this protocol "
                                                    "(marks Peer as assumed-not-to-crash).";
};

struct PermissionImbalance : tag_base {
    static constexpr std::string_view name = "PermissionImbalance";
    static constexpr std::string_view description = "A CSL permission-set invariant was violated at a reduction "
                                                    "step — transferred permissions don't balance across the "
                                                    "send/recv pair.";
    static constexpr std::string_view remediation = "Use Transferable<Perm> in the payload type to signal "
                                                    "permission transfer; verify the sender's exit permission "
                                                    "set matches the receiver's entry permission set.";
};

struct SubtypeMismatch : tag_base {
    static constexpr std::string_view name = "SubtypeMismatch";
    static constexpr std::string_view description = "A subtype relation T ⩽ U does not hold where it was required.";
    static constexpr std::string_view remediation = "Check Gay-Hole rules: Send covariant in payload AND "
                                                    "continuation; Recv contravariant in payload + covariant in "
                                                    "continuation; Select narrower (fewer branches) is subtype; "
                                                    "Offer wider (more branches) is subtype; Stop is bottom "
                                                    "(Stop ⩽ T for every T).";
};

struct DepthBoundReached : tag_base {
    static constexpr std::string_view name = "DepthBoundReached";
    static constexpr std::string_view description = "The bounded-depth SISO async subtype check reached its "
                                                    "depth limit without concluding — conservative rejection.";
    static constexpr std::string_view remediation = "Either widen the depth bound at the call site "
                                                    "(is_subtype_async_v<T, U, NewDepth>), override per-channel "
                                                    "via ProtoSubtypeDepth specialisation, fall back to "
                                                    "synchronous subtyping is_subtype_sync_v, or restructure the "
                                                    "protocol to admit shallower refinement.";
};

struct UnboundedQueue : tag_base {
    static constexpr std::string_view name = "UnboundedQueue";
    static constexpr std::string_view description = "A queue type is not balanced-plus — en-route message count "
                                                    "is unbounded along some reachable path.  Async subtyping "
                                                    "with unbounded queues is undecidable "
                                                    "(Lange-Yoshida 2017).";
    static constexpr std::string_view remediation = "Ensure every runtime queue has a compile-time capacity; add "
                                                    "explicit is_bounded_queue_v<Q, Cap> checks at channel "
                                                    "construction; if the protocol truly needs unbounded queues, "
                                                    "architectural review is required — Crucible does not support "
                                                    "them.";
};

struct Continue_Without_Loop : tag_base {
    static constexpr std::string_view name = "Continue_Without_Loop";
    static constexpr std::string_view description = "A `Continue` combinator was used at a protocol position with "
                                                    "no syntactically-enclosing `Loop<Body>` to bind to.  Continue "
                                                    "is the loop-back marker; without an enclosing Loop, there is "
                                                    "no protocol state to return to.";
    static constexpr std::string_view remediation = "Wrap the Continue (or its enclosing prefix) in `Loop<...>`.  "
                                                    "If the protocol is meant to be one-shot, replace Continue "
                                                    "with `End`.";
};

struct Protocol_Ill_Formed : tag_base {
    static constexpr std::string_view name = "Protocol_Ill_Formed";
    static constexpr std::string_view description = "A session-type expression failed `is_well_formed_v<P>`.  Most "
                                                    "common cause: a free `Continue` outside any enclosing `Loop`. "
                                                    "Other causes: degenerate Select/Offer with zero branches, "
                                                    "ill-typed Delegate/Accept payload, malformed Choice in L4.";
    static constexpr std::string_view remediation = "Walk the protocol type tree manually; verify every `Continue` "
                                                    "has a `Loop` ancestor and every `Select`/`Offer` has at least "
                                                    "one branch.  See Continue_Without_Loop for the most common "
                                                    "specific case.";
};

struct Context_Domain_Collision : tag_base {
    static constexpr std::string_view name = "Context_Domain_Collision";
    static constexpr std::string_view description = "A typing context Γ was constructed with two entries sharing "
                                                    "the same `(session_tag, role_tag)` key, OR `compose_context_t"
                                                    "<Γ1, Γ2>` was applied to contexts whose domains overlap.  "
                                                    "CSL's frame rule requires disjoint contexts.";
    static constexpr std::string_view remediation = "Rename one side's session_tag (most common fix); OR give the "
                                                    "two entries different roles within a shared session; OR lift "
                                                    "the shared entry into a common prefix before composition.";
};

struct Context_Lookup_Miss : tag_base {
    static constexpr std::string_view name = "Context_Lookup_Miss";
    static constexpr std::string_view description = "A `lookup_context_t<Γ, S, R>` (or `update_entry_t`, "
                                                    "`remove_entry_t`) was called for a key (S, R) not present in "
                                                    "Γ.  Operations that mutate-or-read existing entries treat "
                                                    "absence as a strict error.";
    static constexpr std::string_view remediation = "Use `contains_key_v<Γ, S, R>` to test presence before lookup. "
                                                    "Check the (S, R) you're querying matches the Γ's actual "
                                                    "entries; common typos: wrong session tag, swapped role, "
                                                    "querying for a role removed earlier in the protocol.";
};

struct Queue_Empty_Dequeue : tag_base {
    static constexpr std::string_view name = "Queue_Empty_Dequeue";
    static constexpr std::string_view description = "`head_queue_t<Q>` or `tail_queue_t<Q>` was applied to an "
                                                    "empty `Queue<>`.  At runtime the receiver's typing rules "
                                                    "should have BLOCKED at this state, not advanced past it; an "
                                                    "empty-queue dequeue indicates the typing-context reduction "
                                                    "rules are being applied to a non-reachable runtime state.";
    static constexpr std::string_view remediation = "Use `is_queue_empty_v<Q>` to gate dequeue.  Check the L7 "
                                                    "reduction logic that produced this Q — a Recv event must "
                                                    "have a non-empty matching queue at every reachable state.";
};

struct Association_Domain_Mismatch : tag_base {
    static constexpr std::string_view name = "Association_Domain_Mismatch";
    static constexpr std::string_view description = "Condition (1) of HYK24 association `Δ ⊑_s G` failed: Γ's "
                                                    "domain (for the given session tag) does not equal "
                                                    "`roles_of_t<G>`.  Either Γ is missing an entry for a role of "
                                                    "G, or Γ has an extra entry for a role NOT in G.";
    static constexpr std::string_view remediation = "Add `Entry<S, role, projected_local>` for every role in G "
                                                    "that's missing from Γ; remove extra entries for roles G "
                                                    "doesn't have.  `roles_of_t<G>` enumerates the required roles. "
                                                    "`projected_context_t<G, S>` produces the canonical Γ.";
};

struct Merge_Branches_Diverge : tag_base {
    static constexpr std::string_view name = "Merge_Branches_Diverge";
    static constexpr std::string_view description = "Plain merging at L4 third-party projection failed: two or "
                                                    "more `Choice` branches project to STRUCTURALLY DIFFERENT "
                                                    "local types for a non-sender, non-receiver role.  Plain "
                                                    "merge requires identical projections; coinductive full "
                                                    "merging (PMY25 §4.3) would admit some divergence but is not "
                                                    "yet shipped (task SEPLOG-STRUCT-7).";
    static constexpr std::string_view remediation = "Workaround until full merging lands: project to a role that "
                                                    "IS involved in every Choice (sender or receiver); OR "
                                                    "restructure the global type so third-party projections match "
                                                    "across all branches.  Long-term fix: implement coinductive "
                                                    "full merging per task SEPLOG-STRUCT-7.";
};

struct SessionResource_NotPinned : tag_base {
    static constexpr std::string_view name = "SessionResource_NotPinned";
    static constexpr std::string_view description = "mint_session_handle / mint_channel was called with a "
                                                    "Resource that fails the pin-discipline: either an lvalue "
                                                    "reference to a type not derived from safety::Pinned<T>, or "
                                                    "an rvalue reference (which would bind the handle to a "
                                                    "temporary).  The handle could outlive the referenced object, "
                                                    "or the object could be moved out from under the handle, "
                                                    "producing a use-after-free at the next Send/Recv.";
    static constexpr std::string_view remediation = "Three options.  (a) Make the channel Pinned by deriving it "
                                                    "from safety::Pinned<ChannelType>.  This is the canonical "
                                                    "fix for any channel intended to back live SessionHandles.  "
                                                    "(b) Pass the channel by value rather than reference; copies "
                                                    "are fine for value-like channels.  (c) Wrap the channel in "
                                                    "std::reference_wrapper (a value type) if the caller's "
                                                    "lifetime contract is satisfied by other means and you "
                                                    "deliberately want to opt out of the framework's check.";
};

struct ShapeMismatch_SendVsRecv : tag_base {
    static constexpr std::string_view name = "ShapeMismatch_SendVsRecv";
    static constexpr std::string_view description = "A subtype check failed because the LHS combinator class is "
                                                    "Send and the RHS is Recv (or vice versa).  Send and Recv "
                                                    "are duals — neither is a subtype of the other — so any "
                                                    "rule that pairs them at the same protocol position is a "
                                                    "structural mismatch, not a refinement.";
    static constexpr std::string_view remediation = "Check that the two protocols agree on direction at every "
                                                    "position.  Common cause: one side declared from the "
                                                    "PRODUCER's perspective and the other from the CONSUMER's; "
                                                    "use dual_of_t<Proto> to compute the peer-side view.  If a "
                                                    "subtype refactor accidentally swapped Send for Recv at one "
                                                    "branch, this tag fires at the offending pair.";
};

struct ShapeMismatch_SelectVsOffer : tag_base {
    static constexpr std::string_view name = "ShapeMismatch_SelectVsOffer";
    static constexpr std::string_view description = "A subtype check failed because the LHS combinator class is "
                                                    "Select (internal choice — we pick) and the RHS is Offer "
                                                    "(external choice — peer picks), or vice versa.  Select and "
                                                    "Offer are duals; pairing them at the same position is a "
                                                    "structural mismatch.";
    static constexpr std::string_view remediation = "Verify both protocols agree on whether the choice at this "
                                                    "position is INTERNAL (subtype's responsibility — Select) or "
                                                    "EXTERNAL (peer's responsibility — Offer).  Common cause: a "
                                                    "Refactor flipped the direction of choice without flipping "
                                                    "the dual side.";
};

struct BranchCount_Mismatch : tag_base {
    static constexpr std::string_view name = "BranchCount_Mismatch";
    static constexpr std::string_view description = "A Select or Offer subtype check failed because the branch "
                                                    "counts violate Gay-Hole's positional rule.  Select<B1...Bn> "
                                                    "⩽ Select<C1...Cm> requires n ≤ m (subtype picks FEWER "
                                                    "options).  Offer<B1...Bn> ⩽ Offer<C1...Cm> requires n ≥ m "
                                                    "(subtype handles MORE options).";
    static constexpr std::string_view remediation = "For Select: ensure the subtype has FEWER OR EQUAL branches "
                                                    "than the supertype.  Adding branches to a Select widens the "
                                                    "type (the opposite direction of refinement); to refine, "
                                                    "REMOVE branches.  For Offer: ensure the subtype has MORE "
                                                    "OR EQUAL branches than the supertype.  Removing branches "
                                                    "from an Offer narrows the type (wrong direction); to refine, "
                                                    "ADD branches.";
};

struct ProtocolViolation_Self_Loop : tag_base {
    static constexpr std::string_view name = "ProtocolViolation_Self_Loop";
    static constexpr std::string_view description = "A global type contains a Transmission<X, X, P, G> or "
                                                    "Choice<X, X, ...> — a self-transmission where the same role "
                                                    "is both sender and receiver.  In MPST, every communication "
                                                    "event is between two DISTINCT participants; a role cannot "
                                                    "send to itself.  Such types project to nonsense local types "
                                                    "(the same role would be both Send and Recv at the same "
                                                    "position) and would deadlock at runtime if reachable.";
    static constexpr std::string_view remediation = "Check that From and To in your Transmission / Choice are "
                                                    "different role tags.  Common cause: copy-paste error where "
                                                    "both sides reference the same role tag.  If you genuinely "
                                                    "want a participant's local-only state transition, model it "
                                                    "as a Machine<State> transition outside the global protocol "
                                                    "rather than as a self-Transmission.";
};

template <typename T>
inline constexpr bool is_diagnostic_class_v = std::is_base_of_v<tag_base, T> && !std::is_same_v<T, tag_base>;

// The accessors constrain T through a helper struct rather than a `requires`
// clause on the variable template.  A `requires`-clause failure on a variable
// template emits compiler-version-specific text, so neg-compile tests matching
// on it break on a toolchain bump.  The static_assert message below is
// framework-controlled and stable.

namespace detail::diag {

template <typename T, bool IsTag>
struct accessor_check;

template <typename T>
struct accessor_check<T, true> {
    static constexpr std::string_view name = T::name;
    static constexpr std::string_view description = T::description;
    static constexpr std::string_view remediation = T::remediation;
};

// The empty defaults exist so that a compiler which continues past the
// static_assert still reads a well-defined string rather than an incomplete
// type.
template <typename T>
struct accessor_check<T, false> {
    static_assert(is_diagnostic_class_v<T>, "crucible::session::diagnostic [DiagnosticAccessor_NonTag]: "
                                            "diagnostic_name_v / diagnostic_description_v / "
                                            "diagnostic_remediation_v requires T to be derived from "
                                            "diagnostic::tag_base.  A user extension inherits from "
                                            "tag_base and provides constexpr name, description and "
                                            "remediation.");

    static constexpr std::string_view name = "";
    static constexpr std::string_view description = "";
    static constexpr std::string_view remediation = "";
};

}  // namespace detail::diag

template <typename T>
inline constexpr std::string_view diagnostic_name_v = detail::diag::accessor_check<T, is_diagnostic_class_v<T>>::name;

template <typename T>
inline constexpr std::string_view diagnostic_description_v =
    detail::diag::accessor_check<T, is_diagnostic_class_v<T>>::description;

template <typename T>
inline constexpr std::string_view diagnostic_remediation_v =
    detail::diag::accessor_check<T, is_diagnostic_class_v<T>>::remediation;

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

using Catalog =
    std::tuple<ProtocolViolation_Label, ProtocolViolation_Payload, ProtocolViolation_State, Deadlock_Detected,
               Livelock_Detected, StarvationPossible, CrashBranch_Missing, PermissionImbalance, SubtypeMismatch,
               DepthBoundReached, UnboundedQueue, Continue_Without_Loop, Protocol_Ill_Formed, Context_Domain_Collision,
               Context_Lookup_Miss, Queue_Empty_Dequeue, Association_Domain_Mismatch, Merge_Branches_Diverge,
               SessionResource_NotPinned, ShapeMismatch_SendVsRecv, ShapeMismatch_SelectVsOffer, BranchCount_Mismatch,
               ProtocolViolation_Self_Loop>;

inline constexpr std::size_t catalog_size = std::tuple_size_v<Catalog>;

}  // namespace crucible::safety::proto::diagnostic

#define CRUCIBLE_SESSION_ASSERT_CLASSIFIED(cond, tag, msg) \
    static_assert(cond, "crucible::session::diagnostic [" #tag "]: " msg)

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace crucible::safety::proto::diagnostic::detail::diag_self_test {

static_assert(is_diagnostic_class_v<ProtocolViolation_Label>);
static_assert(is_diagnostic_class_v<ProtocolViolation_Payload>);
static_assert(is_diagnostic_class_v<ProtocolViolation_State>);
static_assert(is_diagnostic_class_v<Deadlock_Detected>);
static_assert(is_diagnostic_class_v<Livelock_Detected>);
static_assert(is_diagnostic_class_v<StarvationPossible>);
static_assert(is_diagnostic_class_v<CrashBranch_Missing>);
static_assert(is_diagnostic_class_v<PermissionImbalance>);
static_assert(is_diagnostic_class_v<SubtypeMismatch>);
static_assert(is_diagnostic_class_v<DepthBoundReached>);
static_assert(is_diagnostic_class_v<UnboundedQueue>);

static_assert(!is_diagnostic_class_v<tag_base>);
static_assert(!is_diagnostic_class_v<int>);
static_assert(!is_diagnostic_class_v<void>);

struct RandomStruct {};
static_assert(!is_diagnostic_class_v<RandomStruct>);

struct UserDefinedTag : tag_base {
    static constexpr std::string_view name = "UserDefinedTag";
    static constexpr std::string_view description = "custom class";
    static constexpr std::string_view remediation = "ask the user";
};
static_assert(is_diagnostic_class_v<UserDefinedTag>);

static_assert(diagnostic_name_v<SubtypeMismatch> == "SubtypeMismatch");
static_assert(diagnostic_name_v<CrashBranch_Missing> == "CrashBranch_Missing");
static_assert(diagnostic_name_v<Deadlock_Detected> == "Deadlock_Detected");
static_assert(diagnostic_name_v<UnboundedQueue> == "UnboundedQueue");

static_assert(!diagnostic_description_v<SubtypeMismatch>.empty());
static_assert(!diagnostic_remediation_v<SubtypeMismatch>.empty());
static_assert(!diagnostic_description_v<CrashBranch_Missing>.empty());
static_assert(!diagnostic_remediation_v<CrashBranch_Missing>.empty());

static_assert(diagnostic_name_v<UserDefinedTag> == "UserDefinedTag");
static_assert(diagnostic_remediation_v<UserDefinedTag> == "ask the user");

using D1 = Diagnostic<SubtypeMismatch, int, float>;
using D2 = Diagnostic<CrashBranch_Missing>;

static_assert(is_diagnostic_v<D1>);
static_assert(is_diagnostic_v<D2>);
static_assert(!is_diagnostic_v<SubtypeMismatch>);
static_assert(!is_diagnostic_v<int>);

static_assert(std::is_same_v<typename D1::diagnostic_class, SubtypeMismatch>);
static_assert(std::is_same_v<typename D1::context, std::tuple<int, float>>);
static_assert(std::is_same_v<typename D2::context, std::tuple<>>);

static_assert(D1::name == "SubtypeMismatch");
static_assert(D2::name == "CrashBranch_Missing");

static_assert(catalog_size == 23);
static_assert(std::tuple_size_v<Catalog> == 23);

static_assert(is_diagnostic_class_v<std::tuple_element_t<0, Catalog>>);
static_assert(is_diagnostic_class_v<std::tuple_element_t<5, Catalog>>);
static_assert(is_diagnostic_class_v<std::tuple_element_t<10, Catalog>>);
static_assert(is_diagnostic_class_v<std::tuple_element_t<18, Catalog>>);
static_assert(is_diagnostic_class_v<std::tuple_element_t<21, Catalog>>);
static_assert(is_diagnostic_class_v<std::tuple_element_t<22, Catalog>>);

static_assert(std::is_same_v<std::tuple_element_t<0, Catalog>, ProtocolViolation_Label>);
static_assert(std::is_same_v<std::tuple_element_t<10, Catalog>, UnboundedQueue>);
static_assert(std::is_same_v<std::tuple_element_t<11, Catalog>, Continue_Without_Loop>);
static_assert(std::is_same_v<std::tuple_element_t<17, Catalog>, Merge_Branches_Diverge>);
static_assert(std::is_same_v<std::tuple_element_t<18, Catalog>, SessionResource_NotPinned>);
static_assert(std::is_same_v<std::tuple_element_t<19, Catalog>, ShapeMismatch_SendVsRecv>);
static_assert(std::is_same_v<std::tuple_element_t<20, Catalog>, ShapeMismatch_SelectVsOffer>);
static_assert(std::is_same_v<std::tuple_element_t<21, Catalog>, BranchCount_Mismatch>);
static_assert(std::is_same_v<std::tuple_element_t<22, Catalog>, ProtocolViolation_Self_Loop>);

CRUCIBLE_SESSION_ASSERT_CLASSIFIED(true, SubtypeMismatch, "This condition is true, so the assertion passes silently.");

CRUCIBLE_SESSION_ASSERT_CLASSIFIED(is_diagnostic_class_v<SubtypeMismatch>, ProtocolViolation_Label,
                                   "Every shipped tag is a recognised diagnostic class.");

CRUCIBLE_SESSION_ASSERT_CLASSIFIED((std::is_same_v<tag_base, tag_base>), Deadlock_Detected,
                                   "tag_base is identical to itself — parenthesised to protect the "
                                   "inner comma from the preprocessor.");

// The std::array element type and extent are written out because CTAD from the
// braced-init-list is ambiguous here.
template <std::size_t... Is>
consteval bool catalog_names_distinct_impl(std::index_sequence<Is...>) {
    constexpr auto names = std::array<std::string_view, sizeof...(Is)>{std::tuple_element_t<Is, Catalog>::name...};
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            if (names[i] == names[j]) return false;
        }
    }
    return true;
}

consteval bool catalog_names_distinct() {
    return catalog_names_distinct_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_names_distinct());

}  // namespace crucible::safety::proto::diagnostic::detail::diag_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS
