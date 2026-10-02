// The contract rule: each P2900 contract specifier is an error, in each mode.
// The test compiles this file with -fcontracts and with a directory that holds
// Outside.h, a header outside the root that has a specifier too.  Each line
// with a specifier has a comment that names it.  A template and its
// instantiation share a specifier, so the specifier gives one error.

#include "Outside.h"
#include "fixy/Contracted.h"

namespace probe {

int plain_pre(int value) pre(value > 0);  // pre: a declaration
int plain_post(int value) post(result : result > 0) { return value; }  // post: a definition
int two_specifiers(int value) pre(value > 0) post(result : result > 0);  // pre and post
template <class T>
int template_pre(T value) pre(value > 0);  // pre: a template with no definition

template <class T>
struct Box {
    int member(int value) pre(value > 0);  // pre: a member of a class template
    int defined(int value) post(result : result > 0) { return value; }  // post: a defined member
    template <class U>
    int member_template(U value) pre(value > 0);  // pre: a member template
    struct Inner {
        int nested(int value) pre(value > 0);  // pre: a member of a nested class
    };
    friend int hidden(Box, int value) pre(value > 0) { return value; }  // pre: a hidden friend
    template <class U>
    friend int befriended(Box, U value) pre(value > 0);  // pre: a friend template
};

template <class T>
struct Box<T*> {
    int partial(int value) pre(value > 0);  // pre: a member of a partial specialization
};

struct Plain {
    template <class U>
    int member_template(U value) pre(value > 0);  // pre: a member template
    template <class U>
    friend int plain_friend(Plain, U value) pre(value > 0);  // pre: a friend template
};

inline auto lambda = [](int value) pre(value > 0) { return value; };  // pre: a lambda
inline auto generic = [](auto value) pre(value > 0) { return value; };  // pre: a generic lambda

void local_declarations() {
    int local(int value) pre(value > 0);  // pre: a local declaration
    struct Local {
        int member(int value) pre(value > 0);  // pre: a member of a local class
    };
}

template <>
int template_pre<long>(long value) pre(value > 0);  // pre: an explicit specialization

#define PROBE_PRE(condition) pre(condition)  // pre: the spelling in a macro
int through_macro(int value) PROBE_PRE(value > 0);

int instantiated = Box<int>{}.defined(1) + template_pre(1);

// No specifier: a name, a member and an assertion statement.
int pre = 1;
struct Names {
    int post = 0;
};
int asserted(int value) {
    contract_assert(value > 0);
    return value;
}
#include <foundation/Quarantine.h>
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test needs the specifier")
int opted(int value) pre(value > 0);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace probe
