// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The caller gives the checkpoint check of the pair below a bound of work
// far past the operation limit of a constant evaluation.  The pair is a loop
// over twelve chains of a hundred sends.  Its walk needs about 1.5 times the
// operation limit, so its check meets the limit before its bound.  The walk
// counts the same operations in each build (test/compile_cost/
// check_ceilings.py), so it meets the limit in each build.  The refusal
// names the operation limit and the flag that raises it, and not only the
// failure of the substitution of the cache of the verdict.
//
// Expected diagnostic: the operation count of a constant evaluation exceeds
// its limit.
#include <fixy/Ctx.h>
#include <fixy/session/Checkpoint.h>

#include <cstddef>

namespace neg_sess_checkpoint_operation_limit_types {
struct Wire {};

template <int Index>
struct Word {
    int value = 0;
};

inline constexpr int kChain = 100;

template <int Branch, int Step>
struct chain_sends {
    using type = ::fixy::session::Send<Word<(Branch * kChain) + Step>, typename chain_sends<Branch, Step + 1>::type>;
};
template <int Branch>
struct chain_sends<Branch, kChain> {
    using type = ::fixy::session::Continue;
};
template <int Branch, int Step>
struct chain_receives {
    using type = ::fixy::session::Recv<Word<(Branch * kChain) + Step>, typename chain_receives<Branch, Step + 1>::type>;
};
template <int Branch>
struct chain_receives<Branch, kChain> {
    using type = ::fixy::session::Continue;
};
template <int... Branch>
struct chain_pair {
    using sender = ::fixy::session::Loop<::fixy::session::Select<typename chain_sends<Branch, 0>::type...>>;
    using receiver = ::fixy::session::Loop<::fixy::session::Offer<typename chain_receives<Branch, 0>::type...>>;
};
using ChainPair = chain_pair<0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11>;
using Sender = ChainPair::sender;
using Receiver = ChainPair::receiver;

inline constexpr std::size_t kFarBound = std::size_t{1} << 40;
}  // namespace neg_sess_checkpoint_operation_limit_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_checkpoint_operation_limit_types;
    const ::fixy::BgDrainCtx ctx{::foundation::effects::testing::bg()};
    auto refused = s::mint_checkpoint_session<Sender, Receiver, s::DefaultAbandonmentPolicy, kFarBound>(ctx, Wire{});
    std::move(refused).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
