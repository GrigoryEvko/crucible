// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_checkpoint_compliant is an alias of the verdict of the compliance
// walk, so no user specialization makes a pair compliant whose labels
// disagree.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Checkpoint.h>

#include <type_traits>

namespace neg_sess_trait_spec_is_checkpoint_compliant_types {
using Left = ::fixy::session::Select<::fixy::session::Commit<::fixy::session::Send<int, ::fixy::session::End>>,
                                     ::fixy::session::Roll>;
using Right = ::fixy::session::Offer<::fixy::session::Roll,
                                     ::fixy::session::Commit<::fixy::session::Recv<int, ::fixy::session::End>>>;
}  // namespace neg_sess_trait_spec_is_checkpoint_compliant_types

namespace fixy::session {
template <>
struct is_checkpoint_compliant<CheckpointPair<neg_sess_trait_spec_is_checkpoint_compliant_types::Left,
                                              neg_sess_trait_spec_is_checkpoint_compliant_types::Right>>
    : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
