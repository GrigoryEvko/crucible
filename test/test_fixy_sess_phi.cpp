// Each predicate is invoked on a known-valid protocol and a known-invalid
// one. A new predicate earns both directions here, because a predicate
// that is accidentally always true passes a positive-only file.

#include <crucible/fixy/Sess.h>

#include <type_traits>

namespace fixy_sess = crucible::fixy::sess;

using CleanSend = fixy_sess::Send<int, fixy_sess::End>;
using CleanRecv = fixy_sess::Recv<int, fixy_sess::End>;
using CleanLoop = fixy_sess::Loop<fixy_sess::Send<int, fixy_sess::Continue>>;

static_assert(fixy_sess::phi_safe_v<CleanSend>, "phi_safe_v must accept Send<int, End>.");
static_assert(fixy_sess::phi_safe_v<CleanRecv>, "phi_safe_v must accept Recv<int, End>.");
static_assert(fixy_sess::phi_safe_v<CleanLoop>, "phi_safe_v must accept Loop<Send<int, Continue>>.");
static_assert(fixy_sess::phi_safe_v<fixy_sess::End>, "phi_safe_v must accept End.");

static_assert(!fixy_sess::phi_safe_v<fixy_sess::Continue>, "phi_safe_v must reject bare Continue (no enclosing Loop).");

static_assert(fixy_sess::phi_term_v<fixy_sess::End>, "phi_term_v must accept End (trivially terminates).");
static_assert(fixy_sess::phi_term_v<CleanSend>, "phi_term_v must accept Send<int, End> (no Loop → terminates).");
static_assert(!fixy_sess::phi_term_v<CleanLoop>, "phi_term_v must reject Loop<Send<int, Continue>> (unbounded loop).");

static_assert(!fixy_sess::phi_nterm_v<CleanSend>, "phi_nterm_v must reject Send<int, End> (no Loop → terminates).");
static_assert(!fixy_sess::phi_nterm_v<fixy_sess::End>, "phi_nterm_v must reject End (trivially terminates).");
static_assert(fixy_sess::phi_nterm_v<CleanLoop>,
              "phi_nterm_v must accept Loop<Send<int, Continue>> (well-formed + unbounded).");

static_assert(!(fixy_sess::phi_term_v<CleanSend> && fixy_sess::phi_nterm_v<CleanSend>),
              "phi_term and phi_nterm are mutually exclusive on every protocol.");
static_assert(!(fixy_sess::phi_term_v<CleanLoop> && fixy_sess::phi_nterm_v<CleanLoop>),
              "phi_term and phi_nterm are mutually exclusive on every protocol.");

static_assert(fixy_sess::is_well_formed_v<CleanSend>, "is_well_formed_v re-export must agree with phi_safe_v.");

static_assert(fixy_sess::is_terminal_state_v<fixy_sess::End>,
              "is_terminal_state_v re-export must accept End (terminal state).");
static_assert(!fixy_sess::is_terminal_state_v<CleanSend>,
              "is_terminal_state_v re-export must reject Send (non-terminal head).");

static_assert(fixy_sess::is_dual_v<CleanSend, CleanRecv>, "is_dual_v must witness Send<int, End> ⊥ Recv<int, End>.");

// Each predicate here is at least as strong as phi_safe. phi_live_pp is
// the strictest of them, being phi_live_plus and phi_term together.

static_assert(fixy_sess::phi_df_v<CleanSend>,
              "phi_df_v must accept Send<int, End> (no Select with unmatched branches).");
static_assert(fixy_sess::phi_live_v<CleanSend>, "phi_live_v must accept Send<int, End> (every action progresses).");
static_assert(fixy_sess::phi_live_plus_v<CleanSend>, "phi_live_plus_v must accept Send<int, End>.");
static_assert(fixy_sess::phi_live_pp_v<CleanSend>, "phi_live_pp_v must accept Send<int, End> "
                                                   "(phi_live_plus ∧ phi_term both hold).");

static_assert(!fixy_sess::phi_live_pp_v<CleanLoop>, "phi_live_pp_v must reject unbounded Loop (phi_term fails).");

int main() { return 0; }
