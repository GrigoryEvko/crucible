// The bound of the work of the checkpoint check of fixy/session/Checkpoint.h,
// raised for one pair.
//
// The pair below is a loop over eight chains of 80 sends, about 640
// configurations.  Its check needs more work than the default bound,
// checkpoint_work_bound, so the gate refuses it with the diagnostic
// Checkpoint_Work_Bound.  The same pair is compliant under twice the default
// bound, and the gate with that bound admits it.  The fixture
// neg_sess_checkpoint_work_bound shows the text of the refusal.

#include <fixy/session/Checkpoint.h>

#include <cstddef>

namespace s = ::fixy::session;

namespace {

template <int Index>
struct Word {
    int value = 0;
};

inline constexpr int kChain = 80;

template <int Branch, int Step>
struct chain_sends {
    using type = s::Send<Word<(Branch * kChain) + Step>, typename chain_sends<Branch, Step + 1>::type>;
};
template <int Branch>
struct chain_sends<Branch, kChain> {
    using type = s::Continue;
};
template <int Branch, int Step>
struct chain_receives {
    using type = s::Recv<Word<(Branch * kChain) + Step>, typename chain_receives<Branch, Step + 1>::type>;
};
template <int Branch>
struct chain_receives<Branch, kChain> {
    using type = s::Continue;
};
template <int... Branch>
struct chain_pair {
    using sender = s::Loop<s::Select<typename chain_sends<Branch, 0>::type...>>;
    using receiver = s::Loop<s::Offer<typename chain_receives<Branch, 0>::type...>>;
};
using ChainPair = chain_pair<0, 1, 2, 3, 4, 5, 6, 7>;

inline constexpr std::size_t kRaisedBound = 2 * s::checkpoint_work_bound;

static_assert(s::checkpoint_verdict_of(^^ChainPair::sender, ^^ChainPair::receiver)
              == s::CheckpointVerdict::TooManyConfigurations);
static_assert(s::checkpoint_verdict_of(^^ChainPair::sender, ^^ChainPair::receiver, kRaisedBound)
              == s::CheckpointVerdict::Compliant);
static_assert(s::CheckpointSessionAdmissible<ChainPair::sender, ChainPair::receiver, kRaisedBound>);

}  // namespace

int main() { return 0; }
