// Branch-asymmetric composition appends Q to ONE branch of a choice.
// It is meaningless on a protocol with no choice on its spine: there is
// no branch to single out, and silently composing into every End would
// be uniform composition wearing the asymmetric name.
//
// compose_at_branch is the CURRENT entry point, and this fixture
// instantiates it on a spine that walks Send -> Recv -> End without
// ever reaching a Select or an Offer.  The protocol is well-formed, so
// the refusal comes from the composition clause and not from a
// malformed operand.
//
// The instantiation is forced with sizeof on the trait rather than by
// naming compose_at_branch_t.  Asking for ::type would reject twice —
// once from the clause below and once because the failed
// specialization has no ::type to name — and a fixture that rejects
// for two reasons cannot say which one it was testing.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

struct Msg {};
struct Ack {};

// A choice-free spine.  Every position is Send, Recv or End.
using NoChoice = s::Send<Msg, s::Recv<Ack, s::End>>;
using Tail = s::Send<Ack, s::End>;

static_assert(s::is_well_formed_v<NoChoice>);
static_assert(!s::is_select_v<NoChoice> && !s::is_offer_v<NoChoice>);

}  // namespace

int main() { return sizeof(s::compose_at_branch<NoChoice, 0, Tail>) == 0 ? 1 : 0; }
