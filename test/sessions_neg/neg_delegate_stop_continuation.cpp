// GAPS-048: Delegate<Stop, K> is a crashed handoff, not a path into
// K.  The handle operation must be rejected at compile time so callers
// cannot recover K's continuation-side authority from an already
// stopped delegated endpoint.

#include <crucible/sessions/SessionDelegate.h>

#include <utility>

using namespace crucible::safety::proto;

namespace {

struct Wire {};
struct Ack {};

using BadCarrier = Delegate<Stop, Recv<Ack, End>>;

void transport(Wire&, Wire&&) noexcept {}

[[maybe_unused]] void probe() {
    auto carrier = mint_session_handle<BadCarrier>(Wire{});
    auto stopped = mint_session_handle<Stop>(Wire{});

    (void)std::move(carrier).delegate(std::move(stopped), transport);
}

}  // namespace
