// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A specialization of the inner loop context would send the Continue of a
// receiving loop to a sending body.  The inner loop context is an alias
// over a function that knows each form of a loop context, so the
// specialization does not compile.
//
// Expected diagnostic: specialization of an alias template.
#include <fixy/session/Handle.h>

namespace neg_sess_loop_ctx_inner_specialized_types {
using Receives = ::fixy::session::Loop<::fixy::session::Recv<int, ::fixy::session::Continue>>;
using Sends = ::fixy::session::Loop<::fixy::session::Send<int, ::fixy::session::Continue>>;
}  // namespace neg_sess_loop_ctx_inner_specialized_types

template <>
struct fixy::session::session_loop_ctx_inner_t<neg_sess_loop_ctx_inner_specialized_types::Receives> {
    using type = neg_sess_loop_ctx_inner_specialized_types::Sends;
};

int main() { return 0; }
