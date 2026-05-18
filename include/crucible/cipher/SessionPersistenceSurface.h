#pragma once

// ── crucible::cipher::SessionPersistenceSurface ──────────────────────
//
// fixy-A2-014: thin forward-declaration surface for the load-bearing
// Cipher members that crucible::bridges::SessionPersistence touches —
// CipherOpenView (proof-of-Open carrier) and CipherSessionEventPersistenceRow
// (the Met(X) row needed for cipher.persist_session_events / load_session_events).
//
// Why this header exists
// -----------------------
// Pre-fix, `<crucible/bridges/SessionPersistence.h>` directly pulled the
// FULL `<crucible/Cipher.h>` — which itself drags in Arena.h, MerkleDag.h,
// MetaLog.h, Serialize.h, FederationProtocol.h, CipherTierPromotion.h,
// FileHandle.h, RowHashFold.h, and the entire safety::Decide / Tagged /
// Mutation / OpaqueLifetime stack.  Every TU that touches
// PersistedSessionHandle pays that ~30-include tax.  CLAUDE.md §XV is
// explicit about the discipline ("header-only hot, split cold; if build
// is slow, audit headers"); this header is the lift-out the discipline
// asks for.
//
// What this header DOES expose
// -----------------------------
//   1. Forward declaration of `crucible::Cipher` — sufficient for
//      `Cipher&` references and `safety::ScopedView<Cipher, ...>`
//      template specializations (the view only stores `Carrier const*`).
//   2. Definition of `crucible::cipher_state::Open` — the type-state
//      tag that pairs with ScopedView.  Defined here (not in Cipher.h)
//      so consumers can name `CipherOpenView` without pulling Cipher.h.
//   3. Namespace-scope alias `crucible::CipherOpenView` —
//      `safety::ScopedView<Cipher, cipher_state::Open>` published at
//      namespace scope so it can be referenced from non-template
//      contexts without the nested-name dependency on a complete Cipher.
//   4. Namespace-scope alias `crucible::CipherSessionEventPersistenceRow`
//      — `effects::Row<Effect::IO, Effect::Block>` published at
//      namespace scope.  Class Cipher re-exports both aliases as its
//      `OpenView` / `persist_session_events_required_row` members for
//      backwards compatibility.
//
// What this header does NOT expose
// ---------------------------------
//   * Class Cipher's body — methods, fields, nested types beyond
//     OpenView/required_row.  Consumers that actually CALL Cipher
//     methods (persist_session_events, load_session_events,
//     mint_open_view, open, etc.) MUST explicitly
//     `#include <crucible/Cipher.h>` themselves.
//
// Discipline for SessionPersistence.h
// ------------------------------------
//   * Class template SessionPersistenceState has template-dependent
//     calls into Cipher (`cipher_.persist_session_events<CallerRow>`).
//     These defer to instantiation-time lookup, so this surface header
//     is sufficient for parsing.  The actual call sites (test, bench,
//     PersistedSessionHandle methods) instantiate with complete Cipher
//     in scope.
//   * Field types `Cipher& cipher_`, `CipherOpenView view_` are
//     non-dependent but only need (a) forward decl of Cipher, (b) the
//     CipherOpenView alias — both provided here.  ScopedView's body
//     stores only `Carrier const* ptr_`, so its size and layout are
//     known with just a forward decl.
//
// Axiom coverage
// ---------------
// InitSafe / TypeSafe / NullSafe / MemSafe trivially hold — this header
// declares aliases only, no runtime state.  DetSafe trivially holds
// (purely consteval).  BorrowSafe / ThreadSafe / LeakSafe N/A.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/ScopedView.h>

namespace crucible {

// Forward declaration — sufficient for Cipher& references and
// safety::ScopedView<Cipher, ...> template specializations.
class Cipher;

// fixy-A2-014: cipher_state::Open lives at namespace scope (was
// previously declared inside Cipher.h alongside the class).  Lifting
// the tag here lets the OpenView alias resolve without pulling the
// full Cipher header.  The tag is a phantom — empty struct, no
// invariants of its own.
namespace cipher_state {
    struct Open {};
}

// fixy-A2-014: namespace-scope alias for the proof-of-Open scoped
// view.  Class Cipher re-exports this as `Cipher::OpenView` to keep
// existing call sites (`auto view = cipher.mint_open_view();`) compiling
// unchanged.  Forward-declared `Cipher` is sufficient — ScopedView's
// body holds only `Carrier const*` (see safety/ScopedView.h:92).
using CipherOpenView = safety::ScopedView<Cipher, cipher_state::Open>;

// fixy-A2-014: namespace-scope alias for the Met(X) row required to
// persist SessionEvents into Cipher's cold tier.  Equals Row<IO, Block>
// because the persistence path writes Cipher object files (IO) and
// fsyncs at boundaries (Block).  Class Cipher re-exports this as its
// `persist_session_events_required_row` member.
using CipherSessionEventPersistenceRow =
    ::crucible::effects::Row<
        ::crucible::effects::Effect::IO,
        ::crucible::effects::Effect::Block>;

}  // namespace crucible
