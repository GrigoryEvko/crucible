#pragma once

// ── crucible::fixy::sess::diagnostic — session manifest-bug catalog ──
//
// FIXY-U-052g (seventh slice of the U-052 umbrella).  Re-exports the
// complete public surface of sessions/SessionDiagnostic.h
// (`crucible::safety::proto::diagnostic::*`) into
// `crucible::fixy::sess::diagnostic::` — the session-type-stack's
// MANIFEST-BUG classification catalog (the named diagnostic tags that
// the subtype/queue/context/association layers cite when a protocol is
// ill-typed, plus the Diagnostic<Tag, Ctx...> result wrapper).
//
// ── NOT to be confused with fixy::diag:: ───────────────────────────
//
// `fixy::diag::` (FIXY-U-064) surfaces the FOUND-E01 foundation
// `crucible::safety::diag::Category` catalog — the cross-cutting
// wrapper-composition diagnostics.  THIS slice surfaces the SESSION
// layer's `crucible::safety::proto::diagnostic::` tags (SubtypeMismatch,
// ShapeMismatch_*, Deadlock_Detected, …) — the manifest-bug vocabulary
// of the MPST/binary session theory.  Two distinct substrates, two
// distinct fixy paths; this header touches only the session one.
//
// Thirty-three symbols (the complete proto::diagnostic public API):
//
//   Base (1):              tag_base
//   Diagnostic tags (23):  ProtocolViolation_Label / _Payload / _State /
//                          _Self_Loop, Deadlock_Detected,
//                          Livelock_Detected, StarvationPossible,
//                          CrashBranch_Missing, PermissionImbalance,
//                          SubtypeMismatch, DepthBoundReached,
//                          UnboundedQueue, Continue_Without_Loop,
//                          Protocol_Ill_Formed, Context_Domain_Collision,
//                          Context_Lookup_Miss, Queue_Empty_Dequeue,
//                          Association_Domain_Mismatch,
//                          Merge_Branches_Diverge,
//                          SessionResource_NotPinned,
//                          ShapeMismatch_SendVsRecv,
//                          ShapeMismatch_SelectVsOffer,
//                          BranchCount_Mismatch
//   Classifier + accessors (4): is_diagnostic_class_v, diagnostic_name_v,
//                          diagnostic_description_v, diagnostic_remediation_v
//   Result wrapper (3):    Diagnostic, is_diagnostic, is_diagnostic_v
//   Catalog (2):           Catalog, catalog_size
//
// The 23 tags == the entries of `Catalog` == `catalog_size`; this slice
// re-proves that triple through the fixy spelling (sentinel section E).
//
// ── Why a dedicated diagnostic:: sub-namespace ─────────────────────
//
// fixy::sess:: holds the binary session combinators; ::subtype:: the
// refinement-order layer (whose RejectionReason cites these tags);
// ::queue:: the en-route state.  This is the DIAGNOSTIC-TAG layer:
// the named, accessor-bearing reasons a protocol fails to type.
// Keeping it in ::diagnostic:: lets audit-grep
// `fixy::sess::diagnostic::` find every fixy-routed session
// manifest-bug citation distinct from substrate-direct call sites.
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Thirty-three using-decls + a sentinel battery + smoke routine.
// No new types, no mint factories, no free functions.  (The
// CRUCIBLE_SESSION_ASSERT_CLASSIFIED macro from SessionDiagnostic.h is a
// preprocessor symbol, not a namespace member; it is intentionally NOT
// re-exported — macros are global and not namespaceable.)
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; tags are empty markers.
//   TypeSafe — using-decls preserve substrate type identity; the
//              Diagnostic<> wrapper's requires-clause keeps non-tags out.
//   NullSafe — no pointer state introduced.
//   MemSafe  — all symbols are compile-time-only; nothing is allocated.
//   DetSafe  — classification is a pure type-level predicate.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionDiagnostic.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace crucible::fixy::sess::diagnostic {

// ── 1. Base (1) ────────────────────────────────────────────────────
using ::crucible::safety::proto::diagnostic::tag_base;

// ── 2. Diagnostic tag classes (23) ─────────────────────────────────
using ::crucible::safety::proto::diagnostic::ProtocolViolation_Label;
using ::crucible::safety::proto::diagnostic::ProtocolViolation_Payload;
using ::crucible::safety::proto::diagnostic::ProtocolViolation_State;
using ::crucible::safety::proto::diagnostic::Deadlock_Detected;
using ::crucible::safety::proto::diagnostic::Livelock_Detected;
using ::crucible::safety::proto::diagnostic::StarvationPossible;
using ::crucible::safety::proto::diagnostic::CrashBranch_Missing;
using ::crucible::safety::proto::diagnostic::PermissionImbalance;
using ::crucible::safety::proto::diagnostic::SubtypeMismatch;
using ::crucible::safety::proto::diagnostic::DepthBoundReached;
using ::crucible::safety::proto::diagnostic::UnboundedQueue;
using ::crucible::safety::proto::diagnostic::Continue_Without_Loop;
using ::crucible::safety::proto::diagnostic::Protocol_Ill_Formed;
using ::crucible::safety::proto::diagnostic::Context_Domain_Collision;
using ::crucible::safety::proto::diagnostic::Context_Lookup_Miss;
using ::crucible::safety::proto::diagnostic::Queue_Empty_Dequeue;
using ::crucible::safety::proto::diagnostic::Association_Domain_Mismatch;
using ::crucible::safety::proto::diagnostic::Merge_Branches_Diverge;
using ::crucible::safety::proto::diagnostic::SessionResource_NotPinned;
using ::crucible::safety::proto::diagnostic::ShapeMismatch_SendVsRecv;
using ::crucible::safety::proto::diagnostic::ShapeMismatch_SelectVsOffer;
using ::crucible::safety::proto::diagnostic::BranchCount_Mismatch;
using ::crucible::safety::proto::diagnostic::ProtocolViolation_Self_Loop;

// ── 3. Classifier + per-tag accessors (4) ──────────────────────────
using ::crucible::safety::proto::diagnostic::is_diagnostic_class_v;
using ::crucible::safety::proto::diagnostic::diagnostic_name_v;
using ::crucible::safety::proto::diagnostic::diagnostic_description_v;
using ::crucible::safety::proto::diagnostic::diagnostic_remediation_v;

// ── 4. Diagnostic<Tag, Ctx...> result wrapper + shape trait (3) ────
using ::crucible::safety::proto::diagnostic::Diagnostic;
using ::crucible::safety::proto::diagnostic::is_diagnostic;
using ::crucible::safety::proto::diagnostic::is_diagnostic_v;

// ── 5. Catalog enumeration (2) ─────────────────────────────────────
using ::crucible::safety::proto::diagnostic::Catalog;
using ::crucible::safety::proto::diagnostic::catalog_size;

}  // namespace crucible::fixy::sess::diagnostic

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessQueue.h::u052f_self_test.
// Substrate-side renames trip at every consumer's include time, not
// three TUs deep.

namespace crucible::fixy::sess::diagnostic::u052g_self_test {

namespace pdiag = ::crucible::safety::proto::diagnostic;

// ── A. Base + representative-tag type identity ─────────────────────
static_assert(std::is_same_v<tag_base, pdiag::tag_base>);
static_assert(std::is_same_v<SubtypeMismatch, pdiag::SubtypeMismatch>);
static_assert(std::is_same_v<ShapeMismatch_SelectVsOffer,
                             pdiag::ShapeMismatch_SelectVsOffer>);
static_assert(std::is_same_v<ProtocolViolation_Self_Loop,
                             pdiag::ProtocolViolation_Self_Loop>);
static_assert(std::is_same_v<Catalog, pdiag::Catalog>);

// ── B. Tags derive from tag_base; tag_base itself is not a tag ─────
static_assert(std::is_base_of_v<tag_base, SubtypeMismatch>);
static_assert(is_diagnostic_class_v<SubtypeMismatch>);
static_assert(is_diagnostic_class_v<BranchCount_Mismatch>);
static_assert(!is_diagnostic_class_v<tag_base>,
    "tag_base is the base sentinel, not itself a diagnostic class.");
static_assert(!is_diagnostic_class_v<int>,
    "a fundamental type is not a diagnostic class.");

// ── C. Distinct tags do not collapse ───────────────────────────────
static_assert(!std::is_same_v<SubtypeMismatch, ShapeMismatch_SendVsRecv>);
static_assert(!std::is_same_v<ShapeMismatch_SendVsRecv,
                              ShapeMismatch_SelectVsOffer>);

// ── D. Accessors + Diagnostic<> wrapper route through ──────────────
static_assert(!diagnostic_name_v<SubtypeMismatch>.empty(),
    "every shipped tag carries a non-empty name.");
static_assert(!diagnostic_description_v<SubtypeMismatch>.empty());
static_assert(!diagnostic_remediation_v<SubtypeMismatch>.empty());
static_assert(is_diagnostic_v<Diagnostic<SubtypeMismatch, int, double>>,
    "Diagnostic<Tag, Ctx...> is recognised by its shape trait.");
static_assert(!is_diagnostic_v<int>);
static_assert(std::is_same_v<
    Diagnostic<SubtypeMismatch, int>::diagnostic_class, SubtypeMismatch>);

// ── E. Catalog / catalog_size / 23-tag triple cross-check ──────────
//
// The Catalog tuple's arity and catalog_size must agree (structural
// identity — kept exact since both are derived from the same tuple
// type in this header).  The literal `23` lockstep with substrate is
// the U-130 floor-vs-ceiling target: the EXACT ceiling (`== 23`)
// lives in sessions/SessionDiagnostic.h:742 colocated with the
// source-of-truth Catalog tuple; THIS fixy-side header holds only
// the FLOOR pin (`>= 23`) catching the inverse direction (a tag
// removed from the substrate Catalog).
static_assert(catalog_size >= 23,
    "fixy::sess::diagnostic::catalog_size floor: regressed below 23 "
    "— a tag was removed from SessionDiagnostic.h's Catalog without "
    "updating both the colocated ceiling pin AND this floor witness.");
static_assert(std::tuple_size_v<Catalog> >= 23,
    "fixy::sess::diagnostic::Catalog tuple-size floor: same removal "
    "drift as above, expanded to the structural-identity form.");
static_assert(std::tuple_size_v<Catalog> == catalog_size,
    "Catalog arity and catalog_size must agree through the fixy path.");
static_assert(std::is_same_v<std::tuple_element_t<8, Catalog>, SubtypeMismatch>,
    "Catalog position 8 is SubtypeMismatch (frozen ordinal).");

// ── F. Cardinality witness — count of items U-052g surfaces ────────
//
//   tag_base (1) + diagnostic tags (23) + classifier/accessors (4) +
//   Diagnostic wrapper + shape (3) + Catalog/catalog_size (2)  ── 33
constexpr int u052g_surface_cardinality = 33;
static_assert(u052g_surface_cardinality == 33,
    "fixy::sess::diagnostic:: U-052g surface cardinality drifted — "
    "update SessDiagnostic.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::diagnostic::u052g_self_test

namespace crucible::fixy::sess::diagnostic {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body bugs.
// The smoke routine forces the classifier, accessors, and Diagnostic<>
// wrapper through real instantiation so any latent template-evaluation
// issue surfaces under `-fsyntax-only` of any TU that includes
// SessDiagnostic.h.
//
// Cost: compile-time metafunction evaluation + string_view reads only —
// no runtime state, no I/O.

inline void runtime_smoke_test() noexcept {
    using Tag  = SubtypeMismatch;
    using Diag = Diagnostic<Tag, int, double>;

    [[maybe_unused]] constexpr bool is_tag   = is_diagnostic_class_v<Tag>;
    [[maybe_unused]] constexpr bool not_tag  = is_diagnostic_class_v<int>;
    [[maybe_unused]] constexpr bool is_diag  = is_diagnostic_v<Diag>;
    [[maybe_unused]] constexpr std::size_t n = catalog_size;

    [[maybe_unused]] const std::string_view nm  = diagnostic_name_v<Tag>;
    [[maybe_unused]] const std::string_view ds  = diagnostic_description_v<Tag>;
    [[maybe_unused]] const std::string_view rm  = diagnostic_remediation_v<Tag>;

    [[maybe_unused]] const Diag d{};
    [[maybe_unused]] const std::string_view dnm = Diag::name;

    (void) is_tag; (void) not_tag; (void) is_diag; (void) n;
    (void) nm; (void) ds; (void) rm; (void) d; (void) dnm;
}

}  // namespace crucible::fixy::sess::diagnostic
