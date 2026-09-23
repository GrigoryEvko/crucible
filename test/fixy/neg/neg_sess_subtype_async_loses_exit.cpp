// The subtype sends before it receives, which only the asynchronous
// relation admits, and it never picks the exit that the supertype offers
// after each receive.  The bounded search alone proves the pair.  Exit
// preservation over its derivation refuses it.

#include <fixy/session/Subtype.h>

#include <cstddef>

namespace {

namespace s = ::fixy::session;

struct Job {};
struct Tick {};
struct StopCmd {};
using Patient = s::Loop<s::Recv<Tick, s::Select<s::Send<Job, s::Continue>, s::Send<StopCmd, s::End>>>>;
using Eager = s::Loop<s::Select<s::Send<Job, s::Recv<Tick, s::Continue>>>>;
struct TwoSlots {
    static constexpr std::size_t channel_capacity = 2;
};

}  // namespace

int main() {
    s::assert_subtype_async<Eager, Patient, TwoSlots>();
    return 0;
}
