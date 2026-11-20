// The admission of a local choice reads the loop context through each of
// its known forms.  A type of no known form gives no answer: the walk does
// not count it as a loop with no peer, and the build stops.

#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace neg_sess_local_choice_unknown_loop_context_types {
struct Counter {
    static constexpr s::Network session_network = s::Network::Local;
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
struct NoLoopContext {};
}  // namespace neg_sess_local_choice_unknown_loop_context_types

using namespace neg_sess_local_choice_unknown_loop_context_types;

using Choice = s::Select<s::Send<int, s::End>, s::End>;

int main() {
    constexpr bool is_admitted = s::AdmitsLocalChoice<Counter, Choice, NoLoopContext>;
    static_cast<void>(is_admitted);
    return 0;
}
