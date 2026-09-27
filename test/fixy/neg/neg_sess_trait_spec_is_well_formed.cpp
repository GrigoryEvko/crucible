// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_well_formed is an alias template over the well-formedness algebra
// of the registry, so no user specialization makes a send of the crash
// label well-formed.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

#include <type_traits>

namespace neg_sess_trait_spec_is_well_formed_types {
struct Peer {};
}  // namespace neg_sess_trait_spec_is_well_formed_types

namespace fixy::session {
template <typename LoopCtx>
struct is_well_formed<Send<Crash<neg_sess_trait_spec_is_well_formed_types::Peer>, End>, LoopCtx> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
