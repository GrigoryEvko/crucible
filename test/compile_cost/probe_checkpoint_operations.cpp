// The probe of the operations of the checkpoint walk of
// fixy/session/Checkpoint.h in each build (test/compile_cost/check_ceilings.py,
// THE OPERATIONS).
//
// The pair is a loop over four chains of 20 sends.  Its walk is the heaviest
// constant evaluation of the unit, so the smallest operation limit at which
// the unit compiles is the count of the walk.  The walk reads the parts of
// each position from the kept node of its type, and so it makes no vector.
// The walk that read the template arguments of each position made a vector
// at each read, and the sanitizer guards of a vector in the Debug build with
// AddressSanitizer gave that walk about 3 % more operations than the plain
// flags.  A loop over eight chains of a hundred sends then met the operation
// limit in the Debug build and not in the Release build.
//
// measured: 8.791 G instructions
// operations: 423773

#include <fixy/session/Checkpoint.h>

namespace s = ::fixy::session;

namespace {

template <int Index>
struct Word {
    int value = 0;
};

inline constexpr int kChain = 20;

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
using ChainPair = chain_pair<0, 1, 2, 3>;

static_assert(s::checkpoint_verdict_of(^^ChainPair::sender, ^^ChainPair::receiver) == s::CheckpointVerdict::Compliant);

}  // namespace
