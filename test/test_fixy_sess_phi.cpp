// ── test_fixy_sess_phi — φ-predicate sentinel (FIXY-AUDIT-B5) ──────
//
// Positive-compile sentinel for the φ-predicate re-exports in
// fixy/Sess.h.  Each predicate is invoked on a known-valid protocol
// and a known-invalid one.
//
// After fixy-CR-12, only the three substrate-honest predicates ship:
//
//   phi_safe_v   → is_well_formed_v       (FX §11.18 safe == well-formedness)
//   phi_term_v   → is_terminal_state_v    (terminal-state check)
//   phi_nterm_v  → !is_terminal_state_v   (complement of term)
//
// The previously-shipped phi_df_v / phi_live_v / phi_live_plus_v /
// phi_live_pp_v were removed because they aliased is_well_formed_v
// without proving the property their name claimed.  Substrate gaps
// for the missing predicates remain tracked under Task #346 / #348 /
// #381 per CLAUDE.md L0 §Safety wrappers; re-introduce when the
// substrate ships dedicated proofs.

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

// ─── 2. phi_term — terminal state ─────────────────────────────────

static_assert(fixy_sess::phi_term_v<fixy_sess::End>,
    "phi_term_v must accept End (terminal).");
static_assert(!fixy_sess::phi_term_v<CleanSend>,
    "phi_term_v must reject Send<int, End> (non-terminal head).");

// ─── 3. phi_nterm — non-terminal state ────────────────────────────

static_assert(fixy_sess::phi_nterm_v<CleanSend>,
    "phi_nterm_v must accept Send<int, End> (non-terminal head).");
static_assert(!fixy_sess::phi_nterm_v<fixy_sess::End>,
    "phi_nterm_v must reject End (terminal).");

// ─── Crash-stop family — re-exported substrate predicates ─────────

static_assert(fixy_sess::is_well_formed_v<CleanSend>,
    "is_well_formed_v re-export must agree with phi_safe_v.");

static_assert(fixy_sess::is_terminal_state_v<fixy_sess::End>,
    "is_terminal_state_v re-export must agree with phi_term_v.");

static_assert(fixy_sess::is_dual_v<CleanSend, CleanRecv>,
    "is_dual_v must witness Send<int, End> ⊥ Recv<int, End>.");

// ─── fixy-CR-12: deletion is the witness ──────────────────────────
//
// The four lying predicates (phi_df_v, phi_live_v, phi_live_plus_v,
// phi_live_pp_v) were deleted from fixy/Sess.h.  This TU compiles
// without referencing them — any reintroduction surfaces in code
// review against the doc-block in fixy/Sess.h, which now explicitly
// prohibits re-aliasing well-formedness under those names.

int main() { return 0; }
