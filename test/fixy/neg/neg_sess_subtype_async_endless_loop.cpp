// The loop that never stops against the loop that can stop, through the
// asynchronous relation.  No anticipation helps: the subtype never sends
// the stop, and the relation refuses the pair as the synchronous one does.

#include <fixy/session/Subtype.h>

#include <cstddef>

namespace {

namespace s = ::fixy::session;

struct Job {};
struct StopCmd {};
using Stopping = s::Loop<s::Select<s::Send<Job, s::Continue>, s::Send<StopCmd, s::End>>>;
using Endless = s::Loop<s::Select<s::Send<Job, s::Continue>>>;
struct FourSlots {
    static constexpr std::size_t channel_capacity = 4;
};

}  // namespace

int main() {
    s::assert_subtype_async<Endless, Stopping, FourSlots>();
    return 0;
}
