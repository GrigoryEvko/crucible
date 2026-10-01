// The known-limitation ledger of the subtype attacks, and the notes that
// show where each attack that it held now fails.

#include "session_subtype_attack.h"

#include <array>
#include <string_view>
#include <type_traits>

namespace test_session_subtype_attack_types {

// ── The known-limitation ledger ──────────────────────────────────────
//
// Each entry is an attack that succeeds, with the condition it breaks.
// A pin after each entry asserts that the attack still succeeds, so a
// repair fails the pin, and the entry and its pin go together.  The
// ledger is empty: each attack that it held now fails, and the notes
// below show where.

struct limitation {
    std::string_view attack;
    std::string_view breaks;
};

inline constexpr std::array<limitation, 0> known_limitations{};
static_assert(known_limitations.empty(), "the ledger only shrinks: an attack that succeeds again needs an entry");

// Composition with a bare Continue was an entry here: it turned each End
// of a loop into a loop-back, and the result could never end.  That is
// the capture of a free Continue by a Loop of the prefix, and compose_t
// and compose_at_branch_t refuse it (the fixtures
// neg_sess_compose_captures_continue, neg_sess_compose_captures_under_nested_loop
// and neg_sess_compose_at_branch_captures_continue).  A closed suffix
// composes, and the exit of the loop stays an exit.
using WithExit = Loop<Select<Send<A, Continue>, End>>;
static_assert(std::is_same_v<s::compose_t<WithExit, Send<B, End>>, Loop<Select<Send<A, Continue>, Send<B, End>>>>);
static_assert(std::is_same_v<s::compose_at_branch_t<WithExit, 1, Loop<Send<B, Continue>>>,
                             Loop<Select<Send<A, Continue>, Loop<Send<B, Continue>>>>>);
static_assert(std::is_same_v<s::compose_t<Send<A, End>, Continue>, Send<A, Continue>>,
              "with no Loop above the End, the Continue stays free, and the Loop around the result binds it");

// Subtyping that removed the only exit of a loop was an entry here.  The
// synchronous relation now keeps each exit that the supertype offers
// (fair subtyping, Padovani and Zavattaro, TOPLAS 2026), and the
// asynchronous relation reads the same condition on its derivation.  A
// stream still refines a stream, because the supertype never ends.
using ExitingLoop = Loop<Select<Send<A, Continue>, Send<B, End>>>;
using EndlessLoop = Loop<Select<Send<A, Continue>>>;
static_assert(s::subtype_mismatch_v<EndlessLoop, ExitingLoop> == tr::mismatch::loses_termination);
static_assert(!s::is_subtype_async_v<EndlessLoop, ExitingLoop, ring<4>>);
static_assert(s::is_subtype_sync_v<EndlessLoop, Loop<Select<Send<A, Continue>, Send<B, Continue>>>>);

// The same loss behind an anticipation, which only the asynchronous
// relation admits: the subtype sends before it receives and never picks
// the exit.  The version that keeps the exit still holds.
using PatientLoop = Loop<Recv<B, Select<Send<A, Continue>, Send<C, End>>>>;
using EagerEndless = Loop<Select<Send<A, Recv<B, Continue>>>>;
using EagerExiting = Loop<Select<Send<A, Recv<B, Continue>>, Send<C, Recv<B, End>>>>;
static_assert(!s::is_subtype_async_v<EagerEndless, PatientLoop, ring<2>>);
static_assert(s::is_subtype_async_v<EagerExiting, PatientLoop, ring<2>>
              && !s::is_subtype_sync_v<EagerExiting, PatientLoop>);

// The loss at an inner position, while the root can still end.
using Stream = Loop<Send<C, Continue>>;
using TwoExits = Select<Send<A, End>, Send<B, Select<Send<A, Stream>, Send<C, End>>>>;
using InnerLoss = Select<Send<A, End>, Send<B, Select<Send<A, Stream>>>>;
static_assert(s::subtype_mismatch_v<InnerLoss, TwoExits> == tr::mismatch::loses_termination);
static_assert(!s::is_subtype_async_v<InnerLoss, TwoExits, ring<2>>);

// The Sender note under duality was an entry here: the dual of a noted
// Offer dropped the note, so a server that spoke Select was compatible,
// as a server, with a client whose Offer named the wrong role, and the
// client side of the same check refused.  A choice and its dual now name
// the same note template, so duality keeps the note and is an
// involution, and compatibility answers the same from either side.  The
// fixtures neg_sess_compatible_server_wrong_role and
// neg_sess_compatible_client_wrong_role are the attack from each side.
using NotedClient = Offer<Sender<Bob>, Recv<A, End>>;
using PlainServer = Select<Send<A, End>>;
using NotedServer = Select<Sender<Bob>, Send<A, End>>;
static_assert(!s::CompatibleServer<PlainServer, NotedClient> && !s::CompatibleClient<NotedClient, PlainServer>);
static_assert(s::CompatibleServer<NotedServer, NotedClient> && s::CompatibleClient<NotedClient, NotedServer>);

// The capacity of the asynchronous check was an entry here: the caller
// stated a number, and nothing tied it to the channel the session runs
// on.  The check now reads the capacity from the channel type, and a
// number in its place does not compile (neg_sess_subtype_async_number_capacity).
// mint_forked_async_channel checks at the channel type of the Resources
// that it runs.  The family in test_session_subtype_attack.cpp shows the
// check and the runs agree at each capacity, and main runs the pair that
// ring<4> admits on a channel of one: that run deadlocks, and the check at
// ring<1> refuses the pair.
static_assert(s::is_subtype_async_v<early<4>, late<4>, ring<4>> && !s::is_subtype_async_v<early<4>, late<4>, ring<1>>);

// A payload rule registered after fixy/session/Protocol.h stops the build
// at the next read of a payload rule, because a seal counts the rules of
// the registry.  The fixtures neg_sess_payload_rule_after_seal and
// neg_sess_payload_rule_before_seal are that attack.

// The implication chain was an entry here.  The implication relation of
// fixy/Refined.h is transitive.  The payload order now joins the two ends
// of a chain, and the order stays one way.
using Narrow = ::fixy::Refined<::fixy::in_range<5, 9>, int>;
using Middle = ::fixy::Refined<::fixy::bounded_above<9>, int>;
using Wide = ::fixy::Refined<::fixy::bounded_above<20>, int>;
static_assert(s::is_subtype_sync_v<Send<Narrow, End>, Send<Middle, End>>
              && s::is_subtype_sync_v<Send<Middle, End>, Send<Wide, End>>
              && s::is_subtype_sync_v<Send<Narrow, End>, Send<Wide, End>>);
static_assert(!s::is_subtype_sync_v<Send<Wide, End>, Send<Narrow, End>>
              && s::is_subtype_sync_v<Recv<Wide, End>, Recv<Narrow, End>>
              && !s::is_subtype_sync_v<Recv<Narrow, End>, Recv<Wide, End>>);

}  // namespace test_session_subtype_attack_types
