// Rule TS-Rll2 of Mezzina, Tiezzi and Yoshida (LMCS 2025, Fig. 11): the
// follower of a commit may not roll back, because its checkpoint was
// imposed by the other side.

#include <fixy/session/Checkpoint.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

using Follower = s::Offer<s::Commit<s::Select<s::End, s::Roll>>>;
using Leader = s::Select<s::Commit<s::Offer<s::End, s::Roll>>>;

int main() {
    auto handle = s::mint_checkpoint_session<Follower, Leader>(Wire{});
    (void)handle;
    return 0;
}
