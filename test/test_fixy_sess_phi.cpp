// ── test_fixy_sess_phi — φ-predicate sentinel (FIXY-AUDIT-B5) ──────
//
// Positive-compile sentinel for the seven φ-predicate re-exports in
// fixy/Sess.h.  Each predicate is invoked on a known-valid protocol
// (`Send<int, End>`) and a known-invalid one (a bare `Continue` with
// no enclosing Loop).
//
// The 7 φ-predicates ship today as aliases over the strongest
// substrate predicate available:
//
//   phi_safe_v / phi_df_v / phi_live_v / phi_live_plus_v /
//   phi_live_pp_v   → is_well_formed_v
//   phi_term_v      → is_terminal_state_v
//   phi_nterm_v     → !is_terminal_state_v
//
// Substrate gaps tracked under Task #346 / #348 / #381 per
// CLAUDE.md L0 §Safety wrappers.

#include <crucible/fixy/Sess.h>

#include <type_traits>

namespace fixy_sess = crucible::fixy::sess;

// ─── Canonical valid protocol ─────────────────────────────────────

using CleanSend = fixy_sess::Send<int, fixy_sess::End>;
using CleanRecv = fixy_sess::Recv<int, fixy_sess::End>;
using CleanLoop = fixy_sess::Loop<fixy_sess::Send<int, fixy_sess::Continue>>;

// ─── 1. phi_safe — well-formedness ────────────────────────────────

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

// ─── 2. phi_df — deadlock-freedom (aliased to well-formedness) ────

static_assert(fixy_sess::phi_df_v<CleanSend>,
    "phi_df_v must accept Send<int, End>.");
static_assert(!fixy_sess::phi_df_v<fixy_sess::Continue>,
    "phi_df_v must reject bare Continue.");

// ─── 3. phi_term — terminal state ─────────────────────────────────

static_assert(fixy_sess::phi_term_v<fixy_sess::End>,
    "phi_term_v must accept End (terminal).");
static_assert(!fixy_sess::phi_term_v<CleanSend>,
    "phi_term_v must reject Send<int, End> (non-terminal head).");

// ─── 4. phi_nterm — non-terminal state ────────────────────────────

static_assert(fixy_sess::phi_nterm_v<CleanSend>,
    "phi_nterm_v must accept Send<int, End> (non-terminal head).");
static_assert(!fixy_sess::phi_nterm_v<fixy_sess::End>,
    "phi_nterm_v must reject End (terminal).");

// ─── 5. phi_live — liveness (aliased to well-formedness) ──────────

static_assert(fixy_sess::phi_live_v<CleanSend>,
    "phi_live_v must accept Send<int, End>.");
static_assert(fixy_sess::phi_live_v<CleanLoop>,
    "phi_live_v must accept Loop<Send<int, Continue>>.");

// ─── 6. phi_live_plus — positive liveness (aliased) ───────────────

static_assert(fixy_sess::phi_live_plus_v<CleanSend>,
    "phi_live_plus_v must accept Send<int, End>.");

// ─── 7. phi_live_pp — precise liveness (aliased) ──────────────────

static_assert(fixy_sess::phi_live_pp_v<CleanSend>,
    "phi_live_pp_v must accept Send<int, End>.");

// ─── Crash-stop family — re-exported substrate predicates ─────────

static_assert(fixy_sess::is_well_formed_v<CleanSend>,
    "is_well_formed_v re-export must agree with phi_safe_v.");

static_assert(fixy_sess::is_terminal_state_v<fixy_sess::End>,
    "is_terminal_state_v re-export must agree with phi_term_v.");

static_assert(fixy_sess::is_dual_v<CleanSend, CleanRecv>,
    "is_dual_v must witness Send<int, End> ⊥ Recv<int, End>.");

int main() { return 0; }
