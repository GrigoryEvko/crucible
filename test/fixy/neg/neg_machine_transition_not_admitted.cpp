// A transition is admitted only by an edge declared in the namespace the
// machine names.  The relation here holds Disconnected -> Connected and
// nothing else, so the transition back to Disconnected fails the
// MachineTransition constraint on transition_to, and the satisfaction
// notes name the concept and the relation.

#include <fixy/Machine.h>

#include <foundation/diag/FailClosed.h>

#include <utility>

namespace {
struct Disconnected {};
struct Connected {};

namespace link_edges {
inline constexpr ::foundation::fail_closed::edge<Disconnected, Connected> connect{};
}  // namespace link_edges
}  // namespace

int main() {
    auto up = fixy::mint_machine<Disconnected, ^^link_edges>();
    auto linked = fixy::transition_to(std::move(up), Connected{});
    [[maybe_unused]] auto rolled_back = fixy::transition_to(std::move(linked), Disconnected{});
    return 0;
}
