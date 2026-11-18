// The probe of the compile cost of the permission flow walk of
// fixy/session/Admission.h (test/compile_cost/check_ceilings.py).
//
// The protocol is a ladder of 16 choices.  The two branches of each choice
// send one message and go on to the same next choice, so the protocol has 16
// distinct nodes and 2^16 paths.  The walk keeps the answer of each node, set
// and loop set, so it visits each distinct node one time.  A walk with no
// such answers visits each path.  The code before those answers stops the
// compile at the limit of constant evaluation after 76.6 G instructions,
// more than twelve times the count.
//
// measured: 6.190 G instructions

#include <fixy/session/Admission.h>
#include <foundation/permissions/Permission.h>

namespace s = ::fixy::session;

namespace {

template <int Index>
struct Word {
    int value = 0;
};

template <int Level>
struct shared_ladder {
    using next = typename shared_ladder<Level - 1>::type;
    using type = s::Select<s::Send<Word<2 * Level>, next>, s::Send<Word<(2 * Level) + 1>, next>>;
};
template <>
struct shared_ladder<0> {
    using type = s::End;
};

static_assert(s::PermissionFlowCloses<shared_ladder<16>::type, ::foundation::permissions::EmptyPermSet>);

}  // namespace
