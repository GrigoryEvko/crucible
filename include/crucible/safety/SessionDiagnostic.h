#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto::diagnostic — L11 manifest-bug classification
//                                       (SEPLOG-H2d, task #342)
//
// A vocabulary of TAG TYPES naming every manifest-bug class the
// session-type literature and Crucible's own protocol layer have
// identified.  Purpose:
//
//   * ROUTED ERROR MESSAGES.  Every static_assert across the session-
//     type headers can tag its diagnostic with one of these classes.
//     Build logs become greppable: `[SubtypeMismatch]` finds every
//     subtyping failure; `[CrashBranch_Missing]` finds every
//     unreliable-peer-missing-crash-branch violation.
//
//   * REMEDIATION HINTS AS FIRST-CLASS DATA.  Each tag carries a
//     constexpr `::remediation` hint — a short, actionable message
//     the engineer reads to learn how to fix the bug class, not just
//     this specific instance.
//
//   * A SHARED VOCABULARY for cross-layer cross-referencing.  L6
//     subtype failures, L5 association failures, L8 crash-branch
//     checks can all emit the SAME diagnostic class when they
//     reject the SAME semantic issue.  Users reading the error
//     get consistent classification independent of which layer
//     fired it.
//
// ─── Classes shipped ──────────────────────────────────────────────
//
//   ProtocolViolation_Label      wrong label in Select / Offer
//   ProtocolViolation_Payload    payload type doesn't match schema
//   ProtocolViolation_State      op called in wrong protocol state
//   Deadlock_Detected            causality analysis found a cycle
//   Livelock_Detected            infinite loop with no progress
//   StarvationPossible           pending I/O might never fire
//   CrashBranch_Missing          Offer from unreliable peer lacks
//                                a Recv<Crash<Peer>, _> branch
//   PermissionImbalance          CSL permission set diverges at a
//                                reduction step
//   SubtypeMismatch              T ⩽ U does not hold
//   DepthBoundReached            bounded async subtype check hit
//                                the depth limit; try widening
//   UnboundedQueue               queue type not balanced-plus
//                                (Lange-Yoshida 2017 undecidability)
//
// ─── Extension design ─────────────────────────────────────────────
//
// New tag classes are added by inheriting from `tag_base`.  No
// trait-specialisation boilerplate required.  Example:
//
//   struct MyNewTag : tag_base {
//     static constexpr std::string_view name = "MyNewTag";
//     static constexpr std::string_view description = "…";
//     static constexpr std::string_view remediation = "…";
//   };
//
//   is_diagnostic_class_v<MyNewTag>      // → true, automatically
//   diagnostic_name_v<MyNewTag>          // → "MyNewTag"
//
// ─── Macro for classified diagnostics ─────────────────────────────
//
// CRUCIBLE_SESSION_ASSERT_CLASSIFIED(cond, tag, msg) expands to:
//
//     static_assert(cond, "crucible::session::diagnostic [<tag>]: <msg>")
//
// The `[<tag>]` bracketed prefix is uniform across all session-type
// headers that adopt it, making build-log filtering cheap:
//
//     build.log | grep "\[SubtypeMismatch\]"
//
// Existing assert_* helpers in the session-type headers don't yet
// consistently route through this macro; retrofit is orthogonal
// scope tracked as a follow-up.  This header ships the vocabulary;
// adopters route their own diagnostics through it.
//
// ─── References ───────────────────────────────────────────────────
//
//   session_types.md §III.L11 — the specification of this layer.
//   P2741R3 — C++26 user-generated static_assert messages.  Allows
//     constexpr expressions in static_assert messages, which would
//     make tag-embedded messages more dynamic.  We use the simpler
//     string-literal-concatenation form here for maximum tool
//     compatibility.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::diagnostic {

// ═════════════════════════════════════════════════════════════════════
// ── tag_base: marker for is_diagnostic_class_v ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Every diagnostic tag inherits from tag_base.  This lets the
// detection trait work on inheritance rather than requiring a
// specialisation per tag — new tags plug in with zero churn.

struct tag_base {};

// ═════════════════════════════════════════════════════════════════════
// ── The 11 manifest-bug tags ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each carries three constexpr string_views:
//
//   ::name         short identifier (used in routed messages)
//   ::description  one-sentence summary of what the bug class is
//   ::remediation  one-sentence actionable hint for fixing it

struct ProtocolViolation_Label : tag_base {
    static constexpr std::string_view name = "ProtocolViolation_Label";
    static constexpr std::string_view description =
        "A wrong label was selected in a Select<> or offered in an "
        "Offer<>.";
    static constexpr std::string_view remediation =
        "Check that the branch index picked matches the intended "
        "label; verify the peer's dual protocol has a matching "
        "Offer/Select at the same position.";
};

struct ProtocolViolation_Payload : tag_base {
    static constexpr std::string_view name = "ProtocolViolation_Payload";
    static constexpr std::string_view description =
        "A message was sent or received with the wrong payload type.";
    static constexpr std::string_view remediation =
        "Check the Send<P, K> / Recv<P, K> payload type matches what "
        "the transport's serializer produces; verify the peer's dual "
        "uses the same payload.";
};

struct ProtocolViolation_State : tag_base {
    static constexpr std::string_view name = "ProtocolViolation_State";
    static constexpr std::string_view description =
        "An operation was invoked on a SessionHandle whose protocol "
        "state does not provide that operation.";
    static constexpr std::string_view remediation =
        "Check the handle's current protocol — only Send-state handles "
        "have .send(), only Recv-state have .recv(), only Select-state "
        "have .select<I>(), etc.  Use is_head_v / protocol nested "
        "alias to introspect.";
};

struct Deadlock_Detected : tag_base {
    static constexpr std::string_view name = "Deadlock_Detected";
    static constexpr std::string_view description =
        "Causality analysis detected a cycle in the protocol's "
        "send/recv dependency graph — no participant can make "
        "progress.";
    static constexpr std::string_view remediation =
        "Examine the cycle: identify the mutual-wait pattern "
        "(A waits for B; B waits for A).  Break it by having one "
        "participant send unconditionally first, or restructure the "
        "global type so cross-participant sequencing is acyclic.";
};

struct Livelock_Detected : tag_base {
    static constexpr std::string_view name = "Livelock_Detected";
    static constexpr std::string_view description =
        "The protocol has a cycle of events with no Send or Recv that "
        "actually advances a participant's state — infinite loop with "
        "no progress.";
    static constexpr std::string_view remediation =
        "Check Loop<...Continue> bodies — each iteration must contain "
        "at least one Send or Recv for every participating role; "
        "empty loops are livelocks.";
};

struct StarvationPossible : tag_base {
    static constexpr std::string_view name = "StarvationPossible";
    static constexpr std::string_view description =
        "Under some scheduling a pending I/O might never fire — "
        "liveness property is weaker than required.";
    static constexpr std::string_view remediation =
        "Escalate the φ-level: if the protocol needs live+ (fair "
        "scheduling), verify the scheduler is fair; if live++ (any "
        "scheduling), the protocol itself must guarantee progress "
        "without fairness assumptions.";
};

struct CrashBranch_Missing : tag_base {
    static constexpr std::string_view name = "CrashBranch_Missing";
    static constexpr std::string_view description =
        "An Offer<> receives from a peer not in the reliability set R, "
        "but has no Recv<Crash<Peer>, _> branch to handle that peer's "
        "crash.";
    static constexpr std::string_view remediation =
        "Either add a Recv<Crash<Peer>, RecoveryBody> branch to the "
        "Offer, or add Peer to the ReliableSet<> for this protocol "
        "(marks Peer as assumed-not-to-crash).";
};

struct PermissionImbalance : tag_base {
    static constexpr std::string_view name = "PermissionImbalance";
    static constexpr std::string_view description =
        "A CSL permission-set invariant was violated at a reduction "
        "step — transferred permissions don't balance across the "
        "send/recv pair.";
    static constexpr std::string_view remediation =
        "Use Transferable<Perm> in the payload type to signal "
        "permission transfer; verify the sender's exit permission "
        "set matches the receiver's entry permission set.";
};

struct SubtypeMismatch : tag_base {
    static constexpr std::string_view name = "SubtypeMismatch";
    static constexpr std::string_view description =
        "A subtype relation T ⩽ U does not hold where it was required.";
    static constexpr std::string_view remediation =
        "Check Gay-Hole rules: Send covariant in payload AND "
        "continuation; Recv contravariant in payload + covariant in "
        "continuation; Select narrower (fewer branches) is subtype; "
        "Offer wider (more branches) is subtype; Stop is bottom "
        "(Stop ⩽ T for every T).";
};

struct DepthBoundReached : tag_base {
    static constexpr std::string_view name = "DepthBoundReached";
    static constexpr std::string_view description =
        "The bounded-depth SISO async subtype check reached its "
        "depth limit without concluding — conservative rejection.";
    static constexpr std::string_view remediation =
        "Either widen the depth bound at the call site "
        "(is_subtype_async_v<T, U, NewDepth>), override per-channel "
        "via ProtoSubtypeDepth specialisation, fall back to "
        "synchronous subtyping is_subtype_sync_v, or restructure the "
        "protocol to admit shallower refinement.";
};

struct UnboundedQueue : tag_base {
    static constexpr std::string_view name = "UnboundedQueue";
    static constexpr std::string_view description =
        "A queue type is not balanced-plus — en-route message count "
        "is unbounded along some reachable path.  Async subtyping "
        "with unbounded queues is undecidable "
        "(Lange-Yoshida 2017).";
    static constexpr std::string_view remediation =
        "Ensure every runtime queue has a compile-time capacity; add "
        "explicit is_bounded_queue_v<Q, Cap> checks at channel "
        "construction; if the protocol truly needs unbounded queues, "
        "architectural review is required — Crucible does not support "
        "them.";
};

// ═════════════════════════════════════════════════════════════════════
// ── is_diagnostic_class_v<T> ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Inheritance-based detection: T is a diagnostic class iff T is
// derived from tag_base (and T is not tag_base itself).

template <typename T>
inline constexpr bool is_diagnostic_class_v =
    std::is_base_of_v<tag_base, T> && !std::is_same_v<T, tag_base>;

// ═════════════════════════════════════════════════════════════════════
// ── Accessors (require T be a diagnostic class) ────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
    requires is_diagnostic_class_v<T>
inline constexpr std::string_view diagnostic_name_v = T::name;

template <typename T>
    requires is_diagnostic_class_v<T>
inline constexpr std::string_view diagnostic_description_v = T::description;

template <typename T>
    requires is_diagnostic_class_v<T>
inline constexpr std::string_view diagnostic_remediation_v = T::remediation;

// ═════════════════════════════════════════════════════════════════════
// ── Diagnostic<Tag, Ctx...> wrapper ────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// A type-level wrapper that pairs a diagnostic class with an
// arbitrary context tuple.  Used as a RETURN TYPE of metafunctions
// that need to propagate both a success/failure result AND the
// classified reason for failure.  Example:
//
//   template <typename T, typename U>
//   struct check_subtype_result {
//       using type = std::conditional_t<
//           is_subtype_sync_v<T, U>,
//           std::true_type,
//           Diagnostic<SubtypeMismatch, T, U>
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
// ── Catalog enumeration ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Compile-time tuple of all 11 shipped tag types — useful for
// reflection-based tooling, catalog printers, and diagnostic UIs.

using Catalog = std::tuple<
    ProtocolViolation_Label,
    ProtocolViolation_Payload,
    ProtocolViolation_State,
    Deadlock_Detected,
    Livelock_Detected,
    StarvationPossible,
    CrashBranch_Missing,
    PermissionImbalance,
    SubtypeMismatch,
    DepthBoundReached,
    UnboundedQueue>;

inline constexpr std::size_t catalog_size =
    std::tuple_size_v<Catalog>;

}  // namespace crucible::safety::proto::diagnostic

// ═════════════════════════════════════════════════════════════════════
// ── CRUCIBLE_SESSION_ASSERT_CLASSIFIED macro ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Expands to a static_assert whose message is prefixed with the
// diagnostic class name in square brackets for greppable build logs.
// Uses stringification (#tag) to embed the tag name as a literal.
//
// Usage:
//   CRUCIBLE_SESSION_ASSERT_CLASSIFIED(
//       (is_subtype_sync_v<T, U>),
//       SubtypeMismatch,
//       "T must be a synchronous subtype of U for this substitution.");
//
// IMPORTANT: if the condition contains a comma (e.g., template argument
// lists like is_subtype_sync_v<T, U>), parenthesise the entire
// condition so the preprocessor doesn't split it at the comma.
//
// Produces (on failure):
//   error: static assertion failed: crucible::session::diagnostic
//          [SubtypeMismatch]: T must be a synchronous subtype of U
//          for this substitution.

#define CRUCIBLE_SESSION_ASSERT_CLASSIFIED(cond, tag, msg)               \
    static_assert(cond,                                                  \
        "crucible::session::diagnostic [" #tag "]: " msg)

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::safety::proto::diagnostic::detail::diag_self_test {

// ─── is_diagnostic_class_v — positive and negative ────────────────

// Positive: every shipped tag is recognised.
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

// Negative: tag_base itself is not a tag (it's the marker); plain
// types are not tags.
static_assert(!is_diagnostic_class_v<tag_base>);
static_assert(!is_diagnostic_class_v<int>);
static_assert(!is_diagnostic_class_v<void>);

struct RandomStruct {};
static_assert(!is_diagnostic_class_v<RandomStruct>);

// User-defined extension works automatically.
struct UserDefinedTag : tag_base {
    static constexpr std::string_view name        = "UserDefinedTag";
    static constexpr std::string_view description = "custom class";
    static constexpr std::string_view remediation = "ask the user";
};
static_assert(is_diagnostic_class_v<UserDefinedTag>);

// ─── diagnostic_name_v / description_v / remediation_v ────────────

static_assert(diagnostic_name_v<SubtypeMismatch>        == "SubtypeMismatch");
static_assert(diagnostic_name_v<CrashBranch_Missing>    == "CrashBranch_Missing");
static_assert(diagnostic_name_v<Deadlock_Detected>      == "Deadlock_Detected");
static_assert(diagnostic_name_v<UnboundedQueue>         == "UnboundedQueue");

// Description + remediation present on every tag (non-empty).
static_assert(!diagnostic_description_v<SubtypeMismatch>.empty());
static_assert(!diagnostic_remediation_v<SubtypeMismatch>.empty());
static_assert(!diagnostic_description_v<CrashBranch_Missing>.empty());
static_assert(!diagnostic_remediation_v<CrashBranch_Missing>.empty());

// User-defined tag's accessors work too.
static_assert(diagnostic_name_v<UserDefinedTag>       == "UserDefinedTag");
static_assert(diagnostic_remediation_v<UserDefinedTag> == "ask the user");

// ─── Diagnostic<Tag, Ctx...> wrapper ──────────────────────────────

// Construction with various context types.
using D1 = Diagnostic<SubtypeMismatch, int, float>;
using D2 = Diagnostic<CrashBranch_Missing>;  // no context

// Shape predicate.
static_assert( is_diagnostic_v<D1>);
static_assert( is_diagnostic_v<D2>);
static_assert(!is_diagnostic_v<SubtypeMismatch>);
static_assert(!is_diagnostic_v<int>);

// Field access — diagnostic_class and context.
static_assert(std::is_same_v<typename D1::diagnostic_class, SubtypeMismatch>);
static_assert(std::is_same_v<typename D1::context, std::tuple<int, float>>);
static_assert(std::is_same_v<typename D2::context, std::tuple<>>);

// Name forwarded from the wrapped tag.
static_assert(D1::name == "SubtypeMismatch");
static_assert(D2::name == "CrashBranch_Missing");

// ─── Catalog ──────────────────────────────────────────────────────

static_assert(catalog_size == 11);
static_assert(std::tuple_size_v<Catalog> == 11);

// Each catalog entry is a valid diagnostic class.
static_assert(is_diagnostic_class_v<std::tuple_element_t<0,  Catalog>>);
static_assert(is_diagnostic_class_v<std::tuple_element_t<5,  Catalog>>);
static_assert(is_diagnostic_class_v<std::tuple_element_t<10, Catalog>>);

// The catalog starts with ProtocolViolation_Label and ends with
// UnboundedQueue (ordering is deterministic).
static_assert(std::is_same_v<
    std::tuple_element_t<0, Catalog>, ProtocolViolation_Label>);
static_assert(std::is_same_v<
    std::tuple_element_t<10, Catalog>, UnboundedQueue>);

// ─── Macro compile-test ───────────────────────────────────────────

// Happy path — true condition, macro compiles silently.
CRUCIBLE_SESSION_ASSERT_CLASSIFIED(
    true,
    SubtypeMismatch,
    "This condition is true, so the assertion passes silently.");

CRUCIBLE_SESSION_ASSERT_CLASSIFIED(
    is_diagnostic_class_v<SubtypeMismatch>,
    ProtocolViolation_Label,
    "Every shipped tag is a recognised diagnostic class.");

// Condition containing a comma (template-arg list) must be
// parenthesised so the preprocessor doesn't split at the comma.
CRUCIBLE_SESSION_ASSERT_CLASSIFIED(
    (std::is_same_v<tag_base, tag_base>),
    Deadlock_Detected,
    "tag_base is identical to itself — parenthesised to protect the "
    "inner comma from the preprocessor.");

// ─── Uniqueness: every tag's name is distinct ─────────────────────
//
// O(N²) pairwise-distinctness check.  11 tags × 55 comparisons is
// negligible at compile time.  Explicit std::array construction
// (not CTAD from braced-init-list, which is ambiguous here).

template <std::size_t... Is>
consteval bool catalog_names_distinct_impl(std::index_sequence<Is...>) {
    constexpr auto names = std::array<std::string_view, sizeof...(Is)>{
        std::tuple_element_t<Is, Catalog>::name... };
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            if (names[i] == names[j]) return false;
        }
    }
    return true;
}

consteval bool catalog_names_distinct() {
    return catalog_names_distinct_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_names_distinct());

}  // namespace crucible::safety::proto::diagnostic::detail::diag_self_test
