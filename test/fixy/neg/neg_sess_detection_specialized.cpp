// The detection of a crash ([Γ-⊙]) comes from detection_of of
// fixy/session/Semantics.h.  The rule was once the class template
// detection.  This file tries to give a role a detection of a peer that
// did not crash: it specializes that class template for a live peer.  The
// rule is a function, and no class template gives it, so the
// specialization does not compile.

#include <fixy/session/Semantics.h>
#include <foundation/Quarantine.h>

namespace probe {
struct Alice {};
struct Bob {};
using Empty = fixy::session::TypingContext<>;
using AwaitsCrash =
    fixy::session::Recv<fixy::session::PeerMsg<Alice, fixy::session::global::CrashLabel, void>, fixy::session::End>;
using Detected = fixy::session::global::Actions<fixy::session::global::DetectAction<Bob, Alice>>;
}  // namespace probe

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::session::config::detail::detection<probe::Empty, probe::Bob, probe::Alice, probe::AwaitsCrash> {
    using type = probe::Detected;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
