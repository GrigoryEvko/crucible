// The bound of the work of the checkpoint walk of fixy/session/Checkpoint.h.
//
// Each pair below met the operation limit of a constant evaluation before
// the walk counted its work.  The walk now stops at the default bound,
// checkpoint_work_bound, and gives TooManyConfigurations: for a long chain,
// with one edge for each configuration, and for a row of wide choices, with
// 150 edges for each configuration.  The pairs stand in a test of their own,
// so the compile of one unit stays small.

#include <fixy/session/Checkpoint.h>

namespace s = ::fixy::session;

namespace {

template <int Index>
struct Word {
    int value = 0;
};

// A loop over eight branches of a hundred sends each: about 800
// configurations, with one edge each.
inline constexpr int kLongChain = 100;

template <int Branch, int Step>
struct long_sends {
    using type = s::Send<Word<(Branch * kLongChain) + Step>, typename long_sends<Branch, Step + 1>::type>;
};
template <int Branch>
struct long_sends<Branch, kLongChain> {
    using type = s::Continue;
};
template <int Branch, int Step>
struct long_receives {
    using type = s::Recv<Word<(Branch * kLongChain) + Step>, typename long_receives<Branch, Step + 1>::type>;
};
template <int Branch>
struct long_receives<Branch, kLongChain> {
    using type = s::Continue;
};
template <int... Branch>
struct long_pair {
    using sender = s::Loop<s::Select<typename long_sends<Branch, 0>::type...>>;
    using receiver = s::Loop<s::Offer<typename long_receives<Branch, 0>::type...>>;
};
using LongPair = long_pair<0, 1, 2, 3, 4, 5, 6, 7>;
static_assert(s::checkpoint_verdict_of(^^LongPair::sender, ^^LongPair::receiver)
              == s::CheckpointVerdict::TooManyConfigurations);

// A list of the indices 0 to Count - 1, built by halves, so the list of 149
// indices takes a few small types.
template <int... Index>
struct indices {};
template <typename Left, typename Right, int Offset>
struct joined;
template <int... Left, int... Right, int Offset>
struct joined<indices<Left...>, indices<Right...>, Offset> {
    using type = indices<Left..., (Right + Offset)...>;
};
template <int Count>
struct indices_to {
    using type = typename joined<typename indices_to<Count / 2>::type, typename indices_to<Count - (Count / 2)>::type,
                                 Count / 2>::type;
};
template <>
struct indices_to<0> {
    using type = indices<>;
};
template <>
struct indices_to<1> {
    using type = indices<0>;
};

// The label that rolls back or aborts, one for each index.  Two labels next
// to each other differ, so each label is an edge of its own.  The walk admits
// a label one time when its branches are those of the label before it, so a
// row of one repeated Roll is two edges for each choice.
template <int Index, bool IsEven = Index % 2 == 0>
struct roll_or_abort_of {
    using type = s::Roll;
};
template <int Index>
struct roll_or_abort_of<Index, false> {
    using type = s::Abort;
};
template <int Index>
using roll_or_abort_at = typename roll_or_abort_of<Index>::type;
template <int Index>
using roll_at = s::Roll;

// A choice that commits to Next with its first label and rolls back or
// aborts with each other label.  Each choice is one type, with no type for a
// part of its labels.
template <typename Next, typename Labels, template <int> class Label>
struct rolling_choice;
template <typename Next, int... Index, template <int> class Label>
struct rolling_choice<Next, indices<Index...>, Label> {
    using select = s::Select<s::Commit<typename Next::decider>, Label<Index>...>;
    using offer = s::Offer<s::Commit<typename Next::follower>, Label<Index>...>;
};

// A row of 120 choices.  Each choice commits to the next one, and its 149
// other labels go back to the checkpoint or to the start, which the walk has
// seen.  So the pair has about 120 configurations and 18000 edges.  The
// check of the shape finds the answer of a Roll or an Abort deeper in its
// kept answers at each row, and it passes the bound first.
template <int Steps, int Width, template <int> class Label>
struct rolling_pair {
    using choice = rolling_choice<rolling_pair<Steps - 1, Width, Label>, typename indices_to<Width - 1>::type, Label>;
    using decider = typename choice::select;
    using follower = typename choice::offer;
};
template <int Width, template <int> class Label>
struct rolling_pair<0, Width, Label> {
    using decider = s::End;
    using follower = s::End;
};
using RollingPair = rolling_pair<120, 150, roll_or_abort_at>;
static_assert(s::checkpoint_verdict_of(^^RollingPair::decider, ^^RollingPair::follower)
              == s::CheckpointVerdict::TooManyConfigurations);

// A row of 20 such choices with one repeated label has two edges for each
// choice, so its walk fits the bound.  A walk that admitted each label would
// admit 3000 edges, and their work would pass the bound.
using RepeatedRollPair = rolling_pair<20, 150, roll_at>;
static_assert(s::checkpoint_verdict_of(^^RepeatedRollPair::decider, ^^RepeatedRollPair::follower)
              == s::CheckpointVerdict::Compliant);

}  // namespace

int main() { return 0; }
