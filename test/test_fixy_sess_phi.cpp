// ── test_fixy_sess_phi — φ-predicate sentinel (FIXY-AUDIT-B5) ──────
//
// Positive-compile sentinel for the φ-predicate re-exports in
// fixy/Sess.h.  Each predicate is invoked on a known-valid protocol
// and a known-invalid one.
//
// FIXY-V-069 RESTORED the full 7-predicate FX §11.18 family on top of
// honest substrate predicates from `sessions/SessionPhi.h`:
//
//   phi_safe_v        — well-formedness  (FX §11.18 safe)
//   phi_df_v          — deadlock-freedom (strengthens safe)
//   phi_term_v        — TERMINATION      ("does this protocol terminate?")
//   phi_nterm_v       — NON-termination  (well-formed AND has unbounded loop)
//   phi_live_v        — liveness         (Honda 1998 §3.2)
//   phi_live_plus_v   — positive liveness
//   phi_live_pp_v     — precise liveness (phi_live_plus ∧ phi_term)
//
// phi_term and phi_nterm are MUTUALLY EXCLUSIVE — no protocol both
// terminates and runs forever.  A Send<int, End> trivially terminates
// (no Loop reachable), so phi_term_v<Send<int, End>> == true.  A
// Loop<Send<int, Continue>> never escapes its body, so
// phi_nterm_v<Loop<Send<int, Continue>>> == true.
//
// The pre-V-069 surface aliased phi_term to is_terminal_state_v which
// asked the WRONG question ("is this state terminal?" — true only for
// End) rather than "does this protocol terminate?".  Refer to
// `sessions/SessionPhi.h:436` for the substrate definition that this
// re-export pins.

#include <crucible/fixy/Sess.h>

#include <type_traits>

namespace fixy_sess = crucible::fixy::sess;

// ─── Canonical valid protocol ─────────────────────────────────────

using CleanSend = fixy_sess::Send<int, fixy_sess::End>;
using CleanRecv = fixy_sess::Recv<int, fixy_sess::End>;
using CleanLoop = fixy_sess::Loop<fixy_sess::Send<int, fixy_sess::Continue>>;

// ─── 1. phi_safe — well-formedness (FX §11.18) ────────────────────

static_assert(fixy_sess::phi_safe_v<CleanSend>,
    "phi_safe_v must accept Send<int, End>.");
static_assert(fixy_sess::phi_safe_v<CleanRecv>,
    "phi_safe_v must accept Recv<int, End>.");
static_assert(fixy_sess::phi_safe_v<CleanLoop>,
    "phi_safe_v must accept Loop<Send<int, Continue>>.");
static_assert(fixy_sess::phi_safe_v<fixy_sess::End>,
    "phi_safe_v must accept End.");

// Bare Continue (no enclosing Loop) is not well-formed.
static_assert(!fixy_sess::phi_safe_v<fixy_sess::Continue>,
    "phi_safe_v must reject bare Continue (no enclosing Loop).");

// ─── 2. phi_term — protocol termination (FX §11.18, V-069) ────────

static_assert(fixy_sess::phi_term_v<fixy_sess::End>,
    "phi_term_v must accept End (trivially terminates).");
static_assert(fixy_sess::phi_term_v<CleanSend>,
    "phi_term_v must accept Send<int, End> (no Loop → terminates).");
static_assert(!fixy_sess::phi_term_v<CleanLoop>,
    "phi_term_v must reject Loop<Send<int, Continue>> (unbounded loop).");

// ─── 3. phi_nterm — protocol non-termination (V-069) ──────────────

static_assert(!fixy_sess::phi_nterm_v<CleanSend>,
    "phi_nterm_v must reject Send<int, End> (no Loop → terminates).");
static_assert(!fixy_sess::phi_nterm_v<fixy_sess::End>,
    "phi_nterm_v must reject End (trivially terminates).");
static_assert(fixy_sess::phi_nterm_v<CleanLoop>,
    "phi_nterm_v must accept Loop<Send<int, Continue>> (well-formed + unbounded).");

// ─── 3b. Mutual exclusion — phi_term ⊥ phi_nterm ──────────────────

static_assert(!(fixy_sess::phi_term_v<CleanSend>
              && fixy_sess::phi_nterm_v<CleanSend>),
    "phi_term and phi_nterm are mutually exclusive on every protocol.");
static_assert(!(fixy_sess::phi_term_v<CleanLoop>
              && fixy_sess::phi_nterm_v<CleanLoop>),
    "phi_term and phi_nterm are mutually exclusive on every protocol.");

// ─── Crash-stop family — re-exported substrate predicates ─────────

static_assert(fixy_sess::is_well_formed_v<CleanSend>,
    "is_well_formed_v re-export must agree with phi_safe_v.");

static_assert(fixy_sess::is_terminal_state_v<fixy_sess::End>,
    "is_terminal_state_v re-export must accept End (terminal state).");
static_assert(!fixy_sess::is_terminal_state_v<CleanSend>,
    "is_terminal_state_v re-export must reject Send (non-terminal head).");

static_assert(fixy_sess::is_dual_v<CleanSend, CleanRecv>,
    "is_dual_v must witness Send<int, End> ⊥ Recv<int, End>.");

// ─── 4. Full FX §11.18 family — phi_df / phi_live / phi_live_plus / phi_live_pp ──
//
// V-069 restored these four aliases over substrate-honest predicates.
// Each is at-least-as-strong as phi_safe; phi_live_pp is the strictest
// (= phi_live_plus ∧ phi_term).

static_assert(fixy_sess::phi_df_v<CleanSend>,
    "phi_df_v must accept Send<int, End> (no Select with unmatched branches).");
static_assert(fixy_sess::phi_live_v<CleanSend>,
    "phi_live_v must accept Send<int, End> (every action progresses).");
static_assert(fixy_sess::phi_live_plus_v<CleanSend>,
    "phi_live_plus_v must accept Send<int, End>.");
static_assert(fixy_sess::phi_live_pp_v<CleanSend>,
    "phi_live_pp_v must accept Send<int, End> "
    "(phi_live_plus ∧ phi_term both hold).");

// Loop<Send<int, Continue>> fails phi_term (non-terminating) → fails
// phi_live_pp via the conjunctive definition.
static_assert(!fixy_sess::phi_live_pp_v<CleanLoop>,
    "phi_live_pp_v must reject unbounded Loop (phi_term fails).");

int main() { return 0; }
