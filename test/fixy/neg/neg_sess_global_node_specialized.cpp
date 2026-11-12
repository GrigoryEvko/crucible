// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An explicit specialization of a global Branch gives its next member a
// continuation that its template argument does not give.  The projection
// of this transmission reads the template arguments.  continuation_of of
// fixy/session/Global.h reads the members label and next, and the walks
// of an en-route node call it, so such a walk would be wrong with no error.
// The projection refuses the node whose member disagrees with its argument.
//
// Expected diagnostic: Projection_Specialized_Global_Node.
#include <fixy/session/Projection.h>

namespace neg_sess_global_node_specialized_types {
struct Alice {};
struct Bob {};
struct Hello {};
}  // namespace neg_sess_global_node_specialized_types

template <>
struct fixy::session::global::Branch<neg_sess_global_node_specialized_types::Hello, int, fixy::session::global::End> {
    using label = neg_sess_global_node_specialized_types::Hello;
    using payload = int;
    using next = fixy::session::global::Var;
};

int main() {
    namespace g = ::fixy::session::global;
    using namespace neg_sess_global_node_specialized_types;
    using Hi = g::Msg<Alice, Bob, Hello, int, g::End>;
    using Projected = ::fixy::session::project_t<Hi, Bob>;
    [[maybe_unused]] Projected* projected = nullptr;
    return 0;
}
