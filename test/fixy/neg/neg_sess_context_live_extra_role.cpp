// A context holds a role that the global type does not name, and that
// role still waits for a message.  No role of the protocol sends to it,
// so it waits for ever.  A role outside the global type must have
// finished (Pischke and Yoshida, Top-down = Bottom-up, Definition 6.9).
//
// The global type is live by construction and the roles it names carry
// their projections, so the refusal comes from the extra-role clause.

#include <fixy/session/Liveness.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_context_live_extra_role_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Stranger {};
struct Ask {};

using Question = g::Msg<Alice, Bob, Ask, int, g::End>;

using WithStranger = s::TypingContext<
    s::RoleState<Alice, s::OutQueue<>, typename s::project_t<Question, Alice>::local>,
    s::RoleState<Bob, s::OutQueue<>, typename s::project_t<Question, Bob>::local>,
    s::RoleState<Stranger, s::OutQueue<>, s::Recv<s::PeerMsg<Alice, Ask, int>, s::End>>>;

constexpr int require_live() noexcept {
    s::ensure_context_live<WithStranger, Question>();
    return 0;
}

}  // namespace neg_sess_context_live_extra_role_types

using namespace neg_sess_context_live_extra_role_types;

int main() { return require_live(); }
