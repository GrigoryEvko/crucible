// Attacks on the choices of the protocol algebra: empty choices, one label
// in two branches, labels with one name, labels inside a payload, crash
// branches and positional choices.

#include "session_subtype_attack.h"

#include <meta>
#include <utility>

namespace test_session_subtype_attack_types {

// ── Registration attacks ─────────────────────────────────────────────
//
// The registry of the session layer is sealed in fixy/session/Protocol.h,
// so a combinator that a different header registers stops the build
// (neg_sess_combinator_outside_seal).  The coherence rules, which refuse
// a dual that is no involution or a variance that does not flip, have
// their witnesses in the self-test of foundation/algebra/Transition.h.

// ── Empty choices ────────────────────────────────────────────────────
//
// Under the branch rule an empty Select refines every Select, and a
// substitute of that type never sends.  Each route to one is refused:
// at the top, below a step, inside a loop, as a note with no branch, and
// as a choice of crash branches only.

static_assert(!s::is_subtype_sync_v<Select<>, Select<Send<A, End>>>);
static_assert(!s::is_subtype_async_v<Select<>, Select<Send<A, End>>, ring<4>>);
static_assert(!s::is_subtype_sync_v<Send<A, Select<>>, Send<A, Select<Send<B, End>>>>);
static_assert(!s::is_subtype_sync_v<Loop<Select<Send<A, Continue>, Select<>>>, Loop<Select<Send<A, Continue>>>>);
static_assert(!s::is_subtype_sync_v<Offer<Sender<Bob>>, Offer<Sender<Bob>>>);
static_assert(!s::is_subtype_sync_v<Offer<Recv<s::Crash<Bob>, End>>, Offer<Recv<s::Crash<Bob>, End>>>);
static_assert(s::is_subtype_sync_v<Select<Send<A, End>>, Select<Send<A, End>, Send<B, End>>>,
              "a choice of one branch is well-formed");

// ── Labels ───────────────────────────────────────────────────────────
//
// Two branches that name one label are refused, also through two
// aliases of the label.  Two labels that are distinct types with the
// same name are two labels: identity is by type, never by spelling.

using L0Again = L0;
namespace first {
struct Hello {};
}  // namespace first
namespace second {
struct Hello {};
}  // namespace second

static_assert(
    !s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, L0, int>, End>, Send<s::PeerMsg<Bob, L0Again, int>, End>>>,
    "one label through two aliases");
static_assert(std::meta::identifier_of(^^first::Hello) == std::meta::identifier_of(^^second::Hello));
static_assert(s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, first::Hello, int>, End>,
                                         Send<s::PeerMsg<Bob, second::Hello, int>, End>>>,
              "two labels with one name are two labels");
static_assert(!s::is_subtype_sync_v<Select<Send<s::PeerMsg<Bob, first::Hello, int>, End>>,
                                    Select<Send<s::PeerMsg<Bob, second::Hello, int>, End>>>,
              "a label is not replaced by a label with the same name");
// A label inside a payload is data, not a label.  Two branches that carry
// one label inside a payload do not clash, and their positions are their
// wire words.  A branch of that kind beside a branch that names a label
// key is refused, because the choice then has no single kind of wire word.
static_assert(s::is_well_formed_v<Select<Send<std::pair<s::PeerMsg<Bob, L0, int>, int>, End>,
                                         Send<std::pair<s::PeerMsg<Bob, L0, int>, long>, End>>>);
static_assert(!s::is_well_formed_v<
                  Select<Send<std::pair<s::PeerMsg<Bob, L0, int>, int>, End>, Send<s::PeerMsg<Bob, L0, int>, End>>>,
              "a branch with a label key beside a branch without one");

// Crash branches pair by payload.  A crash branch hidden under a wrapper
// or a loop at the head of a branch is still a crash branch.
using BobCrash = Recv<s::Crash<Bob>, End>;
using AliceCrash = Recv<s::Crash<Alice>, End>;
static_assert(!s::is_well_formed_v<Offer<s::VendorPinned<s::VendorBackend::NV, BobCrash>, Recv<A, End>>>,
              "a wrapped crash branch before a message branch");
static_assert(!s::is_well_formed_v<Offer<Loop<Recv<s::Crash<Bob>, Recv<A, Continue>>>, Recv<A, End>>>,
              "a crash branch under a loop before a message branch");
static_assert(!s::is_subtype_sync_v<Offer<Recv<A, End>, AliceCrash>, Offer<Recv<A, End>, BobCrash>>,
              "a crash branch for one peer does not stand for a crash branch for another");
static_assert(s::is_subtype_sync_v<Offer<Recv<A, End>, Recv<B, End>, BobCrash>, Offer<Recv<A, End>, BobCrash>>);

// In a positional choice a subtype cannot drop a branch that stands
// before another branch: the positions shift, and the pair at the old
// position differs.
static_assert(!s::is_subtype_sync_v<Loop<Select<Send<A, Continue>>>, Loop<Select<Send<B, End>, Send<A, Continue>>>>);

}  // namespace test_session_subtype_attack_types
