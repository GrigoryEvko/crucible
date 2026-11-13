// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit specializes the classification of a payload that
// holds a live endpoint, to let the payload travel.  The predicate is a
// concept over the facts of the payload walk, and a concept has no
// explicit specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Handle.h>
#include <foundation/Quarantine.h>

namespace neg_sess_seal_is_permission_classified_types {
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
struct Evil {
    ::fixy::session::SessionHandle<::fixy::session::Recv<int, ::fixy::session::End>, Wire> endpoint;
};
}  // namespace neg_sess_seal_is_permission_classified_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session {
template <>
inline constexpr bool is_permission_classified_v<neg_sess_seal_is_permission_classified_types::Evil> = true;
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
