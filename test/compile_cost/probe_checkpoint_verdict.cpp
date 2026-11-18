// The probe of the compile cost of the checkpoint verdict of
// fixy/session/Checkpoint.h (test/compile_cost/check_ceilings.py).
//
// The pair is a loop over eight chains of 10 sends, about 80 configurations,
// in the form of test/fixy/test_session_checkpoint_work_bound.cpp.  The
// concept CheckpointSessionAdmissible asks the verdict of the pair eight
// times.  The headers alone cost 5.8 G instructions, and the verdict about
// 6.4 G.  A change that makes the eight asks cost eight walks, or a walk that
// grows faster than the configurations, costs more than twice the count.
//
// GCC keeps the result of a constant call with the same arguments, so the
// eight asks cost one walk also with no variable template of the verdict.
// The code before that variable template, with its linear search of the
// seen configurations, costs 1.53 times the count of this pair and 1.88
// times on a pair of 40 sends for each chain.  Its cost passes twice the count
// only on a pair that the work bound of today refuses.
//
// measured: 12.263 G instructions

#include <fixy/session/Checkpoint.h>

namespace s = ::fixy::session;

namespace {

template <int Index>
struct Word {
    int value = 0;
};

inline constexpr int kChain = 10;

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

static_assert(s::CheckpointSessionAdmissible<ChainPair::sender, ChainPair::receiver>);

}  // namespace
