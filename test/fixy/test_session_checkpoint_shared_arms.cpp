// The checkpoint check of fixy/session/Checkpoint.h on a protocol with shared
// arms, and a session over it.
//
// Each pair below is a row of 40 choices.  Each choice has 32 labels, and
// each label leads to the next choice.  The model of the pair has 41
// configurations, and the protocol tree has 32 to the power 40 nodes.  The
// check of the shape keeps one answer for each node, so the row is
// compliant.  A check that visits the protocol as a tree stops at the bound
// of the work and gives TooManyConfigurations.
//
// Each choice of a row is a named state (Named of fixy/session/Protocol.h),
// so a label names the next choice by its name.  The spelled type of each
// choice, its display text and the debug name of each type that names it
// then hold 32 names, and not 32 to the power 40 parts.  The unit declares
// the rows, mints a checkpoint session and a test channel over them, and
// walks the first choices of each, in a compile with -g.  The rules step-cost
// and step-memory judge the compile of this unit, so a walk or a name that
// spells a row as a tree stops the gate.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Checkpoint.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>

#include <meta>
#include <utility>

namespace s = ::fixy::session;
namespace tr = ::fixy::session::transition;

namespace {

using ::fixy::check::pass;

inline constexpr int kRowLength = 40;
inline constexpr int kLabels = 32;

// A choice of the family `choice` with `labels` branches, each of them
// `next`.  Complexity: linear in the labels.
consteval std::meta::info choice_of(std::meta::info choice, std::meta::info next, int labels) {
    tr::stack<std::meta::info> branches{};
    for (int label = 0; label < labels; ++label)
        tr::push(branches, next);
    return std::meta::substitute(choice, branches);
}

// The choice at place Step of the row of the side that selects, with Labels
// labels.  Each label leads to the named choice at the next place, and the
// row ends at End.
template <int Step, int Labels>
struct SelectRow {
    using protocol = [:choice_of(^^s::Select, ^^s::Named<SelectRow<Step + 1, Labels>>, Labels):];
};
template <int Labels>
struct SelectRow<kRowLength, Labels> {
    using protocol = s::End;
};

// The same row for the side that receives each label.
template <int Step, int Labels>
struct OfferRow {
    using protocol = [:choice_of(^^s::Offer, ^^s::Named<OfferRow<Step + 1, Labels>>, Labels):];
};
template <int Labels>
struct OfferRow<kRowLength, Labels> {
    using protocol = s::End;
};

using Decider = s::Named<SelectRow<0, kLabels>>;
using Follower = s::Named<OfferRow<0, kLabels>>;

// The sessions run over rows of 40 choices of 4 labels.  The tree of such a
// row has 4 to the power 40 paths, and a mint over an unnamed row fails at 10
// choices of 4 labels, at the loop limit of a constant evaluation.  The work
// of a mint grows with the edges of the protocol, so the narrow row keeps the
// compile of the unit small.
inline constexpr int kSessionLabels = 4;
using NarrowDecider = s::Named<SelectRow<0, kSessionLabels>>;
using NarrowFollower = s::Named<OfferRow<0, kSessionLabels>>;

// The row ends at End on the two sides, so each label exchange reaches the
// next pair of choices and the last one reaches End against End.  The dual
// of the row of the decider faces it too.
static_assert(s::checkpoint_verdict_of(^^Decider, ^^Follower) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^NarrowDecider, ^^s::dual_of_t<NarrowDecider>)
              == s::CheckpointVerdict::Compliant);

// The check keeps an answer for a node at one place.  Roll is legal as a
// branch of a choice and not after a send, so the answer of the first Roll
// does not answer the second one.
static_assert(s::checkpoint_verdict_of(^^s::Select<s::Roll, s::Send<int, s::Roll>>,
                                       ^^s::Offer<s::Roll, s::Recv<int, s::Roll>>)
              == s::CheckpointVerdict::NotCheckpointShaped);

// Two labels of one choice are one edge only when the labels and their
// continuations are equal.  Equal labels that are not next to each other and
// lead to one continuation are compliant.  Equal labels that lead to two
// different continuations are ambiguous, also when each continuation by
// itself would be compliant.
using Again = s::Send<int, s::Continue>;
using AgainLater = s::Loop<s::Select<Again, s::End, Again>>;
using AgainLaterFollower = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::End, s::Recv<int, s::Continue>>>;
static_assert(s::checkpoint_verdict_of(^^AgainLater, ^^AgainLaterFollower) == s::CheckpointVerdict::Compliant);
using InnerAgain = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::End, s::Recv<int, s::Continue>>>;
using AgainInner = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::End, s::Recv<int, InnerAgain>>>;
static_assert(s::checkpoint_verdict_of(^^AgainLater, ^^AgainInner) == s::CheckpointVerdict::AmbiguousLabels);

// The display text of a choice of a row holds one name for each label, and
// the dual keeps the names.  A text that spelled a row in full would be
// longer than the limit of a constant evaluation.
inline constexpr std::size_t kTextBound = 256 * kLabels;
static_assert(s::type_display_text_v<SelectRow<0, kLabels>::protocol>.size() < kTextBound);
static_assert(s::type_display_text_v<s::named_state_protocol_t<s::dual_of_t<NarrowDecider>>>.size() < kTextBound);
static_assert(std::is_same_v<s::dual_of_t<s::dual_of_t<NarrowDecider>>, NarrowDecider>);
static_assert(s::is_dual_v<NarrowDecider, s::dual_of_t<NarrowDecider>>);

// A carrier with no peer, so a choice of the checkpoint session puts no
// label on a wire.
struct LocalLog {
    static constexpr s::Network session_network = s::Network::Local;
    int words = 0;
};

struct LeftWire {
    int words = 0;
};
struct RightWire {
    int words = 0;
};

// The walks take the first choices of a row, and the test of named states,
// test_session_named_state, walks a named state to End.
inline constexpr int kWalkedChoices = 3;

// Selects label Step at each choice, from place Step to place
// kWalkedChoices, and then detaches the handle.
template <int Step, typename Handle>
void walk_checkpoint_row(Handle handle) {
    if constexpr (Step == kWalkedChoices) {
        std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    } else {
        walk_checkpoint_row<Step + 1>(std::move(handle).template select<Step>(s::no_label));
    }
}

// The two ends of a test channel take the same label at each choice.
template <int Step, typename LeftHandle, typename RightHandle>
void walk_channel_row(::fixy::TestRunnerCtx const& test, LeftHandle left, RightHandle right) {
    if constexpr (Step == kWalkedChoices) {
        std::move(left).detach(s::detach_reason::TestInstrumentation{});
        std::move(right).detach(s::detach_reason::TestInstrumentation{});
    } else {
        walk_channel_row<Step + 1>(test, std::move(left).template select_local<Step>(test),
                                   std::move(right).template pick_local<Step>(test));
    }
}

// A checkpoint session over the row of the decider.  The watch names its
// record with the display text of the protocol.
void checkpoint_session_walks_the_row(::fixy::TestRunnerCtx const& test) {
    walk_checkpoint_row<0>(s::mint_checkpoint_session<NarrowDecider, NarrowFollower>(test, LocalLog{}));
    pass(test, "  checkpoint_session_walks_the_row: PASSED\n");
}

// A test channel over the row of the decider and its dual.  The handles of
// the right end stand at the duals of the named states.
void channel_walks_the_row(::fixy::TestRunnerCtx const& test) {
    auto [left, right] = s::mint_test_channel<NarrowDecider>(test, LeftWire{}, RightWire{});
    walk_channel_row<0>(test, std::move(left), std::move(right));
    pass(test, "  channel_walks_the_row: PASSED\n");
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    checkpoint_session_walks_the_row(test);
    channel_walks_the_row(test);
    return 0;
}
