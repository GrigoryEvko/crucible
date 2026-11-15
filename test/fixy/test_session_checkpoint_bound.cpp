// The bound of the work of the checkpoint walk of fixy/session/Checkpoint.h.
//
// Each pair below met the operation limit of a constant evaluation before
// the walk counted its work.  The walk now stops at work_bound and gives
// TooManyConfigurations: for a long chain, with one edge for each
// configuration, and for a row of wide choices, with 150 edges for each
// configuration.  The pairs stand in a test of their own, so the compile of
// one unit stays small.

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

// The label that rolls back, one for each index.
template <int Index>
using roll_at = s::Roll;

// A choice that commits to Next with its first label and rolls back with
// each other label.  Each choice is one type, with no type for a part of
// its labels.
template <typename Next, typename Labels>
struct rolling_choice;
template <typename Next, int... Index>
struct rolling_choice<Next, indices<Index...>> {
    using select = s::Select<s::Commit<typename Next::decider>, roll_at<Index>...>;
    using offer = s::Offer<s::Commit<typename Next::follower>, roll_at<Index>...>;
};

// A row of 120 choices.  Each choice commits to the next one, and its 149
// other labels roll back to the checkpoint, which the walk has seen.  So
// the pair has about 120 configurations and 18000 edges.
template <int Steps, int Width>
struct rolling_pair {
    using choice = rolling_choice<rolling_pair<Steps - 1, Width>, typename indices_to<Width - 1>::type>;
    using decider = typename choice::select;
    using follower = typename choice::offer;
};
template <int Width>
struct rolling_pair<0, Width> {
    using decider = s::End;
    using follower = s::End;
};
using RollingPair = rolling_pair<120, 150>;
static_assert(s::checkpoint_verdict_of(^^RollingPair::decider, ^^RollingPair::follower)
              == s::CheckpointVerdict::TooManyConfigurations);

}  // namespace

int main() { return 0; }
