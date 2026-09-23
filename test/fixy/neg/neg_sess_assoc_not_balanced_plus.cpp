// Pischke, Masters and Yoshida, equation (49).  Alice sends Ask to Bob
// while Tell, which she sent earlier, still waits in the same queue.
// Bob receives Tell where he expects Ask.  The context below is the
// projection of the type, entry for entry, and it is still unsafe.  The
// type is not balanced+, because an en-route message sits behind a
// transmission of the same pair, so association refuses it.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Ask {};
struct Tell {};

using OutOfOrder = g::Msg<Alice, Bob, Ask, int, g::EnRoute<Alice, Bob, Tell, int, g::End>>;

static_assert(g::is_global_well_formed_v<OutOfOrder>);
static_assert(!g::is_balanced_plus_v<OutOfOrder>);

constexpr int check_context() noexcept {
    s::ensure_associated<s::projected_context_t<OutOfOrder>, OutOfOrder>();
    return 0;
}

}  // namespace

int main() { return check_context(); }
